#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/wait.h>

bool isRoot() { return geteuid() == 0; }

std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) throw std::runtime_error("popen() failed!");
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

bool commandExists(const std::string& cmd) {
    std::string which = "which " + cmd;
    int ret = system((which + " >/dev/null 2>&1").c_str());
    return ret == 0;
}

void installMissingPackages(const std::vector<std::string>& pkgs) {
    std::string osRelease = exec("cat /etc/os-release 2>/dev/null || echo ''");
    std::string distro;
    if (osRelease.find("ID=arch") != std::string::npos) distro = "arch";
    else if (osRelease.find("ID=ubuntu") != std::string::npos || osRelease.find("ID=debian") != std::string::npos)
        distro = "deb";
    else if (osRelease.find("ID=fedora") != std::string::npos) distro = "fedora";
    else if (osRelease.find("ID=opensuse") != std::string::npos) distro = "opensuse";
    else {
        std::cerr << "Не удалось определить дистрибутив. Установите пакеты вручную.\n";
        return;
    }
    for (const auto& pkg : pkgs) {
        if (commandExists(pkg)) continue;
        std::cout << "Устанавливаю " << pkg << " для " << distro << "...\n";
        std::string installCmd;
        if (distro == "arch") installCmd = "sudo pacman -Sy --noconfirm " + pkg;
        else if (distro == "deb") installCmd = "sudo apt-get update && sudo apt-get install -y " + pkg;
        else if (distro == "fedora") installCmd = "sudo dnf install -y " + pkg;
        else if (distro == "opensuse") installCmd = "sudo zypper install -y " + pkg;
        system(installCmd.c_str());
    }
}

void runCmd(const std::string& cmd) {
    std::cout << "Выполняю: " << cmd << "\n";
    int ret = system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "Команда завершилась с ошибкой: " << cmd << "\n";
    }
}

int main() {
    if (!isRoot()) {
        std::cerr << "Ошибка: скрипт должен запускаться от root (sudo).\n";
        return 1;
    }

    // Проверяем необходимые утилиты
    std::vector<std::string> required = {"wget", "grep", "sed", "awk", "make", "gcc", "fdisk", "mkfs.ext4", "mkswap", "blkid"};
    installMissingPackages(required);

    // Выбор диска
    std::cout << "Доступные диски:\n";
    system("lsblk -d -o NAME,SIZE,MODEL");
    std::string disk;
    std::cout << "Введите диск для установки (например, /dev/sda): ";
    std::getline(std::cin, disk);

    // Подтверждение
    std::cout << "ВНИМАНИЕ: все данные на " << disk << " будут удалены! Продолжить? (yes/no): ";
    std::string confirm;
    std::getline(std::cin, confirm);
    if (confirm != "yes") {
        std::cout << "Операция отменена.\n";
        return 0;
    }

    // Очистка таблицы разделов
    runCmd("sgdisk -Z " + disk);
    // Создание разделов: 512M boot, swap 2G, rest root
    runCmd("sgdisk -n1:0:+512M -t1:EF00 " + disk); // EFI boot
    runCmd("sgdisk -n2:0:+2G  -t2:8200 " + disk);   // swap
    runCmd("sgdisk -n3:0:0    -t3:8300 " + disk);   // root

    // Форматирование
    runCmd("mkfs.fat -F32 " + disk + "1");
    runCmd("mkswap " + disk + "2");
    runCmd("mkfs.ext4 " + disk + "3");

    // Монтирование
    runCmd("mount " + disk + "3 /mnt");
    runCmd("mkdir -p /mnt/boot");
    runCmd("mount " + disk + "1 /mnt/boot");
    runCmd("swapon " + disk + "2");

    // Выбор stage3
    std::string arch = "amd64"; // предположим
    std::string stage3_url = "https://bouncer.gentoo.org/fetch/root/all/releases/amd64/autobuilds/latest-stage3-amd64-openrc.tar.xz";
    std::cout << "Скачиваю stage3...\n";
    runCmd("cd /mnt && wget " + stage3_url);
    std::string stage3_file = exec("ls /mnt/*.tar.xz | head -n1");
    stage3_file.erase(std::remove(stage3_file.begin(), stage3_file.end(), '\n'), stage3_file.end());
    std::cout << "Распаковываю " << stage3_file << "\n";
    runCmd("tar xpf " + stage3_file + " --xattrs-include='*.*' --numeric-owner -C /mnt");

    // Копируем DNS info
    runCmd("cp --dereference /etc/resolv.conf /mnt/etc/");

    // Монтируем необходимые fs
    runCmd("mount -t proc none /mnt/proc");
    runCmd("mount --rbind /sys /mnt/sys");
    runCmd("mount --make-rslave /mnt/sys");
    runCmd("mount --rbind /dev /mnt/dev");
    runCmd("mount --make-rslave /mnt/dev");

    // Chroot и базовая настройка
    std::string chrootScript = R"(
        source /etc/profile
        export PS1="(chroot) $PS1"
        emerge-webrsync
        echo "Europe/Moscow" > /etc/timezone
        emerge --config sys-libs/timezone-data
        echo "en_US.UTF-8 UTF-8" > /etc/locale.gen
        locale-gen
        eselect locale set en_US.utf8
        echo "GENTOO_MIRRORS=\"http://mirror.yandex.ru/gentoo-distfiles/\"" >> /etc/portage/make.conf
        emerge --ask sys-kernel/gentoo-sources
        genkernel --install all
        echo '/dev/sda1  /boot        vfat    defaults,noatime     0 2' > /etc/fstab
        echo '/dev/sda2  none         swap    sw                   0 0' >> /etc/fstab
        echo '/dev/sda3  /            ext4    noatime              0 1' >> /etc/fstab
        passwd
        exit
    )";
    std::ofstream chrootFile("/mnt/chroot_setup.sh");
    chrootFile << chrootScript;
    chrootFile.close();
    runCmd("chroot /mnt /bin/bash chroot_setup.sh");

    std::cout << "Установка завершена! Перезагрузитесь и удалите установочный носитель.\n";
    return 0;
}
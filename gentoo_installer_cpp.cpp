#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>

bool isRoot() { return geteuid() == 0; }

int runCmd(const std::string& cmd) {
    std::cout << "[+] " << cmd << std::endl;
    int ret = system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "[!] Command failed: " << cmd << std::endl;
    }
    return WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;
}

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

bool askYesNo(const std::string& question) {
    std::string ans;
    while (true) {
        std::cout << question << " (yes/no): ";
        std::getline(std::cin, ans);
        if (ans == "yes" || ans == "y") return true;
        if (ans == "no" || ans == "n") return false;
        std::cout << "Пожалуйста, ответьте yes или no." << std::endl;
    }
}

void listDisks() {
    std::cout << "[+] Доступные диски:" << std::endl;
    system("lsblk -d -o NAME,SIZE,MODEL");
}

std::string chooseDisk() {
    while (true) {
        listDisks();
        std::string disk;
        std::cout << "Введите диск для установки Gentoo (например, /dev/sda): ";
        std::getline(std::cin, disk);
        if (disk.rfind("/dev/", 0) == 0 && access(disk.c_str(), F_OK) == 0) {
            if (askYesNo("Вы выбрали " + disk + ". Все данные будут удалены. Продолжить?"))
                return disk;
        } else {
            std::cout << "[!] Неверный путь." << std::endl;
        }
    }
}

void partitionDisk(const std::string& disk) {
    runCmd("sgdisk -Z " + disk);
    const int bootSize = 512; // MiB
    const int swapSize = 2;   // GiB
    runCmd("sgdisk -n1:0:+" + std::to_string(bootSize) + "M -t1:EF00 " + disk);
    runCmd("sgdisk -n2:0:+" + std::to_string(swapSize) + "G -t2:8200 " + disk);
    runCmd("sgdisk -n3:0:0 -t3:8300 " + disk);
    runCmd("sgdisk -p " + disk); // show table
    runCmd("partprobe " + disk);
}

void formatPartitions(const std::string& disk) {
    runCmd("mkfs.vfat -F32 " + disk + "1");
    runCmd("mkswap " + disk + "2");
    runCmd("mkfs.ext4 -F " + disk + "3");
}

void mountPartitions(const std::string& disk) {
    runCmd("mkdir -p /mnt/gentoo");
    runCmd("mount " + disk + "3 /mnt/gentoo");
    runCmd("mkdir -p /mnt/gentoo/boot");
    runCmd("mount " + disk + "1 /mnt/gentoo/boot");
    runCmd("swapon " + disk + "2");
}

std::string getLatestStage3URL() {
    std::string txt = exec("wget -qO- https://distfiles.gentoo.org/releases/amd64/autobuilds/latest-stage3-amd64-desktop-openrc.txt");
    std::istringstream iss(txt);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '-') continue;
        std::istringstream lss(line);
        std::string first;
        lss >> first;
        if (first.find(".tar.xz") != std::string::npos || first.find(".tar.bz2") != std::string::npos) {
            return std::string("https://distfiles.gentoo.org/releases/amd64/autobuilds/") + first;
        }
    }
    std::cerr << "[!] Не удалось определить URL stage3" << std::endl;
    exit(1);
}

void downloadAndExtractStage3(const std::string& url) {
    std::string filename = url.substr(url.find_last_of("/\\")+1);
    std::cout << "[+] Скачивание " << filename << std::endl;
    runCmd("cd /mnt/gentoo && wget -q --show-progress " + url);
    std::cout << "[+] Распаковка stage3..." << std::endl;
    runCmd("cd /mnt/gentoo && tar xpvf " + filename + " --xattrs-include=*.* --numeric-owner");
}

void writeFstab(const std::string& disk) {
    std::string bootUUID = exec(("blkid -s UUID -o value " + disk + "1").c_str());
    std::string rootUUID = exec(("blkid -s UUID -o value " + disk + "3").c_str());
    bootUUID.erase(std::remove(bootUUID.begin(), bootUUID.end(), '\n'), bootUUID.end());
    rootUUID.erase(std::remove(rootUUID.begin(), rootUUID.end(), '\n'), rootUUID.end());
    std::ofstream fstab("/mnt/gentoo/etc/fstab");
    fstab << "# <fs>                  <mountpoint>    <type>      <opts>              <dump/pass>\n";
    fstab << "UUID=" << rootUUID << "      /               ext4        noatime             0 1\n";
    fstab << "UUID=" << bootUUID << "      /boot           vfat        defaults,noatime    0 2\n";
    if (askYesNo("Создать swap раздел?")) {
        std::string swapUUID = exec(("blkid -s UUID -o value " + disk + "2").c_str());
        swapUUID.erase(std::remove(swapUUID.begin(), swapUUID.end(), '\n'), swapUUID.end());
        fstab << "UUID=" << swapUUID << "      none            swap        sw                  0 0\n";
    }
    fstab.close();
}

void configureMirrorLocale() {
    // Use defaults for simplicity; you could extend to ask.
    std::ofstream makeConf("/mnt/gentoo/etc/portage/make.conf", std::ios::app);
    makeConf << "\nGENTOO_MIRRORS=\"http://mirror.yandex.ru/gentoo-distfiles/\"\nLINGUAS=\"en\"\n";
    makeConf.close();
    std::ofstream localeGen("/mnt/gentoo/etc/locale.gen");
    localeGen << "en_US.UTF-8 UTF-8\n";
    localeGen.close();
    runCmd("chroot /mnt/gentoo locale-gen");
}

void copyResolv() {
    runCmd("cp -L /etc/resolv.conf /mnt/gentoo/etc/");
}

void mountVirtualFS() {
    runCmd("mount -t proc none /mnt/gentoo/proc");
    runCmd("mount --rbind /sys /mnt/gentoo/sys");
    runCmd("mount --make-rslave /mnt/gentoo/sys");
    runCmd("mount --rbind /dev /mnt/gentoo/dev");
    runCmd("mount --make-rslave /mnt/gentoo/dev");
    runCmd("mount --rbind /run /mnt/gentoo/run");
    runCmd("mount --make-slave /mnt/gentoo/run");
}

void chrootSetup() {
    // Run a simple chroot script to set timezone, locale, kernel, etc.
    std::string script = R"(
        source /etc/profile
        export PS1="(chroot) $PS1"
        echo "Europe/Moscow" > /etc/timezone
        emerge --config sys-libs/timezone-data
        echo "en_US.UTF-8 UTF-8" > /etc/locale.gen
        locale-gen
        eselect locale set en_US.utf8
        emerge-webrsync
        emerge --ask sys-kernel/gentoo-sources
        genkernel --install all
        passwd
        exit
    )";
    std::ofstream chrootScript("/mnt/gentoo/chroot_setup.sh");
    chrootScript << script;
    chrootScript.close();
    runCmd("chroot /mnt/gentoo /bin/bash chroot_setup.sh");
}

void unmountAll() {
    runCmd("umount -R /mnt/gentoo/dev");
    runCmd("umount -R /mnt/gentoo/sys");
    runCmd("umount -R /mnt/gentoo/run");
    runCmd("umount /mnt/gentoo/proc");
    runCmd("umount /mnt/gentoo/boot");
    runCmd("umount /mnt/gentoo");
    runCmd("swapoff -a");
}

int main() {
    if (!isRoot()) {
        std::cerr << "[!] Запустите скрипт от root (sudo)" << std::endl;
        return 1;
    }

    std::string disk = chooseDisk();
    if (!askYesNo("Уверены, что хотите использовать " + disk + "? Это уничтожит все данные")) {
        std::cout << "[!] Отменено." << std::endl;
        return 0;
    }

    partitionDisk(disk);
    formatPartitions(disk);
    mountPartitions(disk);

    std::string stage3URL = getLatestStage3URL();
    downloadAndExtractStage3(stage3URL);

    writeFstab(disk);
    configureMirrorLocale();
    copyResolv();
    mountVirtualFS();

    chrootSetup();

    unmountAll();

    std::cout << "[✔] Базовая установка Gentoo завершена. Перезагрузитесь и настройте загрузчик." << std::endl;
    return 0;
}
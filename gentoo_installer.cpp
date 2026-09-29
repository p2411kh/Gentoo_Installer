#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <unistd.h>
#include <sys/utsname.h>

bool isRoot() {
    return geteuid() == 0;
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

bool commandExists(const std::string& cmd) {
    std::string which = "which " + cmd;
    int ret = system((which + " >/dev/null 2>&1").c_str());
    return ret == 0;
}

void installMissingPackages(const std::vector<std::string>& pkgs) {
    // Detect distro
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

int main() {
    if (!isRoot()) {
        std::cerr << "Ошибка: скрипт должен запускаться от root (sudo).\n";
        return 1;
    }

    // Проверяем необходимые утилиты
    std::vector<std::string> required = {"wget", "grep", "sed", "awk", "make", "gcc"};
    installMissingPackages(required);

    // Выбор временной зоны
    std::cout << "Доступные таймзоны (пример):\n";
    system("timedatectl list-timezones | head -20");
    std::string tz;
    std::cout << "Введите вашу таймзону (например, Europe/Moscow): ";
    std::getline(std::cin, tz);
    std::string cmd = "timedatectl set-timezone " + tz;
    system(cmd.c_str());

    // Выбор локали
    std::cout << "Генерируем локали...\n";
    std::string locale;
    std::cout << "Введите локаль (например, en_US.UTF-8): ";
    std::getline(std::cin, locale);
    std::ofstream localeFile("/etc/locale.gen", std::ios::app);
    localeFile << locale << " UTF-8\n";
    localeFile.close();
    system("locale-gen");
    std::string localecmd = "localectl set-locale LANG=" + locale;
    system(localecmd.c_str());

    // Выбор зеркала
    std::string mirror;
    std::cout << "Введите URL Gentoo mirror (например, http://mirror.yandex.ru/gentoo-distfiles/): ";
    std::getline(std::cin, mirror);
    std::ofstream makeConf("/etc/portage/make.conf", std::ios::app);
    makeConf << "GENTOO_MIRRORS=\"" << mirror << "\"\n";
    makeConf.close();

    // Выбор ядра
    std::string kernelType;
    std::cout << "Выберите тип ядра: 1 - бинарное (genkernel), 2 - исходное (manual compile): ";
    std::getline(std::cin, kernelType);
    if (kernelType == "1") {
        system("emerge --ask sys-kernel/gentoo-sources");
        system("genkernel all");
    } else {
        system("emerge --ask sys-kernel/gentoo-sources");
        std::cout << "После установки источников отредактируйте /usr/src/linux и соберите ядро вручную.\n";
    }

    std::cout << "Базовая настройка завершена. Продолжите установку Gentoo согласно Handbook.\n";
    return 0;
}
import os
import subprocess
import sys
import shutil


# 1. Прием аргументов от основного скрипта
if len(sys.argv) >= 3:
    selected_mirror = sys.argv[1]
    selected_linguas = sys.argv[2]
else:
    selected_mirror = "https://distfiles.gentoo.org"
    selected_linguas = "ru en"

# 2. Подсчет потоков и флаги оптимизации
total_threads = os.cpu_count() or 1  
half_threads = max(1, total_threads // 2)
makeopts_value = f"-j{half_threads} -l{half_threads}"

common_flags = "-O2 -pipe -march=native"

# 3. Гарантированное создание структуры директорий Portage
portage_dir = "/etc/portage"
os.makedirs(portage_dir, exist_ok=True)

make_conf_path = os.path.join(portage_dir, "make.conf")
if os.path.exists(make_conf_path):
    if os.path.isdir(make_conf_path):
        shutil.rmtree(make_conf_path)
    else:
        os.remove(make_conf_path)

make_config_content = f"""# Настройки оптимизации и процессора
COMMON_FLAGS="{common_flags}"
CFLAGS="${{COMMON_FLAGS}}"
CXXFLAGS="${{COMMON_FLAGS}}"
FCFLAGS="${{COMMON_FLAGS}}"
FFLAGS="${{COMMON_FLAGS}}"

# Настройки региона, локализации и производительности
GENTOO_MIRRORS="{selected_mirror}"
ACCEPT_KEYWORDS="amd64"
LINGUAS="{selected_linguas}"
MAKEOPTS="{makeopts_value}"
ACCEPT_LICENSE="*"
"""

with open(make_conf_path, "w", encoding="utf-8") as f:
    f.write(make_config_content)

print("[+] Файл /etc/portage/make.conf успешно сформирован.")

# 4. Настройка USE-флага dracut для installkernel
package_use_dir = "/etc/portage/package.use"
os.makedirs(package_use_dir, exist_ok=True)

installkernel_use_path = os.path.join(package_use_dir, "installkernel")
with open(installkernel_use_path, "w", encoding="utf-8") as f:
    f.write("sys-kernel/installkernel dracut\n")

print("[+] Синхронизация Portage...")
subprocess.run(["emerge-webrsync"], check=True)

print("[+] Установка ядра и системных утилит...")
subprocess.run(["emerge", "--noreplace", "sys-kernel/linux-firmware"], check=True)
subprocess.run(["emerge", "--noreplace", "sys-kernel/installkernel"], check=True)
subprocess.run(["emerge", "--noreplace", "sys-kernel/gentoo-kernel"], check=True)
subprocess.run(["emerge", "--noreplace", "net-misc/dhcpcd", "sys-boot/grub", "sys-boot/efibootmgr"], check=True)

print("[+] Включение dhcpcd в автозагрузку...")
subprocess.run(["rc-update", "add", "dhcpcd", "default"], check=True)

print("[+] Установка и настройка GRUB...")
subprocess.run(["grub-install", "--target=x86_64-efi", "--efi-directory=/boot", "--bootloader-id=gentoo"], check=True)

os.makedirs("/boot/grub", exist_ok=True)
subprocess.run(["grub-mkconfig", "-o", "/boot/grub/grub.cfg"], check=True)

print("\n[✔] Скрипт chroot_stage завершил работу.")

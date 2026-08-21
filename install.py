import os
import subprocess
import sys
import shutil

REQUIRED_TOOLS = ["wget", "tar", "mkfs.ext4", "mkfs.vfat", "mount"]

try:
    from mirror import GENTOO_MIRRORS_DB, LOCALES_DB
except ImportError:
    print("Ошибка: Не найден файл mirror.py рядом с основным скриптом!")
    sys.exit(1)

if os.geteuid() != 0:
    print("Ошибка: Запустите скрипт через sudo")
    sys.exit(1)

missing_tools = [tool for tool in REQUIRED_TOOLS if shutil.which(tool) is None]
if missing_tools:
    print("Ошибка: В системе отсутствуют утилиты:", ", ".join(missing_tools))
    sys.exit(1)

gentoo_partition = input("Введите путь к разделу для Gentoo (например, /dev/sda2): ")
boot_partition = input("Введите путь к разделу для загрузчика (например, /dev/sda1): ")
swap_decision = input("Будет ли swap? (yes или no): ").strip().lower()

swap_partition = None
if swap_decision == "yes":
    swap_partition = input("Введите путь к разделу SWAP (например, /dev/sda3): ")

stage3_url = "https://distfiles.gentoo.org/releases/amd64/autobuilds/20260816T170110Z/stage3-amd64-desktop-openrc-20260816T170110Z.tar.xz"
stage3_filename = stage3_url.split("/")[-1]

print(f"\nФорматирование {gentoo_partition} в ext4...")
# subprocess.run(["mkfs.ext4", gentoo_partition], check=True)

print(f"Форматирование {boot_partition} в FAT32 для UEFI...")
# subprocess.run(["mkfs.vfat", "-F", "32", boot_partition], check=True)

if swap_partition:
    print(f"Подготовка SWAP на {swap_partition}...")
    # subprocess.run(["mkswap", swap_partition], check=True)
    # subprocess.run(["swapon", swap_partition], check=True)

# 1. Монтирование корневого раздела и загрузчика
os.makedirs("/mnt/gentoo", exist_ok=True)
subprocess.run(["mount", gentoo_partition, "/mnt/gentoo"], check=True)

os.makedirs("/mnt/gentoo/boot", exist_ok=True)
subprocess.run(["mount", boot_partition, "/mnt/gentoo/boot"], check=True)

# 2. Скачивание и распаковка Stage3 (Строго ДО монтирования виртуальных ФС)
os.chdir("/mnt/gentoo")
subprocess.run(["wget", stage3_url], check=True)
subprocess.run([
    "tar", "xpvf", stage3_filename, 
    "--xattrs-include=*.*", 
    "--numeric-owner"
], check=True)

# 3. Выбор настроек
print("\n Выберите регион для скачивания пакетов (зеркало):")
for key, value in GENTOO_MIRRORS_DB.items():
    print(f"[{key}] {value['name']}")

mirror_choice = input("Номер региона: ").strip()
if mirror_choice not in GENTOO_MIRRORS_DB:
    print("Неверный выбор. По умолчанию выбраны США.")
    mirror_choice = "3"

print("\n Выберите основной язык системы:")
for key, value in LOCALES_DB.items():
    print(f"[{key}] {value['name']}")

locale_choice = input("Номер языка: ").strip()
if locale_choice not in LOCALES_DB:
    print("Неверный выбор. По умолчанию выбран Английский.")
    locale_choice = "3"

selected_mirror = GENTOO_MIRRORS_DB[mirror_choice]["url"]
selected_linguas = LOCALES_DB[locale_choice]["linguas"]

# 4. Монтирование виртуальных системных ФС
mount_commands = [
    ["mount", "--rbind", "/dev", "/mnt/gentoo/dev"],
    ["mount", "--make-rslave", "/mnt/gentoo/dev"],
    ["mount", "-t", "proc", "none", "/mnt/gentoo/proc"],
    ["mount", "--rbind", "/sys", "/mnt/gentoo/sys"],
    ["mount", "--make-rslave", "/mnt/gentoo/sys"],
    ["mount", "--rbind", "/run", "/mnt/gentoo/run"],
    ["mount", "--make-slave", "/mnt/gentoo/run"]
]

for cmd in mount_commands:
    subprocess.run(cmd, check=True)

# 5. Подготовка к chroot и передача выбранных параметров
shutil.copy("/etc/resolv.conf", "/mnt/gentoo/etc/resolv.conf")
shutil.copy("chroot_stage.py", "/mnt/gentoo/tmp/chroot_stage.py")

subprocess.run([
    "chroot", "/mnt/gentoo", 
    "python3", "/tmp/chroot_stage.py", 
    selected_mirror, selected_linguas
], check=True)

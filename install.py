import os
import subprocess
import sys
import shutil
import urllib.request

REQUIRED_TOOLS = ["wget", "tar", "mkfs.ext4", "mkfs.vfat", "mount", "umount", "blkid"]

def get_uuid(partition):
    res = subprocess.run(["blkid", "-s", "UUID", "-o", "value", partition], capture_output=True, text=True, check=True)
    return res.stdout.strip()

try:
    from mirror import GENTOO_MIRRORS_DB, LOCALES_DB
except ImportError:
    print("[!] Ошибка: Не найден файл mirror.py рядом с основным скриптом!")
    sys.exit(1)

if os.geteuid() != 0:
    print("[!] Ошибка: Запустите скрипт через sudo")
    sys.exit(1)

missing_tools = [tool for tool in REQUIRED_TOOLS if shutil.which(tool) is None]
if missing_tools:
    print("[!] Ошибка: В системе отсутствуют утилиты:", ", ".join(missing_tools))
    sys.exit(1)

gentoo_partition = input("Введите путь к разделу для Gentoo (например, /dev/sda2): ").strip()
boot_partition = input("Введите путь к разделу для загрузчика (например, /dev/sda1): ").strip()
swap_decision = input("Будет ли swap? (yes или no): ").strip().lower()

swap_partition = None
if swap_decision in ["yes", "y"]:
    swap_partition = input("Введите путь к разделу SWAP (например, /dev/sda3): ").strip()

def get_latest_stage3_url():
    base_url = "https://distfiles.gentoo.org/releases/amd64/autobuilds/"
    txt_url = base_url + "latest-stage3-amd64-desktop-openrc.txt"
    print("[+] Поиск актуального релиза Stage3...")
    try:
        with urllib.request.urlopen(txt_url) as response:
            content = response.read().decode('utf-8')
            # Файл теперь PGP-clearsigned (начинается с "-----BEGIN PGP SIGNED MESSAGE-----"
            # и заканчивается блоком подписи), поэтому нельзя просто пропускать строки
            # с "#" — нужно искать именно строку вида "<timestamp>/<файл>.tar.xz <размер>".
            for line in content.splitlines():
                line = line.strip()
                if not line or line.startswith("#") or line.startswith("-----"):
                    continue
                parts = line.split()
                rel_path = parts[0]
                if rel_path.endswith(".tar.xz") or rel_path.endswith(".tar.bz2"):
                    return base_url + rel_path
        print("[!] Ошибка: в latest-*.txt не найдена строка с именем архива stage3.")
        sys.exit(1)
    except Exception as e:
        print(f"[!] Ошибка при получении актуального Stage3: {e}")
        sys.exit(1)

stage3_url = get_latest_stage3_url()
stage3_filename = stage3_url.split("/")[-1]
print(f"[+] Найден актуальный Stage3: {stage3_filename}")

print(f"\n[+] Форматирование {gentoo_partition} в ext4...")
subprocess.run(["mkfs.ext4", "-F", gentoo_partition], check=True)

print(f"[+] Форматирование {boot_partition} в FAT32 для UEFI...")
subprocess.run(["mkfs.vfat", "-F", "32", boot_partition], check=True)

if swap_partition:
    print(f"[+] Подготовка SWAP на {swap_partition}...")
    subprocess.run(["mkswap", swap_partition], check=True)
    subprocess.run(["swapon", swap_partition], check=True)

# Монтирование целевой файловой системы
os.makedirs("/mnt/gentoo", exist_ok=True)
subprocess.run(["mount", gentoo_partition, "/mnt/gentoo"], check=True)

os.makedirs("/mnt/gentoo/boot", exist_ok=True)
subprocess.run(["mount", boot_partition, "/mnt/gentoo/boot"], check=True)

# Скачивание и распаковка Stage3
os.chdir("/mnt/gentoo")
subprocess.run(["wget", stage3_url], check=True)
print("[+] Распаковка архива Stage3...")
subprocess.run(["tar", "xpvf", stage3_filename, "--xattrs-include=*.*", "--numeric-owner"], check=True)

# Генерация /etc/fstab
boot_uuid = get_uuid(boot_partition)
root_uuid = get_uuid(gentoo_partition)

fstab_content = f"""# <fs>                  <mountpoint>    <type>      <opts>              <dump/pass>
UUID={root_uuid}      /               ext4        noatime             0 1
UUID={boot_uuid}      /boot           vfat        defaults,noatime    0 2
"""

if swap_partition:
    swap_uuid = get_uuid(swap_partition)
    fstab_content += f"UUID={swap_uuid}      none            swap        sw                  0 0\n"

with open("/mnt/gentoo/etc/fstab", "w", encoding="utf-8") as f:
    f.write(fstab_content)

print("[+] Файл /etc/fstab сформирован.")

# Меню выбора параметров
print("\nВыберите регион для скачивания пакетов (зеркало):")
for key, value in GENTOO_MIRRORS_DB.items():
    print(f"[{key}] {value['name']}")
mirror_choice = input("Номер региона: ").strip()
if mirror_choice not in GENTOO_MIRRORS_DB:
    mirror_choice = "3"

print("\nВыберите основной язык системы:")
for key, value in LOCALES_DB.items():
    print(f"[{key}] {value['name']}")
locale_choice = input("Номер языка: ").strip()
if locale_choice not in LOCALES_DB:
    locale_choice = "3"

selected_mirror = GENTOO_MIRRORS_DB[mirror_choice]["url"]
selected_linguas = LOCALES_DB[locale_choice]["linguas"]

# Безопасное монтирование системных псевдо-ФС с размонтированием при выходе
mount_points = ["/dev", "/proc", "/sys", "/run"]
try:
    print("[+] Монтирование виртуальных файловых систем...")
    subprocess.run(["mount", "--rbind", "/dev", "/mnt/gentoo/dev"], check=True)
    subprocess.run(["mount", "--make-rslave", "/mnt/gentoo/dev"], check=True)
    subprocess.run(["mount", "-t", "proc", "none", "/mnt/gentoo/proc"], check=True)
    subprocess.run(["mount", "--rbind", "/sys", "/mnt/gentoo/sys"], check=True)
    subprocess.run(["mount", "--make-rslave", "/mnt/gentoo/sys"], check=True)
    subprocess.run(["mount", "--rbind", "/run", "/mnt/gentoo/run"], check=True)
    subprocess.run(["mount", "--make-slave", "/mnt/gentoo/run"], check=True)

    base_dir = os.path.dirname(os.path.abspath(__file__))
    chroot_script_src = os.path.join(base_dir, "chroot_stage.py")

    shutil.copy("/etc/resolv.conf", "/mnt/gentoo/etc/resolv.conf")
    shutil.copy(chroot_script_src, "/mnt/gentoo/chroot_stage.py")

    print("[+] Переход в chroot окружение...")
    subprocess.run([
        "chroot", "/mnt/gentoo",
        "python3", "/chroot_stage.py",
        selected_mirror, selected_linguas
    ], check=True)

finally:
    print("\n[+] Очистка: Отмонтирование системных директорий...")
    subprocess.run(["umount", "-R", "/mnt/gentoo/dev"], stderr=subprocess.DEVNULL)
    subprocess.run(["umount", "-R", "/mnt/gentoo/sys"], stderr=subprocess.DEVNULL)
    subprocess.run(["umount", "-R", "/mnt/gentoo/run"], stderr=subprocess.DEVNULL)
    subprocess.run(["umount", "/mnt/gentoo/proc"], stderr=subprocess.DEVNULL)
    subprocess.run(["umount", "/mnt/gentoo/boot"], stderr=subprocess.DEVNULL)
    subprocess.run(["umount", "/mnt/gentoo"], stderr=subprocess.DEVNULL)
    print("[✔] Размонтирование завершено.")

# chroot_stage.py
import os
import subprocess
import sys


if len(sys.argv) >= 3:
    selected_mirror = sys.argv[1]
    selected_linguas = sys.argv[2]
else:
    selected_mirror = "https://distfiles.gentoo.org"
    selected_linguas = "ru en"


total_threads = os.cpu_count() or 1  
half_threads = max(1, total_threads // 2)
makeopts_value = f"-j{half_threads} -l{half_threads}"

common_flags = "-O2 -pipe -march=native"


mirrors_config_content = f"""# Настройки оптимизации и процессора
COMMON_FLAGS="{common_flags}"
CFLAGS="${{COMMON_FLAGS}}"
CXXFLAGS="${{COMMON_FLAGS}}"
FCFLAGS="${{COMMON_FLAGS}}"
FFLAGS="${{COMMON_FLAGS}}"


GENTOO_MIRRORS="{selected_mirror}"
ACCEPT_KEYWORDS="amd64"
LINGUAS="{selected_linguas}"
MAKEOPTS="{makeopts_value}"
ACCEPT_LICENSE="*"
"""

make_conf_dir = "/etc/portage/make.conf"
os.makedirs(make_conf_dir, exist_ok=True)
mirrors_conf_path = os.path.join(make_conf_dir, "10-region.conf")

with open(mirrors_conf_path, "w", encoding="utf-8") as f:
    f.write(mirrors_config_content)

package_use_dir = "/etc/portage/package.use"
os.makedirs(package_use_dir, exist_ok=True)

installkernel_use_path = os.path.join(package_use_dir, "installkernel")
with open(installkernel_use_path, "w", encoding="utf-8") as f:
    f.write("sys-kernel/installkernel dracut\n")



subprocess.run(["emerge-webrsync"], check=True)

subprocess.run(["emerge", "--noreplace", "sys-kernel/linux-firmware"], check=True)

subprocess.run(["emerge", "--noreplace", "sys-kernel/installkernel"], check=True)

subprocess.run(["emerge", "--noreplace", "sys-kernel/gentoo-kernel"], check=True)

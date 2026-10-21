# Custom Raspberry Pi 5 OS with Buildroot and SSH

**Goal:** starting from an empty git repo, build a custom Linux image for the Raspberry Pi 5 with **Buildroot**. Take Buildroot's stock Pi 5 config, keep your changes in a separate BR2_EXTERNAL tree, and add the **Dropbear** SSH server with key-only login. Build on **Fedora 44**, flash a microSD card, then log in with `ssh root@miltech-br.local`. Finally, check that a clean clone reproduces the same OS.

**Part 2** (sections 15–20) adds **ROS 2**. You try it on the laptop first, then write publisher and subscriber nodes that count 1, 2, 3… over a topic. These go into the image through a second BR2_EXTERNAL tree (the "meta-layer"), and you run the nodes on the Pi.

**Hardware:** Raspberry Pi 5, USB-C power supply, a spare microSD card, a card reader, an Ethernet cable. A monitor (micro-HDMI) and keyboard are optional and help with debugging.
**Host:** Fedora 44, x86_64, ~15 GB free disk, ~1 GB of downloads. The first build takes 15–40 min.

Commit after every step. The git history then documents the work.

---

## Contents

0. [Overview](#0-overview)
1. [Background](#1-background)
2. [Prepare Fedora](#2-prepare-fedora)
3. [Empty repo + Buildroot submodule](#3-empty-repo--buildroot-submodule)
4. [Stock Pi 5 config and first build](#4-stock-pi-5-config-and-first-build)
5. [BR2_EXTERNAL tree](#5-br2_external-tree)
6. [menuconfig: hostname, password, SSH, mDNS](#6-menuconfig-hostname-password-ssh-mdns)
7. [Rootfs overlay](#7-rootfs-overlay)
8. [Post-build script](#8-post-build-script)
9. [Pi 5 boot files and kernel tweaks](#9-pi-5-boot-files-and-kernel-tweaks)
10. [Final build and offline image check](#10-final-build-and-offline-image-check)
11. [Flash the microSD](#11-flash-the-microsd)
12. [Network, first boot, SSH](#12-network-first-boot-ssh)
13. [Checks on the Pi](#13-checks-on-the-pi)
14. [Reproduce from a clean clone](#14-reproduce-from-a-clean-clone)

**Part 2: ROS 2**

15. [ROS 2 on the laptop](#15-ros-2-on-the-laptop)
16. [Counter nodes](#16-counter-nodes)
17. [Check with the ROS 2 CLI](#17-check-with-the-ros-2-cli)
18. [ROS 2 layer for Buildroot](#18-ros-2-layer-for-buildroot)
19. [Build the image with ROS 2](#19-build-the-image-with-ros-2)
20. [Run the nodes on the Pi](#20-run-the-nodes-on-the-pi)

[Sources](#sources)

---

## 0. Overview

```
 Fedora (laptop)                                                    Raspberry Pi 5
 ────────────────────────────────────────────────────────────       ──────────────
 1  git init + Buildroot submodule (2026.08)
 2  raspberrypi5_defconfig ──► make ──► stock build
 3  BR2_EXTERNAL tree; savedefconfig ──► configs/miltech_pi5_defconfig
 4  menuconfig: hostname, password, dropbear, avahi
 5  overlay: /etc/default/dropbear, /root/.ssh/authorized_keys
 6  post-build.sh: 700/600 permissions, build info
 7  config.txt, DTB overlays (needed on D0 boards), kernel fragment for the fan
 8  make ──► output/images/sdcard.img ──► check without the Pi
 9  dd ──► microSD ──────────────────────────────────────────────►  boot
 10 ssh root@miltech-br.local ◄──────── Ethernet ─────────────────── dropbear
 11 checks
 12 git clone --recurse-submodules ──► same config
```

**Final repo layout** (everything outside `buildroot/` is yours):

```
buildroot-rpi5-ssh/
├── .gitignore                         output/
├── .gitmodules                        (created by git) where Buildroot comes from
├── buildroot/                         submodule: Buildroot 2026.08, never edited
├── external.desc                      tree name: MILTECH
├── Config.in                          empty
├── external.mk                        empty
├── configs/
│   └── miltech_pi5_defconfig          the OS configuration
└── board/miltech-pi5/
    ├── config.txt                     Pi 5 bootloader settings
    ├── linux-fan.fragment             kernel option: built-in fan driver
    ├── post-build.sh                  key permissions + build info
    └── rootfs-overlay/
        ├── etc/default/dropbear       DROPBEAR_ARGS="-s"
        └── root/.ssh/authorized_keys  your public key
```

This follows the Buildroot manual's «Recommended directory structure». The manual's «Custom top Makefile» section describes Buildroot as a git submodule in a `buildroot` subdirectory.

---

## 1. Background

### 1.1 Why a custom OS

| | Raspberry Pi OS Lite | Custom OS (Buildroot) |
|---|---|---|
| Contents | whatever Debian ships | **only what you select** |
| Size | gigabytes | tens of megabytes |
| Boot | 15–30 s | a few seconds |
| Updates | `apt upgrade` per device, devices drift | **whole new image**, all devices identical |
| Network exposure | dozens of services | only what you add |
| Reproducibility | "whatever was installed that day" | config in git → same image a year later |

### 1.2 Buildroot vs Yocto

Both build **toolchain → kernel → userspace → image** from source.

| | **Buildroot** | **Yocto** |
|---|---|---|
| Config | one `.config` (`make menuconfig`) | layers, recipes, `local.conf` |
| Output | an **image** | **packages** + image |
| First build | tens of minutes | hours, 50–100 GB |
| Board support | `configs/<board>_defconfig` + `board/<board>/` | BSP layer, e.g. `meta-raspberrypi` |
| Your changes | **BR2_EXTERNAL** tree | your own `meta-<name>` layer |

### 1.3 How the Pi 5 boots this OS

```
 power on
   ▼  bootloader in on-board EEPROM (not on the card)
 reads microSD partition 1 (FAT32):
   config.txt ──► kernel=Image
   bcm2712-rpi-5-b.dtb / bcm2712d0-rpi-5-b.dtb   (hardware description)
   overlays/bcm2712d0.dtbo   (D0 boards only: hardware description fix, 9.4)
   cmdline.txt ──► root=/dev/mmcblk0p2 rootwait console=tty1 console=ttyAMA10,115200
   ▼  Linux 6.12 mounts partition 2 (ext4) as /
 /sbin/init (BusyBox) ──► /etc/init.d/S*: syslog … S40network (DHCP) … S50avahi-daemon, S50dropbear
   ▼
 login on HDMI (tty1) and debug UART (ttyAMA10)           ssh root@miltech-br.local ◄── laptop
```

The Pi 5 firmware lives in the bootloader EEPROM, so the card holds no `start.elf`/`bootcode.bin`. It only needs `config.txt`, `cmdline.txt`, the kernel and the `.dtb` files.

### 1.4 Toolchain

A cross-compiler (gcc + binutils + glibc) runs on x86_64 and generates aarch64 code. The stock Pi 5 config uses a **prebuilt Bootlin toolchain**, which only exists for x86_64 hosts (`depends on BR2_HOSTARCH = "x86_64"`).

---

## 2. Prepare Fedora

### 2.1 Packages

These are the required host tools from the manual's «System requirements», using Fedora package names:

```bash
sudo dnf install -y gcc gcc-c++ make which sed binutils diffutils bash patch gzip bzip2 \
                    perl tar cpio unzip rsync file bc findutils gawk wget git \
                    ncurses-devel openssl
```

- Install `perl`, not `perl-interpreter`. The latter lacks the modules Buildroot checks for.
- `ncurses-devel` is needed for `menuconfig`. `openssl` is used to hash the root password.
- Buildroot checks the host itself at build start (`support/dependencies/dependencies.sh`).
- Build as a normal user. Never run `sudo make`.

### 2.2 SSH key and git

```bash
ls ~/.ssh/id_ed25519.pub 2>/dev/null || ssh-keygen -t ed25519
cat ~/.ssh/id_ed25519.pub
```

`id_ed25519` is private and never leaves the laptop. `id_ed25519.pub` is public: it goes on the Pi and is safe to commit.

```bash
git config --global user.name  "Your Name"      # if not set yet
git config --global user.email "you@example.com"
```

### 2.3 Check

```bash
uname -m; nproc; free -g; df -h ~; gcc --version | head -1
```

---

## 3. Empty repo + Buildroot submodule

```bash
mkdir ~/buildroot-rpi5-ssh && cd ~/buildroot-rpi5-ssh
git init
git submodule add https://gitlab.com/buildroot.org/buildroot.git buildroot
git -C buildroot checkout 2026.08
echo "output/" > .gitignore
git add .gitignore .gitmodules buildroot
git commit -m "Add Buildroot 2026.08 as submodule"
```

- Your repo stores only the **commit hash** the submodule points to, not a copy of Buildroot. Anyone who clones it gets exactly the same Buildroot version.
- 2026.08 is the current stable release. For a product with a multi-year life, use the LTS branch (2025.02.x). GitHub mirror: `https://github.com/buildroot/buildroot.git`.
- `output/` holds gigabytes of build output, so it is not committed. Buildroot's own `.gitignore` already covers `dl/`.

```bash
git submodule status        # <hash> buildroot (2026.08)
```

All commands below run from the repo root.

---

## 4. Stock Pi 5 config and first build

Build the stock config first. This verifies the host and gets the slow parts (toolchain, kernel) out of the way.

### 4.1 Find and read the defconfig

```bash
make -C buildroot list-defconfigs | grep -i raspberrypi    # raspberrypi5_defconfig
cat buildroot/configs/raspberrypi5_defconfig
```

A defconfig stores **only differences from defaults**. Key lines:

| Line | Meaning |
|---|---|
| `BR2_aarch64=y`, `BR2_cortex_a76=y` | 64-bit ARM, tuned for BCM2712 |
| `BR2_TOOLCHAIN_EXTERNAL_BOOTLIN_AARCH64_GLIBC_STABLE=y` | prebuilt Bootlin toolchain (glibc) |
| `BR2_GLOBAL_PATCH_DIR="board/raspberrypi/patches"` | extra patches applied to packages |
| `BR2_DOWNLOAD_FORCE_CHECK_HASHES=y` | every download is SHA-256 checked |
| `BR2_SYSTEM_DHCP="eth0"` | DHCP on Ethernet |
| `BR2_ROOTFS_POST_BUILD_SCRIPT`, `…POST_IMAGE_SCRIPT` | stock scripts: HDMI console, assemble `sdcard.img` |
| `BR2_LINUX_KERNEL_CUSTOM_TARBALL_LOCATION=…raspberrypi,linux,…` | Raspberry Pi kernel fork (6.12): has the RP1 drivers (Ethernet, GPIO, UART) |
| `BR2_LINUX_KERNEL_DEFCONFIG="bcm2712"` | Raspberry Pi's kernel config for Pi 5 |
| `…_CONFIG_FRAGMENT_FILES="board/raspberrypi/linux-4k-page-size.fragment"` | 4 KB pages instead of 16 KB |
| `BR2_LINUX_KERNEL_INTREE_DTS_NAME=…` | device trees for both Pi 5 chip revisions |
| `BR2_PACKAGE_RPI_FIRMWARE=y`, `…CONFIG_FILE`, `…CMDLINE_FILE` | `config.txt` / `cmdline.txt` for the boot partition |
| `BR2_TARGET_ROOTFS_EXT2_4=y`, `…SIZE="120M"` | ext4 rootfs image |
| `BR2_PACKAGE_HOST_GENIMAGE=y` etc. | host tools to assemble the card image |

`buildroot/board/raspberrypi5` is a symlink to `board/raspberrypi`, which holds `config_5.txt`, `cmdline_5.txt`, `genimage.cfg.in`, the scripts and `readme.txt`.

### 4.2 Configure out of tree and build

```bash
make -C buildroot O=$PWD/output raspberrypi5_defconfig
make -C output menuconfig          # look around, exit without saving
make -C output 2>&1 | tee build-stock.log
```

- `O=` must be absolute, because a relative path resolves from the Buildroot dir. After this step, `make -C output <target>` is enough.
- menuconfig keys: arrows, `Enter` to open, `Esc Esc` to go back, `/` to search, `?` for help. If it says "display too small", enlarge the terminal.
- `>>>` lines show progress: host tools → toolchain → busybox → linux (the longest step) → rpi-firmware → post-build → rootfs.ext4 → post-image.

**`output/` layout:**

| Dir | Contents |
|---|---|
| `build/` | unpacked and built packages, one dir each |
| `host/` | toolchain and tools that run on the laptop |
| `staging/` | headers and libs for linking (sysroot) |
| `target/` | **future `/` on the Pi** (owned by you; root ownership is applied when packing) |
| `images/` | **results**: `Image`, `*.dtb`, `boot.vfat`, `rootfs.ext4`, `sdcard.img` |

`target/` is what goes inside `rootfs.ext4`. `rootfs.ext4` plus `boot.vfat` are combined into `sdcard.img`.

The stock image boots to `buildroot login:` (root, no password), but it has no SSH server. The next steps add one.

---

## 5. BR2_EXTERNAL tree

Editing files inside `buildroot/` mixes your changes with upstream and loses them on upgrade. A **br2-external** tree keeps them separate. Here the tree is the repo root.

### 5.1 Required files

`external.desc`:

```
name: MILTECH
desc: MilTech hw18 - Raspberry Pi 5 with SSH
```

The `name` (only `[A-Za-z0-9_]`) creates the variable **`BR2_EXTERNAL_MILTECH_PATH`**, an absolute path to the tree. Configs use it to reference your files. `desc` shows up in menus.

```bash
touch Config.in external.mk        # needed only for custom packages; empty is fine
mkdir configs
```

### 5.2 Save the starting defconfig

```bash
make -C output BR2_EXTERNAL=$PWD savedefconfig BR2_DEFCONFIG=$PWD/configs/miltech_pi5_defconfig
diff buildroot/configs/raspberrypi5_defconfig configs/miltech_pi5_defconfig && echo same
```

`BR2_EXTERNAL` is remembered in `output/.br2-external.mk`, so you don't pass it again.

### 5.3 ⚠️ Switch to your defconfig

```bash
make -C buildroot O=$PWD/output BR2_EXTERNAL=$PWD miltech_pi5_defconfig
grep BR2_DEFCONFIG output/.config     # …/configs/miltech_pi5_defconfig
```

`.config` remembers which defconfig it was loaded from. If you skip this step, a plain `make -C output savedefconfig` **overwrites `buildroot/configs/raspberrypi5_defconfig`** instead of your file.

```bash
git add external.desc Config.in external.mk configs
git commit -m "br2-external tree MILTECH + stock Pi 5 defconfig as starting point"
```

---

## 6. menuconfig: hostname, password, SSH, mDNS

The loop for every change: `make -C output menuconfig` → edit → Save → `make -C output savedefconfig && git diff configs/`. The diff shows which config line each menu choice produced.

| Menu path | Value |
|---|---|
| System configuration → System hostname | `miltech-br` |
| System configuration → System banner | `MilTech hw18 - Buildroot on Raspberry Pi 5` (`/etc/issue`) |
| System configuration → Root password | hash, see below |
| Target packages → Networking applications → dropbear | `[*]`, and inside it uncheck `client programs` |
| Target packages → Networking applications → avahi | `[*]` + `mDNS/DNS-SD daemon`, uncheck `IPv4LL …` |
| Filesystem images → ext2/3/4 → exact size | `256M` |

**Root password.** It is only used for console login, because SSH passwords get disabled in step 7. Enter a **hash**, not plain text, so the defconfig in git holds no password. Double every `$` so make doesn't expand it:

```bash
openssl passwd -6 | sed 's/\$/$$/g'      # paste the $$6$$… output into the field
```

**Dropbear vs OpenSSH:**
- Dropbear is a single small binary (server + scp + dropbearkey) with no SFTP.
- OpenSSH is several binaries plus OpenSSL.

**avahi** answers `miltech-br.local`. Without it SSH still works, but you have to find the IP yourself.

Expected diff:

```diff
+BR2_TARGET_GENERIC_HOSTNAME="miltech-br"
+BR2_TARGET_GENERIC_ISSUE="MilTech hw18 - Buildroot on Raspberry Pi 5"
+BR2_TARGET_GENERIC_ROOT_PASSWD="$$6$$…"
+BR2_PACKAGE_AVAHI=y
+# BR2_PACKAGE_AVAHI_AUTOIPD is not set
+BR2_PACKAGE_AVAHI_DAEMON=y
+BR2_PACKAGE_DROPBEAR=y
+# BR2_PACKAGE_DROPBEAR_CLIENT is not set
-BR2_TARGET_ROOTFS_EXT2_SIZE="120M"
+BR2_TARGET_ROOTFS_EXT2_SIZE="256M"
```

`# … is not set` is a change too: it disables an option that defaults to `y`.

```bash
make -C output 2>&1 | tee build.log      # adding packages: plain make is enough, minutes
ls output/target/etc/init.d/             # S50avahi-daemon, S50dropbear
git add configs && git commit -m "menuconfig: hostname, root password hash, dropbear, avahi, rootfs 256M"
```

---

## 7. Rootfs overlay

An overlay is a directory tree copied over `output/target` after the packages are installed. For example, `board/miltech-pi5/rootfs-overlay/etc/foo` ends up as `/etc/foo` on the Pi.

```bash
mkdir -p board/miltech-pi5/rootfs-overlay/etc/default board/miltech-pi5/rootfs-overlay/root/.ssh
```

### 7.1 Dropbear options

The init script `output/target/etc/init.d/S50dropbear` sources `/etc/default/dropbear` if it exists, and passes `$DROPBEAR_ARGS` to the server. It adds `-R` itself, which generates the host keys on first connection.

`board/miltech-pi5/rootfs-overlay/etc/default/dropbear`:

```sh
# Sourced by /etc/init.d/S50dropbear. -s: disable password login, keys only
DROPBEAR_ARGS="-s"
```

### 7.2 Public key

```bash
cp ~/.ssh/id_ed25519.pub board/miltech-pi5/rootfs-overlay/root/.ssh/authorized_keys
```

The key is baked into the image before the first boot. It's public, so committing it is fine and keeps clean-clone builds identical.

### 7.3 Enable the overlay

**System configuration → Root filesystem overlay directories**:

```
$(BR2_EXTERNAL_MILTECH_PATH)/board/miltech-pi5/rootfs-overlay
```

Relative paths here resolve from `buildroot/`, so use the external variable.

```bash
make -C output savedefconfig && git diff configs/
make -C output
ls -la output/target/root/.ssh/          # 755 / 644
git add board configs && git commit -m "rootfs overlay: dropbear key-only, authorized_keys"
```

The overlay is copied with `rsync --chmod=u=rwX,go=rX` (`SYSTEM_RSYNC` in `buildroot/system/system.mk`), so everything ends up world-readable. SSH best practice is 700/600, which the next step fixes. The files are owned by your user here; root ownership is applied under `fakeroot` when the rootfs is packed.

---

## 8. Post-build script

Post-build scripts run after packages and overlay are installed, **before** the rootfs images are created. They run from the Buildroot dir with `$1` = `output/target`. They can use env vars such as `TARGET_DIR`, `BINARIES_DIR`, `BR2_CONFIG` and `BR2_EXTERNAL_MILTECH_PATH`.

`board/miltech-pi5/post-build.sh`:

```sh
#!/bin/sh
set -eu
TARGET_DIR="$1"

chmod 700 "$TARGET_DIR/root/.ssh"
chmod 600 "$TARGET_DIR/root/.ssh/authorized_keys"

{
  echo "image=miltech-hw18"
  echo "build_date=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "buildroot=${BR2_VERSION_FULL:-unknown}"
} > "$TARGET_DIR/etc/miltech-build"
```

- `set -eu` makes any error fail the build, instead of silently producing an image without a key.
- `/etc/miltech-build` records what this firmware is and when it was built. `build_date` is UTC.

```bash
chmod +x board/miltech-pi5/post-build.sh
```

**System configuration → Custom scripts to run before creating filesystem images**: keep the stock script and append yours, separated by a space:

```
board/raspberrypi5/post-build.sh $(BR2_EXTERNAL_MILTECH_PATH)/board/miltech-pi5/post-build.sh
```

```bash
make -C output savedefconfig && git diff configs/
make -C output 2>&1 | tail -15          # "Executing post-build script …" for both
ls -la output/target/root/.ssh/          # drwx------ / -rw-------
git add board configs && git commit -m "post-build: ssh permissions, build info"
```

---

## 9. Pi 5 boot files and kernel tweaks

### 9.1 config.txt

The **bootloader** reads `config.txt` before Linux starts. By default the Pi 5 loads `kernel_2712.img`, but Buildroot names the kernel `Image`. Without `kernel=Image` the boot fails with LED code 0 long / 7 short ("kernel not found").

`board/miltech-pi5/config.txt`:

```ini
# Read by the Pi 5 EEPROM bootloader before Linux.
# https://www.raspberrypi.com/documentation/computers/config_txt.html
kernel=Image
arm_64bit=1
disable_overscan=1
disable_splash=1
```

The stock `config_5.txt` has only `kernel=Image` and `disable_overscan=1`. The Pi 5 always boots 64-bit, so `arm_64bit=1` is harmless but redundant.

**Target packages → Hardware handling → Firmware → rpi-firmware → Path to a file stored as boot/config.txt**:

```
$(BR2_EXTERNAL_MILTECH_PATH)/board/miltech-pi5/config.txt
```

### 9.2 cmdline.txt: keep the stock one

```
root=/dev/mmcblk0p2 rootwait console=tty1 console=ttyAMA10,115200
```

| Param | Meaning |
|---|---|
| `root=/dev/mmcblk0p2` | rootfs = microSD partition 2 |
| `rootwait` | wait for the card to appear instead of panicking |
| `console=tty1` | kernel messages and login on HDMI |
| `console=ttyAMA10,115200` | and on the Pi 5 debug UART (3-pin connector between the HDMI ports) |

`cmdline.txt` must be a single line.

```bash
make -C output savedefconfig && git diff configs/
git add board configs && git commit -m "config.txt for Pi 5"
```

### 9.3 ⚠️ Why `rpi-firmware-rebuild` is needed

The overlay and post-build scripts are applied on **every** `make`. `config.txt`, however, is copied by the `rpi-firmware` package, and only at **install time** (`RPI_FIRMWARE_INSTALL_CONFIG` in `package/rpi-firmware/rpi-firmware.mk`). Buildroot doesn't track config changes. So after changing `config.txt` or any rpi-firmware option, run:

```bash
make -C output rpi-firmware-rebuild all
cat output/images/rpi-firmware/config.txt
```

`<pkg>-rebuild` re-runs one package's build and install, and `all` re-assembles the images. Buildroot generates these targets for every package, named after its directory under `package/`.

### 9.4 ⚠️ DTB overlays: newer Pi 5 boards don't boot without them

The stock defconfig disables overlays (`# BR2_PACKAGE_RPI_FIRMWARE_INSTALL_DTB_OVERLAYS is not set`). That works on early boards with BCM2712 **C1**. Newer boards have the **D0** stepping (e.g. `Raspberry Pi 5 Model B Rev 1.1`), and on those the kernel panics ~1.5 s after it starts:

```
SError Interrupt on CPU0, code 0x00000000be000011 -- SError
pc : bcm2712_pull_config_set+0x64/0xd8
…
Kernel panic - not syncing: Asynchronous SError Interrupt
```

Without a monitor this looks like a network problem: the green LED is on, but the Ethernet LEDs stay off and the Pi never shows up on the router.

Why it happens:

- The base dtb describes the pin controller as `brcm,bcm2712-pinctrl`, so the driver uses the C1 register layout.
- D0 has a different layout. On D0 boards the bootloader applies `overlays/bcm2712d0.dtbo`, which switches the controller to `brcm,bcm2712d0-pinctrl`.
- Without the `overlays/` directory nothing gets switched. The driver writes to C1 addresses, the bus returns an SError, and the kernel panics.

Fix: **Target packages → Hardware handling → Firmware → rpi-firmware → `[*] Install DTB overlays`**.

```bash
make -C output savedefconfig && git diff configs/       # the "is not set" line disappears (default is y)
make -C output rpi-firmware-rebuild all
ls output/images/rpi-firmware/overlays/bcm2712d0.dtbo
git add configs && git commit -m "rpi-firmware: install DTB overlays (Pi 5 D0 boot fix)"
```

- No `linux-rebuild` is needed. The RPi kernel already builds dtbs with `-@`, so the `__symbols__` that overlays require are present.
- The overlays add ~2 MB (~360 files), which fits in the 32 MB boot partition. C1 boards are unaffected.

### 9.5 Fan: kernel config fragment

With an Active Cooler, the fan runs **at full speed all the time**:

- The fan is in the device tree (`cooling_fan`, `compatible = "pwm-fan"`), and the thermal sensor driver is built in.
- `bcm2712_defconfig` builds the `pwm-fan` driver as a **module** (`=m`). Raspberry Pi OS loads it via udev. Here `/dev` is devtmpfs-only, so nothing autoloads modules.
- With no driver, nothing drives the PWM and the fan runs at max speed. That is noisy and wastes power.

A quick test: `modprobe pwm-fan` on the Pi quiets the fan until the next reboot.

Permanent options:

| Option | Trade-off |
|---|---|
| mdev (System configuration → /dev management) | loads **every** module for present hardware |
| `etc/modules-load.d/fan.conf` with `pwm-fan` in the overlay | loaded by `S11modules`, but only once the init scripts run |
| **build the driver into the kernel** (chosen) | fan controlled from kernel boot; one kernel rebuild |

The kernel config = base defconfig (`bcm2712`) + **fragments** applied on top. One fragment already exists (`linux-4k-page-size.fragment`); add yours next to it.

`board/miltech-pi5/linux-fan.fragment`:

```
# pwm-fan built in: the Pi 5 Active Cooler is controlled from boot, no module loading needed
CONFIG_SENSORS_PWM_FAN=y
```

**Kernel → Additional configuration fragment files** is a space-separated list. Keep the stock fragment:

```
board/raspberrypi/linux-4k-page-size.fragment $(BR2_EXTERNAL_MILTECH_PATH)/board/miltech-pi5/linux-fan.fragment
```

```bash
make -C output savedefconfig && git diff configs/
make -C output linux-reconfigure all
grep SENSORS_PWM_FAN output/build/linux-custom/.config    # =y
find output/target/lib/modules -name 'pwm-fan*'           # empty: no module anymore
git add board configs && git commit -m "linux: build pwm-fan into the kernel (Pi 5 fan at full speed)"
```

- Use **`linux-reconfigure`**, not `linux-rebuild`. Fragments are merged at the *configure* step, and `-rebuild` starts at *build*, so it would miss the new fragment.
- `make -C output linux-menuconfig` changes live only in `output/` and are lost with it. A fragment lives in git.

---

## 10. Final build and offline image check

```bash
make -C output 2>&1 | tee build.log
cat output/images/rpi-firmware/config.txt                # your file (9.3)
ls output/images/rpi-firmware/overlays/bcm2712d0.dtbo    # D0 overlay (9.4)
```

### 10.1 How `sdcard.img` is assembled

The stock post-image script generates a `genimage` config:

```
image boot.vfat {            ← partition 1: FAT32, 32 MB
	vfat { files = { "bcm2712-rpi-5-b.dtb", …, "rpi-firmware/config.txt", "rpi-firmware/cmdline.txt", "rpi-firmware/overlays", "Image" } }
	size = 32M
}
image sdcard.img {
	hdimage { }                ← MBR partition table
	partition boot   { partition-type = 0xC  bootable = "true" image = "boot.vfat" }
	partition rootfs { partition-type = 0x83 image = "rootfs.ext4" }
}
```

The first block builds the FAT filesystem image. The second lays out the whole disk and places the partition images into it. `sdcard.img` is a byte-for-byte image of the **whole card**.

```bash
fdisk -l output/images/sdcard.img      # img1 32M FAT32 (boot), img2 256M Linux
```

### 10.2 Inspect the rootfs before flashing

```bash
sudo mount -o loop,ro output/images/rootfs.ext4 /mnt
ls -ln /mnt/root/.ssh/                    # -rw------- 0 0 authorized_keys (root-owned)
cat /mnt/etc/default/dropbear /mnt/etc/hostname /mnt/etc/miltech-build
sudo grep '^root:' /mnt/etc/shadow        # root:$6$… (single $ after make expansion)
sudo umount /mnt
```

Root ownership (`0 0`) is applied here, under `fakeroot`. In `output/target` the files were owned by you.

On the host, `etc/dropbear` shows up as a broken symlink (→ `/var/run/dropbear`). That is expected: the link resolves on the Pi. If a file manager offers to delete it, cancel.

---

## 11. Flash the microSD

⚠️ Flashing **erases** the card.

```bash
lsblk -o NAME,SIZE,TYPE,TRAN,RM,MODEL,MOUNTPOINTS     # before and after inserting the card
DEV=/dev/sdX                                         # whole disk (sdb, not sdb1); never nvme0n1
sudo umount ${DEV}?* 2>/dev/null
sudo dd if=output/images/sdcard.img of=$DEV bs=4M conv=fsync status=progress
```

`dd` doesn't ask for confirmation, so **check `DEV` three times**. `conv=fsync` waits until the data is physically written.

GUI alternatives:
- Raspberry Pi Imager → *Use Custom*. Skip its customization dialog, which only applies to Raspberry Pi OS.
- GNOME Disks → *Restore Disk Image…*.

Re-insert the card. Fedora mounts both partitions. Boot should contain `config.txt`, `cmdline.txt`, `Image`, `*.dtb` and `overlays/`. Eject before removing the card:

```bash
for p in ${DEV}?*; do udisksctl unmount -b $p; done   # unmount both partitions (no sudo)
udisksctl power-off -b $DEV                           # flush and power off the reader; now safe to pull
```

`udisksctl` is what the file manager's Eject button uses. If a partition is busy, `cd` out of it first.

---

## 12. Network, first boot, SSH

### 12.1 Network

**A. Router:** connect the Pi to a LAN port. The laptop must be on the same network. The Pi gets an address via DHCP.

**B. Direct cable to the laptop:** NetworkManager can share the connection and run DHCP on it (`10.42.0.0/24`):

```bash
nmcli device status                         # find the wired interface, e.g. enp…
nmcli connection add type ethernet ifname enpXsY con-name pi-direct ipv4.method shared ipv6.method disabled
nmcli connection up pi-direct
sudo firewall-cmd --zone=nm-shared --add-service=mdns     # so *.local resolves
sudo firewall-cmd --zone=nm-shared --add-service=mdns --permanent   # and still does after a reboot
```

Or get the IP with `ip neigh show dev enpXsY`.

### 12.2 Boot

Insert the card, connect Ethernet, then power on. A monitor is optional (use **HDMI0**, next to USB-C).
- The LED goes red → green while booting.
- A blinking pattern is an error code; see «LED warning codes».
- On HDMI you should see `Starting dropbear sshd: OK`, then the banner and `miltech-br login:`.

A power supply under 5 A can limit USB current, but it does not stop the Pi from booting. If the Pi never shows up on the network, check 9.4 first.

### 12.3 Log in

```bash
ping -c 3 miltech-br.local
ssh root@miltech-br.local                 # accept the host key the first time; no password asked
```

`Permission denied (publickey)` usually means ssh didn't offer the right key. Without an agent, it only tries the default names (`id_ed25519`, `id_rsa`, …). If the key in `authorized_keys` has another name, point ssh to it in `~/.ssh/config`:

```
Host miltech-br.local 10.42.0.*
    User root
    IdentityFile ~/.ssh/id_ed25519_rpi5
    IdentitiesOnly yes
```

Each reflash creates new host keys, so ssh then refuses with `Host key verification failed`. Remove the old key: `ssh-keygen -R miltech-br.local; ssh-keygen -R <pi-ip>`.

Check that password login is really disabled:

```bash
ssh -o PubkeyAuthentication=no -o PreferredAuthentications=password root@miltech-br.local
#   Permission denied (publickey).
```

The server only offers `publickey`, which shows `-s` is active.

---

## 13. Checks on the Pi

```bash
cat /etc/os-release /etc/miltech-build     # Buildroot 2026.08, your build date
uname -a                                   # Linux miltech-br 6.12.x … aarch64
cat /proc/cmdline                          # what the kernel actually received
df -h /; free -m; ps                       # tiny footprint
netstat -tuln                              # only :22/tcp (dropbear) and :5353/udp (avahi mDNS)
ls -la /root/.ssh                          # drwx------, -rw-------
ls /etc/dropbear                           # host keys created on first connection (-R)
cat /sys/class/hwmon/*/name                # includes pwmfan (9.5)
cat /sys/class/thermal/thermal_zone0/temp  # e.g. 45000 = 45 °C; fan stays off below ~50 °C
```

- `uname` shows `-v8-16k` even though pages are 4 KB. The suffix comes from `bcm2712_defconfig`; check `KernelPageSize` in `/proc/self/smaps` instead.
- Dropbear has no SFTP, and OpenSSH 9+ `scp` uses SFTP by default. Use `scp -O`:

```bash
scp -O /tmp/hello.txt root@miltech-br.local:/root/
```

- Host keys survive a `reboot` because the rootfs is writable. Shut down with `poweroff`, and unplug once the green LED goes off.
- `date` may show 1970: the image has no NTP, and the RTC has no battery.

---

## 14. Reproduce from a clean clone

```bash
git push
cd /tmp
git clone --recurse-submodules <repo-url> rpi5-check && cd rpi5-check
git -C buildroot describe --tags           # 2026.08: pinned by the submodule
make -C buildroot O=$PWD/output BR2_EXTERNAL=$PWD miltech_pi5_defconfig
make -C output
```

- Without `--recurse-submodules`, run `git submodule update --init`.
- The last two commands are the entire build instructions. Put them in `README.md`.
- To reuse downloads, run `export BR2_DL_DIR=~/buildroot-rpi5-ssh/buildroot/dl` before `make`.
- The generated `.config` matches the original, except `BR2_EXTERNAL_MILTECH_VERSION` (the tree's git version).

---

# Part 2: ROS 2

> ⚠️ **Draft.** This part was written before the first ROS 2 build. Expect to fix details in sections 18–19 along the way, and update this file to match.

```
 Fedora (laptop), container ros:jazzy                               Raspberry Pi 5
 ────────────────────────────────────────────────────────────       ──────────────
 15 demo talker/listener
 16 miltech_counter: counter_pub ──/counter──► counter_sub
 17 ros2 node/topic/service …
 18 meta-ros2/ (2nd BR2_EXTERNAL): ros2 package = colcon + cross toolchain
 19 make ──► sdcard.img with /opt/ros/jazzy ─────────────────────►  boot
 20 ros2 topic echo /counter ◄──────── DDS over Ethernet ─────────── counter_pub
```

Both sides use **ROS 2 Jazzy**, the LTS release supported until 2029.

**What it adds to the repo:**

```
buildroot-rpi5-ssh/
└── meta-ros2/                         second BR2_EXTERNAL tree, name: ROS2
    ├── external.desc, Config.in, external.mk
    ├── host-requirements.txt          host Python tools: colcon, code generators
    ├── ros2.repos                     ROS 2 source repos, pinned
    ├── package/ros2/
    │   ├── Config.in
    │   ├── ros2.mk                    colcon build with Buildroot's toolchain
    │   └── ros2.sh                    /etc/profile.d/ros2.sh on the Pi
    └── src/
        ├── miltech_counter/           your nodes (committed)
        └── rclcpp/, rcl/, …           ROS 2 sources from vcs import (git-ignored)
```

---

## 15. ROS 2 on the laptop

ROS 2 publishes binary packages for Ubuntu but not for Fedora. On the laptop, run it in the official Ubuntu-based container instead. `--net=host` lets the container discover nodes on the Pi later (section 20).

```bash
mkdir -p meta-ros2/src/miltech_counter/src
podman run -d --name ros2 --net=host -e RMW_IMPLEMENTATION=rmw_cyclonedds_cpp \
  -v $PWD/meta-ros2/src/miltech_counter:/ws/src/miltech_counter:Z \
  docker.io/library/ros:jazzy sleep infinity
podman exec ros2 bash -c 'apt-get update && apt-get install -y ros-jazzy-demo-nodes-cpp ros-jazzy-rmw-cyclonedds-cpp
  echo "source /opt/ros/jazzy/setup.bash; [ -f /ws/install/setup.bash ] && source /ws/install/setup.bash" >> /root/.bashrc'
```

- `RMW_IMPLEMENTATION=rmw_cyclonedds_cpp` selects the DDS implementation. Jazzy's default is Fast DDS, but the Pi image uses Cyclone DDS (section 18). Use the same on both sides.
- Open each terminal with `podman exec -it ros2 bash`.
- The container keeps its state until `podman rm -f ros2`. Your code is on the host, so removing the container loses nothing.

Run the official demo nodes, each in its own terminal:

```bash
ros2 run demo_nodes_cpp talker      # Publishing: 'Hello World: 1'
ros2 run demo_nodes_cpp listener    # I heard: [Hello World: 1]
ros2 node list                      # /talker /listener
ros2 topic list                     # /chatter /parameter_events /rosout
```

---

## 16. Counter nodes

One package with three nodes. Each node is a small C++ program built on `rclcpp`.

| Node | Does |
|---|---|
| `counter_pub` | publishes 1, 2, 3… on `/counter` (`std_msgs/msg/UInt32`) once per second, and serves `/counter/enable` (`std_srvs/srv/SetBool`): `true` starts counting, `false` stops it |
| `counter_sub` | prints every number received on `/counter` |
| `counter_switch` | calls `/counter/enable` (extra task): `counter_switch on` or `counter_switch off` |

`meta-ros2/src/miltech_counter/package.xml`:

```xml
<?xml version="1.0"?>
<package format="3">
  <name>miltech_counter</name>
  <version>0.1.0</version>
  <description>Counter publisher/subscriber with a start/stop service</description>
  <maintainer email="you@example.com">Your Name</maintainer>
  <license>Apache-2.0</license>

  <buildtool_depend>ament_cmake</buildtool_depend>
  <depend>rclcpp</depend>
  <depend>std_msgs</depend>
  <depend>std_srvs</depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

`meta-ros2/src/miltech_counter/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.8)
project(miltech_counter)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)

foreach(node counter_pub counter_sub counter_switch)
  add_executable(${node} src/${node}.cpp)
  target_link_libraries(${node} rclcpp::rclcpp ${std_msgs_TARGETS} ${std_srvs_TARGETS})
  install(TARGETS ${node} DESTINATION lib/${PROJECT_NAME})
endforeach()

ament_package()
```

`src/counter_pub.cpp`:

```cpp
// Publishes 1, 2, 3... on /counter every second.
// Service /counter/enable (std_srvs/SetBool) starts and stops counting.
#include <chrono>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/u_int32.hpp>
#include <std_srvs/srv/set_bool.hpp>

using namespace std::chrono_literals;
using std_msgs::msg::UInt32;
using std_srvs::srv::SetBool;

class CounterPub : public rclcpp::Node
{
public:
  CounterPub() : Node("counter_pub")
  {
    pub_ = create_publisher<UInt32>("counter", 10);
    timer_ = create_wall_timer(1s, [this] {
      if (!enabled_) return;
      UInt32 msg;
      msg.data = ++count_;
      RCLCPP_INFO(get_logger(), "publish %u", msg.data);
      pub_->publish(msg);
    });
    srv_ = create_service<SetBool>("counter/enable",
      [this](const SetBool::Request::SharedPtr req, SetBool::Response::SharedPtr res) {
        enabled_ = req->data;
        res->success = true;
        res->message = enabled_ ? "started" : "stopped";
        RCLCPP_INFO(get_logger(), "%s", res->message.c_str());
      });
  }

private:
  bool enabled_ = true;
  uint32_t count_ = 0;
  rclcpp::Publisher<UInt32>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Service<SetBool>::SharedPtr srv_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CounterPub>());
  rclcpp::shutdown();
}
```

`src/counter_sub.cpp`:

```cpp
// Prints every number received on /counter.
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/u_int32.hpp>

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = rclcpp::Node::make_shared("counter_sub");
  auto log = node->get_logger();
  auto sub = node->create_subscription<std_msgs::msg::UInt32>("counter", 10,
    [log](const std_msgs::msg::UInt32 & msg) { RCLCPP_INFO(log, "got %u", msg.data); });
  rclcpp::spin(node);
  rclcpp::shutdown();
}
```

`src/counter_switch.cpp`:

```cpp
// Starts or stops counter_pub: counter_switch on|off
#include <chrono>
#include <cstdio>
#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/set_bool.hpp>

using namespace std::chrono_literals;
using std_srvs::srv::SetBool;

int main(int argc, char ** argv)
{
  auto args = rclcpp::init_and_remove_ros_arguments(argc, argv);
  if (args.size() != 2 || (args[1] != "on" && args[1] != "off")) {
    fprintf(stderr, "usage: counter_switch on|off\n");
    return 2;
  }
  auto node = rclcpp::Node::make_shared("counter_switch");
  auto client = node->create_client<SetBool>("counter/enable");
  if (!client->wait_for_service(5s)) {
    RCLCPP_ERROR(node->get_logger(), "/counter/enable is not available");
    return 1;
  }
  auto req = std::make_shared<SetBool::Request>();
  req->data = args[1] == "on";
  auto future = client->async_send_request(req);
  if (rclcpp::spin_until_future_complete(node, future, 5s) != rclcpp::FutureReturnCode::SUCCESS) {
    RCLCPP_ERROR(node->get_logger(), "no response");
    return 1;
  }
  RCLCPP_INFO(node->get_logger(), "%s", future.get()->message.c_str());
  rclcpp::shutdown();
}
```

Build and run in the container:

```bash
cd /ws && colcon build && source install/setup.bash
ros2 run miltech_counter counter_pub    # terminal 1: [counter_pub]: publish 1, 2, 3…
ros2 run miltech_counter counter_sub    # terminal 2: [counter_sub]: got 1, 2, 3…
```

```bash
git add meta-ros2/src/miltech_counter
git commit -m "miltech_counter: counter publisher/subscriber + start/stop service"
```

---

## 17. Check with the ROS 2 CLI

Run these in a third terminal while both nodes are running:

```bash
ros2 node list                    # /counter_pub /counter_sub
ros2 node info /counter_pub       # publishes /counter, serves /counter/enable
ros2 topic list -t                # /counter [std_msgs/msg/UInt32]
ros2 topic info /counter -v       # 1 publisher, 1 subscriber, QoS of each
ros2 topic echo /counter          # data: 17 …
ros2 topic hz /counter            # average rate: 1.000
ros2 service list -t              # /counter/enable [std_srvs/srv/SetBool]
ros2 interface show std_srvs/srv/SetBool
```

Stop and start the counter (extra task):

```bash
ros2 service call /counter/enable std_srvs/srv/SetBool "{data: false}"   # via the CLI
ros2 run miltech_counter counter_switch on                             # via your own node
```

While the counter is stopped, `counter_sub` prints nothing. After `on`, it continues from the last number.

---

## 18. ROS 2 layer for Buildroot

Buildroot accepts several BR2_EXTERNAL trees, separated by colons: `BR2_EXTERNAL=dir1:dir2`. A second tree is Buildroot's version of a Yocto meta-layer, and it keeps everything ROS-related out of the board tree.

Buildroot has no ROS 2 packages. The layer defines one package, `ros2`. It runs **colcon** with Buildroot's CMake toolchain file and cross-compiles a minimal C++ ROS 2 (about 60 packages) together with `miltech_counter`. The minimal set is `rclcpp`, Cyclone DDS, `std_msgs` and `std_srvs`. Python, `rclpy` and the `ros2` CLI are not included, so the image stays small. Inspect the Pi from the laptop instead (section 20).

### 18.1 Tree files

```bash
mkdir -p meta-ros2/package/ros2
```

`meta-ros2/external.desc`:

```
name: ROS2
desc: ROS 2 Jazzy layer (rclcpp + Cyclone DDS)
```

`meta-ros2/Config.in`:

```
source "$BR2_EXTERNAL_ROS2_PATH/package/ros2/Config.in"
```

`meta-ros2/external.mk`:

```make
include $(sort $(wildcard $(BR2_EXTERNAL_ROS2_PATH)/package/*/*.mk))
```

### 18.2 ROS 2 sources

ROS 2 consists of many git repos, and a `.repos` file lists them for `vcs` (vcstool). The original vcstool package breaks on Python 3.14 (`No module named pkg_resources`), so install its maintained fork `vcs2l`. The full Jazzy list has ~100 repos. `rosinstall_generator` builds a shorter list from the official package index: it starts from the packages you name and follows their declared dependencies.

```bash
sudo dnf install pipx && pipx install vcs2l && pipx install rosinstall-generator   # vcs2l: maintained fork of vcstool, same `vcs` command
cd meta-ros2
rosinstall_generator --rosdistro jazzy --deps --upstream --format repos \
    rclcpp std_msgs std_srvs rmw_cyclonedds_cpp \
  --exclude rmw_fastrtps_cpp rmw_fastrtps_dynamic_cpp rmw_connextdds \
    rosidl_typesupport_fastrtps_c rosidl_typesupport_fastrtps_cpp rosidl_dynamic_typesupport_fastrtps \
    rosidl_generator_py rosidl_generator_rs iceoryx_binding_c iceoryx_posh iceoryx_hoofs \
    ament_lint_auto ament_lint_common ament_cmake_gtest ament_cmake_gmock ament_cmake_google_benchmark \
    ament_cmake_pytest mimick_vendor performance_test_fixture test_msgs osrf_testing_tools_cpp \
    launch_testing_ament_cmake \
  > ros2.repos
vcs import --input ros2.repos src
cd ..
```

- `--upstream`: take each repo from GitHub at its Jazzy **release tag** (e.g. `rclcpp` 28.1.22), so the list is already pinned. To update, run the generator again.
- `--exclude`: without it you get 49 repos. Most of the extra ones are other DDS implementations, Python and Rust message bindings, and test frameworks. Excluding them leaves about 30. `rmw_implementation` lists all DDS implementations as dependencies, and colcon would build any of them it finds in `src/`.
- Four test-only repos remain: `ament_lint`, `launch`, `osrf_pycommon` and `uncrustify_vendor`. The `test_msgs` package inside `rcl_interfaces` is test-only too. colcon's `--packages-up-to` also follows test dependencies and would build them, so `ros2.mk` puts a `COLCON_IGNORE` file in each of them (18.4).
- If you keep the exclude names in a shell variable, zsh passes `$X` as a single word, and the generator then ignores the whole list. Write `${=X}` instead, or list the names directly as above.

Ignore the downloaded sources and keep your package. Add to `.gitignore`:

```
meta-ros2/src/*
!meta-ros2/src/miltech_counter/
```

### 18.3 Host tools

`colcon` and the ROS message code generators (`rosidl`, written in Python) run on the build host. The package creates a venv for them in `output/host/`. Use `/usr/bin/python3` there, not whatever `python3` comes first in `PATH`, since that could be conda.

`meta-ros2/host-requirements.txt`:

```
colcon-common-extensions
catkin_pkg
empy
lark
setuptools
```

### 18.4 The `ros2` package

`meta-ros2/package/ros2/Config.in`:

```
config BR2_PACKAGE_ROS2
	bool "ros2"
	depends on BR2_INSTALL_LIBSTDCPP
	depends on BR2_TOOLCHAIN_HAS_THREADS
	depends on BR2_USE_WCHAR # spdlog
	select BR2_PACKAGE_LIBYAML
	select BR2_PACKAGE_SPDLOG
	help
	  Minimal ROS 2 Jazzy (rclcpp, Cyclone DDS, std_msgs, std_srvs)
	  and the miltech_counter nodes, installed in /opt/ros/jazzy.
```

`meta-ros2/package/ros2/ros2.mk`:

```make
################################################################################
#
# ros2
#
################################################################################

ROS2_VERSION = jazzy
ROS2_SITE = $(BR2_EXTERNAL_ROS2_PATH)/src
ROS2_SITE_METHOD = local
ROS2_LICENSE = Apache-2.0, EPL-2.0 (cyclonedds)
ROS2_DEPENDENCIES = libyaml spdlog

ROS2_PREFIX = /opt/ros/$(ROS2_VERSION)
ROS2_VENV = $(HOST_DIR)/ros2-venv

# colcon and the rosidl code generators run on the build host
define ROS2_HOST_TOOLS
	/usr/bin/python3 -m venv $(ROS2_VENV)
	$(ROS2_VENV)/bin/pip install -q -r $(BR2_EXTERNAL_ROS2_PATH)/host-requirements.txt
endef
ROS2_PRE_CONFIGURE_HOOKS += ROS2_HOST_TOOLS

# Test-only repos pulled in by test dependencies: colcon would build them too
# (uncrustify_vendor even tries to git-clone uncrustify). Hide them from colcon.
define ROS2_IGNORE_TEST_REPOS
	touch $(addsuffix /COLCON_IGNORE,$(addprefix $(@D)/,ament_lint launch osrf_pycommon uncrustify_vendor \
		rcl_interfaces/test_msgs))
endef
ROS2_POST_RSYNC_HOOKS += ROS2_IGNORE_TEST_REPOS

# Each ROS package builds against the ones before it, so colcon installs
# straight into staging. Cyclone options: no host-only tools, no iceoryx/SSL.
# TRACETOOLS_DISABLED: no LTTng tracing (would need lttng-ust).
define ROS2_BUILD_CMDS
	cd $(@D) && $(TARGET_MAKE_ENV) RMW_IMPLEMENTATION=rmw_cyclonedds_cpp \
		$(ROS2_VENV)/bin/colcon --log-base log build \
		--merge-install --install-base $(STAGING_DIR)$(ROS2_PREFIX) \
		--packages-up-to rmw_cyclonedds_cpp miltech_counter \
		--event-handlers console_cohesion+ \
		--cmake-args \
			-DCMAKE_TOOLCHAIN_FILE=$(HOST_DIR)/share/buildroot/toolchainfile.cmake \
			-DCMAKE_BUILD_TYPE=Release \
			-DBUILD_TESTING=OFF \
			-DPython3_EXECUTABLE=$(ROS2_VENV)/bin/python3 \
			-DBUILD_IDLC=OFF -DBUILD_DDSPERF=OFF -DENABLE_SHM=OFF -DENABLE_SSL=OFF \
			-DTRACETOOLS_DISABLED=ON
endef

# runtime files only: no headers, CMake files or Python modules
define ROS2_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)$(ROS2_PREFIX)
	rsync -a --exclude include --exclude cmake --exclude 'python3*' \
		$(STAGING_DIR)$(ROS2_PREFIX)/ $(TARGET_DIR)$(ROS2_PREFIX)/
	$(INSTALL) -D -m 0644 $(ROS2_PKGDIR)/ros2.sh $(TARGET_DIR)/etc/profile.d/ros2.sh
endef

$(eval $(generic-package))
```

- `SITE_METHOD = local`: Buildroot copies `src/` into `output/build/ros2-jazzy/`. After you edit the nodes, `make ros2-rebuild` copies them again.
- `--packages-up-to`: colcon builds only these packages and what they depend on. Python and test packages in the same repos are skipped.
- `libyaml` and `spdlog` come from Buildroot. Their versions match what `libyaml_vendor` and `spdlog_vendor` look for, so nothing is downloaded during the build.
- `rmw_cyclonedds_cpp` is listed explicitly because `rcl` only finds a DDS implementation at runtime.

`meta-ros2/package/ros2/ros2.sh` (on the Pi there is no `bash` for `setup.bash`, so set the variables directly):

```sh
# ROS 2 runtime environment
export AMENT_PREFIX_PATH=/opt/ros/jazzy
export LD_LIBRARY_PATH=/opt/ros/jazzy/lib
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export PATH=$PATH:/opt/ros/jazzy/lib/miltech_counter
```

`AMENT_PREFIX_PATH` is needed at runtime. ROS uses it to find and load the DDS and message type support libraries by name.

```bash
git add .gitignore meta-ros2
git commit -m "meta-ros2: br2-external layer with minimal ROS 2 Jazzy + miltech_counter"
```

---

## 19. Build the image with ROS 2

Load the defconfig with **both** trees, then enable the package:

```bash
make -C buildroot O=$PWD/output BR2_EXTERNAL=$PWD:$PWD/meta-ros2 miltech_pi5_defconfig
make -C output menuconfig     # External options → ROS 2 Jazzy layer → [*] ros2
make -C output savedefconfig
git diff configs/             # + BR2_PACKAGE_ROS2=y
make -C output
```

- If a later build runs without `meta-ros2` in `BR2_EXTERNAL`, Buildroot silently drops `BR2_PACKAGE_ROS2`. From now on, section 14's build command is `make -C buildroot O=$PWD/output BR2_EXTERNAL=$PWD:$PWD/meta-ros2 miltech_pi5_defconfig`. On a clean clone, run `vcs import --input meta-ros2/ros2.repos meta-ros2/src` first.
- The `ros2` package takes about 10–20 min. If it fails, colcon prints the failing package and keeps logs in `output/build/ros2-jazzy/log/latest_build/`.
- If `mkfs.ext4` runs out of space, raise `BR2_TARGET_ROOTFS_EXT2_SIZE` (currently 256M).

Check without the Pi:

```bash
file output/target/opt/ros/jazzy/lib/miltech_counter/counter_pub   # ELF 64-bit … ARM aarch64
du -sh output/target/opt/ros                                       # tens of MB
ls output/target/opt/ros/jazzy/lib/librmw_cyclonedds_cpp.so
```

```bash
git commit -am "defconfig: enable ROS 2"
```

---

## 20. Run the nodes on the Pi

Flash the image as in section 11, then log in with `ssh root@miltech-br.local`. `/etc/profile.d/ros2.sh` has already set up the environment.

```bash
counter_sub &        # got 1, 2, 3…
counter_pub          # publish 1, 2, 3…
```

In a second SSH session, test the service:

```bash
counter_switch off   # counter_pub: stopped
counter_switch on    # counter_pub: started
```

**From the laptop.** The Pi has no `ros2` CLI, but DDS works across the network. With the nodes running on the Pi, the laptop container sees them too:

```bash
ros2 node list                          # /counter_pub /counter_sub (on the Pi)
ros2 topic echo /counter
ros2 run miltech_counter counter_switch off     # stops the counter on the Pi
```

- **Direct cable:** the `nm-shared` firewall zone blocks DDS traffic from the Pi. Allow UDP from the Pi's subnet until the next reboot:

  ```bash
  sudo firewall-cmd --zone=nm-shared --add-rich-rule='rule family=ipv4 source address=10.42.0.0/24 protocol value=udp accept'
  ```

- Both sides must use the same `ROS_DOMAIN_ID` (default 0). If other ROS 2 users are on the same network, pick another ID on both sides, e.g. `export ROS_DOMAIN_ID=42`.

---

## Sources

**Buildroot 2026.08**
- [User manual](https://buildroot.org/downloads/manual/manual.html): System requirements, Building out-of-tree, Understanding when a full rebuild is necessary, Rebuilding packages, Recommended directory structure, Custom top Makefile, Keeping customizations outside of Buildroot, Storing the Buildroot configuration, Customizing the generated target filesystem
- [Downloads](https://buildroot.org/download.html) · [GitLab repo](https://gitlab.com/buildroot.org/buildroot/)
- Files: [`configs/raspberrypi5_defconfig`](https://gitlab.com/buildroot.org/buildroot/-/blob/2026.08/configs/raspberrypi5_defconfig), [`board/raspberrypi/readme.txt`](https://gitlab.com/buildroot.org/buildroot/-/blob/2026.08/board/raspberrypi/readme.txt), [`package/dropbear/S50dropbear`](https://gitlab.com/buildroot.org/buildroot/-/blob/2026.08/package/dropbear/S50dropbear), [`system/system.mk`](https://gitlab.com/buildroot.org/buildroot/-/blob/2026.08/system/system.mk), [`linux/Config.in`](https://gitlab.com/buildroot.org/buildroot/-/blob/2026.08/linux/Config.in), [`board/raspberrypi/linux-4k-page-size.fragment`](https://gitlab.com/buildroot.org/buildroot/-/blob/2026.08/board/raspberrypi/linux-4k-page-size.fragment), [`package/initscripts/init.d/S11modules`](https://gitlab.com/buildroot.org/buildroot/-/blob/2026.08/package/initscripts/init.d/S11modules), [`package/busybox/S10mdev`](https://gitlab.com/buildroot.org/buildroot/-/blob/2026.08/package/busybox/S10mdev)

**Raspberry Pi** ([docs](https://www.raspberrypi.com/documentation/computers/))
- [config.txt → Boot Options](https://www.raspberrypi.com/documentation/computers/config_txt.html#boot-options) · [Kernel command line](https://www.raspberrypi.com/documentation/computers/configuration.html#cmdline) · [UARTs](https://www.raspberrypi.com/documentation/computers/configuration.html#primary-and-secondary-uart)
- [LED warning codes](https://www.raspberrypi.com/documentation/computers/configuration.html#led-warning-flash-codes) · [Boot EEPROM](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#raspberry-pi-boot-eeprom) · [Imager](https://www.raspberrypi.com/documentation/computers/getting-started.html#install-using-imager) · [SSH without a password](https://www.raspberrypi.com/documentation/computers/remote-access.html#configure-ssh-without-a-password)
- RPi kernel 6.12: `drivers/pinctrl/bcm/pinctrl-bcm2712.c`, `arch/arm64/boot/dts/overlays/bcm2712d0-overlay.dts`
- [Mender Hub: Asynchronous SError Interrupt on RPi5](https://hub.mender.io/t/asynchronous-serror-interrupt-rpi5-boot-issue/7275): the same panic, fixed with `bcm2712d0.dtbo`

**ROS 2 Jazzy** ([docs](https://docs.ros.org/en/jazzy/))
- Tutorials: [C++ publisher and subscriber](https://docs.ros.org/en/jazzy/Tutorials/Beginner-Client-Libraries/Writing-A-Simple-Cpp-Publisher-And-Subscriber.html) · [C++ service and client](https://docs.ros.org/en/jazzy/Tutorials/Beginner-Client-Libraries/Writing-A-Simple-Cpp-Service-And-Client.html) · [Understanding topics](https://docs.ros.org/en/jazzy/Tutorials/Beginner-CLI-Tools/Understanding-ROS2-Topics/Understanding-ROS2-Topics.html) · [Understanding services](https://docs.ros.org/en/jazzy/Tutorials/Beginner-CLI-Tools/Understanding-ROS2-Services/Understanding-ROS2-Services.html)
- [`ros2.repos` for Jazzy](https://github.com/ros2/ros2/blob/jazzy/ros2.repos) · [rosinstall_generator](https://github.com/ros-infrastructure/rosinstall_generator) · [ros container images](https://hub.docker.com/_/ros) · [Cross-compiling ROS 2 packages (design article)](https://design.ros2.org/articles/cc_build_tools.html)
- Buildroot manual: Using several external trees, Infrastructure for packages with specific build systems (`generic-package`), Using a CMake toolchain file

**Other:** [nmcli `ipv4.method shared`](https://networkmanager.dev/docs/api/latest/nm-settings-nmcli.html)

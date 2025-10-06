# this script is a part of a project under the MIT license. see `LICENSE.txt` for more details.

#!/bin/bash

# the script assumes you have internet access as you can curl it from the internet
# and defaults to systemd-boot
set -e

# color output
info() { echo -e "\033[0;36m[INFO]\033[0m $*"; }
warn() { echo -e "\033[0;33m[WARN]\033[0m $*"; }
error() { echo -e "\033[0;31m[ERROR]\033[0m $*"; exit 1; }
prompt() { echo -ne "\033[0;32m[?]\033[0m $* "; }

# this script must be run as root and in live environment
[[ $EUID -ne 0 ]] && error "This script must be run as root!"
[[ ! -f /etc/arch-release ]] && error "This script must be run from Arch Linux live ISO!"

info "Arch Linux Bootstrap Script"
echo

# show disk information and make a prompt
# so the user can select their target partition
info "Available disks and partitions:"
lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT
echo

prompt "Enter the partition to format and use as root e.g. /dev/nvme0n1p2:"
read -r ROOT_PART
[[ ! -b "$ROOT_PART" ]] && error "Invalid partition: $ROOT_PART"

# select the filesystem type. this defaults to ext4
prompt "Enter filesystem type (ext4/btrfs) [ext4]:"
read -r FS_TYPE
FS_TYPE=${FS_TYPE:-ext4}

# EFI partition selection
prompt "Enter the EFI partition. Leave empty if none:"
read -r EFI_PART

if [[ -n "$EFI_PART" && ! -b "$EFI_PART" ]]; then
    error "Invalid EFI partition: $EFI_PART"
fi

# swap configuration
prompt "Configure swap? (y/N):"
read -r SWAP_TYPE
SWAP_TYPE=${SWAP_TYPE:-none}

if [[ "$SWAP_TYPE" == "partition" ]]; then
    prompt "Enter swap partition:"
    read -r SWAP_PART
    [[ ! -b "$SWAP_PART" ]] && error "Invalid swap partition: $SWAP_PART"

    info "Setting up swap partition..."
    mkswap "$SWAP_PART"
    swapon "$SWAP_PART"
elif [[ "$SWAP_TYPE" == "file" ]]; then
    prompt "Enter swapfile size in GB [8]:"
    read -r SWAP_SIZE
    SWAP_SIZE=${SWAP_SIZE:-8}
fi

# format confirmation
warn "This will \033[1mformat\033[0m $ROOT_PART as $FS_TYPE. All data will be lost!"
prompt "Type 'format' to confirm:"
read -r CONFIRM
[[ "$CONFIRM" != "format" ]] && error "Installation cancelled"

# format and mount
info "Formatting $ROOT_PART as $FS_TYPE..."
if [[ "$FS_TYPE" == "btrfs" ]]; then
    mkfs.btrfs -f "$ROOT_PART"
elif [[ "$FS_TYPE" == "ext4" ]]; then
    mkfs.ext4 -F "$ROOT_PART"
else
    error "Unsupported filesystem type: $FS_TYPE"
fi

info "Mounting root partition..."
mount "$ROOT_PART" /mnt

# check if EFI partition exists and handle bootloader detection
BOOTLOADER_MODE="none"
if [[ -n "$EFI_PART" ]]; then
    info "Mounting EFI partition..."
    mkdir -p /mnt/boot
    mount "$EFI_PART" /mnt/boot
    
    # check for existing systemd-boot installation
    if [[ -d /mnt/boot/EFI/systemd ]] || [[ -d /mnt/boot/loader ]]; then
        info "Detected existing systemd-boot installation"
        prompt "Setup dual boot? (Y/n):"
        read -r DUAL_BOOT
        DUAL_BOOT=${DUAL_BOOT:-y}
        
        if [[ "$DUAL_BOOT" =~ ^[Yy]$ ]]; then
            BOOTLOADER_MODE="dual"
            info "Will configure dual boot setup (preserving existing entries)"
        else
            prompt "Reinstall systemd-boot? This will remove existing entries! (y/N):"
            read -r REINSTALL
            if [[ "$REINSTALL" =~ ^[Yy]$ ]]; then
                BOOTLOADER_MODE="single"
                info "Will reinstall systemd-boot (existing entries will be removed)"
            else
                BOOTLOADER_MODE="none"
                warn "Skipping bootloader setup"
            fi
        fi
    else
        info "No existing bootloader detected"
        BOOTLOADER_MODE="single"
    fi
fi

prompt "Enter additional packages to install. Press enter for base only:"
read -r EXTRA_PACKAGES

# list all packages to be installed here
info "Installing base system..."
pacstrap -K /mnt base linux linux-firmware base-devel \
              nano git networkmanager sudo $EXTRA_PACKAGES || error "pacstrap failed"

# generate fstab then enter chroot with arch-chroot
info "Generating fstab..."
genfstab -U /mnt >> /mnt/etc/fstab

# configuring system in chroot
info "Configuring system..."
prompt "Enter hostname:"
read -r HOSTNAME
prompt "Enter timezone. Examples: Asia/Bangkok, America/New_York:"
read -r TIMEZONE

# get root partition UUID for bootloader configuration
ROOT_UUID=$(blkid -s UUID -o value "$ROOT_PART")

cat > /mnt/root/configure.sh <<'CHROOT_SCRIPT'
#!/bin/bash
set -e

info() { echo -e "\033[0;36m[INFO]\033[0m $*"; }
warn() { echo -e "\033[0;33m[WARN]\033[0m $*"; }
error() { echo -e "\033[0;31m[ERROR]\033[0m $*"; exit 1; }
prompt() { echo -ne "\033[0;32m[?]\033[0m $* "; }

# timezone
ln -sf /usr/share/zoneinfo/$TIMEZONE /etc/localtime
hwclock --systohc

# locale
sed -i 's/#en_US.UTF-8/en_US.UTF-8/' /etc/locale.gen
locale-gen
echo "LANG=en_US.UTF-8" > /etc/locale.conf

# hostname & daemon
echo "$HOSTNAME" > /etc/hostname
cat > /etc/hosts <<EOF
127.0.0.1   localhost
::1         localhost
127.0.1.1   $HOSTNAME.localdomain $HOSTNAME
EOF
systemctl enable NetworkManager

# regenerate initcpio
mkinitcpio -P

# setup swapfile if requested
if [[ "$SWAP_TYPE" == "file" ]]; then
    info "Creating swapfile of ${SWAP_SIZE}GB..."
    dd if=/dev/zero of=/swapfile bs=1G count=$SWAP_SIZE status=progress
    chmod 600 /swapfile
    mkswap /swapfile
    swapon /swapfile
    echo "/swapfile none swap defaults 0 0" >> /etc/fstab
    info "Swapfile created and enabled"
fi

# account management
info "Set root password:"
passwd

prompt "Create a user account (username):"
read USERNAME

if [[ -n "$USERNAME" ]]; then
    prompt "Make $USERNAME sudo? (y/N):"
    read MAKE_SUDO

    if [[ "$MAKE_SUDO" =~ ^[Yy]$ ]]; then
        useradd -m -G wheel -s /bin/bash "$USERNAME"
        sed -i 's/# %wheel ALL=(ALL:ALL) ALL/%wheel ALL=(ALL:ALL) ALL/' /etc/sudoers
        info "User $USERNAME created with sudo privileges"
    else
        useradd -m -s /bin/bash "$USERNAME"
        info "User $USERNAME created without sudo privileges"
    fi

    info "Set password for $USERNAME:"
    passwd "$USERNAME"
else
    warn "No user created. You'll need to login as root after reboot."
fi

# bootloader setup
if [[ "$BOOTLOADER_MODE" == "single" ]]; then
    info "Installing systemd-boot..."
    bootctl install
    
    # create loader configuration
    cat > /boot/loader/loader.conf <<EOF
default arch.conf
timeout 3
console-mode max
editor no
EOF
    
    # create arch linux boot entry
    mkdir -p /boot/loader/entries
    cat > /boot/loader/entries/arch.conf <<EOF
title   Arch Linux
linux   /vmlinuz-linux
initrd  /initramfs-linux.img
options root=UUID=$ROOT_UUID rw
EOF
    
    info "systemd-boot installed and configured"
    
elif [[ "$BOOTLOADER_MODE" == "dual" ]]; then
    info "Configuring dual boot setup..."
    
    # update systemd-boot if needed
    bootctl update
    
    # create arch linux boot entry with unique name
    ENTRY_NAME="arch"
    COUNTER=1
    while [[ -f "/boot/loader/entries/${ENTRY_NAME}.conf" ]]; do
        ENTRY_NAME="arch-${COUNTER}"
        ((COUNTER++))
    done
    
    mkdir -p /boot/loader/entries
    cat > /boot/loader/entries/${ENTRY_NAME}.conf <<EOF
title   Arch Linux
linux   /vmlinuz-linux
initrd  /initramfs-linux.img
options root=UUID=$ROOT_UUID rw
EOF
    
    info "Created boot entry: ${ENTRY_NAME}.conf"
    info "Existing boot entries preserved"
    
    # show existing entries
    info "Current boot entries:"
    ls -1 /boot/loader/entries/*.conf | xargs -n1 basename
    
elif [[ "$BOOTLOADER_MODE" == "none" ]]; then
    warn "Bootloader setup skipped. You'll need to configure it manually."
fi

info "Chroot configuration complete!"
CHROOT_SCRIPT

# make script executable and run in chroot
chmod +x /mnt/root/configure.sh

# pass variables to chroot script
arch-chroot /mnt /bin/bash -c "
export TIMEZONE='$TIMEZONE'
export HOSTNAME='$HOSTNAME'
export SWAP_TYPE='$SWAP_TYPE'
export SWAP_SIZE='$SWAP_SIZE'
export BOOTLOADER_MODE='$BOOTLOADER_MODE'
export ROOT_UUID='$ROOT_UUID'
/root/configure.sh
"

# cleanup
rm /mnt/root/configure.sh

# final instructions
info "Installation complete!"
if [[ "$BOOTLOADER_MODE" == "none" ]]; then
    warn "Don't forget to install and configure a bootloader before rebooting!"
fi
prompt "Press Enter to exit..."
read

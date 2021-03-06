#!/bin/bash
# 
# Copyright (c) 2015 Josua Mayer
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
# 

# functions
cpio_create_at() {
	local root
	root="$1"

	cd "$root"
	cpio -H newc -o
	return $?
}

# args
if [ $# != 2 ]; then
	echo "Usage: $0 <system-archive> <size_in_megabytes>"
	exit 1
fi
archive=$1
size=$2

# check system
if [ "$UID" != 0 ]; then
	echo "root privileges required!"
	exit 1
fi

progs="dd losetup fdisk fsck.ext4 tar mkfs.ext4 mkfs.vfat qemu-img mkimage partprobe"
for prog in $progs; do
	which $prog 1>/dev/null 2>/dev/null
	if [ $? != 0 ]; then
		echo "Missing $prog!"
		exit 1
	fi
done

# initialize temporary variables
IMG=
LODEV=
MOUNT=

# create exit handler
function cleanup() {
	if [ ! -z "$MOUNT" ]; then
		umount ${LODEV}p1 || true
		umount ${LODEV}p2 || true
		rmdir $MOUNT/boot/efi || true
		rmdir $MOUNT/boot || true
		rmdir $MOUNT || true
	fi
	if [ ! -z "$LODEV" ]; then
		losetup -d $LODEV || true
	fi
	if [ ! -z "$IMG" ] && [ -e "$IMG" ]; then
		rm -f "$IMG" || true
	fi
#	echo cleaned up
}
trap cleanup INT TERM EXIT

# create image file
IMG=$(echo $archive | cut -d. -f1).img
printf "Creating %s with a size of %s: " $IMG $size
qemu-img create "$IMG" $size 1>/dev/null
printf "Done\n"

# attach image to loopback device
printf "Attaching image to unused loopback device: "
LODEV=`losetup -f`
losetup $LODEV "$IMG"
test $? != 0 && exit 1
printf "Done\n"

# create partitions
# optimize for sdcards, and start at block 8192
# p1: tiny EFI at 8192+4M
# p2: rootfs   at 16384+N
printf "Creating partition table: "
echo "o
n
p
1
8192
+4M
t
ef
n
p
2
16384

w
q" | fdisk $LODEV 1>/dev/null 2>/dev/null
printf "Done\n"

# reload partition table
partprobe $LODEV

# create EFI filesystems
printf "Creating new efi filesystem: "
mkfs.vfat -n EFI -F 12 ${LODEV}p1 1>/dev/null 2>/dev/null
test $? != 0 && printf "Failed\n" && exit 1
printf "Done\n"

# create root filesystem
printf "Creating new ext4 filesystem: "
mkfs.ext4 -L rootfs ${LODEV}p2 1>/dev/null 2>/dev/null
test $? != 0 && printf "Failed\n" && exit 1
FS=ext4
printf "Done\n"

# mount filesystems
MOUNT=linux
mkdir $MOUNT
mount ${LODEV}p2 $MOUNT
mkdir -p $MOUNT/boot/efi
mount ${LODEV}p1 $MOUNT/boot/efi

# install files
printf "Unpacking rootfs into image: "
tar -C $MOUNT --numeric-owner -xpf $archive
test $? != 0 && exit 1
printf "Done\n"

# find EFI uuid
EFIUUID=$(lsblk -n -o UUID ${LODEV}p1)

# find rootfs uuid
FSUUID=$(grep -o -E '^UUID=[a-zA-Z0-9-]+[ 	]+/[ 	]+.*' "$MOUNT/etc/fstab" | sed -r 's;^UUID=([a-z0-9-]+).*$;\1;g' | head -1)
if [ -z "$FSUUID" ]; then
	FSUUID=$(lsblk -n -o UUID ${LODEV}p2)
fi

# patch fstab replacing generic /dev/root name if any
printf "Patching fstab with actual rootfs UUID: "
sed -i "s;^/dev/root;UUID=$FSUUID;g" $MOUNT/etc/fstab
test $? != 0 && exit 1
printf "Done\n"

# patch debian initramfs with default root device
printf "Patching initramfs with actual rootfs UUID: "
mkdir -p "$MOUNT/tmp/rd/conf/conf.d"
printf "ROOT=\"%s\"\n" "UUID=$FSUUID" > "$MOUNT/tmp/rd/conf/conf.d/default_root"
echo conf/conf.d/default_root | cpio_create_at "$MOUNT/tmp/rd" 2>/dev/null | gzip >> $MOUNT/boot/initrd.img-*
test $? != 0 && exit 1
rm -rf "$MOUNT/tmp/rd"
printf "Done\n"

# patch fstab with EFI partition
printf "Adding EFI partition to fstab: "
echo "UUID=$EFIUUID /boot/efi vfat defaults 0 0" >> $MOUNT/etc/fstab
printf "Done\n"

printf "Creating EFI/BOOT/BOOTAA64.EFI: "
cat > "$MOUNT/tmp/grub-early.cfg" << EOF || exit 1
search --no-floppy --fs-uuid --set=root $FSUUID
configfile /boot/grub/grub.cfg
EOF
mkdir -p "$MOUNT/boot/efi/EFI/BOOT" 1>/dev/null || exit 1
grub2-mkimage -c "./$MOUNT/tmp/grub-early.cfg" -o "$MOUNT/boot/efi/EFI/BOOT/BOOTAA64.EFI" -O arm64-efi -d "$MOUNT/usr/lib/grub/arm64-efi" -p "" configfile normal acpi fdt part_gpt part_msdos fat ext2 search search_fs_uuid linux echo test help reboot 1>/dev/null || exit 1
rm -f "$MOUNT/tmp/grub-early.cfg"
printf "Done\n"

# flush caches
printf "Flushing kernel filesystem caches: "
sync
printf "Done\n"

# umount
umount ${LODEV}p1
umount ${LODEV}p2
rmdir $MOUNT
MOUNT=

printf "Checking filesystem: "
fsck.ext4 -f -p ${LODEV}p2 1>/dev/null
test $? != 0 && exit 1
printf "Done\n"

printf "Patching rootfs UUID=$FSUUID to match fstab: "    
tune2fs -U $FSUUID ${LODEV}p2 1>/dev/null
test $? != 0 && exit 1
printf "Done\n"

# detach loopback device
losetup -d $LODEV
LODEV=
IMG=

# done
echo "Finished creating image."
exit 0

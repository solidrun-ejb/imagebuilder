#!/bin/bash -e

# import library
source functions.inc

# SETTINGS
DEB_MIRROR=http://ports.ubuntu.com/ubuntu-ports
DEB_RELEASE=trusty
DEB_ARCH=armhf
DUSER=solidrun
DPASS=solidrun

# check build environment
precheck

# install base system
echo Running debootstrap
debootstrap --no-check-gpg --arch=$DEB_ARCH $DEB_RELEASE build $DEB_MIRROR

# add BSP repo
cat > build/etc/apt/sources.list.d/bsp.list << EOF
deb http://repo.gbps.io/BSP:/Cubox-i/Ubuntu_Trusty_Tahr/ ./
deb-src http://repo.gbps.io/BSP:/Cubox-i/Ubuntu_Trusty_Tahr/ ./
EOF
curl -k http://repo.gbps.io/BSP:/Cubox-i/Ubuntu_Trusty_Tahr/Release.key | chroot_run apt-key add -

# pin packages from bsp repo with higher priority
cat > build/etc/apt/preferences.d/bsp-cubox-i << EOF
Package: *
Pin: release o=obs://mx6bs/BSP:Cubox-i/Debian_Wheezy
Pin-Priority: 600
EOF

# install Board-Support
chroot_run apt-get update
chroot_run apt-get install -y kernel-cubox-i u-boot-cubox-i bsp-cuboxi irqbalance-imx

# create boot files
cd build/boot
ln -sv zImage-* zImage
cat > uEnv.txt << EOF
mmcargs=setenv bootargs root=/dev/mmcblk0p1 rootfstype=ext4 rootwait console=tty
EOF
cd ../..

# remove traces of build-system
rm -f build/etc/resolv.conf

# CONFIGURATION

# add update repos
cat >> build/etc/apt/sources.list << EOF
deb http://ports.ubuntu.com/ubuntu-ports trusty-security main
deb http://ports.ubuntu.com/ubuntu-ports trusty-updates main
EOF

# add source repos
cat >> build/etc/apt/sources.list << EOF

deb-src http://ports.ubuntu.com/ubuntu-ports trusty main
deb-src http://ports.ubuntu.com/ubuntu-ports trusty-security main
deb-src http://ports.ubuntu.com/ubuntu-ports trusty-updates main
EOF

# create user
chroot_add_user $DUSER $DPASS "sudo,audio,video"

# configure network
cat >> build/etc/network/interfaces << EOF

auto eth0
iface eth0 inet dhcp
EOF

# PACKING
# make tarball
cd build
tar --numeric-owner -cpvf ../ubuntu-trusty.tar *
cd ..

# compress
pigz ubuntu-trusty.tar

# CLEANUP

# remove working directory
rm -rf build

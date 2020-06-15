# 
# Copyright (c) 2017-2019 Josua Mayer
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

# This is the actual cookbook

# bootstrap a debian system
bootstrap "$buildroot" buster armhf apt-transport-https ca-certificates locales systemd-sysv initramfs-tools busybox curl perl6-readline iputils-ping

# add debian repos
deleterepos "$buildroot" sources.list
addrepo "$buildroot" sources.list http://deb.debian.org/debian buster main non-free
addrepo "$buildroot" sources.list http://deb.debian.org/debian buster-updates main non-free
addrepo "$buildroot" sources.list http://security.debian.org/ buster/updates main non-free

# add BSP repos
addrepo "$buildroot" sources.list.d/solidrun.list https://repo.solid-build.xyz/debian/buster/bsp-any ./
addrepo "$buildroot" sources.list.d/solidrun.list https://repo.solid-build.xyz/debian/buster/bsp-imx6 ./
installpkg_url "$buildroot" https://repo.solid-build.xyz/debian/buster/bsp-any/all/solidrun-keyring_2020.01.09_all.deb

# mount /dev/root from fstab
# Note: breaks flash-kernel package which expects a more meaningful value to include in initrd!
# Note: replaced with uuid by mkimage
cat > "$buildroot/etc/fstab" << EOF
/dev/root / auto defaults 0 0
EOF

# refresh repos
chroot_run "$buildroot" apt-get update

# apply security updates missed by debootstrap
update "$buildroot"

# install HW support packages
installpkg "$buildroot" bsp-solidrun-imx6 linux-image-4.9.y-imx6-sr

# install a bunch of useful applications
installpkg "$buildroot" sudo openssh-server nano psmisc ntfs-3g iw rfkill devmem2 usbutils pciutils i2c-tools mtd-utils command-not-found alsa-utils wget snmpd snmptrapfmt iproute2 sshguard
chroot_run "$buildroot" apt-file update
chroot_run "$buildroot" update-command-not-found

# generate additional entropy to speed up /dev/urandom and reduce boot time
installpkg "$buildroot" haveged

# install automatic filesystem expander script
installpkg "$buildroot" expand-fs runonce parted
install -v -m 700 -o root -g root data/expand-rootfs.sh "$buildroot/etc/runonce.d/"

# for security reasons recreate the ssh keys on first boot
rm -fv "$buildroot"/etc/ssh/*_key{,.pub}
chroot_run "$buildroot" runonce-helper add generate-ssh-keys /usr/sbin/dpkg-reconfigure openssh-server

# add default user
adduser "$buildroot" solidsense aiPh2eim /bin/bash audio sudo video

# set hostname
sethostname "$buildroot" sr-imx6

chroot_run "$buildroot" systemctl set-default multi-user.target

# spawn getty on serial port
chroot_run "$buildroot" systemctl enable serial-getty@ttymxc0.service

# enable watchdog
sed -E -i "s;^#?RuntimeWatchdogSec=.*$;RuntimeWatchdogSec=60;g" "$buildroot/etc/systemd/system.conf"

# make bootable (using debian flash-image)
installpkg "$buildroot" flash-kernel
sed -E -i "s;^LINUX_KERNEL_CMDLINE=.*$;LINUX_KERNEL_CMDLINE=\"log_level=7 net.ifnames=0\";g" "$buildroot/etc/default/flash-kernel"
for dev in \
"SolidRun HummingBoard2 Dual/Quad" \
	"SolidRun HummingBoard2 Dual/Quad (1.5som)" \
	"SolidRun HummingBoard2 Dual/Quad (1.5som+emmc)" \
	"SolidRun HummingBoard2 Solo/DualLite" \
	"SolidRun HummingBoard2 Solo/DualLite (1.5som)" \
	"SolidRun HummingBoard2 Solo/DualLite (1.5som+emmc)" \
	; do
	chroot_run "$buildroot" env FK_MACHINE=\"$dev\" flash-kernel
done

# install SolidSense DTBs
sudo cp data/*dtb "$buildroot"/tmp
chroot_run "$buildroot" install -m 0644 /tmp/imx6dl-hummingboard2-emmc-som-v15.dtb /boot/imx6dl-hummingboard2-emmc-som-v15.dtb
chroot_run "$buildroot" install -m 0644 /tmp/imx6dl-hummingboard2-emmc-som-v15.dtb /boot/dtbs/4.9.150-imx6-sr/imx6dl-hummingboard2-emmc.dtb
chroot_run "$buildroot" install -m 0644 /tmp/imx6q-hummingboard2-emmc-som-v15.dtb /boot/imx6q-hummingboard2-emmc-som-v15.dtb
chroot_run "$buildroot" install -m 0644 /tmp/imx6q-hummingboard2-emmc-som-v15.dtb /boot/dtbs/4.9.150-imx6-sr/imx6q-hummingboard2-emmc.dtb
sudo rm "$buildroot"/tmp/*dtb

# install RaspAP
curl -sL https://install.raspap.com > "$buildroot/raspap.install.sh"
chroot_run "$buildroot" bash raspap.install.sh --yes --openvpn 0 --adblock 0
sudo rm "$buildroot/raspap.install.sh" "$buildroot/tmp/default_hostapd.old"

# enable firewall
sudo cp data/rules.v4 "$buildroot"/tmp
chroot_run "$buildroot" install -m 0644 /tmp/rules.v4 /etc/iptables/rules.v4
chroot_run "$buildroot" update-alternatives --set iptables /usr/sbin/iptables-legacy
chroot_run "$buildroot" update-alternatives --set ip6tables /usr/sbin/ip6tables-legacy
chroot_run "$buildroot" update-alternatives --set arptables /usr/sbin/arptables-legacy
chroot_run "$buildroot" update-alternatives --set ebtables /usr/sbin/ebtables-legacy
sudo rm "$buildroot"/tmp/rules.v4

# install cypress WiFi drivers
installpkg "$buildroot" build-essential unzip linux-headers-4.9.y-imx6-sr flex bison
curl -sL https://community.cypress.com/servlet/JiveServlet/download/19375-1-53475/cypress-fmac-v5.4.18-2020_0402.zip > "$buildroot/cypress-fmac-v5.4.18-2020_0402.zip"
chroot_run "$buildroot" unzip cypress-fmac-v5.4.18-2020_0402.zip
chroot_run "$buildroot" tar xfz cypress-backports-v5.4.18-2020_0402-module-src.tar.gz
MY_KERNEL="/lib/modules/4.9.150-imx6-sr/build/"
MOD_PATH="/lib/modules/4.9.150-imx6-sr"
chroot_run "$buildroot" make -C v5.4.18-backports KLIB=$MY_KERNEL KLIB_BUILD=$MY_KERNEL defconfig-brcmfmac
chroot_run "$buildroot" make -C v5.4.18-backports KLIB=$MY_KERNEL KLIB_BUILD=$MY_KERNEL modules
chroot_run "$buildroot" install -m 0755 -d $MOD_PATH/updates/net/wireless
chroot_run "$buildroot" install -m 0644 v5.4.18-backports/net/wireless/cfg80211.ko $MOD_PATH/updates/net/wireless/cfg80211.ko
chroot_run "$buildroot" install -m 0755 -d $MOD_PATH/updates/compat
chroot_run "$buildroot" install -m 0644 v5.4.18-backports/compat/compat.ko $MOD_PATH/updates/compat/compat.ko
chroot_run "$buildroot" install -m 0755 -d $MOD_PATH/updates/drivers/net/wireless/broadcom/brcm80211/brcmfmac
chroot_run "$buildroot" install -m 0644 v5.4.18-backports/drivers/net/wireless/broadcom/brcm80211/brcmfmac/brcmfmac.ko $MOD_PATH/updates/drivers/net/wireless/broadcom/brcm80211/brcmfmac/brcmfmac.ko
chroot_run "$buildroot" install -m 0755 -d $MOD_PATH/updates/drivers/net/wireless/broadcom/brcm80211/brcmutil
chroot_run "$buildroot" install -m 0644 v5.4.18-backports/drivers/net/wireless/broadcom/brcm80211/brcmutil/brcmutil.ko $MOD_PATH/updates/drivers/net/wireless/broadcom/brcm80211/brcmutil/brcmutil.ko
# install cypress WiFi firmware
chroot_run "$buildroot" tar xfz cypress-firmware-v5.4.18-2020_0402.tar.gz
chroot_run "$buildroot" install -m 0755 -d /lib/firmware/brcm
chroot_run "$buildroot" install -m 0644 /firmware/brcmfmac4359-pcie.bin /lib/firmware/brcm/brcmfmac4359-pcie.bin
chroot_run "$buildroot" install -m 0644 /firmware/brcmfmac4359-pcie.clm_blob /lib/firmware/brcm/brcmfmac4359-pcie.clm_blob
# install jody-w167 nvram
sudo cp data/jody-w167.nvram "$buildroot"/tmp
chroot_run "$buildroot" install -m 0644 /tmp/jody-w167.nvram /lib/firmware/brcm/brcmfmac4359-pcie.txt
# install jody-w167 modules-load.d conf
sudo cp data/jody-w167.conf "$buildroot"/tmp
chroot_run "$buildroot" install -m 0644 /tmp/jody-w167.conf /etc/modules-load.d/jody-w167.conf
# cleanup of install of cypress WiFi
sudo rm "$buildroot"/*tar.gz "$buildroot"/*zip "$buildroot"/tmp/jody-w167.nvram "$buildroot"/tmp/jody-w167.conf "$buildroot"/README
sudo rm -rf "$buildroot"/v5.4.18-backports "$buildroot"/firmware
chroot_run "$buildroot" apt-get --assume-yes remove build-essential unzip linux-headers-4.9.y-imx6-sr flex bison
chroot_run "$buildroot" apt-get --assume-yes autoremove

# remove traces of build-system
cleanup "$buildroot"

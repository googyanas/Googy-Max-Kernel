#!/bin/sh
export KERNELDIR=`readlink -f .`
export RAMFS_SOURCE=`readlink -f $KERNELDIR/../ramfs-sgs3`
export PARENT_DIR=`readlink -f ..`
export USE_SEC_FIPS_MODE=true
# export CROSS_COMPILE=$PARENT_DIR/android_prebuilt/linux-x86/toolchain/arm-eabi-4.4.3/bin/arm-eabi-
# export CROSS_COMPILE=/home/googy/Desktop/arm-2009q3/bin/arm-none-linux-gnueabi-
export CROSS_COMPILE=/home/googy/Desktop/arm-linaro2/bin/arm-linux-gnueabihf-

if [ "${1}" != "" ];then
  export KERNELDIR=`readlink -f ${1}`
fi

RAMFS_TMP="/home/googy/tmp/ramfs-source-sgs3"

if [ ! -f $KERNELDIR/.config ];
then
  make -j5 0googymax_defconfig
fi

. $KERNELDIR/.config

export ARCH=arm

cd $KERNELDIR/
make -j5 || exit 1

#remove previous ramfs files
rm -rf $RAMFS_TMP
rm -rf $RAMFS_TMP.cpio
rm -rf $RAMFS_TMP.cpio.gz
#copy ramfs files to tmp directory
cp -ax $RAMFS_SOURCE $RAMFS_TMP
#clear git repositories in ramfs
find $RAMFS_TMP -name .git -exec rm -rf {} \;
#remove empty directory placeholders
find $RAMFS_TMP -name EMPTY_DIRECTORY -exec rm -rf {} \;
rm -rf $RAMFS_TMP/tmp/*
#remove mercurial repository
rm -rf $RAMFS_TMP/.hg
#copy modules into ramfs
mkdir -p $INITRAMFS/lib/modules
mv -f drivers/media/video/samsung/mali_r3p0_lsi/mali.ko drivers/media/video/samsung/mali_r3p0_lsi/mali_r3p0_lsi.ko
mv -f drivers/net/wireless/bcmdhd.cm/dhd.ko drivers/net/wireless/bcmdhd.cm/dhd_cm.ko
find -name '*.ko' -exec cp -av {} $RAMFS_TMP/lib/modules/ \;
/home/googy/Desktop/arm-linaro2/bin/arm-linux-gnueabihf-strip --strip-unneeded $RAMFS_TMP/lib/modules/*

cd $RAMFS_TMP
find | fakeroot cpio -H newc -o > $RAMFS_TMP.cpio 2>/dev/null
ls -lh $RAMFS_TMP.cpio
gzip -9 $RAMFS_TMP.cpio
cd -

make -j5 zImage || exit 1

./mkbootimg --kernel $KERNELDIR/arch/arm/boot/zImage --ramdisk $RAMFS_TMP.cpio.gz --board smdk4x12 --base 0x10000000 --pagesize 2048 --ramdiskaddr 0x11000000 -o $KERNELDIR/boot.img.pre

$KERNELDIR/mkshbootimg.py $KERNELDIR/boot.img $KERNELDIR/boot.img.pre $KERNELDIR/payload.tar
# rm -f $KERNELDIR/boot.img.pre

cd /home/googy/Desktop/Sources/Googy-Max-Kernel
mv -f -v /home/googy/Desktop/Sources/Googy-Max-Kernel/Kernel/boot.img .
cp -f -v Googy-Max-Kernel_0.zip Googy-Max-Kernel_TEST_CWM.zip
zip -v Googy-Max-Kernel_TEST_CWM.zip boot.img

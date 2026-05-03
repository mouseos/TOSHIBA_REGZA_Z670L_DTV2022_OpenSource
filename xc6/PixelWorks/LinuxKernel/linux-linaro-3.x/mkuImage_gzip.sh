#!/bin/bash
gzip -f vmlinux.mem -c > vmlinux.mem.gz

if [ $# = 0 ]; then
	START=`grep CONFIG_PHYS_OFFSET  .config | cut -d '=' -f 2`
else
	START=$1
	OUTPUT=$2
fi

if [ "$START" = "-" ]; then
	START=`grep CONFIG_PHYS_OFFSET  .config | cut -d '=' -f 2`
fi

if [ -z $OUTPUT ]; then
	OUTPUT=/tftpboot/uImage_arm_gz
fi

./mkimage -A arm -O linux -C gzip -T kernel -a $START -e $START -n "ARM Linux Kernel" -d vmlinux.mem.gz $OUTPUT

echo generate $OUTPUT done




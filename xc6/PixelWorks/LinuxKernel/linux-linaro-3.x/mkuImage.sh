#!/bin/bash
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
	OUTPUT=/tftpboot/uImage_arm
fi

./mkimage -A arm -O linux -C gzip -T kernel -a $START -e $START -n "ARM Linux Kernel" -d vmlinux.mem $OUTPUT

echo generate $OUTPUT done




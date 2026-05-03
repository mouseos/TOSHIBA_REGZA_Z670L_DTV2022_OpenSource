#!/bin/sh

export RFS=rootfs
export CPIO="$RFS"_mini.cpio.gz
if [ -d $RFS ]
then
    echo "creating $CPIO from $RFS"
    (cd "$RFS"; sudo find . | sudo cpio -o -H newc | gzip) > "$CPIO"
else
    echo "Can not read directory $RFS"
    exit 1
fi

exit 0

#!/bin/sh

export RFS=rootfs
export CPIO="$RFS"_mini.cpio.gz
if [ -d $RFS ]
then
    echo "Removing exist directory $RFS"
    sudo rm -rf $RFS
fi

if [ -f $CPIO ]
then
    mkdir $RFS
    echo "Extracting $CPIO to folder $RFS"
    (cd $RFS; cat ../$CPIO | gunzip | sudo cpio -i -d -H newc --no-absolute-filenames)
else
    echo "Can not read file $CPIO"
    exit 1
fi

exit 0

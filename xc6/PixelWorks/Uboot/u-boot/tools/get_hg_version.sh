#!/bin/bash
#which hg &>/dev/null
if [ -z $TOPDIR ]; then
	curdir=`pwd`
else 
	curdir=$TOPDIR
fi

hg summary &>/dev/null
if [ $? -eq '0' ]; then
	VER=`hg summary | head -n1  | cut -d ":" -f 2`
	if [ -z $VER ]; then
		VER=`date +%Y%m%d%H%M`
	else
		echo $VER > $curdir/tools/hg_version.txt
	fi
else
	if [ -f $TOPDIR/tools/hg_version.txt ];	then
		VER=`cat $curdir/tools/hg_version.txt`
	else
		VER=`date +%Y%m%d%H%M`
	fi
fi
echo $VER

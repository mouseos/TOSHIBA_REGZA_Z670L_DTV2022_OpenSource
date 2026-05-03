#!/bin/bash
echo "Build SDK"
make distclean
make xc6_sdk -j4 &>/dev/null
if [ $? != 0 ]; then
	echo "SDK build failed!!"
	exit -1
fi
echo "Build SECURE SDK"
make distclean
make xc6_secu_sdk -j4 &>/dev/null
if [ $? != 0 ]; then
	echo "Secure SDK build failed!!"
	exit -1
fi

for i in 1 2 3 4 5 6
do 
echo "Build Customer$i"
make distclean
make xc6_customer$i -j4 &>/dev/null
if [ $? != 0 ]; then
	echo "Customer$i build failed!!"
	exit -1
fi
done

#!/bin/sh

# usage: 
# ./scripts/install-one.sh

# build first
# ./scripts/build.sh

# if [ $? != 0 ]; then
#   exit 2
# fi

# push to device
adb push olsrd /sdcard/TEMP/olsrd

# will require user to accept Superuser.apk prompt
adb shell "su -c 'dd if=/sdcard/TEMP/olsrd of=/data/data/android.adhoc.manet/bin/olsrd'"

if [ $? == 0 ]; then
  echo "Install successful"
  exit 0
else
  echo "Install failed"
  exit 2
fi

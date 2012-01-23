#!/bin/sh

# usage: 
# ./scripts/install-all.sh

# build first
./scripts/build.sh

if [ $? != 0 ]; then
  exit 2
fi

# push to device
push_command='push olsrd /sdcard/TEMP/olsrd'

# will require user to accept Superuser.apk prompt
move_command='shell \"su -c \\\"mv /sdcard/TEMP/olsrd /data/data/android.adhoc.manet/bin/olsrd\\\"\"'

# load each device
adb devices | awk '$2 == "device" { print "adb -s "$1" '"$push_command"'" }' | sh -x
adb devices | awk '$2 == "device" { print "adb -s "$1" '"$move_command"'" }' | sh -x


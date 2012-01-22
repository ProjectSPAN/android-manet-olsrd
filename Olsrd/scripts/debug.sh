#!/bin/sh

# usage: 
# ./scripts/debug.sh

# install first
./scripts/install.sh

if [ $? != 0 ]; then
  exit 2
fi

# remotely debug over USB

adb forward tcp:9999 tcp:9999

# remote command
command="su -c 'killall olsrd; ./data/data/android.adhoc.manet/lib/gdbserver localhost:9999 /data/data/android.adhoc.manet/bin/olsrd -f /data/data/android.adhoc.manet/conf/olsrd.conf -d 6 -ignore /data/data/android.adhoc.manet/conf/routing_ignore_list.conf'"

echo "Executing adb shell command. Debug through Eclipse ..."

# user may be prompted by Superuser.apk
adb shell "$command"


# open new terminal
# gnome-terminal -e "bash -c \"adb shell $command; exec bash\""



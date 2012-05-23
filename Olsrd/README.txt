# build

#SPAN - Smart Phone AdHoc Networking project
#©2012 The MITRE Corporation

export PATH=/home/dev/Desktop/PROJECTS/ANDROID:$PATH
export BISON_PKGDATADIR=/home/dev/Desktop/PROJECTS/ANDROID/external/bison/data

. /home/dev/Desktop/PROJECTS/ANDROID/build/envsetup.sh
make OS=android DEBUG=1 build_all


# push to device

adb push olsrd /sdcard/TEMP/olsrd
su -c "cp /sdcard/TEMP/olsrd /data/data/android.adhoc.manet/bin/olsrd"


# remotely debug over USB

adb forward tcp:9999 tcp:9999

adb shell
su
./data/data/android.adhoc.manet/lib/gdbserver localhost:9999 /data/data/android.adhoc.manet/bin/olsrd -f /data/data/android.adhoc.manet/conf/olsrd.conf -d 6


/home/dev/Desktop/android-9-toolchain/bin/arm-linux-androideabi-gdb -s olsrd
(gdb) list
(gdb) break main
(gdb) target remote localhost:9999
(gdb) continue


# remotely debug over IP

adb shell
su
./data/data/android.adhoc.manet/lib/gdbserver 10.7.0.156:9999 /data/data/android.adhoc.manet/bin/olsrd -f /data/data/android.adhoc.manet/conf/olsrd.conf -d 6


/home/dev/Desktop/android-9-toolchain/bin/arm-linux-androideabi-gdb -s olsrd
(gdb) list
(gdb) break main
(gdb) target remote 10.7.0.156:9999
(gdb) continue



# remotely debug over USB [NOT WORKING]
# http://ian-ni-lewis.blogspot.com/2011/05/ndk-debugging-without-root-access.html

adb forward tcp:5039 localfilesystem:/data/data/android.adhoc.manet/debug-socket

adb shell
su -c "chmod 777 /data/data/android.adhoc.manet/bin/olsrd"
// adb shell run-as android.adhoc.manet lib/gdbserver +debug-socket /data/data/android.adhoc.manet/bin/olsrd


adb shell run-as android.adhoc.manet lib/gdbserver +debug-socket /data/data/android.adhoc.manet/bin/olsrd -f /data/data/android.adhoc.manet/conf/olsrd.conf -d 6


adb shell
su
./data/data/android.adhoc.manet/lib/gdbserver 10.7.0.156:9999 /data/data/android.adhoc.manet/bin/olsrd -f /data/data/android.adhoc.manet/conf/olsrd.conf -d 6


# enable setuid bit

sudo chown root olsrd
sudo chmod u+s olsrd


# enable "run-as" process execution priv.

sudo chmod a+x olsrd

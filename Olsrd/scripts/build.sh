#!/bin/sh

# usage: 
# ./scripts/build.sh

# variables
aosp_path='/home/dev/Desktop/PROJECTS/ANDROID'

export PATH=$aosp_path:$PATH
export BISON_PKGDATADIR=$aosp_path/external/bison/data

source $aosp_path/build/envsetup.sh

make OS=android DEBUG=1 build_all
success=$?

# copy binary into other project(s)
# copy will fail if destination dir does not exist
cp olsrd ../../android-manet-service/AndroidManetService/res/raw

if [ $success == 0 ]; then
  echo "Build successful"
  exit 0
else
  echo "Build failed"
  exit 2
fi



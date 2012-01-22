#!/bin/sh

# usage: 
# ./scripts/build.sh

# variables
aosp_path='/home/dev/Desktop/PROJECTS/ANDROID'

export PATH=$aosp_path:$PATH
export BISON_PKGDATADIR=$aosp_path/external/bison/data

source $aosp_path/build/envsetup.sh

make OS=android DEBUG=1 build_all

if [ $? == 0 ]; then
  echo "Build successful"
  exit 0
else
  echo "Build failed"
  exit 2
fi

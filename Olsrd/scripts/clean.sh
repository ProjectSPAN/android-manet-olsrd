#!/bin/sh

# usage: 
# ./scripts/clean.sh

make clean
success=$?

if [ $success == 0 ]; then
  echo "Clean successful"
  exit 0
else
  echo "Clean failed"
  exit 2
fi



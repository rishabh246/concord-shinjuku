#!/bin/sh
LOAD_LEVEL=${1:-50}

rm -rf /tmpfs/experiments/leveldb/
make clean 2> /dev/null
make -j6 -s LOAD_LEVEL=$LOAD_LEVEL  2> /dev/null
./dp/shinjuku
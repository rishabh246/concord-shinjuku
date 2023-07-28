#!/bin/sh

# Options to pass
# SCHEDULE_METHOD = < 0, 1, 2, 3 > | < pi, yield, none, concord > 
# FAKE_WORK = <0,1> 
# RUN_UBENCH = <0,1>
# -> (if run_ubench == 0) BENCHMARK_TYPE <0, 1, 2, 3, 4, 5> 
# -> (if run_ubench == 1) BENCHMARK_TYPE <1>

LOAD_LEVEL=${1:-50}

rm -rf /tmpfs/experiments/leveldb/
make clean
make -j6 -s LOAD_LEVEL=$LOAD_LEVEL FAKE_WORK=1

sudo ./dp/shinjuku 
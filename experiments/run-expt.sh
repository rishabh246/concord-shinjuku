#!/bin/bash

PERCENTILE=99
declare -a load_levels=("1" "10" "20" "30" "40" "50" "60" "70" "80" "90" "100")
sudo rm -f latency.csv slowdown.csv temp.csv temp.txt latency.txt slowdown.txt 
touch latency.csv slowdown.csv temp.txt
for load in "${load_levels[@]}"
  do
    echo "Running benchmark for load level = $load"
    sudo ./build_and_run.sh $load > temp.txt
    sed -i '$ d' temp.txt 
    RPS=$(grep "Dispatched pkts" temp.txt | cut -d ":" -f6)

    echo "Calculating latency"
    grep "latency, slowdown" temp.txt | cut -d ":" -f5 > latency.txt
    python3 ../scripts/percentile.py latency.txt temp.csv
    LATENCY="$load,$RPS,$(grep "$PERCENTILE," temp.csv | cut -d "," -f2)"
    echo $LATENCY >> latency.csv

    echo "Calculating slowdown"
    grep "latency, slowdown" temp.txt | cut -d ":" -f6 > slowdown.txt 
    python3 ../scripts/percentile.py slowdown.txt temp.csv
    LATENCY="$load,$RPS,$(grep "$PERCENTILE," temp.csv | cut -d "," -f2)"
    echo $LATENCY >> slowdown.csv
  done

sudo rm -f temp.csv temp.txt latency.txt slowdown.txt
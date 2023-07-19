#!/bin/sh

# Remove kernel modules
sudo rmmod pcidma
sudo rmmod dune

# Set huge pages
sudo sh -c 'for i in /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages; do echo 8192 > $i; done'

# Unbind NICs
sudo ./deps/dpdk/tools/dpdk_nic_bind.py -u 0000:83:00.0

# Build required kernel modules.
sudo make -s -j16 -C deps/dune
make -s -j16 -C deps/pcidma
make -s -j16 -C deps/dpdk config T=x86_64-native-linuxapp-gcc
cd deps/dpdk
    git apply ../dpdk.mk.patch
    git apply ../dpdk_i40e.patch
cd ../../
make -s -j16 -C deps/dpdk

# Insert kernel modules
sudo insmod deps/dune/kern/dune.ko
sudo insmod deps/pcidma/pcidma.ko

# Add tmpfs
sudo mkdir -p /tmpfs
mountpoint -q /tmpfs || sudo mount -t tmpfs -o size=8G,mode=1777 tmpfs /tmpfs
mkdir -p /tmpfs/experiments/
VTX_OK="3,5,7"
VTX=$(sudo modprobe msr | sudo rdmsr 0x3A)
X2APIC=$(lscpu | grep x2apic)

# Check if VTX is enabled
if [[ $VTX_OK == *$VTX* ]]; then
    echo "VT-x enabled - OK"
else
    echo "VT-x not enabled - ERROR"
fi

# Check if x2apic is disabled
if [ "$X2APIC" == "" ];then
    echo "No x2apic flag - OK"
else
    echo "x2apic needs to disabled from grub menu (noxapic). -ERROR"
fi

echo "Checks done."


# TODO: make configurable
cp s3.conf shinjuku.conf

# Install dependencies
./deps/fetch-deps.sh

# Setup
sudo ./setup.sh


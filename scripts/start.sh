repo_root=$(realpath $(dirname $0)/../)

smp=4
memory=4G
accel=kvm
hddImg="$repo_root/vm/hdd.qcow2"
installImg="$repo_root/vm/disk.iso"

# line by line description:
#
# 1. define a Q35 machine, accelerated by $accel with $memory memory
# 2. forward local connections to port 2222 to a SSH server (port 22) on a VM
# 3. mount install CD
# 4. mount HDD
# 5. create 'edu' device

qemu-system-x86_64 \
    -display gtk -M q35,accel=$accel -smp $smp -m $memory \
    -nic user,hostfwd=tcp:127.0.0.1:2222-:22 \
    -drive file=$hddImg,format=qcow2,media=disk \
    -drive file=$installImg,media=cdrom \
    -virtfs local,path=$repo_root,mount_tag=ol-edu-driver,security_model=mapped-xattr \
    -device edu -device edu -device edu -device edu -device edu

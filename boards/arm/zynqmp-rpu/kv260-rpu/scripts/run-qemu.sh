#!/bin/bash
# Boot ./nuttx on RPU0 in Xilinx QEMU.  Build it with kv260-rpu:qemu.
P=~/dev/petalinux-kv260/xilinx-kv260-starterkit-2024.2
Q=$P/build/tmp/sysroots-components/x86_64/qemu-xilinx-system-native/usr/bin/qemu-system-aarch64
D=$P/build/tmp/deploy/images/xilinx-k26-kv/qemu-hw-devicetrees

exec $Q -M arm-generic-fdt -hw-dtb $D/board-zynqmp-kv260.dtb \
  -display none -monitor none -serial mon:stdio -serial null \
  -device loader,file=nuttx,cpu-num=4 \
  -device loader,addr=0xff5e0238,data=0x0017d7fd,data-len=4 \
  -device loader,addr=0xff9a0100,data=0x00000001,data-len=4 \
  -device loader,addr=0xff5e023c,data=0x00188fca,data-len=4 \
  "$@"
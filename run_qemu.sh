#!/bin/bash
set -e

# Assume script is inside the <petalinux-project> folder.

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
PROJECT_DIR="$(realpath "$SCRIPT_DIR")"

IMAGES_DIR="$PROJECT_DIR/images/linux"


# Which QEMU to use
# -----------------> Set correct path of custom built qemu binaries <--------------------------
QEMU_SYSTEM_MICROBLAZEEL="/home/kavindu/qemu-device-build/qemu/build/qemu-system-microblazeel"
QEMU_SYSTEM_AARCH64="/home/kavindu/qemu-device-build/qemu/build/qemu-system-aarch64"

# -----------------> Set correct path of custom built qemu hw-dtb file <-----------------------
HW_DTB_FOLDER="/home/kavindu/qemu-device-build/qemu-devicetrees/LATEST/MULTI_ARCH"

# Create temp folder for QEMU communication
MACHINE_PATH=$(mktemp -d)

echo "INFO: Using machine-path: $MACHINE_PATH"


"$QEMU_SYSTEM_MICROBLAZEEL" \
  -M microblaze-fdt \
  -serial mon:stdio \
  -serial /dev/null \
  -display none \
  -kernel $IMAGES_DIR/pmu_rom_qemu_sha3.elf \
  -device loader,file=$IMAGES_DIR/pmufw.elf \
  -hw-dtb ${HW_DTB_FOLDER}/zynqmp-pmu.dtb \
  -machine-path "$MACHINE_PATH" \
  -device loader,addr=0xfd1a0074,data=0x1011003,data-len=4 \
  -device loader,addr=0xfd1a007C,data=0x1010f03,data-len=4 &


# Save PID in case want to clean up later
PMU_PID=$!

# Give it a moment to initialize the socket
sleep 2

"$QEMU_SYSTEM_AARCH64" \
  -M arm-generic-fdt \
  -serial mon:stdio\
  -serial /dev/null \
  -display none  \
  -device loader,file=$IMAGES_DIR/system.dtb,addr=0x100000,force-raw=on \
  -device loader,file=$IMAGES_DIR/u-boot.elf \
  -device loader,file=$IMAGES_DIR/Image,addr=0x200000,force-raw=on \
  -device loader,file=$IMAGES_DIR/rootfs.cpio.gz.u-boot,addr=0x4000000,force-raw=on \
  -device loader,file=$IMAGES_DIR/bl31.elf,cpu-num=0 \
  -global xlnx,zynqmp-boot.cpu-num=0 \
  -global xlnx,zynqmp-boot.use-pmufw=true \
  -global xlnx,zynqmp-boot.drive=pmu-cfg \
  -blockdev node-name=pmu-cfg,filename=$IMAGES_DIR/pmu-conf.bin,driver=file \
  -device loader,file=$IMAGES_DIR/boot.scr,addr=0x20000000,force-raw=on \
  -hw-dtb ${HW_DTB_FOLDER}/zcu102-arm.dtb \
  -net nic \
  -net nic \
  -net nic \
  -net nic \
  -machine-path "$MACHINE_PATH" \
  -d guest_errors -D /tmp/qemu.log \
  -m 4G


# Cleanup to happen automatically even if press Ctrl+A then X
trap "kill $PMU_PID 2>/dev/null; rm -rf $MACHINE_PATH" EXIT
echo "INFO: QEMU run completed. Cleaned up temp folder."

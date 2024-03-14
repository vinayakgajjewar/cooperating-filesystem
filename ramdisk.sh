#!/bin/bash

# to fully create a mount  disk image residing inside of this ramdisk:
#  qemu-img create -f raw ram-fs/<name>.img <size>
#  sudo losetup --Pf --show ram-fs/<name.img
#  sudo chown ryan:ryan /dev/loopN

set -e

if [ $# -ne 1 ]; then
  echo -e "Usage: $0 <mountpoint>"
  exit 1
fi

MOUNT_DIR=$1
MOUNT_DIR="$(readlink -f "$MOUNT_DIR")"
mkdir -p "$MOUNT_DIR"

[ "$(mount | grep -o "$MOUNT_DIR")" = "$MOUNT_DIR" ] && exit 0

if [ "$(uname)" = "Linux" ]; then
  sudo mount -t ramfs -o size=1g ramfs "$MOUNT_DIR"  # size parameter seems to be ignored here?
  sudo chown -R "$(whoami)":"$(whoami)" "$MOUNT_DIR"
  mount | grep ram
  echo "To unmount and free memory run \`sudo umount $(basename "$MOUNT_DIR")\`"
else
  SIZE=$((1024 * 1024 * 1024))
  DEVICE=$(hdiutil attach -nomount ram://$(($SIZE / 512)) | xargs)
  newfs_hfs -v cs171-ramdisk "$DEVICE"
  diskutil mount -mountPoint "$MOUNT_DIR" "$DEVICE"
  mount | grep ram
  echo "To unmount and free memory run \`diskutil eject $(basename "$MOUNT_DIR")\` or \`hdiutil detach '$DEVICE'\`"
fi
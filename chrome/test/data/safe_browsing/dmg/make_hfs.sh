#!/bin/sh

# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script is used to generate an HFS file system with several types of
# files of different sizes.

set -eu

FILESYSTEM_TYPE="$1"
RAMDISK_SIZE="$2"
OUT_FILE="$3"

VOLUME_NAME="SafeBrowsingDMG"
UNICODE_FILENAME="Tĕsẗ 🐐 "

if [[ ! $FILESYSTEM_TYPE ]]; then
  echo "Need to specify a filesystem type. See \`diskutil listfilesystems'."
  exit 1
fi

if [[ ! $RAMDISK_SIZE ]]; then
  echo "Need to specify a volume size in bytes."
  exit 1
fi

if [[ ! $OUT_FILE ]]; then
  echo "Need to specify a destination filename."
  exit 1
fi

RAMDISK_VOLUME=$(hdiutil attach -nomount ram://$RAMDISK_SIZE)
diskutil erasevolume "${FILESYSTEM_TYPE}" "${VOLUME_NAME}" ${RAMDISK_VOLUME}
diskutil mount ${RAMDISK_VOLUME}

pushd "/Volumes/${VOLUME_NAME}"

touch .metadata_never_index

mkdir -p first/second/third/fourth/fifth

pushd first
pushd second
pushd third
pushd fourth
pushd fifth

dd if=/dev/random of=random bs=1 count=768 &> /dev/null

popd   # fourth

touch "Hello World"
touch "hEllo wOrld"  # No-op on case-insensitive filesystem.

popd  # third

ln -s fourth/fifth/random symlink-random

popd  # second

echo "Poop" > "${UNICODE_FILENAME}"
ditto --hfsCompression "${UNICODE_FILENAME}" goat-output.txt

popd  # first

ln "second/${UNICODE_FILENAME}" unicode_name

popd  # volume root

echo "This is a test HFS+ filesystem generated by" \
    "chrome/test/data/safe_browsing/dmg/make_hfs.sh." > README.txt

popd  # Original PWD

# Unmount the volume, copy the raw device to a file, and then destroy it.
diskutil unmount force ${RAMDISK_VOLUME}
dd if=${RAMDISK_VOLUME} of="${OUT_FILE}" &> /dev/null
diskutil eject ${RAMDISK_VOLUME}

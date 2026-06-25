#!/bin/bash
# Converts the 20MB volume image into a BlueSCSI-ready device image
# (partition map + SCSI driver) using djjr.
#
# djjr's to-device step already installs the "Apple HD SC 7.3.5"
# driver by default (confirmed via `djjr analyze` on its output) --
# the same driver djjr's separate replace-lido step exists to swap
# *to* on older images. Since to-device's output already has it,
# there's nothing for replace-lido to do here; it's not run.
#
# Separate from deploy.sh on purpose: that script is the fast Mini
# vMac test loop (which wants the raw, unpartitioned volume image);
# this one produces the actual release artifact for real hardware.
set -e

cd "$(dirname "$0")"

VOLUME_IMAGE="vmac/ArtfulType_20M.dsk"
DEVICE_IMAGE_FINAL="vmac/The Artful Type.hda"

echo "Converting volume image to device image..."
djjr convert to-device "$VOLUME_IMAGE" "$DEVICE_IMAGE_FINAL"

echo "Done: $DEVICE_IMAGE_FINAL"

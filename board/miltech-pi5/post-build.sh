#!/bin/sh
set -eu
TARGET_DIR="$1"

chmod 700 "$TARGET_DIR/root/.ssh"
chmod 600 "$TARGET_DIR/root/.ssh/authorized_keys"

{
  echo "image=miltech-hw18"
  echo "build_date=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "buildroot=${BR2_VERSION_FULL:-unknown}"
} > "$TARGET_DIR/etc/miltech-build"

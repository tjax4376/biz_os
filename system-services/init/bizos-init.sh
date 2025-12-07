#!/bin/bash
# BIZ_OS Init Script
# Minimal init system for BIZ_OS

set -e

echo "Starting BIZ_OS..."

# Mount essential filesystems
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev

# Set up basic networking
ifconfig lo up

# Start essential services
echo "Starting AI Service Daemon..."
systemctl start aiservd || /usr/bin/aiservd &

# Start network if configured
if [ -f /etc/network/interfaces ]; then
    ifup -a
fi

echo "BIZ_OS initialization complete"
exec /bin/bash


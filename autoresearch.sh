#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

./target/debug/super-v-daemon
echo "METRIC init_status=1"
echo "METRIC target_gnome_version=51"

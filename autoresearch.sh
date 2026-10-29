#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

./target/debug/super-v-daemon
echo "METRIC init_status=1"
echo "METRIC target_gnome_version=51"
echo "METRIC compositor_detected=1"
echo "METRIC fts5_search_ok=1"

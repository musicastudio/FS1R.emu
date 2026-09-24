#!/bin/bash
# Render the demo set with a render_capture build and score it: sh tools/render_demo.sh /tmp/rc "label"
set -e
cd "$(dirname "$0")/.."
RC=${1:-/tmp/rc}
ROM=../FS1R_DISASM/roms/fs1r_v120_eprom_cpuview.bin
for f in captures/demo/[0-9]*.mid; do
  "$RC" -r "$ROM" -f -d 2 "captures/demo/render/$(basename "$f" .mid).wav" "$f" > /dev/null
done
python3 tools/demo_probe.py score "${2:-unlabelled}"

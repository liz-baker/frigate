#!/usr/bin/env bash
# Regenerates every packaged icon from assets/icon/icon.png (a 1024x1024 PNG
# with an alpha channel). The macOS .icns is built during packaging with
# iconutil, which only exists on macOS.
set -euo pipefail
cd "$(dirname "$0")/.."
master=assets/icon/icon.png

magick "$master" -resize 512x512 assets/icon/icon-512.png
# The Wii U launcher requires a 128x128 PNG.
magick "$master" -resize 128x128 assets/wiiu/icon.png
magick "$master" -define icon:auto-resize=256,128,64,48,32,24,16 assets/icon/frigate.ico

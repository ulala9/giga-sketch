#!/bin/sh
# SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
# SPDX-License-Identifier: CC0-1.0
#
# Regenerates the Giga Sketch branding images from the SVG sources in this
# folder. Needs ImageMagick with the librsvg delegate. Run it from this folder
# after editing logo.svg or splash-background.svg, then rebuild.
set -e
cd "$(dirname "$0")"
for s in 16 22 32 48 64 128 256 512 1024; do
    magick -background none -density 400 logo.svg -resize ${s}x${s} -depth 8 "${s}-apps-gigasketch.png"
done
gzip -9 -n -c logo.svg > sc-apps-gigasketch.svgz
magick -density 96 splash-background.svg -attenuate 0.4 +noise Uniform -quality 92 splash-background.jpg

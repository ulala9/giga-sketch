#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Giga Sketch: generate the line-art tool icons.

Every icon is drawn once here on a 16x16 grid and written twice:
  krita/pics/giga-icons/light_<name>.svg  (light lines, for dark themes)
  krita/pics/giga-icons/dark_<name>.svg   (dark lines, for light themes)
plus giga-icons.qrc. <name> is the icon name Krita asks for (the tool's
iconName), so KisIconUtils::loadIcon() can pick ours first when
"Giga Sketch tool icons" is on.

Style: 1.25 px round strokes, no fills except a soft tint (opacity 0.35) or
small solid accents. Run: python3 giga-tools/make_tool_icons.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'krita', 'pics', 'giga-icons')

COLORS = {'light': '#d2d2d2', 'dark': '#373737'}

# Shorthands used inside the icon bodies:
#   {s}  stroke style   {t}  soft tint fill   {f}  solid fill
S = 'fill:none;stroke:{c};stroke-width:1.25;stroke-linecap:round;stroke-linejoin:round'
T = 'fill:{c};fill-opacity:0.35;stroke:none'
F = 'fill:{c};stroke:none'
D = S + ';stroke-dasharray:1.6 1.6'   # dashed line (selections)

def p(d, style=S):
    return '<path style="%s" d="%s"/>' % (style, d)

ICONS = {
    # ---------- batch 1: paint, retouch, fill ----------
    # Brush: handle, ferrule, solid tip
    'krita_tool_freehand': [
        p('M14 2 9.4 6.6'),
        p('M10.2 5.8l-2.4 2.4', S.replace('1.25', '2.4').replace('round;stroke-linejoin', 'butt;stroke-linejoin')),
        p('M7.2 8.6c-1.7.2-2.6 1.4-2.8 2.8-.2 1.3-1.1 2.2-2.4 2.5 2.4.5 4.6-.3 5.4-1.9.6-1.1.6-2.5-.2-3.4z', F),
    ],
    # Multibrush: mirror axis and two mirrored strokes
    'krita_tool_multihand': [
        p('M8 1.5v13', D),
        p('M5.6 2.6C2.6 4 6 7.4 3 11'),
        p('M10.4 2.6c3 1.4-.4 4.8 2.6 8.4'),
        '<circle style="%s" cx="3" cy="12" r="1.5"/>' % F,
        '<circle style="%s" cx="13" cy="12" r="1.5"/>' % F,
    ],
    # Dynamic brush: brush tip with motion lines
    'krita_tool_dyna': [
        p('M14.5 1.5 10.3 5.7'),
        p('M11 5l-2.1 2.1', S.replace('1.25', '2.2').replace('round;stroke-linejoin', 'butt;stroke-linejoin')),
        p('M8.4 7.6c-1.6.2-2.4 1.2-2.6 2.5-.2 1.2-1 2-2.2 2.3 2.2.4 4.2-.3 5-1.7.5-1 .5-2.3-.2-3.1z', F),
        p('M1.5 5h3.5M1 7.5h2.5M2 10h1.2'),
    ],
    # Calligraphy: pen nib with slit and breather hole
    'calligraphy': [
        p('M5.5 1.5h5l1.3 5.2L8 14.5 4.2 6.7z'),
        p('M8 8.6v5.4'),
        p('M8 7.6a.9.9 0 1 0 0-.01z', F),
    ],
    # Smart patch: bandage
    'krita_tool_smart_patch': [
        '<g transform="rotate(-45 8 8)">' +
        '<rect style="%s" x="1.2" y="5.2" width="13.6" height="5.6" rx="2.8"/>' % S +
        '<rect style="%s" x="5.6" y="5.2" width="4.8" height="5.6"/>' % T +
        '<path style="%s" d="M5.6 5.2v5.6M10.4 5.2v5.6"/>' % S +
        '</g>',
    ],
    # Gradient: frame with fading bars
    'krita_tool_gradient': [
        '<rect style="%s" x="2" y="2" width="12" height="12" rx="1.6"/>' % S,
        '<rect style="fill:{c};fill-opacity:0.9" x="3.4" y="3.4" width="2.4" height="9.2"/>',
        '<rect style="fill:{c};fill-opacity:0.55" x="5.8" y="3.4" width="2.4" height="9.2"/>',
        '<rect style="fill:{c};fill-opacity:0.25" x="8.2" y="3.4" width="2.4" height="9.2"/>',
    ],
    # Fill: tipped bucket, paint inside, a drop
    'krita_tool_color_fill': [
        p('M2.6 7.4 7.4 2.6l5 5-4.8 4.8z'),
        p('M3.6 8.4h7.8l-3.8 3.8z', T),
        p('M5 5C3.8 3.6 5.2 1.6 6.8 2.4'),
        p('M13.6 9.6s-1.6 2-1.6 3.1a1.6 1.6 0 0 0 3.2 0c0-1.1-1.6-3.1-1.6-3.1z', F),
    ],
    # Enclose and fill: dashed region with a drop inside
    'krita_tool_enclose_and_fill': [
        '<rect style="%s" x="1.6" y="1.6" width="12.8" height="12.8" rx="2.4"/>' % D,
        p('M8 4.8s-2.4 3-2.4 4.6a2.4 2.4 0 0 0 4.8 0c0-1.6-2.4-4.6-2.4-4.6z', T),
        p('M8 4.8s-2.4 3-2.4 4.6a2.4 2.4 0 0 0 4.8 0c0-1.6-2.4-4.6-2.4-4.6z'),
    ],
    # Lasso fill (Giga's own tool): tinted lasso loop with its rope
    'giga_tool_lasso_fill': [
        p('M8.6 1.6c3.2 0 5.8 1.7 5.8 4.5 0 2.8-2.8 4.7-6.1 4.7-1.1 0-2.1-.2-3-.6C3.3 9.5 1.6 8 1.6 6.1 1.6 3.5 4.9 1.6 8.6 1.6z', T),
        p('M8.6 1.6c3.2 0 5.8 1.7 5.8 4.5 0 2.8-2.8 4.7-6.1 4.7-1.1 0-2.1-.2-3-.6C3.3 9.5 1.6 8 1.6 6.1 1.6 3.5 4.9 1.6 8.6 1.6z'),
        p('M5.3 10.2c-.5.9-.4 1.8.4 2.2.9.4 1.5-.3 1.1-1-.4-.6-1.6-.3-2 .6-.4.9-.2 1.9.4 2.6'),
    ],
    # Colourize mask (lazy brush): two tinted regions with scribbles
    'krita_tool_lazybrush': [
        '<circle style="%s" cx="5.4" cy="10.6" r="3.6"/>' % T,
        '<circle style="%s" cx="5.4" cy="10.6" r="3.6"/>' % S,
        '<circle style="%s" cx="10.6" cy="5.4" r="3.6"/>' % S,
        p('M3.6 10.8c.8-.9 1.6.7 2.4-.2.5-.5 1-.3 1.2.2'),
        p('M8.8 5.6c.8-.9 1.6.7 2.4-.2.5-.5 1-.3 1.2.2'),
    ],
}

HEADER = ('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" version="1.1" viewBox="0 0 16 16">\n'
          '  <!-- Giga Sketch tool icon "%s" (generated by giga-tools/make_tool_icons.py). '
          'SPDX-FileCopyrightText: 2026 The Giga Sketch contributors. SPDX-License-Identifier: GPL-3.0-or-later -->\n')

def main():
    os.makedirs(OUT, exist_ok=True)
    files = []
    for name, parts in sorted(ICONS.items()):
        for variant, color in COLORS.items():
            body = '\n'.join('  ' + part.replace('{c}', color) for part in parts)
            fname = '%s_%s.svg' % (variant, name)
            with open(os.path.join(OUT, fname), 'w') as fh:
                fh.write(HEADER % name + body + '\n</svg>\n')
            files.append(fname)
    with open(os.path.join(OUT, 'giga-icons.qrc'), 'w') as fh:
        fh.write('<!--\n  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors\n'
                 '  SPDX-License-Identifier: GPL-3.0-or-later\n-->\n<RCC>\n    <qresource prefix="/giga-icons">\n')
        for f in files:
            fh.write('        <file>%s</file>\n' % f)
        fh.write('    </qresource>\n</RCC>\n')
    print('wrote %d icons (%d files) to %s' % (len(ICONS), len(files), os.path.normpath(OUT)))

if __name__ == '__main__':
    main()

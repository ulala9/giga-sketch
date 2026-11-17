/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_NOTES_DECORATION_H
#define GIGA_NOTES_DECORATION_H

#include "canvas/kis_canvas_decoration.h"

/**
 * Giga Sketch: draws the sticky notes (GigaNote) on top of everything on the
 * canvas: the grid, guides and assistants. The pictures of the reference
 * layer stay where Krita draws them; only the notes come up here (GigaNote::
 * paint() is a no-op once this exists, see GigaNote::setDrawnOnTop()).
 *
 * Painted on the interface thread, in widget coordinates, every time the
 * canvas repaints the area (notes are cheap: a box and a little text).
 */
class GigaNotesDecoration : public KisCanvasDecoration
{
    Q_OBJECT
public:
    explicit GigaNotesDecoration(QPointer<KisView> view);
    ~GigaNotesDecoration() override;

protected:
    void drawDecoration(QPainter &gc, const QRectF &updateArea, const KisCoordinatesConverter *converter,
                        KisCanvas2 *canvas) override;
};

#endif // GIGA_NOTES_DECORATION_H

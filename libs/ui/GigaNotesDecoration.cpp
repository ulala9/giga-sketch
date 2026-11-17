/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaNotesDecoration.h"

#include <QPainter>
#include <algorithm>

#include "GigaNote.h"
#include "KisDocument.h"
#include "KisReferenceImagesLayer.h"
#include "KisView.h"
#include "kis_coordinates_converter.h"

GigaNotesDecoration::GigaNotesDecoration(QPointer<KisView> view)
    : KisCanvasDecoration(QStringLiteral("gigaNotesDecoration"), view)
{
    // Above the grid (0), guides (90) and assistants (95).
    setPriority(100);
    GigaNote::setDrawnOnTop(true);
}

GigaNotesDecoration::~GigaNotesDecoration()
{
}

void GigaNotesDecoration::drawDecoration(QPainter &gc, const QRectF &updateArea, const KisCoordinatesConverter *converter,
                                         KisCanvas2 *canvas)
{
    Q_UNUSED(canvas);
    if (!view() || !view()->document() || !converter) return;
    KisSharedPtr<KisReferenceImagesLayer> layer = view()->document()->referenceImagesLayer();
    if (!layer) return;

    QVector<GigaNote *> notes = GigaNote::notesOf(layer.data());
    std::stable_sort(notes.begin(), notes.end(), [](GigaNote *a, GigaNote *b) { return a->zIndex() < b->zIndex(); });

    const QTransform documentToWidget = converter->documentToWidgetTransform();
    for (GigaNote *note : notes) {
        if (!note->isVisible()) continue; // hidden (Show Annotations off) or on the Board
        if (!note->boundingRect().intersects(updateArea)) continue; // updateArea is in document coordinates
        gc.save();
        gc.setTransform(note->absoluteTransformation() * documentToWidget, true);
        note->paintOnTop(gc);
        gc.restore();
    }
}

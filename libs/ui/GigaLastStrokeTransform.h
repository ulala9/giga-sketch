/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_LAST_STROKE_TRANSFORM_H
#define GIGA_LAST_STROKE_TRANSFORM_H

#include <QObject>
#include <QPointer>
#include <QRect>

#include "kritaui_export.h"
#include "kis_types.h"
#include "tool/GigaLastStroke.h"

class KisDocument;
class KisViewManager;
class KUndo2Command;
class KUndo2Stack;

/**
 * Giga Sketch: Edit > Transform Last Stroke (Ctrl+Shift+Alt+T).
 *
 * Opens the Transform tool on just the stroke that was painted last, so it
 * can be warped/liquified without touching anything else on the layer.
 *
 * How it works (all on the GUI thread; the painting itself runs in Krita's
 * normal stroke queue):
 *  1. Check that the last undo entry really is that stroke.
 *  2. Repaint the recorded stroke onto a hidden layer that is not part of the
 *     document (no undo entry). That gives the stroke on its own, soft edges
 *     included, and none of the pixels around it.
 *  3. Undo the stroke on the real layer, so what it covered comes back.
 *  4. Hand the stroke image to the Transform tool the way "Paste and
 *     Transform" does (KisTool::newActivationWithExternalSource).
 *  5. Applied: rebuild the history as two entries, the stroke as drawn and
 *     "Transform Last Stroke", so one Ctrl+Z shows the stroke as drawn.
 *     Cancelled: redo the original stroke, everything is as before.
 */
class KRITAUI_EXPORT GigaLastStrokeTransform : public QObject
{
    Q_OBJECT
public:
    explicit GigaLastStrokeTransform(QObject *parent = nullptr);
    ~GigaLastStrokeTransform() override;

    /// The menu action. Shows a short canvas message when it can't run.
    void trigger(KisViewManager *view);

    /// True while the Transform tool is open on a lifted stroke.
    bool isActive() const { return m_active; }

    /// Bounding rect of the 64x64 tiles where @p a and @p b differ (empty if equal).
    static QRect differingRect(KisPaintDeviceSP a, KisPaintDeviceSP b);

private Q_SLOTS:
    void slotTransformFinished(bool applied, int mode);
    void slotDocumentRemoved();

private:
    void tryTrigger(int attempt);
    KisPaintDeviceSP renderStrokeOnly(GigaLastStrokeRecordSP record, KisImageSP image, KisNodeSP layer);
    void pushPixels(KisImageSP image, KisNodeSP layer, KisPaintDeviceSP source,
                    const QRect &rect, const KUndo2MagicString &name);
    void rememberReplacement(KisImageSP image, KUndo2Stack *stack);
    void endSession();

    QPointer<KisViewManager> m_pendingView;
    bool m_active {false};
    bool m_preparing {false};   ///< inside tryTrigger (its waits run the event loop)

    // The running session
    QPointer<KisDocument> m_doc;
    KisImageWSP m_image;
    KisNodeSP m_layer;
    GigaLastStrokeRecordSP m_record;
    KisPaintDeviceSP m_withStroke;   ///< layer as drawn (copy-on-write copy)
    int m_indexAfterLift {-1};       ///< undo index right after the stroke was undone
};

#endif // GIGA_LAST_STROKE_TRANSFORM_H

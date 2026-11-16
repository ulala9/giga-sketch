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
#include <QStack>
#include <QString>

#include "kritaui_export.h"
#include "kis_types.h"
#include "tool/GigaLastStroke.h"
#include "tool/GigaStrokePath.h"

class KisDocument;
class KisPaintLayer;
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
 *
 * "Points" mode (Giga Sketch, feature/stroke-points) instead opens the
 * hidden Edit Stroke Points tool: dots along the stroke that can be dragged,
 * and the stroke is *repainted* with its own brush along the bent path
 * (GigaStrokePath), so it stays crisp. While editing:
 *  - the real layer shows "lifted layer + repainted stroke", written by small
 *    no-undo jobs on the image's own threads (nothing waits on them);
 *  - only one repaint runs at a time; edits made meanwhile are merged into
 *    one more repaint after it (no pile-up while dragging fast);
 *  - Ctrl+Z / Ctrl+Shift+Z step through the dot moves (interceptUndo).
 * Applied: history = <stroke as drawn> then "Edit Stroke Points".
 * Pressing the shortcut again switches between Pixels and Points.
 */
class KRITAUI_EXPORT GigaLastStrokeTransform : public QObject
{
    Q_OBJECT
public:
    explicit GigaLastStrokeTransform(QObject *parent = nullptr);
    ~GigaLastStrokeTransform() override;

    /// The menu action. Shows a short canvas message when it can't run.
    void trigger(KisViewManager *view);

    /// True while the Transform tool (or the points tool) is open on a lifted stroke.
    bool isActive() const { return m_active; }

    /// Which mode the command opens in: remembered between uses.
    enum EditMode { PixelsMode = 0, PointsMode = 1 };
    static EditMode editMode();
    static void setEditMode(EditMode mode);

    /// Id of the hidden Edit Stroke Points tool (plugins/tools/basictools).
    static QString pointsToolId();

    /// The running Points session, or null. Used by the points tool.
    static GigaLastStrokeTransform *pointsSession();

    /// KisDocument's Ctrl+Z / Ctrl+Shift+Z ask this first. True = handled
    /// here (it stepped through the dot moves of a Points session).
    static bool interceptUndo(KisDocument *doc, bool redo);

    // --- Points mode, for the points tool (GUI thread only) ---
    GigaStrokePath &path() { return m_path; }
    /// Dot spacing on screen (px), remembered.
    static int pointSpacing();
    static void setPointSpacing(int screenPixels);
    /// Call before changing dots (remembers the shape for Ctrl+Z).
    void pointsBeginEdit();
    /// Call after changing dots: repaints the stroke (in the background).
    void pointsChanged();
    /// True while this class starts its own repaint stroke. Starting a
    /// stroke makes Krita ask the active tool to end its work (as Enter
    /// does); the points tool must ignore that request, not apply.
    bool startingRepaint() const { return m_startingRepaint; }
    /// True while a repaint is running or waiting.
    bool pointsRepainting() const { return m_renderBusy || m_renderDirty; }
    /// Apply (Enter / tool switch) or cancel (Esc). @p switchBack = return to
    /// the tool used before (false when the user already switched tools).
    void pointsFinish(bool apply, bool switchBack);
    /// Leave the dots and continue in the Transform tool with the stroke as
    /// it is now (Pixels mode).
    void pointsSwitchToPixels();

Q_SIGNALS:
    /// Points session: the dots or the repainted stroke changed.
    void sigPointsChanged();
    /// Points session ended (applied, cancelled or switched to Pixels).
    void sigPointsEnded();

public:

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

    // Points mode
    bool startPoints(KisViewManager *view, KisImageSP image, KisDocument *doc,
                     KisPaintLayer *layer, GigaLastStrokeRecordSP rec);
    void startPreviewRender();
    void previewRendered(int serial, const QRect &rect, KisPaintDeviceSP strokeDevice);
    bool waitPreview(int timeoutMs);
    void restoreLifted();
    void endPoints(bool switchBack);
    void pointsUndo(bool redo);
    void openTransformTool(KisViewManager *view, KisPaintDeviceSP strokeOnly);

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

    // Points session
    bool m_points {false};
    bool m_finishing {false};
    bool m_reopenInPoints {false};   ///< Pixels -> Points switch in progress
    QPointer<KisViewManager> m_view;
    QString m_previousToolId;
    GigaStrokePath m_path;
    KisPaintDeviceSP m_lifted;       ///< layer without the stroke (copy-on-write copy)
    KisPaintDeviceSP m_lastRender;   ///< newest repainted stroke on its own
    QRect m_previewRect;             ///< where the layer differs from m_lifted
    QRect m_drawnRect;               ///< where the stroke as drawn differs from m_lifted
    bool m_startingRepaint {false};
    bool m_renderBusy {false};
    bool m_renderDirty {false};
    int m_editSerial {0};
    int m_renderedSerial {-1};
    QStack<QVector<GigaStrokePath::Vertex>> m_undoShapes;
    QStack<QVector<GigaStrokePath::Vertex>> m_redoShapes;
};

#endif // GIGA_LAST_STROKE_TRANSFORM_H

/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_REFERENCE_BOARD_H
#define GIGA_REFERENCE_BOARD_H

#include <QColor>
#include <QList>
#include <QObject>
#include <QRectF>
#include <QSize>
#include <QTransform>
#include <QVector>

#include <kundo2command.h>

#include "GigaReferenceLayout.h"
#include "kritaui_export.h"

class KisReferenceImage;
class KisReferenceImagesLayer;
class KoCanvasBase;

/**
 * Giga Sketch: shared logic for the Reference Board, used by the Reference
 * Images tool (canvas buttons) and the Reference Board panel.
 *
 * Everything here runs on the interface thread. Changes go through one undo
 * command, GeometryCommand, that stores each image's canvas transform, Board
 * placement and on/off-Board state before and after.
 */
namespace GigaReferenceBoard
{

enum class Order {
    Current = 0, ///< reading order of where the images are now
    Colour,      ///< hue gradient (greys last, dark to light)
    Shape,       ///< tall to wide
    DateAdded,   ///< the order they were added in
};

enum class TargetShape {
    Square = 0,
    Canvas, ///< aspect of the document
    Board,  ///< aspect of the Board panel
};

/// Remembered between sessions (KisConfig group "GigaReferenceBoard").
struct KRITAUI_EXPORT Settings {
    GigaReferenceLayout::NormalizeMode normalizeMode = GigaReferenceLayout::NormalizeMode::SameArea;
    GigaReferenceLayout::Style style = GigaReferenceLayout::Style::JustifiedRows;
    Order order = Order::Current;
    TargetShape targetShape = TargetShape::Board;
    int spacing = 16; ///< screen pixels at 100 % zoom (document points)

    // Board look
    QColor background = QColor(38, 38, 38);
    bool shadows = true;
    bool roundedCorners = true;
    bool captions = false;
    bool flipWithCanvas = true; ///< mirror the Board when the canvas is mirrored
    bool rotateWithCanvas = true; ///< turn the Board by the canvas rotation (plus its own)
    bool dimOthers = true;

    /// Shrink oversized references in the saved file (open document untouched).
    bool shrinkOnSave = false;

    /// Arrange leaves sticky notes (GigaNote) where they are.
    bool keepNotesInPlace = true;

    static Settings load();
    void save() const;
};

/// One image's full placement: canvas transform + Board state.
struct KRITAUI_EXPORT ImageState {
    QTransform transform;
    bool onBoard = false;
    QRectF boardRect;
};

KRITAUI_EXPORT ImageState stateOf(const KisReferenceImage *image);

class KRITAUI_EXPORT GeometryCommand : public KUndo2Command
{
public:
    GeometryCommand(const KUndo2MagicString &text,
                    const QVector<KisReferenceImage *> &images,
                    const QVector<ImageState> &before,
                    const QVector<ImageState> &after,
                    KUndo2Command *parent = nullptr);
    void redo() override;
    void undo() override;

    const QVector<KisReferenceImage *> &images() const { return m_images; }
    const QVector<ImageState> &before() const { return m_before; }
    const QVector<ImageState> &after() const { return m_after; }

private:
    void apply(const QVector<ImageState> &states);
    QVector<KisReferenceImage *> m_images;
    QVector<ImageState> m_before;
    QVector<ImageState> m_after;
};

/// Replaces stored pixels (Resample to Current Size). Old pixels are kept in
/// the command so it can be undone until the document is closed.
class KRITAUI_EXPORT ResampleCommand : public KUndo2Command
{
public:
    ResampleCommand(const QVector<KisReferenceImage *> &images, const QVector<QSize> &targets, KUndo2Command *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    QVector<KisReferenceImage *> m_images;
    QVector<QImage> m_old;
    QVector<QImage> m_new;
};

/// Emits changed() whenever a Board command is applied (redo or undo), so
/// every open Board panel can repaint and animate.
class KRITAUI_EXPORT Notifier : public QObject
{
    Q_OBJECT
public:
    static Notifier *instance();
    void notify() { ++m_generation; Q_EMIT changed(); }
    quint64 generation() const { return m_generation; }
Q_SIGNALS:
    void changed();
    void settingsChanged();
private:
    quint64 m_generation = 0;
};

// ---- helpers ----------------------------------------------------------------

/// Size on the canvas, without rotation (document points).
KRITAUI_EXPORT QSizeF displayedSize(const KisReferenceImage *image);

/// Images in the layer that are on the canvas / on the Board, in layer order.
KRITAUI_EXPORT QVector<KisReferenceImage *> canvasImages(KisReferenceImagesLayer *layer);
KRITAUI_EXPORT QVector<KisReferenceImage *> boardImages(KisReferenceImagesLayer *layer);

/// Sorts images for arranging. currentRects = where each one is now.
KRITAUI_EXPORT QVector<int> orderFor(const QVector<KisReferenceImage *> &images,
                                     const QVector<QRectF> &currentRects,
                                     Order order,
                                     KisReferenceImagesLayer *layer);

KRITAUI_EXPORT qreal aspectFor(TargetShape shape, KoCanvasBase *canvas);

// All of these return nullptr when there is nothing to do. Push the result
// with canvas->addCommand(); for canvas commands, then call animateOnCanvas().
KRITAUI_EXPORT GeometryCommand *normalizeOnCanvas(const QVector<KisReferenceImage *> &images,
                                                  GigaReferenceLayout::NormalizeMode mode,
                                                  KisReferenceImage *matchTo);
KRITAUI_EXPORT GeometryCommand *arrangeOnCanvas(const QVector<KisReferenceImage *> &images,
                                                const Settings &settings, KoCanvasBase *canvas,
                                                KisReferenceImagesLayer *layer);
KRITAUI_EXPORT GeometryCommand *sendToBoard(const QVector<KisReferenceImage *> &images,
                                            KisReferenceImagesLayer *layer, const Settings &settings);
KRITAUI_EXPORT GeometryCommand *sendToCanvas(const QVector<KisReferenceImage *> &images);
KRITAUI_EXPORT GeometryCommand *normalizeOnBoard(const QVector<KisReferenceImage *> &images,
                                                 GigaReferenceLayout::NormalizeMode mode,
                                                 KisReferenceImage *matchTo);
KRITAUI_EXPORT GeometryCommand *arrangeOnBoard(const QVector<KisReferenceImage *> &images,
                                               const Settings &settings, KoCanvasBase *canvas,
                                               KisReferenceImagesLayer *layer);
KRITAUI_EXPORT GeometryCommand *moveOnBoard(KisReferenceImage *image, const QRectF &newRect);

/// Where the next new Board images go: a group laid out with the Board
/// style, under what is already there.
KRITAUI_EXPORT QVector<QRectF> placeNewOnBoard(const QVector<QSizeF> &sizes,
                                               const QVector<KisReferenceImage *> &alreadyOnBoard,
                                               const Settings &settings);

/// Smoothly moves the canvas images of an applied command from their old
/// to their new transforms (~200 ms, ease-out). Purely visual: the command
/// already holds the final state, and an undo/redo meanwhile stops it.
KRITAUI_EXPORT void animateOnCanvas(GeometryCommand *command);

// ---- Resample to Current Size -----------------------------------------------

/// Pixel size that keeps both the canvas size and the Board focus size sharp
/// (x 1.5 headroom). Invalid when the image is already small enough.
KRITAUI_EXPORT QSize resampleTarget(const KisReferenceImage *image);
KRITAUI_EXPORT ResampleCommand *resample(const QVector<KisReferenceImage *> &images);
/// "6000 × 4000 → 900 × 600", or "3 images: 412 MB → 9 MB" (memory), or an
/// empty string when nothing would shrink.
KRITAUI_EXPORT QString resampleSummary(const QVector<KisReferenceImage *> &images);
/// Same rule, used while saving when Settings::shrinkOnSave is on. Thread
/// safe (saving may run in the background).
KRITAUI_EXPORT QSize shrinkOnSaveTarget(const KisReferenceImage &image);

/// The Board panel reports its size here (device pixels) for the resample
/// rule and the "Board" target shape. Thread safe.
KRITAUI_EXPORT void setBoardViewportSize(const QSize &size);
KRITAUI_EXPORT QSize boardViewportSize();

} // namespace GigaReferenceBoard

#endif

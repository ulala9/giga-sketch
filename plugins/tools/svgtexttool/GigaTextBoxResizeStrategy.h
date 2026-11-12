/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_TEXT_BOX_RESIZE_STRATEGY_H
#define GIGA_TEXT_BOX_RESIZE_STRATEGY_H

#include <KoInteractionStrategy.h>

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>

class KoPathShape;
class KoShape;
class KoSvgTextShape;
class KUndo2Command;

/**
 * Giga Sketch: Photoshop-style text boxes.
 *
 * A text box is a text shape whose text flows inside one invisible
 * rectangle (SVG 2 "shape-inside"). The box has 8 handles; dragging one
 * resizes the rectangle and Krita's text layout reflows the text. The
 * letters are never stretched.
 */
namespace GigaTextBox
{
/// Handles, clockwise from the top-left corner.
enum Handle { TopLeft = 0, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left, HandleCount };

/// The box rectangle of @p text, or null when the text isn't a plain box
/// (no shape-inside, several shapes, a non-path shape, or a rotated/scaled
/// rectangle, which the handles can't resize correctly).
KoPathShape *boxOf(const KoSvgTextShape *text);

/// Handle position in the box's own (local) coordinates.
QPointF handleLocal(const QRectF &rect, int handle);

/// Handle position in document coordinates.
QPointF handleDocument(KoPathShape *box, int handle);

/// A new invisible rectangle (document coordinates) to use as a text box.
KoPathShape *createBoxShape(const QRectF &documentRect);

/// Undoable "Convert to Point Text": takes the box away (and deletes it
/// once the command is gone for good), so the text lays out freely again.
KUndo2Command *makeRemoveBoxCommand(KoSvgTextShape *text, KoPathShape *box);
} // namespace GigaTextBox

class GigaTextBoxResizeStrategy : public KoInteractionStrategy
{
public:
    GigaTextBoxResizeStrategy(KoToolBase *tool, KoSvgTextShape *text, KoPathShape *box, int handle, const QPointF &clicked);
    ~GigaTextBoxResizeStrategy() override = default;

    void handleMouseMove(const QPointF &mouseLocation, Qt::KeyboardModifiers modifiers) override;
    KUndo2Command *createCommand() override;
    void cancelInteraction() override;
    void finishInteraction(Qt::KeyboardModifiers modifiers) override;

private:
    void apply(const QRectF &localRect);

    KoSvgTextShape *m_text {nullptr};
    KoPathShape *m_box {nullptr};
    int m_handle {GigaTextBox::BottomRight};
    QTransform m_toLocal;      ///< document -> box coordinates, frozen at the start
    QPointF m_grabOffset;      ///< handle position minus click position (local)
    QRectF m_startRect;        ///< box outline at the start (local)
    QPointF m_startPosition;
    QSizeF m_startSize;
    qreal m_minSize {1.0};
};

#endif // GIGA_TEXT_BOX_RESIZE_STRATEGY_H

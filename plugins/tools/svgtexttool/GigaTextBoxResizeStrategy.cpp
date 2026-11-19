/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaTextBoxResizeStrategy.h"

#include <QPointer>

#include <KoPathShape.h>
#include <KoShapeBackground.h>
#include <KoShapeStrokeModel.h>
#include <KoSvgTextProperties.h>
#include <KoSvgTextShape.h>
#include <KoToolBase.h>
#include <kundo2command.h>
#include <kundo2magicstring.h>
#include <klocalizedstring.h>

namespace GigaTextBox
{

KoPathShape *boxOf(const KoSvgTextShape *text)
{
    if (!text) return nullptr;
    const QList<KoShape *> inside = text->shapesInside();
    if (inside.size() != 1 || !text->shapesSubtract().isEmpty()) return nullptr;
    KoPathShape *box = dynamic_cast<KoPathShape *>(inside.first());
    if (!box) return nullptr;
    // Only a plain (unrotated, unscaled) rectangle frame; anything fancier
    // is edited with the shape tools, as before.
    if (box->transformation().type() > QTransform::TxTranslate) return nullptr;
    if (box->outline().elementCount() > 6) return nullptr; // more than a rectangle
    return box;
}

QPointF handleLocal(const QRectF &r, int handle)
{
    switch (handle) {
    case TopLeft:     return r.topLeft();
    case Top:         return QPointF(r.center().x(), r.top());
    case TopRight:    return r.topRight();
    case Right:       return QPointF(r.right(), r.center().y());
    case BottomRight: return r.bottomRight();
    case Bottom:      return QPointF(r.center().x(), r.bottom());
    case BottomLeft:  return r.bottomLeft();
    case Left:        return QPointF(r.left(), r.center().y());
    default:          return r.center();
    }
}

QPointF handleDocument(KoPathShape *box, int handle)
{
    return box->absoluteTransformation().map(handleLocal(box->outlineRect(), handle));
}

KoPathShape *createBoxShape(const QRectF &documentRect)
{
    KoPathShape *box = new KoPathShape();
    box->moveTo(documentRect.topLeft());
    box->lineTo(documentRect.topRight());
    box->lineTo(documentRect.bottomRight());
    box->lineTo(documentRect.bottomLeft());
    box->close();
    box->normalize(); // points start at (0,0); the offset becomes the position
    // Invisible: it only defines where the text flows.
    box->setStroke(KoShapeStrokeModelSP());
    box->setBackground(QSharedPointer<KoShapeBackground>());
    return box;
}

KUndo2Command *makeRemoveBoxCommand(KoSvgTextShape *text, KoPathShape *box);

} // namespace GigaTextBox

namespace
{

/// Detach a text box rectangle from its text; owns it while detached.
class RemoveBoxCommand : public KUndo2Command
{
public:
    RemoveBoxCommand(KoSvgTextShape *text, KoPathShape *box)
        : KUndo2Command(kundo2_i18n("Convert to Point Text"))
        , m_text(text), m_box(box)
    {
    }
    ~RemoveBoxCommand() override
    {
        if (m_detached) delete m_box; // nobody else owns it now
    }
    void redo() override
    {
        m_text->update();
        m_text->removeShapesFromContours({m_box}, true, true);
        m_detached = true;
        m_text->relayout();
        m_text->update();
    }
    void undo() override
    {
        m_text->update();
        m_text->addShapeContours({m_box}, true); // its local position is unchanged
        m_detached = false;
        m_text->relayout();
        m_text->update();
    }

private:
    KoSvgTextShape *m_text;
    KoPathShape *m_box;
    bool m_detached {false};
};

/// Undo/redo for one box resize: the rectangle's size and position.
class ResizeCommand : public KUndo2Command
{
public:
    ResizeCommand(KoSvgTextShape *text, KoPathShape *box,
                  const QSizeF &oldSize, const QPointF &oldPos,
                  const QSizeF &newSize, const QPointF &newPos)
        : KUndo2Command(kundo2_i18n("Resize Text Box"))
        , m_text(text), m_box(box)
        , m_oldSize(oldSize), m_oldPos(oldPos), m_newSize(newSize), m_newPos(newPos)
    {
    }

    void redo() override { set(m_newSize, m_newPos); }
    void undo() override { set(m_oldSize, m_oldPos); }

private:
    void set(const QSizeF &size, const QPointF &pos)
    {
        m_text->update();
        m_box->setSize(size);
        m_box->setPosition(pos);
        m_text->gigaContourChanged(m_box); // reflow with the new box
        m_text->update();
    }

    KoSvgTextShape *m_text;
    KoPathShape *m_box;
    QSizeF m_oldSize;
    QPointF m_oldPos;
    QSizeF m_newSize;
    QPointF m_newPos;
};

} // namespace

GigaTextBoxResizeStrategy::GigaTextBoxResizeStrategy(KoToolBase *tool, KoSvgTextShape *text, KoPathShape *box,
                                                     int handle, const QPointF &clicked)
    : KoInteractionStrategy(tool)
    , m_text(text)
    , m_box(box)
    , m_handle(handle)
{
    m_toLocal = box->absoluteTransformation().inverted();
    m_startRect = box->outlineRect();
    m_startPosition = box->position();
    m_startSize = box->size();
    // Grab the handle where it is, not where the click landed inside it.
    m_grabOffset = GigaTextBox::handleLocal(m_startRect, handle) - m_toLocal.map(clicked);

    // Never smaller than roughly one letter.
    KoSvgTextProperties props = text->textProperties();
    props.inheritFrom(KoSvgTextProperties::defaultProperties(), true);
    m_minSize = qMax<qreal>(4.0, props.fontSize().value);
}

void GigaTextBoxResizeStrategy::handleMouseMove(const QPointF &mouseLocation, Qt::KeyboardModifiers modifiers)
{
    using namespace GigaTextBox;
    const QPointF p = m_toLocal.map(mouseLocation) + m_grabOffset;
    const QPointF c = m_startRect.center();
    const bool fromCenter = modifiers.testFlag(Qt::AltModifier);
    const bool keepRatio = modifiers.testFlag(Qt::ShiftModifier);

    const bool movesLeft = m_handle == TopLeft || m_handle == Left || m_handle == BottomLeft;
    const bool movesRight = m_handle == TopRight || m_handle == Right || m_handle == BottomRight;
    const bool movesTop = m_handle == TopLeft || m_handle == Top || m_handle == TopRight;
    const bool movesBottom = m_handle == BottomLeft || m_handle == Bottom || m_handle == BottomRight;

    qreal left = m_startRect.left(), right = m_startRect.right();
    qreal top = m_startRect.top(), bottom = m_startRect.bottom();

    // Alt: grow/shrink around the center. Otherwise the opposite side stays put.
    if (movesLeft || movesRight) {
        if (fromCenter) {
            const qreal half = qMax(m_minSize / 2, movesRight ? p.x() - c.x() : c.x() - p.x());
            left = c.x() - half;
            right = c.x() + half;
        } else if (movesLeft) {
            left = qMin(p.x(), right - m_minSize);
        } else {
            right = qMax(p.x(), left + m_minSize);
        }
    }
    if (movesTop || movesBottom) {
        if (fromCenter) {
            const qreal half = qMax(m_minSize / 2, movesBottom ? p.y() - c.y() : c.y() - p.y());
            top = c.y() - half;
            bottom = c.y() + half;
        } else if (movesTop) {
            top = qMin(p.y(), bottom - m_minSize);
        } else {
            bottom = qMax(p.y(), top + m_minSize);
        }
    }

    // Shift on a corner: keep the box's proportions.
    const bool corner = (movesLeft || movesRight) && (movesTop || movesBottom);
    if (keepRatio && corner && m_startRect.width() > 0 && m_startRect.height() > 0) {
        const qreal ratio = m_startRect.width() / m_startRect.height();
        qreal w = right - left;
        qreal h = bottom - top;
        if (w / m_startRect.width() > h / m_startRect.height()) {
            h = w / ratio;
        } else {
            w = h * ratio;
        }
        if (fromCenter) {
            left = c.x() - w / 2; right = c.x() + w / 2;
            top = c.y() - h / 2; bottom = c.y() + h / 2;
        } else {
            if (movesLeft) left = right - w; else right = left + w;
            if (movesTop) top = bottom - h; else bottom = top + h;
        }
    }

    apply(QRectF(QPointF(left, top), QPointF(right, bottom)));
}

void GigaTextBoxResizeStrategy::apply(const QRectF &localRect)
{
    m_text->update();
    m_box->setSize(localRect.size());
    // The box's corner was at m_startRect.topLeft() (in the start frame);
    // place the resized outline so its corner lands at localRect.topLeft().
    m_box->setPosition(m_startPosition + localRect.topLeft() - m_box->outlineRect().topLeft());
    m_text->gigaContourChanged(m_box); // reflow with the new box
    m_text->update();
}

KUndo2Command *GigaTextBoxResizeStrategy::createCommand()
{
    const QSizeF newSize = m_box->size();
    const QPointF newPos = m_box->position();
    if (newSize == m_startSize && newPos == m_startPosition) return nullptr;
    return new ResizeCommand(m_text, m_box, m_startSize, m_startPosition, newSize, newPos);
}

void GigaTextBoxResizeStrategy::cancelInteraction()
{
    m_text->update();
    m_box->setSize(m_startSize);
    m_box->setPosition(m_startPosition);
    m_text->gigaContourChanged(m_box);
    m_text->update();
}

void GigaTextBoxResizeStrategy::finishInteraction(Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers);
}

KUndo2Command *GigaTextBox::makeRemoveBoxCommand(KoSvgTextShape *text, KoPathShape *box)
{
    return new RemoveBoxCommand(text, box);
}

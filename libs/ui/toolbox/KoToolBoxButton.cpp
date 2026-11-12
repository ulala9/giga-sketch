/*
 * SPDX-FileCopyrightText: 2015 Friedrich W. H. Kossebau <kossebau@kde.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KoToolBoxButton_p.h"

#include <kis_debug.h>

#include <KoToolManager.h>
#include <QAction>
#include <QPalette>
#include <QApplication>
#include <klocalizedstring.h>
#include <QKeySequence>
#include <KoIcon.h>
#include <QPainter>
#include <QPainterPath>

KoToolBoxButton::KoToolBoxButton(KoToolAction *toolAction, QWidget *parent)
    : QToolButton(parent)
    , m_toolAction(toolAction)
{
    setObjectName(m_toolAction->id());
    // ensure same L&F
    setCheckable(true);
    setAutoRaise(true);
    setIcon(kisIcon(m_toolAction->iconName()));

    // only a fallback
    setToolTip(m_toolAction->toolTip());

    connect(this, SIGNAL(clicked(bool)), m_toolAction, SLOT(trigger()));
}

void KoToolBoxButton::attachAction(QAction *action)
{
    if (!action) {
        return;
    }

    connect(action, SIGNAL(changed()), SLOT(slotUpdateActionData()));
    setDataFromToolAction(action);
}

void KoToolBoxButton::setHighlightColor()
{
    QPalette p = qApp->palette();
    if (isChecked()) {
        QPalette palette_highlight(p);
        QColor c = p.color(QPalette::Highlight);
        palette_highlight.setColor(QPalette::Button, c);
        setPalette(palette_highlight);
    }
    else {
        setPalette(p);
    }
}

void KoToolBoxButton::setDataFromToolAction(QAction *action)
{
    if (!action) {
        warnUI << "No tool action available, using fallback!";
        setToolTip(m_toolAction->toolTip());
        return;
    }
    setToolTip(action->toolTip());
}

void KoToolBoxButton::setGroupIndicator(bool show)
{
    if (m_groupIndicator == show) return;
    m_groupIndicator = show;
    update();
}

void KoToolBoxButton::paintEvent(QPaintEvent *event)
{
    QToolButton::paintEvent(event);
    if (!m_groupIndicator) return;

    // Small filled triangle in the bottom-right corner.
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = qMax(3.0, width() / 7.0);
    const QPointF corner(width() - 2.0, height() - 2.0);
    QPainterPath triangle;
    triangle.moveTo(corner);
    triangle.lineTo(corner - QPointF(s, 0));
    triangle.lineTo(corner - QPointF(0, s));
    triangle.closeSubpath();
    QColor c = palette().color(QPalette::ButtonText);
    c.setAlphaF(0.7);
    p.fillPath(triangle, c);
}

void KoToolBoxButton::slotUpdateActionData()
{
    QAction *action = qobject_cast<QAction*>(sender());
    if (action) {
        setDataFromToolAction(action);
    }
}

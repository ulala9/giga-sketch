/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "GigaMotion.h"

#include <QEvent>
#include <QPainter>
#include <QPointer>
#include <QVariantAnimation>
#include <QWidget>

#include <kis_config.h>

namespace {

/// The fading cover. Child of the widget it covers, always on top of its siblings.
class GigaMotionCover : public QWidget
{
public:
    GigaMotionCover(QWidget *target, const QPixmap &picture, int ms)
        : QWidget(target)
        , m_picture(picture)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        setGeometry(target->rect());
        target->installEventFilter(this);   // follow resizes while fading

        m_anim = new QVariantAnimation(this);
        m_anim->setStartValue(1.0);
        m_anim->setEndValue(0.0);
        m_anim->setDuration(ms);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_opacity = v.toReal();
            update();
        });
        connect(m_anim, &QVariantAnimation::finished, this, &QObject::deleteLater);

        show();
        raise();
        m_anim->start();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == parent()) {
            if (event->type() == QEvent::Resize) {
                setGeometry(parentWidget()->rect());
            } else if (event->type() == QEvent::ChildAdded || event->type() == QEvent::ZOrderChange) {
                raise(); // panels restored after us must stay under the cover
            }
        }
        return false;
    }

    void paintEvent(QPaintEvent *) override
    {
        if (m_opacity <= 0.0) return;
        QPainter p(this);
        p.setOpacity(m_opacity);
        if (!m_picture.isNull()) {
            p.drawPixmap(QPoint(0, 0), m_picture);
        } else {
            p.fillRect(rect(), parentWidget()->palette().window());
        }
    }

private:
    QPixmap m_picture;
    QVariantAnimation *m_anim {nullptr};
    qreal m_opacity {1.0};
};

int s_enabled = -1; // -1 = not read yet

} // namespace

bool GigaMotion::enabled()
{
    if (s_enabled < 0) {
        s_enabled = KisConfig(true).readEntry<bool>("gigaUiAnimations", true) ? 1 : 0;
    }
    return s_enabled == 1;
}

void GigaMotion::setEnabled(bool value)
{
    s_enabled = value ? 1 : 0;
    KisConfig(false).writeEntry<bool>("gigaUiAnimations", value);
}

void GigaMotion::reveal(QWidget *w, int ms)
{
    // The widget may not be shown or sized yet (e.g. a menu in aboutToShow);
    // the cover follows its size.
    if (!w || !enabled()) return;
    // One cover at a time: a second show while fading just keeps the first.
    for (QObject *child : w->children()) {
        if (dynamic_cast<GigaMotionCover *>(child)) return;
    }
    new GigaMotionCover(w, QPixmap(), ms);
}

QPixmap GigaMotion::snapshot(QWidget *window)
{
    if (!window || !enabled() || !window->isVisible()) return QPixmap();
    return window->grab();
}

void GigaMotion::crossFade(QWidget *window, const QPixmap &before, int ms)
{
    if (!window || before.isNull() || !enabled()) return;
    new GigaMotionCover(window, before, ms);
}

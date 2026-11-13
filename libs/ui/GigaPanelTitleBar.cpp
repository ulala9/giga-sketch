/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaPanelTitleBar.h"

#include "GigaFloatingPanel.h"
#include "GigaFloatingPanelManager.h"
#include "KisMainWindow.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

#include <klocalizedstring.h>

namespace {
constexpr int HEADER_HEIGHT = 22;
}

GigaPanelTitleBar::GigaPanelTitleBar(QDockWidget *dock, QMainWindow *mainWindow)
    : QWidget(dock)
    , m_dock(dock)
    , m_mainWindow(mainWindow)
{
    // One style sheet for the header's buttons (cheaper than one each).
    setStyleSheet(QStringLiteral(
        "QToolButton{color:#bdbdbd;border:0;border-radius:3px;font-size:11px;}"
        "QToolButton:hover{background:rgba(255,255,255,30);}"
        "QToolButton::menu-indicator{image:none;}"));
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 2, 0);
    layout->setSpacing(0);

    m_title = new QLabel(dock->windowTitle(), this);
    QFont f = m_title->font();
    f.setPointSizeF(f.pointSizeF() * 0.92);
    f.setBold(true);
    m_title->setFont(f);
    QPalette titlePalette = m_title->palette();
    titlePalette.setColor(QPalette::WindowText, QColor(0xd6, 0xd6, 0xd6));
    m_title->setPalette(titlePalette); // (a palette is cheaper than a style sheet)
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents); // drags reach the dock
    layout->addWidget(m_title);
    layout->addStretch(1);
    QObject::connect(dock, &QDockWidget::windowTitleChanged, m_title, &QLabel::setText);

    auto makeButton = [this](const QString &text, const QString &tip) {
        QToolButton *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setFixedSize(20, 18);
        return b;
    };

    m_collapse = makeButton(QStringLiteral("«"), i18nc("@info:tooltip", "Collapse or expand this panel (or double-click the header)"));
    QObject::connect(m_collapse, &QToolButton::clicked, this, [this]() { setCollapsed(!m_collapsed); });
    layout->addWidget(m_collapse);

    m_menu = makeButton(QStringLiteral("≡"), i18nc("@info:tooltip", "Panel menu"));
    m_menu->setPopupMode(QToolButton::InstantPopup);
    QMenu *menu = new QMenu(m_menu);
    QObject::connect(menu->addAction(i18nc("@action:inmenu panel menu", "Collapse / Expand")),
                     &QAction::triggered, this, [this]() { setCollapsed(!m_collapsed); });
    QAction *floatAction = menu->addAction(i18nc("@action:inmenu panel menu", "Float"));
    QObject::connect(floatAction, &QAction::triggered, this, [this]() {
        if (!m_dock) return;
        // Float inside the window when that's on; otherwise Qt's own floating.
        KisMainWindow *window = qobject_cast<KisMainWindow *>(m_mainWindow);
        GigaFloatingPanelManager *manager = window ? window->gigaFloatingPanels() : nullptr;
        if (manager && manager->toggleFloat(m_dock)) return;
        m_dock->setFloating(!m_dock->isFloating());
    });
    QObject::connect(menu, &QMenu::aboutToShow, this, [this, floatAction]() {
        KisMainWindow *window = qobject_cast<KisMainWindow *>(m_mainWindow);
        GigaFloatingPanelManager *manager = window ? window->gigaFloatingPanels() : nullptr;
        const bool floating = m_dock && manager && manager->isFloating(m_dock);
        floatAction->setText(floating ? i18nc("@action:inmenu panel menu", "Dock")
                                      : i18nc("@action:inmenu panel menu", "Float"));
    });
    menu->addSeparator();
    QObject::connect(menu->addAction(i18nc("@action:inmenu panel menu", "Close")),
                     &QAction::triggered, this, [this]() { if (m_dock) m_dock->hide(); });
    QObject::connect(menu->addAction(i18nc("@action:inmenu panel menu", "Close Tab Group")),
                     &QAction::triggered, this, [this]() {
        if (!m_dock || !m_mainWindow) return;
        const QList<QDockWidget *> group = m_mainWindow->tabifiedDockWidgets(m_dock);
        for (QDockWidget *other : group) other->hide();
        m_dock->hide();
    });
    m_menu->setMenu(menu);
    layout->addWidget(m_menu);
}

QSize GigaPanelTitleBar::sizeHint() const
{
    return QSize(QWidget::sizeHint().width(), HEADER_HEIGHT);
}

QSize GigaPanelTitleBar::minimumSizeHint() const
{
    return QSize(60, HEADER_HEIGHT);
}

void GigaPanelTitleBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(35, 35, 37));
    p.setPen(QColor(24, 24, 26));
    p.drawLine(rect().bottomLeft(), rect().bottomRight());
}

void GigaPanelTitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Photoshop: double-click a panel header to collapse it. (Qt's default
    // would float the panel instead; use the panel menu for that.)
    setCollapsed(!m_collapsed);
    event->accept();
}

void GigaPanelTitleBar::setCollapsed(bool collapsed)
{
    if (!m_dock || collapsed == m_collapsed) return;
    m_collapsed = collapsed;
    // Floating inside the window: the floating frame does the collapsing.
    if (GigaFloatingPanel *floating = qobject_cast<GigaFloatingPanel *>(m_dock->parentWidget())) {
        floating->setCollapsed(collapsed);
        m_collapse->setText(collapsed ? QStringLiteral("»") : QStringLiteral("«"));
        return;
    }
    if (QWidget *content = m_dock->widget()) {
        content->setVisible(!collapsed);
    }
    if (collapsed) {
        m_expandedMaxHeight = m_dock->maximumHeight();
        m_dock->setMaximumHeight(HEADER_HEIGHT + 2);
    } else {
        m_dock->setMaximumHeight(m_expandedMaxHeight);
    }
    m_collapse->setText(collapsed ? QStringLiteral("»") : QStringLiteral("«"));
}

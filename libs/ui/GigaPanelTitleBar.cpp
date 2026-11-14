/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaPanelTitleBar.h"

#include "GigaFloatingPanel.h"
#include "GigaFloatingPanelManager.h"
#include "KisMainWindow.h"

#include <QContextMenuEvent>
#include <QDockWidget>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

#include <klocalizedstring.h>
#include <kis_icon_utils.h>

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

    // Float/Dock and Close are buttons right on the header (one click),
    // not hidden in a menu. The same actions are on a right-click menu,
    // with Close Tab Group.
    auto toggleFloat = [this]() {
        if (!m_dock) return;
        // Float inside the window when that's on; otherwise Qt's own floating.
        KisMainWindow *window = qobject_cast<KisMainWindow *>(m_mainWindow);
        GigaFloatingPanelManager *manager = window ? window->gigaFloatingPanels() : nullptr;
        if (manager && manager->toggleFloat(m_dock)) return;
        m_dock->setFloating(!m_dock->isFloating());
    };
    auto closeGroup = [this]() {
        if (!m_dock || !m_mainWindow) return;
        const QList<QDockWidget *> group = m_mainWindow->tabifiedDockWidgets(m_dock);
        for (QDockWidget *other : group) other->hide();
        m_dock->hide();
    };

    m_float = makeButton(QString(), QString());
    m_float->setIcon(KisIconUtils::loadIcon("docker_float"));
    m_float->setIconSize(QSize(12, 12));
    QObject::connect(m_float, &QToolButton::clicked, this, toggleFloat);
    layout->addWidget(m_float);

    m_close = makeButton(QString(), i18nc("@info:tooltip", "Close this panel (Shift+click: close its whole tab group)"));
    m_close->setIcon(KisIconUtils::loadIcon("docker_close"));
    m_close->setIconSize(QSize(12, 12));
    QObject::connect(m_close, &QToolButton::clicked, this, [this, closeGroup]() {
        if (QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier)) {
            closeGroup();
        } else if (m_dock) {
            m_dock->hide();
        }
    });
    layout->addWidget(m_close);

    // Right-click on the header: the same actions as a menu.
    m_contextMenu = new QMenu(this);
    QObject::connect(m_contextMenu->addAction(i18nc("@action:inmenu panel menu", "Collapse / Expand")),
                     &QAction::triggered, this, [this]() { setCollapsed(!m_collapsed); });
    QAction *floatAction = m_contextMenu->addAction(i18nc("@action:inmenu panel menu", "Float"));
    QObject::connect(floatAction, &QAction::triggered, this, toggleFloat);
    QObject::connect(m_contextMenu, &QMenu::aboutToShow, this, [this, floatAction]() {
        floatAction->setText(isFloatingHere() ? i18nc("@action:inmenu panel menu", "Dock")
                                              : i18nc("@action:inmenu panel menu", "Float"));
    });
    m_contextMenu->addSeparator();
    QObject::connect(m_contextMenu->addAction(i18nc("@action:inmenu panel menu", "Close")),
                     &QAction::triggered, this, [this]() { if (m_dock) m_dock->hide(); });
    QObject::connect(m_contextMenu->addAction(i18nc("@action:inmenu panel menu", "Close Tab Group")),
                     &QAction::triggered, this, closeGroup);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    updateFloatButton();
}

bool GigaPanelTitleBar::isFloatingHere() const
{
    KisMainWindow *window = qobject_cast<KisMainWindow *>(m_mainWindow);
    GigaFloatingPanelManager *manager = window ? window->gigaFloatingPanels() : nullptr;
    return m_dock && ((manager && manager->isFloating(m_dock)) || m_dock->isFloating());
}

void GigaPanelTitleBar::updateFloatButton()
{
    if (!m_float) return;
    const QString tip = isFloatingHere() ? i18nc("@info:tooltip", "Dock this panel back")
                                         : i18nc("@info:tooltip", "Float this panel over the canvas");
    if (m_float->toolTip() != tip) m_float->setToolTip(tip);
}

void GigaPanelTitleBar::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_contextMenu) m_contextMenu->popup(event->globalPos());
    event->accept();
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
    // The header repaints whenever its panel floats or docks (it moves to a
    // new parent): keep the Float/Dock tooltip in step. Only a string compare.
    updateFloatButton();
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

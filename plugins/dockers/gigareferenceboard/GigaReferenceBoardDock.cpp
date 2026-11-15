/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaReferenceBoardDock.h"
#include "GigaReferenceBoardView.h"

#include <GigaReferenceBoard.h>
#include <kis_canvas2.h>
#include <kis_icon_utils.h>

#include <klocalizedstring.h>

#include <QActionGroup>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace
{
QToolButton *makeButton(QWidget *parent, const QString &icon, const QString &text, const QString &tip)
{
    QToolButton *b = new QToolButton(parent);
    if (!icon.isEmpty()) b->setIcon(KisIconUtils::loadIcon(icon));
    b->setText(text);
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setToolButtonStyle(icon.isEmpty() ? Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly);
    return b;
}

/// Adds a radio group whose items set one integer setting.
void addChoices(QMenu *menu, const QString &title, const QStringList &labels, int current,
                const std::function<void(int)> &set)
{
    menu->addSection(title);
    QActionGroup *group = new QActionGroup(menu);
    for (int i = 0; i < labels.size(); ++i) {
        QAction *a = menu->addAction(labels[i]);
        a->setCheckable(true);
        a->setChecked(i == current);
        group->addAction(a);
        QObject::connect(a, &QAction::triggered, menu, [set, i]() { set(i); });
    }
}

void addToggle(QMenu *menu, const QString &label, bool on, const std::function<void(bool)> &set)
{
    QAction *a = menu->addAction(label);
    a->setCheckable(true);
    a->setChecked(on);
    QObject::connect(a, &QAction::toggled, menu, [set](bool v) { set(v); });
}

template<typename F>
void changeSettings(F change)
{
    GigaReferenceBoard::Settings s = GigaReferenceBoard::Settings::load();
    change(s);
    s.save(); // also tells the view and the tool options
}
} // namespace

GigaReferenceBoardDock::GigaReferenceBoardDock()
    : QDockWidget(i18nc("@title:window", "Reference Board"))
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget *header = new QWidget(page);
    QHBoxLayout *row = new QHBoxLayout(header);
    row->setContentsMargins(4, 2, 4, 2);
    row->setSpacing(2);

    m_view = new GigaReferenceBoardView(page);

    QToolButton *arrange = makeButton(header, QString(), i18nc("@action:button", "Arrange"),
                                      i18n("Tidy the Board up (click), or choose the layout style (arrow)"));
    arrange->setPopupMode(QToolButton::MenuButtonPopup);
    m_arrangeMenu = new QMenu(arrange);
    arrange->setMenu(m_arrangeMenu);
    connect(arrange, &QToolButton::clicked, m_view, &GigaReferenceBoardView::arrange);
    connect(m_arrangeMenu, &QMenu::aboutToShow, this, &GigaReferenceBoardDock::buildArrangeMenu);

    QToolButton *fit = makeButton(header, QStringLiteral("zoom-fit"), i18n("Fit All"),
                                  i18n("Show the whole Board (Home or Esc)"));
    connect(fit, &QToolButton::clicked, m_view, &GigaReferenceBoardView::fitAll);

    QToolButton *add = makeButton(header, QStringLiteral("krita_tool_reference_images"), i18n("Add from Canvas"),
                                  i18n("Move the selected reference images (or all of them) from the canvas to the Board"));
    connect(add, &QToolButton::clicked, m_view, &GigaReferenceBoardView::addFromCanvas);

    QToolButton *back = makeButton(header, QString(), i18nc("@action:button send all board images back to the canvas", "To Canvas"),
                                   i18n("Put every Board image back exactly where it was on the canvas"));
    connect(back, &QToolButton::clicked, m_view, &GigaReferenceBoardView::sendAllToCanvas);

    QToolButton *look = makeButton(header, QStringLiteral("configure"), i18n("Look"), i18n("Board look and options"));
    look->setPopupMode(QToolButton::InstantPopup);
    m_lookMenu = new QMenu(look);
    look->setMenu(m_lookMenu);
    connect(m_lookMenu, &QMenu::aboutToShow, this, &GigaReferenceBoardDock::buildLookMenu);

    row->addWidget(arrange);
    row->addWidget(fit);
    row->addStretch(1);
    row->addWidget(add);
    row->addWidget(back);
    row->addWidget(look);

    layout->addWidget(header);
    layout->addWidget(m_view, 1);
    setWidget(page);

    setEnabled(false);
}

GigaReferenceBoardDock::~GigaReferenceBoardDock()
{
}

void GigaReferenceBoardDock::setCanvas(KoCanvasBase *canvas)
{
    if (m_canvas == canvas) return;
    m_canvas = dynamic_cast<KisCanvas2 *>(canvas);
    setEnabled(m_canvas != nullptr);
    m_view->setCanvas(m_canvas);
}

void GigaReferenceBoardDock::unsetCanvas()
{
    m_canvas = nullptr;
    setEnabled(false);
    m_view->setCanvas(nullptr);
}

void GigaReferenceBoardDock::buildArrangeMenu()
{
    using namespace GigaReferenceBoard;
    m_arrangeMenu->clear();
    const Settings s = Settings::load();

    addChoices(m_arrangeMenu, i18n("Layout"),
               {i18n("Justified rows"), i18n("Masonry"), i18n("Packed"), i18n("Grid"), i18n("Auto")}, int(s.style),
               [this](int v) {
                   changeSettings([v](Settings &s) { s.style = GigaReferenceLayout::Style(v); });
                   m_view->arrange();
               });
    addChoices(m_arrangeMenu, i18n("Order"), {i18n("Current"), i18n("Colour"), i18n("Shape"), i18n("Date added")},
               int(s.order), [this](int v) {
                   changeSettings([v](Settings &s) { s.order = Order(v); });
                   m_view->arrange();
               });
    addChoices(m_arrangeMenu, i18n("Shape"), {i18n("Square"), i18n("Canvas"), i18n("Board panel")},
               int(s.targetShape), [this](int v) {
                   changeSettings([v](Settings &s) { s.targetShape = TargetShape(v); });
                   m_view->arrange();
               });
    const QList<int> spacings = {0, 8, 16, 32, 64};
    QStringList spacingLabels;
    for (int sp : spacings) spacingLabels << i18nc("spacing in pixels", "%1 px", sp);
    addChoices(m_arrangeMenu, i18n("Spacing"), spacingLabels, spacings.indexOf(s.spacing), [this, spacings](int v) {
        changeSettings([v, spacings](Settings &s) { s.spacing = spacings[v]; });
        m_view->arrange();
    });

    m_arrangeMenu->addSection(i18n("Normalize sizes"));
    const QStringList modes = {i18n("Same area"), i18n("Same height"), i18n("Same width"), i18n("Match selected")};
    for (int i = 0; i < modes.size(); ++i) {
        m_arrangeMenu->addAction(modes[i], this, [this, i]() { m_view->normalize(i); });
    }
}

void GigaReferenceBoardDock::buildLookMenu()
{
    using namespace GigaReferenceBoard;
    m_lookMenu->clear();
    const Settings s = Settings::load();

    m_lookMenu->addAction(i18n("Background Colour..."), this, [this]() {
        const QColor c = QColorDialog::getColor(Settings::load().background, this, i18n("Board Background"));
        if (c.isValid()) changeSettings([c](Settings &s) { s.background = c; });
    });
    addToggle(m_lookMenu, i18n("Shadows"), s.shadows, [](bool v) { changeSettings([v](Settings &s) { s.shadows = v; }); });
    addToggle(m_lookMenu, i18n("Rounded Corners"), s.roundedCorners,
              [](bool v) { changeSettings([v](Settings &s) { s.roundedCorners = v; }); });
    addToggle(m_lookMenu, i18n("File Name Captions"), s.captions,
              [](bool v) { changeSettings([v](Settings &s) { s.captions = v; }); });
    addToggle(m_lookMenu, i18n("Dim Others When Focused"), s.dimOthers,
              [](bool v) { changeSettings([v](Settings &s) { s.dimOthers = v; }); });
    addToggle(m_lookMenu, i18n("Flip With Canvas"), s.flipWithCanvas,
              [](bool v) { changeSettings([v](Settings &s) { s.flipWithCanvas = v; }); });
    m_lookMenu->actions().last()->setToolTip(
        i18n("When the canvas is flipped (Mirror View), flip the Board too."));
    m_lookMenu->addSeparator();
    addToggle(m_lookMenu, i18n("Shrink Oversized References When Saving"), s.shrinkOnSave,
              [](bool v) { changeSettings([v](Settings &s) { s.shrinkOnSave = v; }); });
    m_lookMenu->actions().last()->setToolTip(
        i18n("Saves each reference at about the size it is shown at (×1.5), like Resample to Current Size. "
             "The open document keeps full detail until you reopen the file."));
    m_lookMenu->setToolTipsVisible(true);

    m_lookMenu->addSection(i18n("Controls"));
    QAction *help = m_lookMenu->addAction(
        i18n("Wheel: zoom · Middle/Space-drag: pan · Click: focus\n"
             "←/→: next image · Esc/Home: overview · Ctrl+click: pick colour"));
    help->setEnabled(false);
}

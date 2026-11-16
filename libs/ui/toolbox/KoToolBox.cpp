/*
 * SPDX-FileCopyrightText: 2005-2009 Thomas Zander <zander@kde.org>
 * SPDX-FileCopyrightText: 2009 Peter Simonsson <peter.simonsson@gmail.com>
 * SPDX-FileCopyrightText: 2010 Cyrille Berger <cberger@cberger.net>
 * SPDX-FileCopyrightText: 2022 Alvin Wong <alvin@alvinhc.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KoToolBox_p.h"
#include "KoToolBoxLayout_p.h"
#include "KoToolBoxButton_p.h"
#include "kis_assert.h"

#include <QButtonGroup>
#include <QToolButton>
#include <QStyleOption>
#include <QActionGroup>
#include <QPainter>
#include <QHash>
#include <QApplication>
#include <QStyle>
#include <QTimer>
#include <QMenu>
#include <GigaMotion.h>
#include <QAction>
#include <QScreen>
#include <QContextMenuEvent>
#include <algorithm>

#include <klocalizedstring.h>
#include <WidgetsDebug.h>
#include <kconfiggroup.h>
#include <ksharedconfig.h>
#include <KisPortingUtils.h>

#include <kactioncollection.h>
#include <KisViewManager.h>
#include <KoCanvasController.h>
#include <KoShapeLayer.h>

#define BUTTON_MARGIN 10

// Giga Sketch: Photoshop-style tool groups. Each group shows one button (the
// last tool used from it); press-and-hold or right-click lists the others.
// Groups live in four sections, so separators fall between tool families.
// Tools not listed here keep Krita's normal placement, so none can vanish.
struct GigaToolGroup {
    const char *key;
    const char *section;
    QStringList toolIds;
};

static const QList<GigaToolGroup> &gigaToolGroups()
{
    static const QList<GigaToolGroup> groups = {
        // Move, select, crop, sample
        {"move",    "giga-1-select", {"KritaTransform/KisToolMove", "KisToolTransform"}},
        {"marquee", "giga-1-select", {"KisToolSelectRectangular", "KisToolSelectElliptical"}},
        {"lasso",   "giga-1-select", {"KisToolSelectOutline", "KisToolSelectPolygonal", "KisToolSelectMagnetic", "KisToolSelectPath"}},
        {"wand",    "giga-1-select", {"KisToolSelectContiguous", "KisToolSelectSimilar"}},
        {"crop",    "giga-1-select", {"KisToolCrop"}},
        {"sample",  "giga-1-select", {"KritaSelected/KisToolColorSampler", "KritaShape/KisToolMeasure", "KisAssistantTool", "ToolReferenceImages"}},
        // Paint, retouch, fill
        {"brush",   "giga-2-paint",  {"KritaShape/KisToolBrush", "KritaShape/KisToolMultiBrush", "KritaShape/KisToolDyna", "KarbonCalligraphyTool"}},
        {"heal",    "giga-2-paint",  {"KritaShape/KisToolSmartPatch"}},
        {"fill",    "giga-2-paint",  {"KritaFill/KisToolGradient", "KritaFill/KisToolFill", "KisToolEncloseAndFill", "GigaToolLassoFill", "KritaShape/KisToolLazyBrush"}},
        // Vector, text, shapes
        {"pen",     "giga-3-vector", {"KisToolPath", "KisToolPencil", "PathTool"}},
        {"type",    "giga-3-vector", {"SvgTextTool"}},
        {"select-shapes", "giga-3-vector", {"InteractionTool", "KritaShape/KisToolKnife"}},
        {"shapes",  "giga-3-vector", {"KritaShape/KisToolRectangle", "KritaShape/KisToolEllipse", "KisToolPolygon", "KisToolPolyline", "KritaShape/KisToolLine"}},
        // View
        {"hand",    "giga-4-view",   {"PanTool"}},
        {"zoom",    "giga-4-view",   {"ZoomTool"}},
    };
    return groups;
}

// Finds a tool's group: returns the group index (or -1) and its position.
static int gigaGroupOf(const QString &toolId, int *position = nullptr)
{
    const QList<GigaToolGroup> &groups = gigaToolGroups();
    for (int g = 0; g < groups.size(); g++) {
        const int pos = groups[g].toolIds.indexOf(toolId);
        if (pos >= 0) {
            if (position) *position = pos;
            return g;
        }
    }
    return -1;
}

static const char *GIGA_GROUPED_KEY = "gigaGroupedTools";
static QString gigaLastToolKey(const char *groupKey) { return QStringLiteral("gigaGroupLast_") + QLatin1String(groupKey); }

static int buttonSize(int screen)
{
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(screen < QGuiApplication::screens().size() && screen >= 0, 16);

    QRect rc = QGuiApplication::screens().at(screen)->availableGeometry();
    if (rc.width() <= 1024) {
        return 12;
    }
    else if (rc.width() <= 1377) {
        return 14;
    }
    else  if (rc.width() <= 1920 ) {
        return 16;
    }
    else {
        return 22;
    }

}

class KoToolBox::Private
{
public:
    void addSection(Section *section, const QString &name);

    QList<QToolButton*> buttons;
    KoToolBoxButton *selectedButton {nullptr};
    QHash<QString, KoToolBoxButton*> buttonsByToolId;
    QMap<QString, Section*> sections;
    KoToolBoxLayout *layout {0};
    QButtonGroup *buttonGroup {0};
    QHash<QToolButton*, QString> visibilityCodes;
    bool floating {false};
    int iconSize {0};
    QMap<QAction*,int> contextIconSizes;
    QAction *defaultIconSizeAction {0};
    Qt::Orientation orientation {Qt::Vertical};

    // Giga Sketch tool groups
    bool gigaGrouped {true};
    QHash<int, QList<KoToolBoxButton*>> gigaMembers; ///< group index -> buttons, in table order
    QHash<int, QMenu*> gigaMenus;                    ///< group index -> its popup menu
    QHash<QToolButton*, bool> codeVisible;           ///< visibility decided by setButtonsVisible()
};

void KoToolBox::Private::addSection(Section *section, const QString &name)
{
    section->setName(name);
    layout->addSection(section);
    sections.insert(name, section);
}

KoToolBox::KoToolBox()
    : d(new Private)
{
    d->layout = new KoToolBoxLayout(this);

    // Giga Sketch: grouped tools come first, in four family sections.
    d->gigaGrouped = KSharedConfig::openConfig()->group("KoToolBox").readEntry(GIGA_GROUPED_KEY, true);
    if (d->gigaGrouped) {
        QStringList sectionNames;
        Q_FOREACH (const GigaToolGroup &group, gigaToolGroups()) {
            if (!sectionNames.contains(QLatin1String(group.section))) sectionNames << QLatin1String(group.section);
        }
        Q_FOREACH (const QString &name, sectionNames) {
            d->addSection(new Section(this), name);
        }
    }

    // add defaults
    d->addSection(new Section(this), "main");
    d->addSection(new Section(this), "dynamic");

    d->buttonGroup = new QButtonGroup(this);

    // Get screen the widget exists in, but fall back to primary screen if invalid.
    const int widgetsScreen = KisPortingUtils::getScreenNumberForWidget(QApplication::activeWindow());
    const int primaryScreen = 0; //In QT, primary screen should always be the first index of QGuiApplication::screens()
    const int screen = (widgetsScreen >= 0 && widgetsScreen < QGuiApplication::screens().size()) ? widgetsScreen : primaryScreen;
    const int toolbuttonSize = buttonSize(screen);
    KConfigGroup cfg =  KSharedConfig::openConfig()->group("KoToolBox");
    d->iconSize = cfg.readEntry("iconSize", toolbuttonSize);

    Q_FOREACH (KoToolAction *toolAction, KoToolManager::instance()->toolActionList()) {
        addButton(toolAction);
    }

    applyIconSize();

    // Update visibility of buttons
    setButtonsVisible(QList<QString>());

    connect(KoToolManager::instance(), SIGNAL(changedTool(KoCanvasController*)),
            this, SLOT(setActiveTool(KoCanvasController*)));
    connect(KoToolManager::instance(), SIGNAL(currentLayerChanged(const KoCanvasController*,const KoShapeLayer*)),
            this, SLOT(setCurrentLayer(const KoCanvasController*,const KoShapeLayer*)));
    connect(KoToolManager::instance(), SIGNAL(toolCodesSelected(QList<QString>)), this, SLOT(setButtonsVisible(QList<QString>)));
    connect(KoToolManager::instance(),
            SIGNAL(addedTool(KoToolAction*,KoCanvasController*)),
            this, SLOT(toolAdded(KoToolAction*,KoCanvasController*)));

}

KoToolBox::~KoToolBox()
{
    delete d;
}

void KoToolBox::applyIconSize()
{
    Q_FOREACH (QToolButton *button, d->buttons) {
        button->setIconSize(QSize(d->iconSize, d->iconSize));
    }

    Q_FOREACH (Section *section, d->sections.values())  {
        section->setButtonSize(QSize(d->iconSize + BUTTON_MARGIN,  d->iconSize + BUTTON_MARGIN));
    }
}


void KoToolBox::setViewManager(KisViewManager *viewManager)
{
    KisKActionCollection *actionCollection = viewManager->actionCollection();
    Q_FOREACH(KoToolAction *toolAction, KoToolManager::instance()->toolActionList()) {
        QAction *toolQAction = actionCollection->action(toolAction->id());
        auto button = d->buttonsByToolId.find(toolAction->id());
        if (button == d->buttonsByToolId.end()) {
            qWarning() << "Toolbox is missing button for tool" << toolAction->id();
            continue;
        }
        (*button)->attachAction(toolQAction);
    }
}

void KoToolBox::addButton(KoToolAction *toolAction)
{
    KoToolBoxButton *button = new KoToolBoxButton(toolAction, this);

    d->buttons << button;

    QString sectionToBeAddedTo;
    const QString section = toolAction->section();
    if (section.contains(qApp->applicationName())) {
        sectionToBeAddedTo = "main";
    } else if (section.contains("main")) {
        sectionToBeAddedTo = "main";
    }  else if (section.contains("dynamic")) {
        sectionToBeAddedTo = "dynamic";
    } else {
        sectionToBeAddedTo = section;
    }

    int priority = toolAction->priority();

    // Giga Sketch: grouped tools go to their family section, in a fixed slot
    // per group (group index * 100 + position), so a group's button never
    // moves when you switch tools inside it.
    int gigaPosition = 0;
    const int gigaGroup = d->gigaGrouped ? gigaGroupOf(toolAction->id(), &gigaPosition) : -1;
    if (gigaGroup >= 0) {
        sectionToBeAddedTo = QLatin1String(gigaToolGroups()[gigaGroup].section);
        priority = gigaGroup * 100 + gigaPosition;

        QList<KoToolBoxButton*> &members = d->gigaMembers[gigaGroup];
        members << button;
        std::sort(members.begin(), members.end(), [](KoToolBoxButton *a, KoToolBoxButton *b) {
            int pa = 0, pb = 0;
            gigaGroupOf(a->toolAction()->id(), &pa);
            gigaGroupOf(b->toolAction()->id(), &pb);
            return pa < pb;
        });

        // (Re)build the group's menu: one entry per tool in the group.
        QMenu *menu = d->gigaMenus.value(gigaGroup);
        if (!menu) {
            menu = new QMenu(this);
            d->gigaMenus.insert(gigaGroup, menu);
            // Giga Sketch: the tool group popup fades in.
            connect(menu, &QMenu::aboutToShow, menu, [menu]() { GigaMotion::reveal(menu, 100); });
        }
        menu->clear();
        Q_FOREACH (KoToolBoxButton *member, members) {
            KoToolAction *memberAction = member->toolAction();
            QAction *entry = menu->addAction(member->icon(), memberAction->iconText());
            entry->setToolTip(memberAction->toolTip());
            connect(entry, SIGNAL(triggered()), memberAction, SLOT(trigger()));
        }
        const bool hasOthers = members.size() > 1;
        Q_FOREACH (KoToolBoxButton *member, members) {
            member->setGroupIndicator(hasOthers);
            if (hasOthers) {
                // Click = use this tool; press and hold = group menu.
                member->setMenu(menu);
                member->setPopupMode(QToolButton::DelayedPopup);
                member->installEventFilter(this);
            }
        }
    }

    Section *sectionWidget = d->sections.value(sectionToBeAddedTo);
    if (sectionWidget == 0) {
        sectionWidget = new Section(this);
        d->addSection(sectionWidget, sectionToBeAddedTo);
    }
    sectionWidget->addButton(button, priority);

    d->buttonGroup->addButton(button);
    d->buttonsByToolId.insert(toolAction->id(), button);

    d->visibilityCodes.insert(button, toolAction->visibilityCode());
}

void KoToolBox::setActiveTool(KoCanvasController *canvas)
{
    Q_UNUSED(canvas);

    QString id = KoToolManager::instance()->activeToolId();

    // Giga Sketch: the active tool becomes its group's visible button.
    if (d->gigaGrouped) {
        const int group = gigaGroupOf(id);
        if (group >= 0) {
            KConfigGroup cfg = KSharedConfig::openConfig()->group("KoToolBox");
            const QString key = gigaLastToolKey(gigaToolGroups()[group].key);
            if (cfg.readEntry(key, QString()) != id) {
                cfg.writeEntry(key, id);
            }
            gigaApplyGroups();
        }
    }

    KoToolBoxButton *button = d->buttonsByToolId.value(id);
    if (button) {
        button->setChecked(true);
        button->setHighlightColor();
        if (d->selectedButton) {
            d->selectedButton->setHighlightColor();
        }
        d->selectedButton = button;
    }
    else {
        warnWidgets << "KoToolBox::setActiveTool(" << id << "): no such button found";
    }
}

void KoToolBox::setButtonsVisible(const QList<QString> &codes)
{
    Q_FOREACH (QToolButton *button, d->visibilityCodes.keys()) {
        QString code = d->visibilityCodes.value(button);

        if (code.startsWith(QLatin1String("flake/"))) {
            continue;
        }

        if (code.endsWith( QLatin1String( "/always"))) {
            button->setVisible(true);
            button->setEnabled(true);
        }
        else if (code.isEmpty()) {
            button->setVisible(true);
            button->setEnabled( codes.count() != 0 );
        }
        else {
            button->setVisible( codes.contains(code) );
        }
    }
    // Giga Sketch: remember what Krita decided, then hide the non-current
    // members of each tool group.
    Q_FOREACH (QToolButton *button, d->visibilityCodes.keys()) {
        d->codeVisible.insert(button, !button->isHidden());
    }
    gigaApplyGroups();

    layout()->invalidate();
    update();
}

void KoToolBox::gigaApplyGroups()
{
    if (!d->gigaGrouped) return;
    KConfigGroup cfg = KSharedConfig::openConfig()->group("KoToolBox");

    for (auto it = d->gigaMembers.constBegin(); it != d->gigaMembers.constEnd(); ++it) {
        const QList<KoToolBoxButton*> &members = it.value();
        if (members.isEmpty()) continue;

        const QString lastId = cfg.readEntry(gigaLastToolKey(gigaToolGroups()[it.key()].key), QString());
        KoToolBoxButton *shown = members.first();
        Q_FOREACH (KoToolBoxButton *member, members) {
            if (member->toolAction()->id() == lastId) shown = member;
        }
        Q_FOREACH (KoToolBoxButton *member, members) {
            member->setVisible(member == shown && d->codeVisible.value(member, true));
        }
    }
    layout()->invalidate();
    update();
}

bool KoToolBox::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ContextMenu) {
        KoToolBoxButton *button = qobject_cast<KoToolBoxButton*>(watched);
        if (button && button->menu()) {
            QContextMenuEvent *contextEvent = static_cast<QContextMenuEvent*>(event);
            button->menu()->exec(contextEvent->globalPos());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void KoToolBox::setCurrentLayer(const KoCanvasController *canvas, const KoShapeLayer *layer)
{
    Q_UNUSED(canvas);
    const bool enabled = layer == 0 || (layer->isShapeEditable() && layer->isVisible());
    foreach (QToolButton *button, d->visibilityCodes.keys()) {
        if (d->visibilityCodes[button].endsWith( QLatin1String( "/always") ) ) {
            continue;
        }
        button->setEnabled(enabled);
    }
}

void KoToolBox::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    const QList<Section*> sections = d->sections.values();
    QList<Section*>::const_iterator iterator = sections.begin();
    int halfSpacing = layout()->spacing();
    if (halfSpacing > 0) {
        halfSpacing /= 2;
    }
    while(iterator != sections.end()) {
        Section *section = *iterator;
        QStyleOption styleoption;
        styleoption.palette = palette();

        if (section->separators() & Section::SeparatorTop) {
            int y = section->y() - halfSpacing;
            styleoption.state = QStyle::State_None;
            styleoption.rect = QRect(section->x(), y-1, section->width(), 2);

            style()->drawPrimitive(QStyle::PE_IndicatorToolBarSeparator, &styleoption, &painter);
        }

        if (section->separators() & Section::SeparatorLeft && section->isLeftToRight()) {
            int x = section->x() - halfSpacing;
            styleoption.state = QStyle::State_Horizontal;
            styleoption.rect = QRect(x-1, section->y(), 2, section->height());

            style()->drawPrimitive(QStyle::PE_IndicatorToolBarSeparator, &styleoption, &painter);
        } else if (section->separators() & Section::SeparatorLeft && section->isRightToLeft()) {
            int x = section->x() + section->width() + halfSpacing;
            styleoption.state = QStyle::State_Horizontal;
            styleoption.rect = QRect(x-1, section->y(), 2, section->height());

            style()->drawPrimitive(QStyle::PE_IndicatorToolBarSeparator, &styleoption, &painter);
        }

        ++iterator;
    }

    painter.end();
}

void KoToolBox::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) {
        Q_FOREACH (QToolButton *button, d->buttons) {
            KoToolBoxButton* toolBoxButton = qobject_cast<KoToolBoxButton*>(button);
            if (toolBoxButton) {
                toolBoxButton->setHighlightColor();
            }
        }
    }
}

void KoToolBox::setOrientation(Qt::Orientation orientation)
{
    d->orientation = orientation;
    d->layout->setOrientation(orientation);
    QTimer::singleShot(0, this, SLOT(update()));
    Q_FOREACH (Section* section, d->sections) {
        section->setOrientation(orientation);
    }
}

void KoToolBox::setFloating(bool v)
{
    d->floating = v;
}

void KoToolBox::toolAdded(KoToolAction *toolAction, KoCanvasController *canvas)
{
    Q_UNUSED(canvas);
    addButton(toolAction);
    setButtonsVisible(QList<QString>());

}

void KoToolBox::slotContextIconSize()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (action) {
        int iconSize = -1;
        if (action == d->defaultIconSizeAction) {
            iconSize = buttonSize(KisPortingUtils::getScreenNumberForWidget(QApplication::activeWindow()));
            QAction *action = d->contextIconSizes.key(iconSize);
            if (action) {
                action->setChecked(true);
            }
        } else if (d->contextIconSizes.contains(action)) {
            iconSize = d->contextIconSizes.value(action);
        }
        KIS_SAFE_ASSERT_RECOVER(iconSize >= 0) { iconSize = 16; }

        KConfigGroup cfg =  KSharedConfig::openConfig()->group("KoToolBox");
        cfg.writeEntry("iconSize", iconSize);
        d->iconSize = iconSize;

        applyIconSize();
    }
}

void KoToolBox::setupIconSizeMenu(QMenu *menu)
{
    if (d->contextIconSizes.isEmpty()) {
        d->defaultIconSizeAction = menu->addAction(i18nc("@item:inmenu Icon size", "Default"),
                                                   this, SLOT(slotContextIconSize()));

        QActionGroup *sizeGroup = new QActionGroup(menu);
        QList<int> sizes;
        sizes << 12 << 14 << 16 << 22 << 32 << 48 << 64; //<< 96 << 128 << 192 << 256;
        Q_FOREACH (int i, sizes) {
            QAction *action = menu->addAction(i18n("%1x%2", i, i), this, SLOT(slotContextIconSize()));
            d->contextIconSizes.insert(action, i);
            action->setActionGroup(sizeGroup);
            action->setCheckable(true);
            if (d->iconSize == i) {
                action->setChecked(true);
            }
        }

        // Giga Sketch: switch between grouped (Photoshop-style) and flat tools.
        menu->addSeparator();
        QAction *grouped = menu->addAction(i18nc("@item:inmenu", "Group Related Tools (restart to apply)"));
        grouped->setCheckable(true);
        grouped->setChecked(d->gigaGrouped);
        connect(grouped, &QAction::toggled, this, [](bool on) {
            KSharedConfig::openConfig()->group("KoToolBox").writeEntry(GIGA_GROUPED_KEY, on);
        });
    }
}

KoToolBoxLayout *KoToolBox::toolBoxLayout() const
{
    return d->layout;
}

#include "moc_KoToolBoxScrollArea_p.cpp"

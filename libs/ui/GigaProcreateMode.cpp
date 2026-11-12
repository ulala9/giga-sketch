/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaProcreateMode.h"

#include <QAction>
#include <QDockWidget>
#include <QMenu>
#include <QLabel>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QPainter>
#include <QSlider>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QtMath>

#include <klocalizedstring.h>
#include <kactioncollection.h>
#include <kxmlguifactory.h>

#include <KoCanvasResourceProvider.h>
#include <KoColor.h>
#include <KoToolManager.h>
#include <KisResourceModel.h>
#include <KisResourceTypes.h>
#include <brushengine/kis_paintop_preset.h>

#include "KisMainWindow.h"
#include "GigaProcreateWidgets.h"
#include "KisViewManager.h"
#include "KisResourceServerProvider.h"
#include "kis_canvas_resource_provider.h"
#include "kis_config.h"
#include "kis_icon_utils.h"

namespace {

const char *TOOL_BRUSH = "KritaShape/KisToolBrush";
const char *TOOL_SELECT = "KisToolSelectOutline";
const char *TOOL_TRANSFORM = "KisToolTransform";

// Brush size slider: logarithmic, 1 to 1000 px, so small sizes get room.
constexpr int SLIDER_STEPS = 1000;
constexpr qreal MAX_SIZE = 1000.0;

int sizeToSlider(qreal size)
{
    return qBound(0, qRound(std::log(qMax(1.0, size)) / std::log(MAX_SIZE) * SLIDER_STEPS), SLIDER_STEPS);
}

qreal sliderToSize(int value)
{
    return std::pow(MAX_SIZE, qreal(value) / SLIDER_STEPS);
}

// Which brush slot is in use: Procreate's Paint / Smudge / Erase.
enum class Slot { Paint, Smudge, Erase };
Slot s_slot = Slot::Paint;

} // namespace

GigaProcreateMode::GigaProcreateMode(KisMainWindow *mainWindow, QAction *nextModeAction)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
    , m_toggle(nextModeAction) // "Next Interface Mode (F9)", owned by GigaInterfaceModes
{
}

QToolButton *GigaProcreateMode::addButton(QToolBar *bar, const QString &icon, const QString &text, const QString &toolTip)
{
    QToolButton *button = new QToolButton(bar);
    button->setAutoRaise(true);
    if (!icon.isEmpty()) {
        button->setIcon(KisIconUtils::loadIcon(icon));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    } else {
        button->setText(text);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
    button->setToolTip(toolTip.isEmpty() ? text : toolTip);
    bar->addWidget(button);
    return button;
}

void GigaProcreateMode::triggerAction(const QString &name)
{
    if (QAction *action = m_mainWindow->actionCollection()->action(name)) {
        action->trigger();
    }
}

void GigaProcreateMode::activateTool(const QString &toolId)
{
    KoToolManager::instance()->switchToolRequested(toolId);
}

void GigaProcreateMode::buildBars()
{
    if (m_topBar) return;

    // ---------------- Top bar ----------------
    m_topBar = new QToolBar(i18nc("@title:window", "Procreate Top Bar"), m_mainWindow);
    m_topBar->setObjectName(QStringLiteral("GigaProcreateTopBar"));
    m_topBar->setMovable(false);
    m_topBar->setIconSize(QSize(22, 22));
    // Procreate-like: thin, dark, borderless; text buttons on the left,
    // frameless icons on the right.
    m_topBar->setStyleSheet(QStringLiteral(
        "QToolBar#GigaProcreateTopBar{background:#1c1c1e;border:0;padding:2px 8px;spacing:4px;}"
        "QToolBar#GigaProcreateTopBar QToolButton{border:0;color:#e8e8e8;padding:4px 8px;border-radius:7px;}"
        "QToolBar#GigaProcreateTopBar QToolButton:hover{background:rgba(255,255,255,22);}"
        "QToolBar#GigaProcreateTopBar QToolButton::menu-indicator{image:none;}"));

    // ---------------- Right bar (vertical, right edge) ----------------
    m_rightBar = new QToolBar(i18nc("@title:window", "Procreate Side Bar"), m_mainWindow);
    m_rightBar->setObjectName(QStringLiteral("GigaProcreateRightBar"));
    m_rightBar->setOrientation(Qt::Vertical);
    m_rightBar->setMovable(false);
    m_rightBar->setIconSize(QSize(22, 22));
    m_rightBar->setStyleSheet(QStringLiteral(
        "QToolBar#GigaProcreateRightBar{background:#1c1c1e;border:0;padding:8px 4px;spacing:6px;}"
        "QToolBar#GigaProcreateRightBar QToolButton{border:0;color:#e8e8e8;padding:5px 6px;border-radius:7px;}"
        "QToolBar#GigaProcreateRightBar QToolButton:hover{background:rgba(255,255,255,22);}"
        "QToolBar#GigaProcreateRightBar QToolButton::menu-indicator{image:none;}"
        "QToolBar#GigaProcreateRightBar QSlider::groove:vertical{background:rgba(255,255,255,40);width:6px;border-radius:3px;}"
        "QToolBar#GigaProcreateRightBar QSlider::handle:vertical{background:#f0f0f0;height:16px;width:18px;margin:0 -6px;border-radius:8px;}"));

    auto menuButton = [this](const QString &text, QMenu *menu) {
        QToolButton *button = addButton(m_topBar, QString(), text, QString());
        button->setMenu(menu);
        button->setPopupMode(QToolButton::InstantPopup);
        return button;
    };
    auto addNamed = [this](QMenu *menu, const char *name) {
        if (QAction *action = m_mainWindow->actionCollection()->action(QString::fromLatin1(name))) {
            menu->addAction(action);
        }
    };

    QMenu *gallery = new QMenu(m_topBar);
    addNamed(gallery, "file_new");
    addNamed(gallery, "file_open");
    addNamed(gallery, "file_open_recent");
    menuButton(i18nc("Procreate-style top bar", "Gallery"), gallery);

    QMenu *actions = new QMenu(m_topBar);
    addNamed(actions, "file_save");
    addNamed(actions, "file_save_as");
    addNamed(actions, "file_export_file");
    actions->addSeparator();
    addNamed(actions, "imagesize");
    addNamed(actions, "canvassize");
    addNamed(actions, "image_properties");
    actions->addSeparator();
    addNamed(actions, "options_configure");
    if (m_toggle) {
        actions->addSeparator();
        actions->addAction(m_toggle);
    }
    menuButton(i18nc("Procreate-style top bar", "Actions"), actions);

    QMenu *filters = qobject_cast<QMenu *>(m_mainWindow->guiFactory()->container(QStringLiteral("Filter"), m_mainWindow));
    if (filters) {
        menuButton(i18nc("Procreate-style top bar", "Adjustments"), filters);
    }

    QToolButton *selection = addButton(m_topBar, QString(), i18nc("Procreate-style top bar", "Selection"), QString());
    connect(selection, &QToolButton::clicked, this, [this]() { activateTool(QString::fromLatin1(TOOL_SELECT)); });
    QToolButton *transform = addButton(m_topBar, QString(), i18nc("Procreate-style top bar", "Transform"), QString());
    connect(transform, &QToolButton::clicked, this, [this]() { activateTool(QString::fromLatin1(TOOL_TRANSFORM)); });


    QToolButton *brush = addButton(m_rightBar, QStringLiteral("krita_tool_freehand"), QString(),
                                   i18nc("@info:tooltip", "Paint (tap again for the brush library)"));
    connect(brush, &QToolButton::clicked, this, [this, brush]() { brushClicked(brush); });
    QToolButton *smudge = addButton(m_rightBar, QStringLiteral("colorsmudge"), QString(),
                                    i18nc("@info:tooltip", "Smudge (tap again for the brush library)"));
    connect(smudge, &QToolButton::clicked, this, [this, smudge]() { smudgeClicked(smudge); });
    QToolButton *eraser = addButton(m_rightBar, QStringLiteral("draw-eraser"), QString(),
                                    i18nc("@info:tooltip", "Erase (tap again for the brush library)"));
    connect(eraser, &QToolButton::clicked, this, [this, eraser]() { eraserClicked(eraser); });
    QToolButton *layers = addButton(m_rightBar, QStringLiteral("all-layers"), QString(), i18nc("@info:tooltip", "Layers"));
    connect(layers, &QToolButton::clicked, this, [this, layers]() {
        showPopover(new GigaLayersPanel(m_mainWindow->viewManager()), layers);
    });
    m_colorDot = addButton(m_rightBar, QString(), QString(), i18nc("@info:tooltip", "Colors"));
    m_colorDot->setToolButtonStyle(Qt::ToolButtonIconOnly);
    connect(m_colorDot, &QToolButton::clicked, this, [this]() {
        showPopover(new GigaColorPanel(m_mainWindow->viewManager()), m_colorDot);
    });

    m_mainWindow->addToolBarBreak(Qt::TopToolBarArea);
    m_mainWindow->addToolBar(Qt::TopToolBarArea, m_topBar);

    // Brush size and opacity live in their own popover (one button), so the
    // bar stays small. Undo/redo: Ctrl+Z / Ctrl+Shift+Z. Picker: Alt+click.
    QToolButton *brushSettings = addButton(m_rightBar, QStringLiteral("brushsize-increase"), QString(),
                                           i18nc("@info:tooltip", "Brush size and opacity"));
    connect(brushSettings, &QToolButton::clicked, this, [this, brushSettings]() {
        if (!m_mainWindow->viewManager()) return;
        KisCanvasResourceProvider *provider = m_mainWindow->viewManager()->canvasResourceProvider();

        QWidget *panel = new QWidget();
        panel->setMinimumWidth(260);
        QFormLayout *form = new QFormLayout(panel);
        form->setContentsMargins(0, 0, 0, 0);

        QSlider *size = new QSlider(Qt::Horizontal, panel);
        size->setRange(0, SLIDER_STEPS);
        size->setValue(sizeToSlider(provider->size()));
        QLabel *sizeValue = new QLabel(QStringLiteral("%1 px").arg(qRound(provider->size())), panel);
        connect(size, &QSlider::valueChanged, panel, [provider, sizeValue](int value) {
            provider->setSize(sliderToSize(value));
            sizeValue->setText(QStringLiteral("%1 px").arg(qRound(sliderToSize(value))));
        });
        QHBoxLayout *sizeRow = new QHBoxLayout();
        sizeRow->addWidget(size, 1);
        sizeRow->addWidget(sizeValue);
        form->addRow(i18nc("brush setting", "Size"), sizeRow);

        QSlider *opacity = new QSlider(Qt::Horizontal, panel);
        opacity->setRange(1, 100);
        opacity->setValue(qRound(provider->opacity() * 100));
        QLabel *opacityValue = new QLabel(QStringLiteral("%1%").arg(opacity->value()), panel);
        connect(opacity, &QSlider::valueChanged, panel, [provider, opacityValue](int value) {
            provider->setOpacity(value / 100.0);
            opacityValue->setText(QStringLiteral("%1%").arg(value));
        });
        QHBoxLayout *opacityRow = new QHBoxLayout();
        opacityRow->addWidget(opacity, 1);
        opacityRow->addWidget(opacityValue);
        form->addRow(i18nc("brush setting", "Opacity"), opacityRow);

        showPopover(panel, brushSettings);
    });

    m_mainWindow->addToolBar(Qt::RightToolBarArea, m_rightBar);

    m_topBar->hide();
    m_rightBar->hide();
}

void GigaProcreateMode::showPopover(QWidget *content, QWidget *anchor)
{
    GigaPopover *popover = new GigaPopover(m_mainWindow);
    popover->setContent(content);
    if (anchor && m_rightBar && m_rightBar->isAncestorOf(anchor)) {
        popover->showLeftOf(anchor); // right-edge bar: open towards the canvas
    } else {
        popover->showBelow(anchor);
    }
}

void GigaProcreateMode::showBrushLibrary(QWidget *anchor)
{
    showPopover(new GigaBrushLibrary(m_mainWindow->viewManager()), anchor);
}

void GigaProcreateMode::connectResources()
{
    if (m_resourcesConnected || !m_mainWindow->viewManager()) return;
    KisCanvasResourceProvider *provider = m_mainWindow->viewManager()->canvasResourceProvider();
    if (!provider || !provider->resourceManager()) return;

    connect(provider->resourceManager(), &KoCanvasResourceProvider::canvasResourceChanged,
            this, [this](int key, const QVariant &) {
        if (key == KoCanvasResource::Size || key == KoCanvasResource::Opacity) updateSideBar();
    });
    connect(provider, &KisCanvasResourceProvider::sigOpacityChanged, this, &GigaProcreateMode::updateSideBar);
    connect(provider, &KisCanvasResourceProvider::sigFGColorChanged, this, &GigaProcreateMode::updateColorDot);
    m_resourcesConnected = true;
}

void GigaProcreateMode::updateSideBar()
{
    if (!m_sizeSlider || !m_mainWindow->viewManager()) return;
    KisCanvasResourceProvider *provider = m_mainWindow->viewManager()->canvasResourceProvider();
    m_updatingSliders = true;
    m_sizeSlider->setValue(sizeToSlider(provider->size()));
    m_opacitySlider->setValue(qRound(provider->opacity() * 100));
    m_updatingSliders = false;
}

void GigaProcreateMode::updateColorDot()
{
    if (!m_colorDot || !m_mainWindow->viewManager()) return;
    const QColor color = m_mainWindow->viewManager()->canvasResourceProvider()->fgColor().toQColor();
    QPixmap pixmap(26, 26);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(200, 200, 200), 1.5));
    p.setBrush(color);
    p.drawEllipse(QRectF(1.5, 1.5, 23, 23));
    m_colorDot->setIcon(QIcon(pixmap));
}

// --- Paint / Smudge / Erase slots ---------------------------------------
// Erase uses the separate eraser (erase_action swaps to the eraser brush).
// Smudge remembers its own preset the same way, in gigaBrushSmudge* keys.

static void savePresetSlot(KisMainWindow *mw, const QString &slot)
{
    KisCanvasResourceProvider *provider = mw->viewManager()->canvasResourceProvider();
    KisConfig cfg(false);
    if (KisPaintOpPresetSP preset = provider->currentPreset()) {
        cfg.writeEntry(QStringLiteral("gigaBrush%1Preset").arg(slot), preset->name());
    }
    cfg.writeEntry(QStringLiteral("gigaBrush%1Size").arg(slot), provider->size());
    cfg.writeEntry(QStringLiteral("gigaBrush%1Opacity").arg(slot), provider->opacity());
}

static void loadPresetSlot(KisMainWindow *mw, const QString &slot, const QString &fallbackNameContains = QString())
{
    KisCanvasResourceProvider *provider = mw->viewManager()->canvasResourceProvider();
    KisConfig cfg(true);
    QString name = cfg.readEntry(QStringLiteral("gigaBrush%1Preset").arg(slot), QString());
    if (name.isEmpty() && !fallbackNameContains.isEmpty()) {
        // First use: pick a bundled preset whose name matches (e.g. "smudge").
        KisResourceModel model(ResourceType::PaintOpPresets);
        for (int row = 0; row < model.rowCount(); row++) {
            const QString candidate = model.data(model.index(row, 0), Qt::UserRole + KisAbstractResourceModel::Name).toString();
            if (candidate.contains(fallbackNameContains, Qt::CaseInsensitive)) {
                name = candidate;
                break;
            }
        }
    }
    if (!name.isEmpty()) {
        KisPaintOpPresetSP preset = KisResourceServerProvider::instance()->paintOpPresetServer()->resource(QString(), QString(), name);
        if (preset && preset->valid() && preset != provider->currentPreset()) {
            provider->setPaintOpPreset(preset);
        }
    }
    const qreal size = cfg.readEntry<qreal>(QStringLiteral("gigaBrush%1Size").arg(slot), 0.0);
    if (size > 0.0) provider->setSize(size);
    const qreal opacity = cfg.readEntry<qreal>(QStringLiteral("gigaBrush%1Opacity").arg(slot), 0.0);
    if (opacity > 0.0) provider->setOpacity(opacity);
}

void GigaProcreateMode::brushClicked(QToolButton *button)
{
    if (!m_mainWindow->viewManager()) return;
    QAction *erase = m_mainWindow->actionCollection()->action(QStringLiteral("erase_action"));
    const bool brushToolActive = KoToolManager::instance()->activeToolId() == QLatin1String(TOOL_BRUSH);

    if (brushToolActive && s_slot == Slot::Paint && !(erase && erase->isChecked())) {
        // Already painting: tapping again opens the brush library.
        showBrushLibrary(button);
        return;
    }
    if (s_slot == Slot::Smudge) {
        savePresetSlot(m_mainWindow, QStringLiteral("Smudge"));
        loadPresetSlot(m_mainWindow, QStringLiteral("Paint"));
    }
    if (erase && erase->isChecked()) erase->setChecked(false); // restores the paint brush
    s_slot = Slot::Paint;
    activateTool(QString::fromLatin1(TOOL_BRUSH));
}

void GigaProcreateMode::smudgeClicked(QToolButton *button)
{
    if (!m_mainWindow->viewManager()) return;
    QAction *erase = m_mainWindow->actionCollection()->action(QStringLiteral("erase_action"));
    const bool brushToolActive = KoToolManager::instance()->activeToolId() == QLatin1String(TOOL_BRUSH);

    if (brushToolActive && s_slot == Slot::Smudge) {
        showBrushLibrary(button);
        return;
    }
    if (erase && erase->isChecked()) erase->setChecked(false); // back to the paint brush first
    savePresetSlot(m_mainWindow, QStringLiteral("Paint"));
    loadPresetSlot(m_mainWindow, QStringLiteral("Smudge"), QStringLiteral("smudge"));
    s_slot = Slot::Smudge;
    activateTool(QString::fromLatin1(TOOL_BRUSH));
}

void GigaProcreateMode::eraserClicked(QToolButton *button)
{
    if (!m_mainWindow->viewManager()) return;
    QAction *erase = m_mainWindow->actionCollection()->action(QStringLiteral("erase_action"));
    if (!erase) return;
    const bool brushToolActive = KoToolManager::instance()->activeToolId() == QLatin1String(TOOL_BRUSH);

    if (brushToolActive && erase->isChecked()) {
        showBrushLibrary(button);
        return;
    }
    if (s_slot == Slot::Smudge) {
        // Go back to the paint brush first, so the eraser swap saves the
        // paint brush (not the smudge brush) as the one to return to.
        savePresetSlot(m_mainWindow, QStringLiteral("Smudge"));
        loadPresetSlot(m_mainWindow, QStringLiteral("Paint"));
    }
    s_slot = Slot::Erase;
    activateTool(QString::fromLatin1(TOOL_BRUSH));
    erase->setChecked(true); // swaps to the eraser brush
}

void GigaProcreateMode::setActive(bool active)
{
    if (active == m_active) return;
    buildBars();
    connectResources();

    // Saving/restoring each mode's window layout is done by GigaInterfaceModes;
    // this only clears the stage and shows or hides the Procreate bars.
    if (active) {
        Q_FOREACH (QDockWidget *dock, m_mainWindow->dockWidgets()) {
            dock->hide();
        }
        Q_FOREACH (QToolBar *bar, m_mainWindow->findChildren<QToolBar *>()) {
            if (bar != m_topBar && bar != m_rightBar) bar->hide();
        }
        m_mainWindow->menuBar()->hide();
        m_mainWindow->statusBar()->hide();
        m_topBar->show();
        m_rightBar->show();
        updateSideBar();
        updateColorDot();
    } else {
        m_topBar->hide();
        m_rightBar->hide();
        m_mainWindow->menuBar()->show();
        m_mainWindow->statusBar()->show();
    }

    m_active = active;
}

void GigaProcreateMode::hideBars()
{
    if (m_topBar) m_topBar->hide();
    if (m_rightBar) m_rightBar->hide();
}

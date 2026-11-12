/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaProcreateMode.h"

#include <QAction>
#include <QDockWidget>
#include <QMenu>
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
const char *TOOL_SAMPLER = "KritaSelected/KisToolColorSampler";
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

GigaProcreateMode::GigaProcreateMode(KisMainWindow *mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
{
    m_toggle = new QAction(i18nc("@action:inmenu", "Procreate Mode"), this);
    m_toggle->setCheckable(true);
    m_toggle->setShortcut(QKeySequence(Qt::Key_F9)); // F12 is often user-bound (e.g. Show Canvas Only)
    m_toggle->setShortcutContext(Qt::WindowShortcut);
    m_toggle->setToolTip(i18nc("@info:tooltip", "Switch between the Studio layout and a minimal, Procreate-style layout (F9)"));
    connect(m_toggle, &QAction::toggled, this, &GigaProcreateMode::setActive);
    // Also on the window itself, so F9 works while the menu bar is hidden.
    m_mainWindow->addAction(m_toggle);

    m_studioState = QByteArray::fromBase64(KisConfig(true).readEntry<QByteArray>("gigaStudioState", QByteArray()));
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
    actions->addSeparator();
    actions->addAction(m_toggle);
    menuButton(i18nc("Procreate-style top bar", "Actions"), actions);

    QMenu *filters = qobject_cast<QMenu *>(m_mainWindow->guiFactory()->container(QStringLiteral("Filter"), m_mainWindow));
    if (filters) {
        menuButton(i18nc("Procreate-style top bar", "Adjustments"), filters);
    }

    QToolButton *selection = addButton(m_topBar, QString(), i18nc("Procreate-style top bar", "Selection"), QString());
    connect(selection, &QToolButton::clicked, this, [this]() { activateTool(QString::fromLatin1(TOOL_SELECT)); });
    QToolButton *transform = addButton(m_topBar, QString(), i18nc("Procreate-style top bar", "Transform"), QString());
    connect(transform, &QToolButton::clicked, this, [this]() { activateTool(QString::fromLatin1(TOOL_TRANSFORM)); });

    QWidget *spacer = new QWidget(m_topBar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_topBar->addWidget(spacer);

    QToolButton *brush = addButton(m_topBar, QStringLiteral("krita_tool_freehand"), QString(),
                                   i18nc("@info:tooltip", "Paint (tap again for the brush library)"));
    connect(brush, &QToolButton::clicked, this, [this, brush]() { brushClicked(brush); });
    QToolButton *smudge = addButton(m_topBar, QStringLiteral("colorsmudge"), QString(),
                                    i18nc("@info:tooltip", "Smudge (tap again for the brush library)"));
    connect(smudge, &QToolButton::clicked, this, [this, smudge]() { smudgeClicked(smudge); });
    QToolButton *eraser = addButton(m_topBar, QStringLiteral("draw-eraser"), QString(),
                                    i18nc("@info:tooltip", "Erase (tap again for the brush library)"));
    connect(eraser, &QToolButton::clicked, this, [this, eraser]() { eraserClicked(eraser); });
    QToolButton *layers = addButton(m_topBar, QStringLiteral("all-layers"), QString(), i18nc("@info:tooltip", "Layers"));
    connect(layers, &QToolButton::clicked, this, [this, layers]() {
        showPopover(new GigaLayersPanel(m_mainWindow->viewManager()), layers);
    });
    m_colorDot = addButton(m_topBar, QString(), QString(), i18nc("@info:tooltip", "Colors"));
    m_colorDot->setToolButtonStyle(Qt::ToolButtonIconOnly);
    connect(m_colorDot, &QToolButton::clicked, this, [this]() {
        showPopover(new GigaColorPanel(m_mainWindow->viewManager()), m_colorDot);
    });

    m_mainWindow->addToolBarBreak(Qt::TopToolBarArea);
    m_mainWindow->addToolBar(Qt::TopToolBarArea, m_topBar);

    // ---------------- Side bar: floating pill over the canvas ----------------
    QWidget *overlayHost = m_mainWindow->centralWidget();
    m_sideBar = new GigaSideBarPill(overlayHost);
    m_sizeSlider = m_sideBar->sizeSlider();
    m_opacitySlider = m_sideBar->opacitySlider();

    m_sizeSlider->setRange(0, SLIDER_STEPS);
    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_updatingSliders || !m_mainWindow->viewManager()) return;
        m_mainWindow->viewManager()->canvasResourceProvider()->setSize(sliderToSize(value));
    });
    m_opacitySlider->setRange(1, 100);
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_updatingSliders || !m_mainWindow->viewManager()) return;
        m_mainWindow->viewManager()->canvasResourceProvider()->setOpacity(value / 100.0);
    });
    connect(m_sideBar->samplerButton(), &QToolButton::clicked, this, [this]() { activateTool(QString::fromLatin1(TOOL_SAMPLER)); });
    connect(m_sideBar->undoButton(), &QToolButton::clicked, this, [this]() { triggerAction(QStringLiteral("edit_undo")); });
    connect(m_sideBar->redoButton(), &QToolButton::clicked, this, [this]() { triggerAction(QStringLiteral("edit_redo")); });

    m_topBar->hide();
    m_sideBar->hide();
}

void GigaProcreateMode::showPopover(QWidget *content, QWidget *anchor)
{
    GigaPopover *popover = new GigaPopover(m_mainWindow);
    popover->setContent(content);
    popover->showBelow(anchor);
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

    if (active) {
        // Remember the Studio layout, then clear the stage.
        m_studioState = m_mainWindow->saveState();
        KisConfig(false).writeEntry<QByteArray>("gigaStudioState", m_studioState.toBase64());

        Q_FOREACH (QDockWidget *dock, m_mainWindow->dockWidgets()) {
            dock->hide();
        }
        Q_FOREACH (QToolBar *bar, m_mainWindow->findChildren<QToolBar *>()) {
            if (bar != m_topBar) bar->hide();
        }
        m_mainWindow->menuBar()->hide();
        m_mainWindow->statusBar()->hide();
        m_topBar->show();
        m_sideBar->show();
        m_sideBar->reposition();
        updateSideBar();
        updateColorDot();
    } else {
        m_topBar->hide();
        m_sideBar->hide();
        if (!m_studioState.isEmpty()) {
            m_mainWindow->restoreState(m_studioState);
        }
        m_topBar->hide(); // the saved Studio state may not know about them
        m_sideBar->hide();
        m_mainWindow->menuBar()->show();
        m_mainWindow->statusBar()->show();
    }

    m_active = active;
    if (m_toggle->isChecked() != active) {
        m_toggle->setChecked(active);
    }
    KisConfig(false).writeEntry<bool>("gigaProcreateMode", active);
}

/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaNewDocumentDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <klocalizedstring.h>

#include <KoColor.h>
#include <KoColorModelStandardIds.h>
#include <KoColorSpaceRegistry.h>

#include "KisDocument.h"
#include "KisPart.h"
#include "kis_config.h"
#include "dialogs/KisDlgCreateNewDocument.h"
#include "kis_canvas_resource_provider.h"
#include "KisMainWindow.h"
#include "KisViewManager.h"

namespace {

struct Preset {
    QString name;
    int width;     // pixels
    int height;    // pixels
    double ppi;
};

struct PresetGroup {
    QString title;
    QList<Preset> presets;
};

QList<PresetGroup> presetGroups()
{
    return {
        {i18nc("new document preset group", "Paper (300 ppi)"), {
            {i18nc("paper size", "A4"), 2480, 3508, 300},
            {i18nc("paper size", "A3"), 3508, 4961, 300},
            {i18nc("paper size", "A5"), 1748, 2480, 300},
            {i18nc("paper size", "US Letter"), 2550, 3300, 300},
            {i18nc("paper size", "Tabloid"), 3300, 5100, 300},
        }},
        {i18nc("new document preset group", "Screen"), {
            {i18nc("screen size", "HD 1920 × 1080"), 1920, 1080, 72},
            {i18nc("screen size", "QHD 2560 × 1440"), 2560, 1440, 72},
            {i18nc("screen size", "4K UHD 3840 × 2160"), 3840, 2160, 72},
        }},
        {i18nc("new document preset group", "Social"), {
            {i18nc("social size", "Square post 1080 × 1080"), 1080, 1080, 72},
            {i18nc("social size", "Portrait post 1080 × 1350"), 1080, 1350, 72},
            {i18nc("social size", "Story 1080 × 1920"), 1080, 1920, 72},
        }},
        {i18nc("new document preset group", "Illustration"), {
            {i18nc("illustration size", "Sketch 2000 × 2000"), 2000, 2000, 150},
            {i18nc("illustration size", "Illustration 3000 × 4000"), 3000, 4000, 300},
            {i18nc("illustration size", "Comic page 3300 × 5100"), 3300, 5100, 600},
        }},
    };
}

enum Unit { Pixels = 0, Inches = 1, Centimeters = 2 };

double pixelsToUnit(double px, int unit, double ppi)
{
    if (unit == Inches) return px / ppi;
    if (unit == Centimeters) return px / ppi * 2.54;
    return px;
}

double unitToPixels(double value, int unit, double ppi)
{
    if (unit == Inches) return value * ppi;
    if (unit == Centimeters) return value / 2.54 * ppi;
    return value;
}

enum Background { White = 0, Black, Transparent, BackgroundColor };

const int PRESET_ROLE = Qt::UserRole + 1;

} // namespace

GigaNewDocumentDialog::GigaNewDocumentDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "New Document"));
    resize(760, 460);

    // --- Left: presets ---
    m_presets = new QListWidget(this);
    m_presets->setMinimumWidth(280);
    Q_FOREACH (const PresetGroup &group, presetGroups()) {
        QListWidgetItem *header = new QListWidgetItem(group.title, m_presets);
        QFont bold = header->font();
        bold.setBold(true);
        header->setFont(bold);
        header->setFlags(Qt::NoItemFlags); // section title, not selectable
        Q_FOREACH (const Preset &preset, group.presets) {
            QListWidgetItem *item = new QListWidgetItem(QStringLiteral("    ") + preset.name, m_presets);
            item->setData(PRESET_ROLE, QVariantList{preset.width, preset.height, preset.ppi});
        }
    }
    connect(m_presets, SIGNAL(itemSelectionChanged()), SLOT(slotPresetChosen()));

    // --- Right: details ---
    m_name = new QLineEdit(i18nc("default new document name", "Untitled"), this);

    m_width = new QDoubleSpinBox(this);
    m_height = new QDoubleSpinBox(this);
    for (QDoubleSpinBox *box : {m_width, m_height}) {
        box->setRange(1, 100000);
        box->setDecimals(0);
    }
    m_width->setValue(2480);
    m_height->setValue(3508);

    m_unit = new QComboBox(this);
    m_unit->addItems({i18nc("unit", "Pixels"), i18nc("unit", "Inches"), i18nc("unit", "Centimeters")});
    connect(m_unit, SIGNAL(currentIndexChanged(int)), SLOT(slotUnitChanged(int)));

    QToolButton *swap = new QToolButton(this);
    swap->setText(QStringLiteral("⇄"));
    swap->setToolTip(i18nc("@info:tooltip", "Swap width and height (portrait / landscape)"));
    connect(swap, SIGNAL(clicked()), SLOT(slotSwapOrientation()));

    QHBoxLayout *sizeRow = new QHBoxLayout();
    sizeRow->addWidget(m_width);
    sizeRow->addWidget(new QLabel(QStringLiteral("×"), this));
    sizeRow->addWidget(m_height);
    sizeRow->addWidget(m_unit);
    sizeRow->addWidget(swap);

    m_resolution = new QDoubleSpinBox(this);
    m_resolution->setRange(1, 10000);
    m_resolution->setDecimals(0);
    m_resolution->setSuffix(i18nc("pixels per inch", " ppi"));
    m_resolution->setValue(300);

    m_colorMode = new QComboBox(this);
    m_colorMode->addItems({i18nc("color mode", "RGB Color, 8 bit"),
                           i18nc("color mode", "RGB Color, 16 bit"),
                           i18nc("color mode", "CMYK Color, 8 bit"),
                           i18nc("color mode", "Grayscale, 8 bit")});

    m_background = new QComboBox(this);
    m_background->addItems({i18nc("background", "White"), i18nc("background", "Black"),
                            i18nc("background", "Transparent"), i18nc("background", "Background Color")});

    QFormLayout *form = new QFormLayout();
    form->addRow(i18nc("@label", "Name:"), m_name);
    form->addRow(i18nc("@label", "Size:"), sizeRow);
    form->addRow(i18nc("@label", "Resolution:"), m_resolution);
    form->addRow(i18nc("@label", "Color Mode:"), m_colorMode);
    form->addRow(i18nc("@label", "Background:"), m_background);

    QDialogButtonBox *buttons = new QDialogButtonBox(this);
    QPushButton *create = buttons->addButton(i18nc("@action:button", "Create"), QDialogButtonBox::AcceptRole);
    create->setDefault(true);
    buttons->addButton(QDialogButtonBox::Cancel);
    QPushButton *more = buttons->addButton(i18nc("@action:button", "More Options..."), QDialogButtonBox::ActionRole);
    more->setToolTip(i18nc("@info:tooltip", "Open the full New Document dialog (templates, colour profiles, layers)"));
    connect(create, SIGNAL(clicked()), SLOT(slotCreate()));
    connect(more, SIGNAL(clicked()), SLOT(slotMoreOptions()));
    connect(buttons, SIGNAL(rejected()), SLOT(reject()));

    QVBoxLayout *right = new QVBoxLayout();
    right->addLayout(form);
    right->addStretch(1);
    right->addWidget(buttons);

    QHBoxLayout *main = new QHBoxLayout(this);
    main->addWidget(m_presets, 1);
    main->addLayout(right, 1);

    // Start on A4, like a typical illustration setup.
    for (int i = 0; i < m_presets->count(); i++) {
        if (m_presets->item(i)->data(PRESET_ROLE).isValid()) {
            m_presets->setCurrentRow(i);
            break;
        }
    }
}

void GigaNewDocumentDialog::slotPresetChosen()
{
    QListWidgetItem *item = m_presets->currentItem();
    if (!item) return;
    const QVariantList data = item->data(PRESET_ROLE).toList();
    if (data.size() < 3) return;

    m_resolution->setValue(data[2].toDouble());
    m_unit->setCurrentIndex(Pixels);
    m_width->setValue(data[0].toInt());
    m_height->setValue(data[1].toInt());
    m_name->setText(item->text().trimmed());
}

void GigaNewDocumentDialog::slotUnitChanged(int index)
{
    // Convert the shown numbers so the size in pixels stays the same.
    const double ppi = m_resolution->value();
    const double wPx = unitToPixels(m_width->value(), m_currentUnit, ppi);
    const double hPx = unitToPixels(m_height->value(), m_currentUnit, ppi);
    m_currentUnit = index;
    const int decimals = (index == Pixels) ? 0 : 2;
    m_width->setDecimals(decimals);
    m_height->setDecimals(decimals);
    m_width->setValue(pixelsToUnit(wPx, index, ppi));
    m_height->setValue(pixelsToUnit(hPx, index, ppi));
}

void GigaNewDocumentDialog::slotSwapOrientation()
{
    const double w = m_width->value();
    m_width->setValue(m_height->value());
    m_height->setValue(w);
}

QSize GigaNewDocumentDialog::sizeInPixels() const
{
    const double ppi = m_resolution->value();
    return QSize(qMax(1, qRound(unitToPixels(m_width->value(), m_currentUnit, ppi))),
                 qMax(1, qRound(unitToPixels(m_height->value(), m_currentUnit, ppi))));
}

KisDocument *GigaNewDocumentDialog::createDocument()
{
    QString modelId = RGBAColorModelID.id();
    QString depthId = Integer8BitsColorDepthID.id();
    switch (m_colorMode->currentIndex()) {
    case 1: depthId = Integer16BitsColorDepthID.id(); break;
    case 2: modelId = CMYKAColorModelID.id(); break;
    case 3: modelId = GrayAColorModelID.id(); break;
    default: break;
    }
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->colorSpace(modelId, depthId, QString());
    if (!cs) {
        cs = KoColorSpaceRegistry::instance()->rgb8();
    }

    QColor background = Qt::white;
    switch (m_background->currentIndex()) {
    case Black: background = Qt::black; break;
    case Transparent: background = QColor(255, 255, 255, 0); break;
    case BackgroundColor: {
        KisMainWindow *mainWindow = qobject_cast<KisMainWindow *>(parentWidget());
        if (mainWindow && mainWindow->viewManager() && mainWindow->viewManager()->canvasResourceProvider()) {
            background = mainWindow->viewManager()->canvasResourceProvider()->bgColor().toQColor();
        }
        break;
    }
    default: break;
    }

    KisDocument *doc = static_cast<KisDocument *>(KisPart::instance()->createDocument());
    const QSize size = sizeInPixels();
    const double resolution = m_resolution->value() / 72.0; // Krita stores pixels per point
    KisConfig cfg(true);
    // Like Photoshop: the background is a real layer you can paint on.
    doc->newImage(m_name->text(), size.width(), size.height(), cs, KoColor(background, cs),
                  KisConfig::RASTER_LAYER, qMax(1, cfg.numDefaultLayers()), QString(), resolution);
    doc->setModified(false);
    return doc;
}

void GigaNewDocumentDialog::slotCreate()
{
    KisDocument *doc = createDocument();
    if (!doc) return;
    KisPart::instance()->startCustomDocument(doc);
    accept();
}

void GigaNewDocumentDialog::slotMoreOptions()
{
    QWidget *owner = parentWidget();
    reject();
    KisDlgCreateNewDocument *dlg = new KisDlgCreateNewDocument(owner);
    dlg->exec();
    dlg->deleteLater();
}

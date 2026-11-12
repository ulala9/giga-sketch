/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaTextOptionsBar.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDockWidget>
#include <QDoubleValidator>
#include <QLineEdit>
#include <QMainWindow>
#include <QPainter>
#include <QStyle>
#include <QToolButton>

#include <klocalizedstring.h>

#include <KoCanvasResourceProvider.h>
#include <KoColor.h>
#include <KoColorBackground.h>
#include <KoColorSpaceRegistry.h>
#include <KoFontFamily.h>
#include <KoSvgText.h>
#include <KoSvgTextPropertyData.h>
#include <KisResourceModel.h>
#include <KisResourceModelProvider.h>
#include <KisResourceTypes.h>
#include <kis_canvas_resource_provider.h>
#include <kis_icon_utils.h>

namespace {

// Font sizes offered in the size dropdown, in points (like Photoshop's list).
const QList<qreal> SIZE_PRESETS = {6, 8, 9, 10, 11, 12, 14, 18, 24, 30, 36, 48, 60, 72};

// Anti-aliasing menu entries -> Krita's text-rendering values.
// None = monochrome hinting (crisp, aliased), Sharp = hinted,
// Smooth = unhinted outlines.
const QList<int> ANTI_ALIAS_VALUES = {
    KoSvgText::RenderingAuto,
    KoSvgText::RenderingOptimizeSpeed,
    KoSvgText::RenderingOptimizeLegibility,
    KoSvgText::RenderingGeometricPrecision,
};

enum Alignment { AlignLeft, AlignCenter, AlignRight, AlignJustify };

QString styleLabel(const KoSvgText::FontFamilyStyleInfo &style)
{
    if (style.localizedLabels.isEmpty()) return QString();
    return style.localizedLabels.value(QLocale(QLocale::English), style.localizedLabels.values().first());
}

int styleWeight(const KoSvgText::FontFamilyStyleInfo &style)
{
    return qRound(style.instanceCoords.value(QStringLiteral("wght"), 400.0));
}

int styleWidth(const KoSvgText::FontFamilyStyleInfo &style)
{
    return qRound(style.instanceCoords.value(QStringLiteral("wdth"), 100.0));
}

QFont::Style styleSlant(const KoSvgText::FontFamilyStyleInfo &style)
{
    if (style.isItalic) return QFont::StyleItalic;
    if (style.isOblique) return QFont::StyleOblique;
    return QFont::StyleNormal;
}

QIcon colorSwatch(const QColor &color)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setPen(QColor(128, 128, 128));
    p.setBrush(color);
    p.drawRect(0, 0, 15, 15);
    return QIcon(pixmap);
}

KisAllResourcesModel *fontModel()
{
    return KisResourceModelProvider::resourceModel(ResourceType::FontFamilies);
}

} // namespace

GigaTextOptionsBar::GigaTextOptionsBar(QWidget *parent)
    : QToolBar(i18nc("@title:window", "Text Options"), parent)
{
    setObjectName(QStringLiteral("GigaTextOptionsBar"));
    setIconSize(QSize(16, 16));

    m_orientation = new QToolButton(this);
    m_orientation->setCheckable(true);
    m_orientation->setIcon(KisIconUtils::loadIcon("format-text-direction-vertical-rl"));
    m_orientation->setToolTip(i18nc("@info:tooltip", "Toggle vertical text"));
    connect(m_orientation, SIGNAL(clicked()), SLOT(slotOrientationClicked()));
    addWidget(m_orientation);
    addSeparator();

    // Font family: editable so you can type to search, like Photoshop.
    m_family = new QComboBox(this);
    m_family->setEditable(true);
    m_family->setInsertPolicy(QComboBox::NoInsert);
    m_family->setMinimumContentsLength(18);
    m_family->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_family->setToolTip(i18nc("@info:tooltip", "Font family"));
    m_family->completer()->setCompletionMode(QCompleter::PopupCompletion);
    m_family->completer()->setFilterMode(Qt::MatchContains);
    m_family->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    connect(m_family, SIGNAL(activated(int)), SLOT(slotFamilyChosen()));
    addWidget(m_family);

    m_style = new QComboBox(this);
    m_style->setMinimumContentsLength(10);
    m_style->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_style->setToolTip(i18nc("@info:tooltip", "Font style"));
    connect(m_style, SIGNAL(activated(int)), SLOT(slotStyleChosen(int)));
    addWidget(m_style);
    addSeparator();

    m_size = new QComboBox(this);
    m_size->setEditable(true);
    m_size->setInsertPolicy(QComboBox::NoInsert);
    m_size->setMinimumContentsLength(6);
    m_size->setToolTip(i18nc("@info:tooltip", "Font size (pt)"));
    Q_FOREACH (qreal size, SIZE_PRESETS) {
        m_size->addItem(i18nc("font size in points", "%1 pt", size), size);
    }
    m_size->lineEdit()->setValidator(nullptr);
    connect(m_size, SIGNAL(activated(int)), SLOT(slotSizeChosen()));
    connect(m_size->lineEdit(), SIGNAL(editingFinished()), SLOT(slotSizeChosen()));
    addWidget(m_size);

    m_antiAlias = new QComboBox(this);
    m_antiAlias->addItem(i18nc("text anti-aliasing", "Auto"));
    m_antiAlias->addItem(i18nc("text anti-aliasing", "None"));
    m_antiAlias->addItem(i18nc("text anti-aliasing", "Sharp"));
    m_antiAlias->addItem(i18nc("text anti-aliasing", "Smooth"));
    m_antiAlias->setToolTip(i18nc("@info:tooltip", "Anti-aliasing method"));
    connect(m_antiAlias, SIGNAL(activated(int)), SLOT(slotAntiAliasChosen(int)));
    addWidget(m_antiAlias);
    addSeparator();

    QButtonGroup *alignGroup = new QButtonGroup(this);
    alignGroup->setExclusive(true);
    auto makeAlignButton = [this, alignGroup](const char *icon, const QString &tip, int id) {
        QToolButton *button = new QToolButton(this);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setIcon(KisIconUtils::loadIcon(icon));
        button->setToolTip(tip);
        alignGroup->addButton(button, id);
        addWidget(button);
        return button;
    };
    m_alignLeft = makeAlignButton("format-justify-left", i18nc("@info:tooltip", "Align left"), AlignLeft);
    m_alignCenter = makeAlignButton("format-justify-center", i18nc("@info:tooltip", "Align center"), AlignCenter);
    m_alignRight = makeAlignButton("format-justify-right", i18nc("@info:tooltip", "Align right"), AlignRight);
    m_alignJustify = makeAlignButton("format-justify-fill", i18nc("@info:tooltip", "Justify (last line aligned left)"), AlignJustify);
    connect(alignGroup, SIGNAL(idClicked(int)), SLOT(slotAlignmentClicked(int)));
    addSeparator();

    m_color = new QToolButton(this);
    m_color->setAutoRaise(true);
    m_color->setToolTip(i18nc("@info:tooltip", "Text color"));
    connect(m_color, SIGNAL(clicked()), SLOT(slotColorClicked()));
    addWidget(m_color);

    m_panels = new QToolButton(this);
    m_panels->setAutoRaise(true);
    m_panels->setIcon(KisIconUtils::loadIcon("format-text-color"));
    m_panels->setText(i18nc("@action:button", "Character & Paragraph"));
    m_panels->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_panels->setToolTip(i18nc("@info:tooltip", "Show the Character and Paragraph panels"));
    connect(m_panels, SIGNAL(clicked()), SLOT(slotOpenPanels()));
    addWidget(m_panels);
    addSeparator();

    m_commit = new QToolButton(this);
    m_commit->setAutoRaise(true);
    m_commit->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    m_commit->setToolTip(i18nc("@info:tooltip", "Commit: finish editing this text"));
    connect(m_commit, SIGNAL(clicked()), SIGNAL(commitRequested()));
    addWidget(m_commit);
}

GigaTextOptionsBar::~GigaTextOptionsBar()
{
}

void GigaTextOptionsBar::setResourceProviders(KisCanvasResourceProvider *provider, KoCanvasResourceProvider *canvasResources)
{
    m_connections.clear();
    m_provider = provider;
    m_canvasResources = canvasResources;
    if (m_provider) {
        m_connections.addConnection(m_provider, SIGNAL(sigTextPropertiesChanged()), this, SLOT(updateFromProperties()));
        m_connections.addConnection(m_provider, SIGNAL(sigCharacterPropertiesChanged()), this, SLOT(updateFromProperties()));
    }
    if (m_canvasResources) {
        // Foreground colour = colour of new text; keep the swatch in sync.
        m_connections.addConnection(m_canvasResources, SIGNAL(canvasResourceChanged(int,QVariant)), this, SLOT(updateFromProperties()));
    }
    updateFromProperties();
}

bool GigaTextOptionsBar::isCharacterSelection() const
{
    if (!m_provider) return false;
    const KoSvgTextPropertyData chr = m_provider->characterTextPropertyData();
    return chr.enabled && chr.spanSelection;
}

KoSvgTextProperties GigaTextOptionsBar::currentParagraphProperties() const
{
    if (!m_provider) return KoSvgTextProperties::defaultProperties();
    KoSvgTextProperties props = m_provider->textPropertyData().commonProperties;
    props.inheritFrom(KoSvgTextProperties::defaultProperties(), true);
    return props;
}

KoSvgTextProperties GigaTextOptionsBar::currentCharacterProperties() const
{
    if (!m_provider) return KoSvgTextProperties::defaultProperties();
    if (!isCharacterSelection()) return currentParagraphProperties();
    const KoSvgTextPropertyData chr = m_provider->characterTextPropertyData();
    KoSvgTextProperties props = chr.commonProperties;
    props.inheritFrom(chr.inheritedProperties, true);
    props.inheritFrom(KoSvgTextProperties::defaultProperties(), true);
    return props;
}

void GigaTextOptionsBar::applyCharacter(const QSet<KoSvgTextProperties::PropertyId> &ids,
                                        const std::function<void(KoSvgTextProperties &)> &change)
{
    if (!m_provider) return;
    // A selected range gets the change; otherwise the whole text does (or,
    // with no text, the next new text). Photoshop behaves the same way.
    if (isCharacterSelection()) {
        KoSvgTextPropertyData data = m_provider->characterTextPropertyData();
        change(data.commonProperties);
        Q_FOREACH (KoSvgTextProperties::PropertyId id, ids) data.tristate.remove(id);
        m_provider->setCharacterPropertyData(data);
    } else {
        applyParagraph(ids, change);
    }
}

void GigaTextOptionsBar::applyParagraph(const QSet<KoSvgTextProperties::PropertyId> &ids,
                                        const std::function<void(KoSvgTextProperties &)> &change)
{
    if (!m_provider) return;
    KoSvgTextPropertyData data = m_provider->textPropertyData();
    change(data.commonProperties);
    Q_FOREACH (KoSvgTextProperties::PropertyId id, ids) data.tristate.remove(id);
    m_provider->setTextPropertyData(data);
}

void GigaTextOptionsBar::fillFamilies()
{
    if (m_familiesFilled) return;
    KisAllResourcesModel *model = fontModel();
    if (!model) return;

    QStringList names;
    for (int row = 0; row < model->rowCount(); row++) {
        const QModelIndex idx = model->index(row, 0);
        const QString name = model->data(idx, Qt::UserRole + KisAbstractResourceModel::Name).toString();
        if (!name.isEmpty()) names << name;
    }
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);

    m_family->blockSignals(true);
    m_family->addItems(names);
    m_family->blockSignals(false);
    m_familiesFilled = true;
}

void GigaTextOptionsBar::fillStyles(const QString &family)
{
    m_style->blockSignals(true);
    m_style->clear();
    KisAllResourcesModel *model = fontModel();
    if (model && !family.isEmpty()) {
        const QVector<KoResourceSP> found = model->resourcesForName(family);
        KoFontFamilySP fontFamily = found.isEmpty() ? KoFontFamilySP() : found.first().dynamicCast<KoFontFamily>();
        if (fontFamily) {
            Q_FOREACH (const KoSvgText::FontFamilyStyleInfo &style, fontFamily->styles()) {
                const QString label = styleLabel(style);
                if (label.isEmpty()) continue;
                // Store weight, width and slant so choosing the entry is cheap.
                m_style->addItem(label, QVariantList{styleWeight(style), styleWidth(style), int(styleSlant(style))});
            }
        }
    }
    if (m_style->count() == 0) {
        // Font without named styles: offer the basic four.
        m_style->addItem(i18nc("font style", "Regular"), QVariantList{400, 100, int(QFont::StyleNormal)});
        m_style->addItem(i18nc("font style", "Italic"), QVariantList{400, 100, int(QFont::StyleItalic)});
        m_style->addItem(i18nc("font style", "Bold"), QVariantList{700, 100, int(QFont::StyleNormal)});
        m_style->addItem(i18nc("font style", "Bold Italic"), QVariantList{700, 100, int(QFont::StyleItalic)});
    }
    m_style->blockSignals(false);
}

void GigaTextOptionsBar::updateFromProperties()
{
    if (m_updating || !isVisible()) return;
    m_updating = true;

    fillFamilies();

    const KoSvgTextPropertyData charData = isCharacterSelection()
        ? m_provider->characterTextPropertyData()
        : (m_provider ? m_provider->textPropertyData() : KoSvgTextPropertyData());
    const KoSvgTextProperties chr = currentCharacterProperties();
    const KoSvgTextProperties para = currentParagraphProperties();
    auto mixed = [&charData](KoSvgTextProperties::PropertyId id) { return charData.tristate.contains(id); };

    // Font family (blank when the selection mixes fonts, like Photoshop).
    const QStringList families = chr.propertyOrDefault(KoSvgTextProperties::FontFamiliesId).toStringList();
    const QString family = families.isEmpty() ? QString() : families.first();
    m_family->setEditText(mixed(KoSvgTextProperties::FontFamiliesId) ? QString() : family);

    // Font style: pick the entry matching weight + slant.
    fillStyles(family);
    const int weight = chr.propertyOrDefault(KoSvgTextProperties::FontWeightId).toInt();
    const QFont::Style slant = chr.propertyOrDefault(KoSvgTextProperties::FontStyleId).value<KoSvgText::CssFontStyleData>().style;
    int bestStyle = -1;
    int bestDistance = INT_MAX;
    for (int i = 0; i < m_style->count(); i++) {
        const QVariantList data = m_style->itemData(i).toList();
        if (data.size() < 3) continue;
        const int distance = qAbs(data[0].toInt() - weight) + ((data[2].toInt() == int(slant)) ? 0 : 1000);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestStyle = i;
        }
    }
    const bool styleMixed = mixed(KoSvgTextProperties::FontWeightId) || mixed(KoSvgTextProperties::FontStyleId);
    m_style->setCurrentIndex(styleMixed ? -1 : bestStyle);

    // Size in points.
    if (mixed(KoSvgTextProperties::FontSizeId)) {
        m_size->setEditText(QString());
    } else {
        const qreal size = chr.fontSize().value;
        m_size->setEditText(i18nc("font size in points", "%1 pt", QString::number(size, 'f', size == qRound(size) ? 0 : 1)));
    }

    // Anti-aliasing.
    m_antiAlias->setCurrentIndex(qMax(0, ANTI_ALIAS_VALUES.indexOf(para.propertyOrDefault(KoSvgTextProperties::TextRenderingId).toInt())));

    // Alignment, mirroring the docker's logic: justify via text-align,
    // otherwise left/center/right via text-anchor.
    const int anchor = para.propertyOrDefault(KoSvgTextProperties::TextAnchorId).toInt();
    const int alignAll = para.propertyOrDefault(KoSvgTextProperties::TextAlignAllId).toInt();
    if (alignAll == KoSvgText::AlignJustify) {
        m_alignJustify->setChecked(true);
    } else if (anchor == KoSvgText::AnchorMiddle) {
        m_alignCenter->setChecked(true);
    } else if (anchor == KoSvgText::AnchorEnd) {
        m_alignRight->setChecked(true);
    } else {
        m_alignLeft->setChecked(true);
    }

    m_orientation->setChecked(para.propertyOrDefault(KoSvgTextProperties::WritingModeId).toInt() != KoSvgText::HorizontalTB);

    // Colour: the text's own fill if it has one, else the foreground colour.
    QColor color = Qt::black;
    if (m_canvasResources) {
        color = m_canvasResources->foregroundColor().toQColor();
    }
    if (chr.hasProperty(KoSvgTextProperties::FillId)) {
        KoColorBackground *bg = dynamic_cast<KoColorBackground *>(chr.background().data());
        if (bg) color = bg->color();
    }
    m_color->setIcon(colorSwatch(color));

    m_updating = false;
}

void GigaTextOptionsBar::slotFamilyChosen()
{
    if (m_updating) return;
    const QString family = m_family->currentText().trimmed();
    if (family.isEmpty() || m_family->findText(family, Qt::MatchFixedString) < 0) return;
    applyCharacter({KoSvgTextProperties::FontFamiliesId}, [family](KoSvgTextProperties &p) {
        p.setProperty(KoSvgTextProperties::FontFamiliesId, QStringList{family});
    });
}

void GigaTextOptionsBar::slotStyleChosen(int index)
{
    if (m_updating || index < 0) return;
    const QVariantList data = m_style->itemData(index).toList();
    if (data.size() < 3) return;
    const int weight = data[0].toInt();
    const int width = data[1].toInt();
    const QFont::Style slant = QFont::Style(data[2].toInt());
    applyCharacter({KoSvgTextProperties::FontWeightId, KoSvgTextProperties::FontStretchId, KoSvgTextProperties::FontStyleId},
                   [=](KoSvgTextProperties &p) {
        p.setProperty(KoSvgTextProperties::FontWeightId, weight);
        p.setProperty(KoSvgTextProperties::FontStretchId, width);
        p.setProperty(KoSvgTextProperties::FontStyleId, QVariant::fromValue(KoSvgText::CssFontStyleData(slant)));
    });
}

void GigaTextOptionsBar::slotSizeChosen()
{
    if (m_updating) return;
    // Accept "12", "12pt", "12 pt", "12,5".
    QString text = m_size->currentText();
    text.remove(QStringLiteral("pt")).replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const qreal size = text.trimmed().toDouble(&ok);
    if (!ok || size <= 0.0) return;
    if (qFuzzyCompare(size, currentCharacterProperties().fontSize().value)) return;
    applyCharacter({KoSvgTextProperties::FontSizeId}, [size](KoSvgTextProperties &p) {
        p.setFontSize(KoSvgText::CssLengthPercentage(size));
    });
}

void GigaTextOptionsBar::slotAntiAliasChosen(int index)
{
    if (m_updating || index < 0 || index >= ANTI_ALIAS_VALUES.size()) return;
    const int value = ANTI_ALIAS_VALUES[index];
    applyParagraph({KoSvgTextProperties::TextRenderingId}, [value](KoSvgTextProperties &p) {
        p.setProperty(KoSvgTextProperties::TextRenderingId, value);
    });
}

void GigaTextOptionsBar::slotAlignmentClicked(int alignment)
{
    if (m_updating) return;
    const QSet<KoSvgTextProperties::PropertyId> ids = {KoSvgTextProperties::TextAnchorId,
                                                       KoSvgTextProperties::TextAlignAllId,
                                                       KoSvgTextProperties::TextAlignLastId};
    applyParagraph(ids, [alignment](KoSvgTextProperties &p) {
        // Point text aligns with text-anchor, wrapped text with text-align;
        // set both so the button works for either kind.
        switch (alignment) {
        case AlignCenter:
            p.setProperty(KoSvgTextProperties::TextAnchorId, KoSvgText::AnchorMiddle);
            p.setProperty(KoSvgTextProperties::TextAlignAllId, KoSvgText::AlignCenter);
            p.removeProperty(KoSvgTextProperties::TextAlignLastId);
            break;
        case AlignRight:
            p.setProperty(KoSvgTextProperties::TextAnchorId, KoSvgText::AnchorEnd);
            p.setProperty(KoSvgTextProperties::TextAlignAllId, KoSvgText::AlignEnd);
            p.removeProperty(KoSvgTextProperties::TextAlignLastId);
            break;
        case AlignJustify:
            p.setProperty(KoSvgTextProperties::TextAnchorId, KoSvgText::AnchorStart);
            p.setProperty(KoSvgTextProperties::TextAlignAllId, KoSvgText::AlignJustify);
            p.setProperty(KoSvgTextProperties::TextAlignLastId, KoSvgText::AlignStart);
            break;
        case AlignLeft:
        default:
            p.setProperty(KoSvgTextProperties::TextAnchorId, KoSvgText::AnchorStart);
            p.setProperty(KoSvgTextProperties::TextAlignAllId, KoSvgText::AlignStart);
            p.removeProperty(KoSvgTextProperties::TextAlignLastId);
            break;
        }
    });
}

void GigaTextOptionsBar::slotOrientationClicked()
{
    if (m_updating) return;
    const bool vertical = m_orientation->isChecked();
    applyParagraph({KoSvgTextProperties::WritingModeId}, [vertical](KoSvgTextProperties &p) {
        p.setProperty(KoSvgTextProperties::WritingModeId, vertical ? KoSvgText::VerticalRL : KoSvgText::HorizontalTB);
    });
}

void GigaTextOptionsBar::slotColorClicked()
{
    if (!m_canvasResources) return;
    const QColor current = m_canvasResources->foregroundColor().toQColor();
    const QColor chosen = QColorDialog::getColor(current, this, i18nc("@title:window", "Text Color"));
    if (!chosen.isValid()) return;
    // Setting the foreground colour is how Krita colours text: the text
    // tool applies it to the selected text, and new text uses it.
    m_canvasResources->setForegroundColor(KoColor(chosen, KoColorSpaceRegistry::instance()->rgb8()));
}

void GigaTextOptionsBar::slotOpenPanels()
{
    QMainWindow *mainWindow = qobject_cast<QMainWindow *>(window());
    if (!mainWindow) return;
    QDockWidget *dock = mainWindow->findChild<QDockWidget *>(QStringLiteral("TextProperties"));
    if (!dock) return;
    dock->show();
    dock->raise();
}

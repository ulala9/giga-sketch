/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaCharacterPanel.h"

#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <klocalizedstring.h>

#include <KoSvgText.h>

#include "GigaFontPicker.h"
#include "GigaTextPropertyAccess.h"

using Id = KoSvgTextProperties::PropertyId;

namespace {

const QList<qreal> SIZE_PRESETS = {6, 8, 9, 10, 11, 12, 14, 18, 24, 30, 36, 48, 60, 72};
const QList<qreal> TRACKING_PRESETS = {-100, -75, -50, -25, -10, -5, 0, 5, 10, 25, 50, 75, 100, 200};
const QList<int> ANTI_ALIAS_VALUES = {
    KoSvgText::RenderingAuto,
    KoSvgText::RenderingOptimizeSpeed,
    KoSvgText::RenderingOptimizeLegibility,
    KoSvgText::RenderingGeometricPrecision,
};

QString pt(qreal v)
{
    return i18nc("length in points", "%1 pt", QString::number(v, 'f', qFuzzyCompare(v, qRound(v)) ? 0 : 1));
}

QIcon swatch(const QColor &color)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setPen(QColor(128, 128, 128));
    p.setBrush(color);
    p.drawRect(0, 0, 15, 15);
    return QIcon(pixmap);
}

QLabel *smallLabel(const QString &text, const QString &tip, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setToolTip(tip);
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::Disabled, QPalette::WindowText));
    label->setPalette(pal);
    return label;
}

QComboBox *editableCombo(QWidget *parent)
{
    QComboBox *combo = new QComboBox(parent);
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setMinimumContentsLength(5);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    return combo;
}

} // namespace

bool gigaParseNumber(QString text, qreal &value)
{
    text.remove(QStringLiteral("pt")).replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    value = text.trimmed().toDouble(&ok);
    return ok;
}

GigaCharacterPanel::GigaCharacterPanel(GigaTextPropertyAccess *access, QWidget *parent)
    : QWidget(parent)
    , m_access(access)
{
    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setSpacing(4);

    // Font family (opens the font picker) and style.
    m_family = new GigaFontComboBox(this);
    m_family->setEditable(true);
    m_family->setInsertPolicy(QComboBox::NoInsert);
    m_family->setToolTip(i18nc("@info:tooltip", "Font family"));
    m_family->completer()->setCompletionMode(QCompleter::PopupCompletion);
    m_family->completer()->setFilterMode(Qt::MatchContains);
    m_family->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    outer->addWidget(m_family);
    m_picker = new GigaFontPicker(this);
    connect(m_family, &GigaFontComboBox::pickerRequested, this, [this]() {
        fillFamilies();
        m_access->beginFontPreview();
        m_picker->popup(m_family, m_families, m_family->currentText());
    });
    connect(m_family, QOverload<int>::of(&QComboBox::activated), this, [this]() {
        if (m_updating) return;
        const QString family = m_family->currentText().trimmed();
        if (!m_families.contains(family)) return;
        m_access->setFamily(family);
        GigaFontPicker::addRecent(family);
    });
    connect(m_picker, &GigaFontPicker::previewRequested, m_access, &GigaTextPropertyAccess::previewFamily);
    connect(m_picker, &GigaFontPicker::chosen, m_access, &GigaTextPropertyAccess::setFamily);
    connect(m_picker, &GigaFontPicker::previewCancelled, m_access, &GigaTextPropertyAccess::cancelFontPreview);

    m_style = new QComboBox(this);
    m_style->setToolTip(i18nc("@info:tooltip", "Font style"));
    outer->addWidget(m_style);
    connect(m_style, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (m_updating || index < 0) return;
        const QVariantList d = m_style->itemData(index).toList();
        if (d.size() < 3) return;
        const int weight = d[0].toInt(), width = d[1].toInt();
        const QFont::Style slant = QFont::Style(d[2].toInt());
        m_access->applyCharacter({Id::FontWeightId, Id::FontStretchId, Id::FontStyleId}, [=](KoSvgTextProperties &p) {
            p.setProperty(Id::FontWeightId, weight);
            p.setProperty(Id::FontStretchId, width);
            p.setProperty(Id::FontStyleId, QVariant::fromValue(KoSvgText::CssFontStyleData(slant)));
        });
    });

    // Two columns of numeric fields, like Photoshop.
    QGridLayout *grid = new QGridLayout();
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);
    outer->addLayout(grid);

    m_size = editableCombo(this);
    Q_FOREACH (qreal v, SIZE_PRESETS) m_size->addItem(pt(v));
    m_leading = editableCombo(this);
    m_leading->addItem(i18nc("leading", "Auto"));
    Q_FOREACH (qreal v, SIZE_PRESETS) m_leading->addItem(pt(v));
    m_kerning = new QComboBox(this);
    m_kerning->addItem(i18nc("kerning", "Metrics"));
    m_kerning->addItem(QStringLiteral("0"));
    m_tracking = editableCombo(this);
    Q_FOREACH (qreal v, TRACKING_PRESETS) m_tracking->addItem(QString::number(v));
    m_baseline = new QDoubleSpinBox(this);
    m_baseline->setRange(-1000, 1000);
    m_baseline->setDecimals(1);
    m_baseline->setSuffix(i18nc("unit suffix", " pt"));
    m_baseline->setKeyboardTracking(false);
    m_color = new QToolButton(this);
    m_color->setAutoRaise(true);
    m_color->setToolTip(i18nc("@info:tooltip", "Text color"));

    grid->addWidget(smallLabel(i18nc("character panel", "Size"), i18nc("@info:tooltip", "Font size"), this), 0, 0);
    grid->addWidget(m_size, 0, 1);
    grid->addWidget(smallLabel(i18nc("character panel", "Leading"), i18nc("@info:tooltip", "Distance between lines"), this), 0, 2);
    grid->addWidget(m_leading, 0, 3);
    grid->addWidget(smallLabel(i18nc("character panel", "Kerning"), i18nc("@info:tooltip", "Metrics: use the font's own kerning. 0: no kerning."), this), 1, 0);
    grid->addWidget(m_kerning, 1, 1);
    grid->addWidget(smallLabel(i18nc("character panel", "Tracking"), i18nc("@info:tooltip", "Letter spacing, in thousandths of an em"), this), 1, 2);
    grid->addWidget(m_tracking, 1, 3);
    grid->addWidget(smallLabel(i18nc("character panel", "Baseline"), i18nc("@info:tooltip", "Baseline shift: move characters up or down"), this), 2, 0);
    grid->addWidget(m_baseline, 2, 1);
    grid->addWidget(smallLabel(i18nc("character panel", "Color"), i18nc("@info:tooltip", "Text color"), this), 2, 2);
    grid->addWidget(m_color, 2, 3, Qt::AlignLeft);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);

    connect(m_size, QOverload<int>::of(&QComboBox::activated), this, [this]() { applySize(); });
    connect(m_size->lineEdit(), &QLineEdit::editingFinished, this, [this]() { applySize(); });
    connect(m_leading, QOverload<int>::of(&QComboBox::activated), this, [this]() { applyLeading(); });
    connect(m_leading->lineEdit(), &QLineEdit::editingFinished, this, [this]() { applyLeading(); });
    connect(m_tracking, QOverload<int>::of(&QComboBox::activated), this, [this]() { applyTracking(); });
    connect(m_tracking->lineEdit(), &QLineEdit::editingFinished, this, [this]() { applyTracking(); });
    connect(m_baseline, &QDoubleSpinBox::valueChanged, this, [this]() { applyBaseline(); });
    connect(m_kerning, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (m_updating) return;
        m_access->applyCharacter({Id::KerningId}, [index](KoSvgTextProperties &p) {
            // Metrics = the font's kerning ("auto"); 0 = none.
            p.setProperty(Id::KerningId, QVariant::fromValue(index == 0 ? KoSvgText::AutoValue() : KoSvgText::AutoValue(0.0)));
        });
    });
    connect(m_color, &QToolButton::clicked, this, [this]() {
        const QColor chosen = QColorDialog::getColor(m_access->textColor(), this, i18nc("@title:window", "Text Color"));
        m_access->setForegroundColor(chosen);
    });

    // Style toggles, drawn as "T" glyphs like Photoshop's.
    QHBoxLayout *toggles = new QHBoxLayout();
    toggles->setSpacing(1);
    outer->addLayout(toggles);
    auto makeToggle = [this, toggles](const QString &text, const QString &tip, std::function<void(QFont &)> styleFont) {
        QToolButton *b = new QToolButton(this);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setText(text);
        b->setToolTip(tip);
        QFont f = b->font();
        styleFont(f);
        b->setFont(f);
        b->setFixedSize(26, 24);
        toggles->addWidget(b);
        return b;
    };
    m_bold = makeToggle(QStringLiteral("T"), i18nc("@info:tooltip", "Faux bold"), [](QFont &f) { f.setBold(true); });
    m_italic = makeToggle(QStringLiteral("T"), i18nc("@info:tooltip", "Italic"), [](QFont &f) { f.setItalic(true); });
    m_allCaps = makeToggle(QStringLiteral("TT"), i18nc("@info:tooltip", "All caps"), [](QFont &) {});
    m_smallCaps = makeToggle(QStringLiteral("Tᴛ"), i18nc("@info:tooltip", "Small caps"), [](QFont &) {});
    m_super = makeToggle(QStringLiteral("T¹"), i18nc("@info:tooltip", "Superscript"), [](QFont &) {});
    m_sub = makeToggle(QStringLiteral("T₁"), i18nc("@info:tooltip", "Subscript"), [](QFont &) {});
    m_underline = makeToggle(QStringLiteral("T"), i18nc("@info:tooltip", "Underline"), [](QFont &f) { f.setUnderline(true); });
    m_strike = makeToggle(QStringLiteral("T"), i18nc("@info:tooltip", "Strikethrough"), [](QFont &f) { f.setStrikeOut(true); });
    toggles->addStretch(1);

    connect(m_bold, &QToolButton::clicked, this, [this](bool on) {
        m_access->applyCharacter({Id::FontWeightId}, [on](KoSvgTextProperties &p) { p.setProperty(Id::FontWeightId, on ? 700 : 400); });
    });
    connect(m_italic, &QToolButton::clicked, this, [this](bool on) {
        m_access->applyCharacter({Id::FontStyleId}, [on](KoSvgTextProperties &p) {
            p.setProperty(Id::FontStyleId, QVariant::fromValue(KoSvgText::CssFontStyleData(on ? QFont::StyleItalic : QFont::StyleNormal)));
        });
    });
    // All caps and small caps exclude each other, as do super and subscript.
    connect(m_allCaps, &QToolButton::clicked, this, [this](bool on) {
        m_access->applyCharacter({Id::TextTransformId, Id::FontVariantCapsId}, [on](KoSvgTextProperties &p) {
            KoSvgText::TextTransformInfo t = p.propertyOrDefault(Id::TextTransformId).value<KoSvgText::TextTransformInfo>();
            t.capitals = on ? KoSvgText::TextTransformUppercase : KoSvgText::TextTransformNone;
            p.setProperty(Id::TextTransformId, QVariant::fromValue(t));
            if (on) p.setProperty(Id::FontVariantCapsId, int(KoSvgText::CapsNormal));
        });
    });
    connect(m_smallCaps, &QToolButton::clicked, this, [this](bool on) {
        m_access->applyCharacter({Id::FontVariantCapsId, Id::TextTransformId}, [on](KoSvgTextProperties &p) {
            p.setProperty(Id::FontVariantCapsId, int(on ? KoSvgText::CapsSmall : KoSvgText::CapsNormal));
            if (on) {
                KoSvgText::TextTransformInfo t = p.propertyOrDefault(Id::TextTransformId).value<KoSvgText::TextTransformInfo>();
                t.capitals = KoSvgText::TextTransformNone;
                p.setProperty(Id::TextTransformId, QVariant::fromValue(t));
            }
        });
    });
    connect(m_super, &QToolButton::clicked, this, [this](bool on) {
        m_access->applyCharacter({Id::FontVariantPositionId}, [on](KoSvgTextProperties &p) {
            p.setProperty(Id::FontVariantPositionId, int(on ? KoSvgText::PositionSuper : KoSvgText::PositionNormal));
        });
    });
    connect(m_sub, &QToolButton::clicked, this, [this](bool on) {
        m_access->applyCharacter({Id::FontVariantPositionId}, [on](KoSvgTextProperties &p) {
            p.setProperty(Id::FontVariantPositionId, int(on ? KoSvgText::PositionSub : KoSvgText::PositionNormal));
        });
    });
    auto decoration = [this](KoSvgText::TextDecoration flag, bool on) {
        m_access->applyCharacter({Id::TextDecorationLineId}, [flag, on](KoSvgTextProperties &p) {
            KoSvgText::TextDecorations lines = p.propertyOrDefault(Id::TextDecorationLineId).value<KoSvgText::TextDecorations>();
            lines.setFlag(flag, on);
            p.setProperty(Id::TextDecorationLineId, QVariant::fromValue(lines));
        });
    };
    connect(m_underline, &QToolButton::clicked, this, [decoration](bool on) { decoration(KoSvgText::DecorationUnderline, on); });
    connect(m_strike, &QToolButton::clicked, this, [decoration](bool on) { decoration(KoSvgText::DecorationLineThrough, on); });

    // Anti-aliasing (applies to the whole text).
    QHBoxLayout *aaRow = new QHBoxLayout();
    outer->addLayout(aaRow);
    aaRow->addWidget(smallLabel(i18nc("character panel", "Anti-aliasing"), i18nc("@info:tooltip", "Anti-aliasing method"), this));
    m_antiAlias = new QComboBox(this);
    m_antiAlias->addItem(i18nc("text anti-aliasing", "Auto"));
    m_antiAlias->addItem(i18nc("text anti-aliasing", "None"));
    m_antiAlias->addItem(i18nc("text anti-aliasing", "Sharp"));
    m_antiAlias->addItem(i18nc("text anti-aliasing", "Smooth"));
    aaRow->addWidget(m_antiAlias, 1);
    connect(m_antiAlias, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (m_updating || index < 0 || index >= ANTI_ALIAS_VALUES.size()) return;
        const int value = ANTI_ALIAS_VALUES[index];
        m_access->applyParagraph({Id::TextRenderingId}, [value](KoSvgTextProperties &p) { p.setProperty(Id::TextRenderingId, value); });
    });

    outer->addStretch(1);

    connect(m_access, &GigaTextPropertyAccess::changed, this, &GigaCharacterPanel::updateFromProperties);
}

QWidget *GigaCharacterPanel::field(const QString &name) const
{
    const QHash<QString, QWidget *> fields = {
        {"family", m_family}, {"style", m_style}, {"size", m_size}, {"leading", m_leading},
        {"kerning", m_kerning}, {"tracking", m_tracking}, {"baseline", m_baseline}, {"bold", m_bold},
        {"italic", m_italic}, {"allCaps", m_allCaps}, {"smallCaps", m_smallCaps}, {"super", m_super},
        {"sub", m_sub}, {"underline", m_underline}, {"strike", m_strike}, {"antiAlias", m_antiAlias},
    };
    return fields.value(name);
}

void GigaCharacterPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateFromProperties();
}

void GigaCharacterPanel::fillFamilies()
{
    if (!m_families.isEmpty()) return;
    m_families = GigaTextPropertyAccess::fontFamilies();
    const bool was = m_family->blockSignals(true);
    const QString text = m_family->currentText();
    m_family->addItems(m_families);
    m_family->setEditText(text);
    m_family->blockSignals(was);
}

void GigaCharacterPanel::fillStyles(const QString &family)
{
    const bool was = m_style->blockSignals(true);
    m_style->clear();
    Q_FOREACH (const GigaTextPropertyAccess::StyleEntry &s, GigaTextPropertyAccess::stylesFor(family)) {
        m_style->addItem(s.label, QVariantList{s.weight, s.width, int(s.slant)});
    }
    m_style->blockSignals(was);
}

void GigaCharacterPanel::updateFromProperties()
{
    if (m_updating || !isVisible()) return;
    m_updating = true;
    fillFamilies();

    const KoSvgTextProperties chr = m_access->characterProperties();
    const KoSvgTextProperties para = m_access->paragraphProperties();
    auto mixed = [this](Id id) { return m_access->characterMixed(id); };

    const QStringList families = chr.propertyOrDefault(Id::FontFamiliesId).toStringList();
    const QString family = families.isEmpty() ? QString() : families.first();
    m_family->setEditText(mixed(Id::FontFamiliesId) ? QString() : family);

    fillStyles(family);
    const QList<GigaTextPropertyAccess::StyleEntry> styles = GigaTextPropertyAccess::stylesFor(family);
    const int weight = chr.propertyOrDefault(Id::FontWeightId).toInt();
    const QFont::Style slant = chr.propertyOrDefault(Id::FontStyleId).value<KoSvgText::CssFontStyleData>().style;
    m_style->setCurrentIndex((mixed(Id::FontWeightId) || mixed(Id::FontStyleId)) ? -1 : GigaTextPropertyAccess::bestStyle(styles, weight, slant));

    m_size->setEditText(mixed(Id::FontSizeId) ? QString() : pt(chr.fontSize().value));

    const qreal leading = GigaTextPropertyAccess::leadingFromProperties(chr);
    m_leading->setEditText(mixed(Id::LineHeightId) ? QString() : (leading < 0 ? m_leading->itemText(0) : pt(leading)));

    const KoSvgText::AutoValue kerning = chr.propertyOrDefault(Id::KerningId).value<KoSvgText::AutoValue>();
    m_kerning->setCurrentIndex(mixed(Id::KerningId) ? -1 : (kerning.isAuto ? 0 : 1));

    m_tracking->setEditText(mixed(Id::LetterSpacingId) ? QString()
                                                       : QString::number(qRound(GigaTextPropertyAccess::trackingFromProperties(chr))));

    m_baseline->setValue(GigaTextPropertyAccess::baselineShiftFromProperties(chr));
    m_color->setIcon(swatch(m_access->textColor()));

    m_bold->setChecked(weight >= 600);
    m_italic->setChecked(slant != QFont::StyleNormal);
    const KoSvgText::TextTransformInfo transform = chr.propertyOrDefault(Id::TextTransformId).value<KoSvgText::TextTransformInfo>();
    m_allCaps->setChecked(transform.capitals == KoSvgText::TextTransformUppercase);
    m_smallCaps->setChecked(chr.propertyOrDefault(Id::FontVariantCapsId).toInt() == KoSvgText::CapsSmall);
    const int position = chr.propertyOrDefault(Id::FontVariantPositionId).toInt();
    m_super->setChecked(position == KoSvgText::PositionSuper);
    m_sub->setChecked(position == KoSvgText::PositionSub);
    const KoSvgText::TextDecorations lines = chr.propertyOrDefault(Id::TextDecorationLineId).value<KoSvgText::TextDecorations>();
    m_underline->setChecked(lines.testFlag(KoSvgText::DecorationUnderline));
    m_strike->setChecked(lines.testFlag(KoSvgText::DecorationLineThrough));

    m_antiAlias->setCurrentIndex(qMax(0, ANTI_ALIAS_VALUES.indexOf(para.propertyOrDefault(Id::TextRenderingId).toInt())));

    m_updating = false;
}

void GigaCharacterPanel::applySize()
{
    if (m_updating) return;
    qreal size = 0;
    if (!gigaParseNumber(m_size->currentText(), size) || size <= 0) return;
    if (qFuzzyCompare(size, m_access->characterProperties().fontSize().value)) return;
    m_access->applyCharacter({Id::FontSizeId}, [size](KoSvgTextProperties &p) { p.setFontSize(KoSvgText::CssLengthPercentage(size)); });
}

void GigaCharacterPanel::applyLeading()
{
    if (m_updating) return;
    const QString text = m_leading->currentText().trimmed();
    qreal leading = -1;
    if (text.compare(m_leading->itemText(0), Qt::CaseInsensitive) != 0) {
        if (!gigaParseNumber(text, leading) || leading <= 0) return;
    }
    const qreal current = GigaTextPropertyAccess::leadingFromProperties(m_access->characterProperties());
    if (qFuzzyCompare(leading + 2, current + 2)) return; // (+2: Auto is -1, compare safely)
    m_access->applyCharacter({Id::LineHeightId}, [leading](KoSvgTextProperties &p) { GigaTextPropertyAccess::setLeading(p, leading); });
}

void GigaCharacterPanel::applyTracking()
{
    if (m_updating) return;
    qreal tracking = 0;
    if (!gigaParseNumber(m_tracking->currentText(), tracking)) return;
    if (qFuzzyCompare(tracking + 10000, GigaTextPropertyAccess::trackingFromProperties(m_access->characterProperties()) + 10000)) return;
    m_access->applyCharacter({Id::LetterSpacingId}, [tracking](KoSvgTextProperties &p) { GigaTextPropertyAccess::setTracking(p, tracking); });
}

void GigaCharacterPanel::applyBaseline()
{
    if (m_updating) return;
    const qreal shift = m_baseline->value();
    if (qFuzzyCompare(shift + 10000, GigaTextPropertyAccess::baselineShiftFromProperties(m_access->characterProperties()) + 10000)) return;
    m_access->applyCharacter({Id::BaselineShiftModeId, Id::BaselineShiftValueId},
                             [shift](KoSvgTextProperties &p) { GigaTextPropertyAccess::setBaselineShift(p, shift); });
}

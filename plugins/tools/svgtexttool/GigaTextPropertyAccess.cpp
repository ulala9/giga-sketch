/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaTextPropertyAccess.h"

#include <QLocale>

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

GigaTextPropertyAccess::GigaTextPropertyAccess(QObject *parent)
    : QObject(parent)
{
}

void GigaTextPropertyAccess::setProviders(KisCanvasResourceProvider *provider, KoCanvasResourceProvider *canvasResources)
{
    m_connections.clear();
    m_provider = provider;
    m_canvasResources = canvasResources;
    if (m_provider) {
        m_connections.addConnection(m_provider, SIGNAL(sigTextPropertiesChanged()), this, SIGNAL(changed()));
        m_connections.addConnection(m_provider, SIGNAL(sigCharacterPropertiesChanged()), this, SIGNAL(changed()));
    }
    if (m_canvasResources) {
        // Foreground colour = colour of new text.
        m_connections.addConnection(m_canvasResources, SIGNAL(canvasResourceChanged(int,QVariant)), this, SIGNAL(changed()));
    }
    Q_EMIT changed();
}

bool GigaTextPropertyAccess::hasProvider() const
{
    return !m_provider.isNull();
}

bool GigaTextPropertyAccess::isCharacterSelection() const
{
    if (!m_provider) return false;
    const KoSvgTextPropertyData chr = m_provider->characterTextPropertyData();
    return chr.enabled && chr.spanSelection;
}

KoSvgTextProperties GigaTextPropertyAccess::paragraphProperties() const
{
    if (!m_provider) return KoSvgTextProperties::defaultProperties();
    KoSvgTextProperties props = m_provider->textPropertyData().commonProperties;
    props.inheritFrom(KoSvgTextProperties::defaultProperties(), true);
    return props;
}

KoSvgTextProperties GigaTextPropertyAccess::characterProperties() const
{
    if (!m_provider) return KoSvgTextProperties::defaultProperties();
    if (!isCharacterSelection()) return paragraphProperties();
    const KoSvgTextPropertyData chr = m_provider->characterTextPropertyData();
    KoSvgTextProperties props = chr.commonProperties;
    props.inheritFrom(chr.inheritedProperties, true);
    props.inheritFrom(KoSvgTextProperties::defaultProperties(), true);
    return props;
}

bool GigaTextPropertyAccess::characterMixed(KoSvgTextProperties::PropertyId id) const
{
    if (!m_provider) return false;
    const KoSvgTextPropertyData data = isCharacterSelection() ? m_provider->characterTextPropertyData()
                                                              : m_provider->textPropertyData();
    return data.tristate.contains(id);
}

bool GigaTextPropertyAccess::paragraphMixed(KoSvgTextProperties::PropertyId id) const
{
    return m_provider && m_provider->textPropertyData().tristate.contains(id);
}

void GigaTextPropertyAccess::applyCharacter(const QSet<KoSvgTextProperties::PropertyId> &ids,
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

void GigaTextPropertyAccess::applyParagraph(const QSet<KoSvgTextProperties::PropertyId> &ids,
                                            const std::function<void(KoSvgTextProperties &)> &change)
{
    if (!m_provider) return;
    KoSvgTextPropertyData data = m_provider->textPropertyData();
    change(data.commonProperties);
    Q_FOREACH (KoSvgTextProperties::PropertyId id, ids) data.tristate.remove(id);
    m_provider->setTextPropertyData(data);
}

QColor GigaTextPropertyAccess::textColor() const
{
    QColor color = Qt::black;
    if (m_canvasResources) color = m_canvasResources->foregroundColor().toQColor();
    const KoSvgTextProperties chr = characterProperties();
    if (chr.hasProperty(KoSvgTextProperties::FillId)) {
        if (KoColorBackground *bg = dynamic_cast<KoColorBackground *>(chr.background().data())) color = bg->color();
    }
    return color;
}

void GigaTextPropertyAccess::setForegroundColor(const QColor &color)
{
    if (!m_canvasResources || !color.isValid()) return;
    m_canvasResources->setForegroundColor(KoColor(color, KoColorSpaceRegistry::instance()->rgb8()));
}

void GigaTextPropertyAccess::setFamily(const QString &family)
{
    applyCharacter({KoSvgTextProperties::FontFamiliesId}, [family](KoSvgTextProperties &p) {
        p.setProperty(KoSvgTextProperties::FontFamiliesId, QStringList{family});
    });
}

bool GigaTextPropertyAccess::beginFontPreview()
{
    // A mix of fonts can't be put back by setting a single value.
    m_previewAllowed = m_provider && !characterMixed(KoSvgTextProperties::FontFamiliesId);
    m_previewOriginal = characterProperties().propertyOrDefault(KoSvgTextProperties::FontFamiliesId).toStringList();
    return m_previewAllowed;
}

void GigaTextPropertyAccess::previewFamily(const QString &family)
{
    // Krita merges consecutive font edits on the same text into one undo step.
    if (m_previewAllowed) setFamily(family);
}

void GigaTextPropertyAccess::cancelFontPreview()
{
    if (!m_previewAllowed || m_previewOriginal.isEmpty()) return;
    const QStringList original = m_previewOriginal;
    applyCharacter({KoSvgTextProperties::FontFamiliesId}, [original](KoSvgTextProperties &p) {
        p.setProperty(KoSvgTextProperties::FontFamiliesId, original);
    });
}

QStringList GigaTextPropertyAccess::fontFamilies()
{
    QStringList names;
    KisAllResourcesModel *model = KisResourceModelProvider::resourceModel(ResourceType::FontFamilies);
    if (!model) return names;
    for (int row = 0; row < model->rowCount(); row++) {
        const QModelIndex idx = model->index(row, 0);
        const QString name = model->data(idx, Qt::UserRole + KisAbstractResourceModel::Name).toString();
        if (!name.isEmpty()) names << name;
    }
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

QList<GigaTextPropertyAccess::StyleEntry> GigaTextPropertyAccess::stylesFor(const QString &family)
{
    QList<StyleEntry> styles;
    KisAllResourcesModel *model = KisResourceModelProvider::resourceModel(ResourceType::FontFamilies);
    if (model && !family.isEmpty()) {
        const QVector<KoResourceSP> found = model->resourcesForName(family);
        KoFontFamilySP fontFamily = found.isEmpty() ? KoFontFamilySP() : found.first().dynamicCast<KoFontFamily>();
        if (fontFamily) {
            Q_FOREACH (const KoSvgText::FontFamilyStyleInfo &style, fontFamily->styles()) {
                if (style.localizedLabels.isEmpty()) continue;
                const QString label = style.localizedLabels.value(QLocale(QLocale::English), style.localizedLabels.values().first());
                if (label.isEmpty()) continue;
                const QFont::Style slant = style.isItalic ? QFont::StyleItalic
                                         : style.isOblique ? QFont::StyleOblique : QFont::StyleNormal;
                styles << StyleEntry{label,
                                     qRound(style.instanceCoords.value(QStringLiteral("wght"), 400.0)),
                                     qRound(style.instanceCoords.value(QStringLiteral("wdth"), 100.0)),
                                     slant};
            }
        }
    }
    if (styles.isEmpty()) {
        // Font without named styles: offer the basic four.
        styles << StyleEntry{QStringLiteral("Regular"), 400, 100, QFont::StyleNormal}
               << StyleEntry{QStringLiteral("Italic"), 400, 100, QFont::StyleItalic}
               << StyleEntry{QStringLiteral("Bold"), 700, 100, QFont::StyleNormal}
               << StyleEntry{QStringLiteral("Bold Italic"), 700, 100, QFont::StyleItalic};
    }
    return styles;
}

int GigaTextPropertyAccess::bestStyle(const QList<StyleEntry> &styles, int weight, QFont::Style slant)
{
    int best = -1;
    int bestDistance = INT_MAX;
    for (int i = 0; i < styles.size(); i++) {
        const int distance = qAbs(styles[i].weight - weight) + (styles[i].slant == slant ? 0 : 1000);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

qreal GigaTextPropertyAccess::trackingFromProperties(const KoSvgTextProperties &resolved)
{
    const KoSvgText::AutoLengthPercentage spacing =
        resolved.propertyOrDefault(KoSvgTextProperties::LetterSpacingId).value<KoSvgText::AutoLengthPercentage>();
    if (spacing.isAuto) return 0.0;
    const qreal fontSize = resolved.fontSize().value;
    switch (spacing.length.unit) {
    case KoSvgText::CssLengthPercentage::Em:
        return spacing.length.value * 1000.0;
    case KoSvgText::CssLengthPercentage::Absolute:
        return fontSize > 0 ? spacing.length.value / fontSize * 1000.0 : 0.0;
    default:
        return spacing.length.value * 1000.0; // other units: treat like em
    }
}

void GigaTextPropertyAccess::setTracking(KoSvgTextProperties &p, qreal thousandthsEm)
{
    if (qFuzzyIsNull(thousandthsEm)) {
        p.removeProperty(KoSvgTextProperties::LetterSpacingId);
        return;
    }
    p.setProperty(KoSvgTextProperties::LetterSpacingId,
                  QVariant::fromValue(KoSvgText::AutoLengthPercentage(thousandthsEm / 1000.0, KoSvgText::CssLengthPercentage::Em)));
}

qreal GigaTextPropertyAccess::leadingFromProperties(const KoSvgTextProperties &resolved)
{
    const KoSvgText::LineHeightInfo lh =
        resolved.propertyOrDefault(KoSvgTextProperties::LineHeightId).value<KoSvgText::LineHeightInfo>();
    if (lh.isNormal) return -1.0;
    const qreal fontSize = resolved.fontSize().value;
    if (lh.isNumber) return lh.value * fontSize;
    switch (lh.length.unit) {
    case KoSvgText::CssLengthPercentage::Absolute:
        return lh.length.value;
    case KoSvgText::CssLengthPercentage::Em:
    case KoSvgText::CssLengthPercentage::Percentage:
        return lh.length.value * fontSize;
    default:
        return lh.length.value * fontSize;
    }
}

void GigaTextPropertyAccess::setLeading(KoSvgTextProperties &p, qreal points)
{
    KoSvgText::LineHeightInfo lh;
    if (points < 0) {
        lh.isNormal = true;
    } else {
        lh.isNormal = false;
        lh.isNumber = false;
        lh.length = KoSvgText::CssLengthPercentage(points);
    }
    p.setProperty(KoSvgTextProperties::LineHeightId, QVariant::fromValue(lh));
}

qreal GigaTextPropertyAccess::baselineShiftFromProperties(const KoSvgTextProperties &resolved)
{
    const int mode = resolved.propertyOrDefault(KoSvgTextProperties::BaselineShiftModeId).toInt();
    if (mode != KoSvgText::ShiftLengthPercentage) return 0.0;
    const KoSvgText::CssLengthPercentage v =
        resolved.propertyOrDefault(KoSvgTextProperties::BaselineShiftValueId).value<KoSvgText::CssLengthPercentage>();
    return v.unit == KoSvgText::CssLengthPercentage::Absolute ? v.value : v.value * resolved.fontSize().value;
}

void GigaTextPropertyAccess::setBaselineShift(KoSvgTextProperties &p, qreal points)
{
    if (qFuzzyIsNull(points)) {
        p.setProperty(KoSvgTextProperties::BaselineShiftModeId, int(KoSvgText::ShiftNone));
        p.removeProperty(KoSvgTextProperties::BaselineShiftValueId);
        return;
    }
    p.setProperty(KoSvgTextProperties::BaselineShiftModeId, int(KoSvgText::ShiftLengthPercentage));
    p.setProperty(KoSvgTextProperties::BaselineShiftValueId, QVariant::fromValue(KoSvgText::CssLengthPercentage(points)));
}

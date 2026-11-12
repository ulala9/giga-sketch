/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaParagraphPanel.h"

#include <QButtonGroup>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <klocalizedstring.h>

#include <KoSvgText.h>

#include "GigaTextPropertyAccess.h"

using Id = KoSvgTextProperties::PropertyId;

namespace {

/// Alignment icon: four lines; the last line shows how the final line of a
/// justified paragraph sits (or all lines for plain alignment).
QIcon alignIcon(int align, const QColor &color)
{
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setPen(QPen(color, 1.6));
    const int full = 14, shortLen = 8, left = 2;
    for (int i = 0; i < 4; i++) {
        const int y = 3 + i * 4;
        const bool last = i == 3;
        const bool justified = align >= GigaParagraphPanel::JustifyLeft;
        int len = full;
        int x = left;
        int lineAlign = align;
        if (justified) {
            if (!last || align == GigaParagraphPanel::JustifyAll) { len = full; x = left; p.drawLine(x, y, x + len, y); continue; }
            lineAlign = align - GigaParagraphPanel::JustifyLeft; // last line: left/centre/right
        } else if (i % 2 == 1) {
            len = shortLen + 2;
        }
        if (justified || i % 2 == 1) {
            if (justified) len = shortLen;
            if (lineAlign == GigaParagraphPanel::AlignCenter) x = left + (full - len) / 2;
            else if (lineAlign == GigaParagraphPanel::AlignRight) x = left + full - len;
        }
        p.drawLine(x, y, x + len, y);
    }
    return QIcon(pixmap);
}

QDoubleSpinBox *pointsBox(QWidget *parent, qreal min)
{
    QDoubleSpinBox *box = new QDoubleSpinBox(parent);
    box->setRange(min, 1000);
    box->setDecimals(1);
    box->setSuffix(i18nc("unit suffix", " pt"));
    box->setKeyboardTracking(false); // apply on Enter / focus out / arrows, not every keystroke
    return box;
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

qreal indentPoints(const KoSvgTextProperties &resolved)
{
    const KoSvgText::TextIndentInfo indent = resolved.propertyOrDefault(Id::TextIndentId).value<KoSvgText::TextIndentInfo>();
    return indent.length.unit == KoSvgText::CssLengthPercentage::Absolute ? indent.length.value
                                                                          : indent.length.value * resolved.fontSize().value;
}

} // namespace

GigaParagraphPanel::GigaParagraphPanel(GigaTextPropertyAccess *access, QWidget *parent)
    : QWidget(parent)
    , m_access(access)
{
    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setSpacing(6);

    QHBoxLayout *alignRow = new QHBoxLayout();
    alignRow->setSpacing(1);
    outer->addLayout(alignRow);
    m_align = new QButtonGroup(this);
    m_align->setExclusive(true);
    const QString tips[AlignCount] = {
        i18nc("@info:tooltip", "Align left"),
        i18nc("@info:tooltip", "Align centre"),
        i18nc("@info:tooltip", "Align right"),
        i18nc("@info:tooltip", "Justify, last line left"),
        i18nc("@info:tooltip", "Justify, last line centred"),
        i18nc("@info:tooltip", "Justify, last line right"),
        i18nc("@info:tooltip", "Justify all lines"),
    };
    for (int a = 0; a < AlignCount; a++) {
        QToolButton *b = new QToolButton(this);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setIcon(alignIcon(a, palette().color(QPalette::WindowText)));
        b->setToolTip(tips[a]);
        m_align->addButton(b, a);
        alignRow->addWidget(b);
        if (a == AlignRight) alignRow->addSpacing(8);
    }
    alignRow->addStretch(1);
    connect(m_align, &QButtonGroup::idClicked, this, [this](int id) { applyAlign(id); });

    QGridLayout *grid = new QGridLayout();
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);
    outer->addLayout(grid);
    m_indentLeft = pointsBox(this, 0);
    m_indentRight = pointsBox(this, 0);
    m_firstLine = pointsBox(this, -1000);
    m_spaceBefore = pointsBox(this, 0);
    m_spaceAfter = pointsBox(this, 0);
    grid->addWidget(smallLabel(i18nc("paragraph panel", "Indent left"), i18nc("@info:tooltip", "Indent the whole paragraph from the start side"), this), 0, 0);
    grid->addWidget(m_indentLeft, 0, 1);
    grid->addWidget(smallLabel(i18nc("paragraph panel", "Indent right"), i18nc("@info:tooltip", "Indent the whole paragraph from the end side"), this), 0, 2);
    grid->addWidget(m_indentRight, 0, 3);
    grid->addWidget(smallLabel(i18nc("paragraph panel", "First line"), i18nc("@info:tooltip", "Extra indent of each paragraph's first line (negative: hanging)"), this), 1, 0);
    grid->addWidget(m_firstLine, 1, 1);
    grid->addWidget(smallLabel(i18nc("paragraph panel", "Space before"), i18nc("@info:tooltip", "Space above each paragraph"), this), 2, 0);
    grid->addWidget(m_spaceBefore, 2, 1);
    grid->addWidget(smallLabel(i18nc("paragraph panel", "Space after"), i18nc("@info:tooltip", "Space below each paragraph"), this), 2, 2);
    grid->addWidget(m_spaceAfter, 2, 3);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);

    auto connectReal = [this](QDoubleSpinBox *box, Id id) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this, id](double value) {
            if (m_updating) return;
            m_access->applyParagraph({id}, [id, value](KoSvgTextProperties &p) {
                if (qFuzzyIsNull(value)) p.removeProperty(id);
                else p.setProperty(id, value);
            });
        });
    };
    connectReal(m_indentLeft, Id::GigaIndentStartId);
    connectReal(m_indentRight, Id::GigaIndentEndId);
    connectReal(m_spaceBefore, Id::GigaSpaceBeforeId);
    connectReal(m_spaceAfter, Id::GigaSpaceAfterId);
    connect(m_firstLine, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_updating) return;
        m_access->applyParagraph({Id::TextIndentId}, [value](KoSvgTextProperties &p) {
            KoSvgText::TextIndentInfo indent = p.propertyOrDefault(Id::TextIndentId).value<KoSvgText::TextIndentInfo>();
            indent.length = KoSvgText::CssLengthPercentage(value);
            indent.eachLine = true; // every paragraph, like Photoshop
            p.setProperty(Id::TextIndentId, QVariant::fromValue(indent));
        });
    });

    outer->addStretch(1);
    connect(m_access, &GigaTextPropertyAccess::changed, this, &GigaParagraphPanel::updateFromProperties);
}

QDoubleSpinBox *GigaParagraphPanel::spinBox(const QString &name) const
{
    if (name == QLatin1String("indentLeft")) return m_indentLeft;
    if (name == QLatin1String("indentRight")) return m_indentRight;
    if (name == QLatin1String("firstLine")) return m_firstLine;
    if (name == QLatin1String("spaceBefore")) return m_spaceBefore;
    if (name == QLatin1String("spaceAfter")) return m_spaceAfter;
    return nullptr;
}

void GigaParagraphPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateFromProperties();
}

void GigaParagraphPanel::applyAlign(int align)
{
    if (m_updating) return;
    const QSet<Id> ids = {Id::TextAnchorId, Id::TextAlignAllId, Id::TextAlignLastId};
    m_access->applyParagraph(ids, [align](KoSvgTextProperties &p) {
        // Point text aligns with text-anchor, wrapped text with text-align;
        // set both so the buttons work for either kind.
        switch (align) {
        case AlignCenter:
            p.setProperty(Id::TextAnchorId, KoSvgText::AnchorMiddle);
            p.setProperty(Id::TextAlignAllId, KoSvgText::AlignCenter);
            p.removeProperty(Id::TextAlignLastId);
            break;
        case AlignRight:
            p.setProperty(Id::TextAnchorId, KoSvgText::AnchorEnd);
            p.setProperty(Id::TextAlignAllId, KoSvgText::AlignEnd);
            p.removeProperty(Id::TextAlignLastId);
            break;
        case JustifyLeft:
        case JustifyCenter:
        case JustifyRight:
        case JustifyAll: {
            p.setProperty(Id::TextAnchorId, KoSvgText::AnchorStart);
            p.setProperty(Id::TextAlignAllId, KoSvgText::AlignJustify);
            const int last = align == JustifyCenter ? KoSvgText::AlignCenter
                           : align == JustifyRight ? KoSvgText::AlignEnd
                           : align == JustifyAll ? KoSvgText::AlignJustify
                                                 : KoSvgText::AlignStart;
            p.setProperty(Id::TextAlignLastId, last);
            break;
        }
        case AlignLeft:
        default:
            p.setProperty(Id::TextAnchorId, KoSvgText::AnchorStart);
            p.setProperty(Id::TextAlignAllId, KoSvgText::AlignStart);
            p.removeProperty(Id::TextAlignLastId);
            break;
        }
    });
}

void GigaParagraphPanel::updateFromProperties()
{
    if (m_updating || !isVisible()) return;
    m_updating = true;
    const KoSvgTextProperties para = m_access->paragraphProperties();

    const int alignAll = para.propertyOrDefault(Id::TextAlignAllId).toInt();
    const int anchor = para.propertyOrDefault(Id::TextAnchorId).toInt();
    int align = AlignLeft;
    if (alignAll == KoSvgText::AlignJustify) {
        const int last = para.propertyOrDefault(Id::TextAlignLastId).toInt();
        align = last == KoSvgText::AlignCenter ? JustifyCenter
              : (last == KoSvgText::AlignEnd || last == KoSvgText::AlignRight) ? JustifyRight
              : last == KoSvgText::AlignJustify ? JustifyAll
                                                : JustifyLeft;
    } else if (alignAll == KoSvgText::AlignCenter || anchor == KoSvgText::AnchorMiddle) {
        align = AlignCenter;
    } else if (alignAll == KoSvgText::AlignEnd || alignAll == KoSvgText::AlignRight || anchor == KoSvgText::AnchorEnd) {
        align = AlignRight;
    }
    if (QAbstractButton *b = m_align->button(align)) b->setChecked(true);

    m_indentLeft->setValue(para.propertyOrDefault(Id::GigaIndentStartId).toReal());
    m_indentRight->setValue(para.propertyOrDefault(Id::GigaIndentEndId).toReal());
    m_firstLine->setValue(indentPoints(para));
    m_spaceBefore->setValue(para.propertyOrDefault(Id::GigaSpaceBeforeId).toReal());
    m_spaceAfter->setValue(para.propertyOrDefault(Id::GigaSpaceAfterId).toReal());
    m_updating = false;
}

/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaTextPanelsTest.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QTest>
#include <QToolButton>

#include <KoCanvasResourceProvider.h>
#include <KoSvgText.h>
#include <KoSvgTextPropertyData.h>
#include <kis_canvas_resource_provider.h>

#include <GigaCharacterPanel.h>
#include <GigaParagraphPanel.h>
#include <GigaTextPropertyAccess.h>
#include <simpletest.h>

using Id = KoSvgTextProperties::PropertyId;

namespace {

/// A canvas resource provider with no window, holding an empty text.
struct Fixture {
    KoCanvasResourceProvider resources;
    KisCanvasResourceProvider provider {nullptr};
    GigaTextPropertyAccess access;
    Fixture()
    {
        provider.setResourceManager(&resources);
        KoSvgTextPropertyData data;
        data.enabled = true;
        provider.setTextPropertyData(data);
        access.setProviders(&provider, &resources);
    }
    KoSvgTextProperties props() const { return provider.textPropertyData().commonProperties; }
};

void typeInto(QComboBox *combo, const QString &text)
{
    combo->lineEdit()->setText(text);
    Q_EMIT combo->lineEdit()->editingFinished();
}

} // namespace

void GigaTextPanelsTest::testTrackingConversions()
{
    KoSvgTextProperties p = KoSvgTextProperties::defaultProperties();
    p.setFontSize(KoSvgText::CssLengthPercentage(12));
    GigaTextPropertyAccess::setTracking(p, 50);
    QCOMPARE(qRound(GigaTextPropertyAccess::trackingFromProperties(p)), 50);

    // Letter spacing saved in points by other tools: 1.2 pt at 12 pt = 100/1000 em.
    p.setProperty(Id::LetterSpacingId, QVariant::fromValue(KoSvgText::AutoLengthPercentage(1.2)));
    QCOMPARE(qRound(GigaTextPropertyAccess::trackingFromProperties(p)), 100);
}

void GigaTextPanelsTest::testTrackingField()
{
    Fixture f;
    GigaCharacterPanel panel(&f.access);
    panel.show();
    QComboBox *tracking = qobject_cast<QComboBox *>(panel.field("tracking"));
    typeInto(tracking, "50");
    const KoSvgText::AutoLengthPercentage spacing = f.props().property(Id::LetterSpacingId).value<KoSvgText::AutoLengthPercentage>();
    QVERIFY(!spacing.isAuto);
    QCOMPARE(spacing.length.unit, KoSvgText::CssLengthPercentage::Em);
    QVERIFY(qAbs(spacing.length.value - 0.05) < 1e-6);
    QCOMPARE(tracking->currentText(), QString("50")); // read back
}

void GigaTextPanelsTest::testLeadingField()
{
    Fixture f;
    GigaCharacterPanel panel(&f.access);
    panel.show();
    QComboBox *leading = qobject_cast<QComboBox *>(panel.field("leading"));
    typeInto(leading, "18 pt");
    KoSvgText::LineHeightInfo lh = f.props().property(Id::LineHeightId).value<KoSvgText::LineHeightInfo>();
    QVERIFY(!lh.isNormal);
    QCOMPARE(lh.length.value, 18.0);

    typeInto(leading, leading->itemText(0)); // "Auto"
    lh = f.props().property(Id::LineHeightId).value<KoSvgText::LineHeightInfo>();
    QVERIFY(lh.isNormal);
}

void GigaTextPanelsTest::testStyleToggles()
{
    Fixture f;
    GigaCharacterPanel panel(&f.access);
    panel.show();
    qobject_cast<QToolButton *>(panel.field("bold"))->click();
    QCOMPARE(f.props().property(Id::FontWeightId).toInt(), 700);
    QVERIFY(qobject_cast<QToolButton *>(panel.field("bold"))->isChecked());

    qobject_cast<QToolButton *>(panel.field("underline"))->click();
    qobject_cast<QToolButton *>(panel.field("strike"))->click();
    const KoSvgText::TextDecorations lines = f.props().property(Id::TextDecorationLineId).value<KoSvgText::TextDecorations>();
    QVERIFY(lines.testFlag(KoSvgText::DecorationUnderline));
    QVERIFY(lines.testFlag(KoSvgText::DecorationLineThrough));

    qobject_cast<QToolButton *>(panel.field("super"))->click();
    QCOMPARE(f.props().property(Id::FontVariantPositionId).toInt(), int(KoSvgText::PositionSuper));
}

void GigaTextPanelsTest::testCapsExcludeEachOther()
{
    Fixture f;
    GigaCharacterPanel panel(&f.access);
    panel.show();
    QToolButton *allCaps = qobject_cast<QToolButton *>(panel.field("allCaps"));
    QToolButton *smallCaps = qobject_cast<QToolButton *>(panel.field("smallCaps"));
    allCaps->click();
    QCOMPARE(f.props().property(Id::TextTransformId).value<KoSvgText::TextTransformInfo>().capitals, KoSvgText::TextTransformUppercase);
    smallCaps->click();
    QCOMPARE(f.props().property(Id::FontVariantCapsId).toInt(), int(KoSvgText::CapsSmall));
    QCOMPARE(f.props().property(Id::TextTransformId).value<KoSvgText::TextTransformInfo>().capitals, KoSvgText::TextTransformNone);
    QVERIFY(!allCaps->isChecked());
    QVERIFY(smallCaps->isChecked());
}

void GigaTextPanelsTest::testJustifyLastCentred()
{
    Fixture f;
    GigaParagraphPanel panel(&f.access);
    panel.show();
    panel.alignGroup()->button(GigaParagraphPanel::JustifyCenter)->click();
    QCOMPARE(f.props().property(Id::TextAlignAllId).toInt(), int(KoSvgText::AlignJustify));
    QCOMPARE(f.props().property(Id::TextAlignLastId).toInt(), int(KoSvgText::AlignCenter));
    QVERIFY(panel.alignGroup()->button(GigaParagraphPanel::JustifyCenter)->isChecked());

    panel.alignGroup()->button(GigaParagraphPanel::AlignRight)->click();
    QCOMPARE(f.props().property(Id::TextAnchorId).toInt(), int(KoSvgText::AnchorEnd));
    QVERIFY(!f.props().hasProperty(Id::TextAlignLastId));
}

void GigaTextPanelsTest::testFirstLineIndent()
{
    Fixture f;
    GigaParagraphPanel panel(&f.access);
    panel.show();
    panel.spinBox("firstLine")->setValue(12);
    const KoSvgText::TextIndentInfo indent = f.props().property(Id::TextIndentId).value<KoSvgText::TextIndentInfo>();
    QCOMPARE(indent.length.value, 12.0);
    QVERIFY(indent.eachLine);

    panel.spinBox("spaceAfter")->setValue(6);
    QCOMPARE(f.props().property(Id::GigaSpaceAfterId).toReal(), 6.0);
    QCOMPARE(panel.spinBox("spaceAfter")->value(), 6.0);
}

SIMPLE_TEST_MAIN(GigaTextPanelsTest)

/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "TestGigaParagraphSpacing.h"

#include <KoSvgTextShape.h>
#include <KoSvgTextShapeMarkupConverter.h>

namespace {

// Two paragraphs separated by a hard line break (white-space: pre).
QString twoParagraphs(const QString &extraStyle)
{
    return QString("<text style=\"white-space: pre; font-family: sans-serif; font-size: 12; %1\">"
                   "First paragraph\nSecond paragraph</text>").arg(extraStyle);
}

// A wrapped (box) text: inline-size forces line breaks.
QString wrappedText(const QString &extraStyle)
{
    return QString("<text style=\"font-family: sans-serif; font-size: 12; inline-size: 120; %1\">"
                   "word word word word word word word word word word word word</text>").arg(extraStyle);
}

QRectF layoutRect(const QString &svg)
{
    KoSvgTextShape shape;
    KoSvgTextShapeMarkupConverter converter(&shape);
    converter.convertFromSvg(svg, QString(), QRectF(0, 0, 500, 500), 72.0);
    return shape.outlineRect();
}

int lineCount(const QString &svg)
{
    // Line count approximated from height divided by the single-line height.
    const qreal oneLine = layoutRect("<text style=\"font-family: sans-serif; font-size: 12;\">word</text>").height();
    return qRound(layoutRect(svg).height() / oneLine);
}

QString roundTrip(const QString &svg)
{
    KoSvgTextShape shape;
    KoSvgTextShapeMarkupConverter converter(&shape);
    converter.convertFromSvg(svg, QString(), QRectF(0, 0, 500, 500), 72.0);
    QString out, styles;
    converter.convertToSvg(&out, &styles);
    return out;
}

} // namespace

void TestGigaParagraphSpacing::testSpaceAfterAddsGap()
{
    const QRectF plain = layoutRect(twoParagraphs(""));
    const QRectF spaced = layoutRect(twoParagraphs("-giga-space-after: 30;"));
    QVERIFY2(qAbs((spaced.height() - plain.height()) - 30.0) < 0.5,
             qPrintable(QString("plain %1, spaced %2").arg(plain.height()).arg(spaced.height())));
}

void TestGigaParagraphSpacing::testSpaceBeforeAndAfterAdd()
{
    const QRectF plain = layoutRect(twoParagraphs(""));
    const QRectF spaced = layoutRect(twoParagraphs("-giga-space-before: 10; -giga-space-after: 15;"));
    QVERIFY(qAbs((spaced.height() - plain.height()) - 25.0) < 0.5);
}

void TestGigaParagraphSpacing::testIndentStartMovesText()
{
    const QRectF plain = layoutRect(wrappedText(""));
    const QRectF indented = layoutRect(wrappedText("-giga-indent-start: 40;"));
    QVERIFY2(qAbs((indented.left() - plain.left()) - 40.0) < 0.5,
             qPrintable(QString("plain %1, indented %2").arg(plain.left()).arg(indented.left())));
}

void TestGigaParagraphSpacing::testIndentsNarrowWrapWidth()
{
    // Indents take width away from the box, so the same words need more lines.
    const int plainLines = lineCount(wrappedText(""));
    const int indentedLines = lineCount(wrappedText("-giga-indent-start: 30; -giga-indent-end: 30;"));
    QVERIFY2(indentedLines > plainLines, qPrintable(QString("%1 vs %2 lines").arg(plainLines).arg(indentedLines)));
    QVERIFY(layoutRect(wrappedText("-giga-indent-start: 30; -giga-indent-end: 30;")).right()
            <= layoutRect(wrappedText("")).left() + 120.0 - 30.0 + 0.5);
}

void TestGigaParagraphSpacing::testRoundTrip()
{
    const QString out = roundTrip(twoParagraphs("-giga-space-before: 4; -giga-space-after: 8; -giga-indent-start: 12; -giga-indent-end: 16;"));
    QVERIFY2(out.contains("-giga-space-before: 4"), qPrintable(out));
    QVERIFY(out.contains("-giga-space-after: 8"));
    QVERIFY(out.contains("-giga-indent-start: 12"));
    QVERIFY(out.contains("-giga-indent-end: 16"));
    // Loading what we saved gives the same layout.
    QCOMPARE(layoutRect(out).height(), layoutRect(twoParagraphs("-giga-space-before: 4; -giga-space-after: 8; -giga-indent-start: 12; -giga-indent-end: 16;")).height());
}

void TestGigaParagraphSpacing::testUnusedPropertiesAreNotWritten()
{
    // Texts that don't use the new properties are saved exactly as before.
    QVERIFY(!roundTrip(twoParagraphs("")).contains("-giga-"));
}

SIMPLE_TEST_MAIN(TestGigaParagraphSpacing)

/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "TestGigaTextBox.h"

#include <KoPathShape.h>
#include <KoSvgTextShape.h>
#include <KoSvgTextShapeMarkupConverter.h>

namespace {

const QString WORDS = QStringLiteral(
    "<text style=\"font-family: sans-serif; font-size: 12; white-space: pre-wrap;\">"
    "word word word word word word word word word word word word word word word word</text>");

KoPathShape *makeBox(const QRectF &r)
{
    // Same construction as the text tool's GigaTextBox::createBoxShape().
    KoPathShape *box = new KoPathShape();
    box->moveTo(r.topLeft());
    box->lineTo(r.topRight());
    box->lineTo(r.bottomRight());
    box->lineTo(r.bottomLeft());
    box->close();
    box->normalize();
    return box;
}

void load(KoSvgTextShape &text, const QString &svg)
{
    KoSvgTextShapeMarkupConverter converter(&text);
    converter.convertFromSvg(svg, QString(), QRectF(0, 0, 500, 500), 72.0);
}

qreal textHeight(KoSvgTextShape &text)
{
    // Height of the laid out glyphs (not of the box).
    // (Selection boxes of all the text, i.e. the lines it occupies.)
    const int end = text.posForIndex(text.plainText().size());
    return text.selectionBoxes(0, end).boundingRect().height();
}

} // namespace

void TestGigaTextBox::testNarrowerBoxMakesMoreLines()
{
    KoSvgTextShape wide;
    load(wide, WORDS);
    wide.addShapeContours({makeBox(QRectF(0, 0, 400, 300))}, true);
    wide.relayout();

    KoSvgTextShape narrow;
    load(narrow, WORDS);
    narrow.addShapeContours({makeBox(QRectF(0, 0, 100, 300))}, true);
    narrow.relayout();

    QVERIFY2(textHeight(narrow) > textHeight(wide) * 1.5,
             qPrintable(QString("wide %1, narrow %2").arg(textHeight(wide)).arg(textHeight(narrow))));
}

void TestGigaTextBox::testResizingTheBoxReflows()
{
    // The resize handles only change the rectangle; the text must follow.
    KoSvgTextShape text;
    load(text, WORDS);
    KoPathShape *box = makeBox(QRectF(0, 0, 400, 300));
    text.addShapeContours({box}, true);
    text.relayout();
    const qreal before = textHeight(text);

    box->setSize(QSizeF(100, 300));
    text.gigaContourChanged(box); // as the resize handles do
    const qreal after = textHeight(text);
    QVERIFY2(after > before * 1.5, qPrintable(QString("before %1, after %2").arg(before).arg(after)));
}

void TestGigaTextBox::testOverflowDetected()
{
    KoSvgTextShape text;
    load(text, WORDS);
    KoPathShape *box = makeBox(QRectF(0, 0, 100, 20)); // about one line
    text.addShapeContours({box}, true);
    text.relayout();
    QVERIFY(text.gigaTextOverflows());

    box->setSize(QSizeF(400, 300)); // now everything fits
    text.gigaContourChanged(box);
    QVERIFY(!text.gigaTextOverflows());
}

void TestGigaTextBox::testNoBoxNoOverflow()
{
    KoSvgTextShape text;
    load(text, WORDS);
    text.relayout();
    QVERIFY(!text.gigaTextOverflows());
}

void TestGigaTextBox::testSaveAndReload()
{
    // A box text is saved as standard SVG 2 shape-inside and comes back as a box.
    KoSvgTextShape text;
    load(text, WORDS);
    text.addShapeContours({makeBox(QRectF(0, 0, 150, 300))}, true);
    text.relayout();
    QString svg, styles;
    KoSvgTextShapeMarkupConverter(&text).convertToSvg(&svg, &styles);
    QVERIFY2(svg.contains("shape-inside"), qPrintable(svg));
}

SIMPLE_TEST_MAIN(TestGigaTextBox)

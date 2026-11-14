/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "TestGigaReferenceLayout.h"

#include <GigaReferenceLayout.h>
#include <simpletest.h>

#include <QRandomGenerator>

using namespace GigaReferenceLayout;

namespace {
QVector<QSizeF> mixedSizes(int n = 23, quint32 seed = 7)
{
    QRandomGenerator rng(seed);
    QVector<QSizeF> sizes;
    for (int i = 0; i < n; ++i) {
        sizes.append(QSizeF(100 + rng.bounded(900), 100 + rng.bounded(900)));
    }
    sizes.append(QSizeF(3000, 400)); // panorama
    sizes.append(QSizeF(300, 1600)); // tall strip
    return sizes;
}

bool near(qreal a, qreal b, qreal tol = 1e-6)
{
    return qAbs(a - b) <= tol * qMax<qreal>(1.0, qMax(qAbs(a), qAbs(b)));
}
}

void TestGigaReferenceLayout::testNormalize()
{
    const QVector<QSizeF> sizes = {QSizeF(100, 50), QSizeF(400, 400), QSizeF(30, 90)};

    const QVector<QSizeF> area = normalize(sizes, NormalizeMode::SameArea);
    const qreal a0 = area[0].width() * area[0].height();
    for (int i = 0; i < sizes.size(); ++i) {
        QVERIFY(near(area[i].width() * area[i].height(), a0, 1e-9));
        QVERIFY(near(area[i].width() / area[i].height(), sizes[i].width() / sizes[i].height(), 1e-9));
    }
    // Target = the average area, so the total stays the same.
    QVERIFY(near(a0 * 3, 100 * 50 + 400 * 400 + 30 * 90, 1e-9));

    const QVector<QSizeF> height = normalize(sizes, NormalizeMode::SameHeight);
    for (const QSizeF &s : height) QVERIFY(near(s.height(), (50 + 400 + 90) / 3.0));

    const QVector<QSizeF> width = normalize(sizes, NormalizeMode::SameWidth);
    for (const QSizeF &s : width) QVERIFY(near(s.width(), (100 + 400 + 30) / 3.0));

    const QVector<QSizeF> match = normalize(sizes, NormalizeMode::MatchSelected, 2);
    for (const QSizeF &s : match) QVERIFY(near(s.width() * s.height(), 30 * 90, 1e-9));
    QVERIFY(near(match[2].width(), 30));

    // Bad match index falls back to Same area.
    QCOMPARE(normalize(sizes, NormalizeMode::MatchSelected, 9), area);
}

void TestGigaReferenceLayout::testStylesKeepAspectAndDoNotOverlap_data()
{
    QTest::addColumn<int>("style");
    QTest::addColumn<qreal>("spacing");
    QTest::addColumn<qreal>("aspect");
    for (int s = 0; s <= int(Style::Auto); ++s) {
        for (qreal spacing : {0.0, 12.0}) {
            for (qreal aspect : {1.0, 1.78, 0.5}) {
                QTest::addRow("style%d gap%g aspect%g", s, spacing, aspect) << s << spacing << aspect;
            }
        }
    }
}

void TestGigaReferenceLayout::testStylesKeepAspectAndDoNotOverlap()
{
    QFETCH(int, style);
    QFETCH(qreal, spacing);
    QFETCH(qreal, aspect);

    const QVector<QSizeF> sizes = mixedSizes();
    Options options;
    options.style = Style(style);
    options.spacing = spacing;
    options.targetAspect = aspect;
    const Result result = arrange(sizes, options);

    QCOMPARE(result.rects.size(), sizes.size());
    QVERIFY(result.usedStyle != Style::Auto);
    for (int i = 0; i < sizes.size(); ++i) {
        const QRectF &r = result.rects[i];
        QVERIFY2(r.width() > 0 && r.height() > 0, qPrintable(QString::number(i)));
        QVERIFY2(near(r.width() / r.height(), sizes[i].width() / sizes[i].height(), 1e-6), qPrintable(QString::number(i)));
        QVERIFY(r.left() >= -1e-6 && r.top() >= -1e-6);
    }
    for (int i = 0; i < sizes.size(); ++i) {
        for (int j = i + 1; j < sizes.size(); ++j) {
            // Shrink a hair so touching edges don't count; with spacing,
            // grow by just under the gap: they must still not touch.
            const qreal pad = spacing > 0 ? spacing * 0.49 : -1e-6;
            const QRectF a = result.rects[i].adjusted(-pad, -pad, pad, pad);
            const QRectF b = result.rects[j].adjusted(-pad, -pad, pad, pad);
            QVERIFY2(!a.intersects(b), qPrintable(QString("%1 overlaps %2").arg(i).arg(j)));
        }
    }
    // Nothing jumps in size: the total image area stays about the same.
    if (Style(style) != Style::Packed) {
        qreal in = 0.0, out = 0.0;
        for (const QSizeF &s : sizes) in += s.width() * s.height();
        for (const QRectF &r : result.rects) out += r.width() * r.height();
        QVERIFY2(qAbs(out / in - 1.0) < 0.1, qPrintable(QString::number(out / in)));
    }
    // Sanity: a usable layout, not a single endless strip.
    QVERIFY2(score(result.rects, aspect) > 0.25, qPrintable(QString::number(score(result.rects, aspect))));
}

void TestGigaReferenceLayout::testJustifiedRowsShareRightEdge()
{
    const QVector<QSizeF> sizes = mixedSizes(40, 3);
    Options options;
    options.style = Style::JustifiedRows;
    options.spacing = 10;
    options.targetAspect = 1.5;
    const Result result = arrange(sizes, options);

    // Group by row (same top); every row but the last ends at the same x.
    QMap<qreal, qreal> rowRight;
    QMap<qreal, qreal> rowHeight;
    for (const QRectF &r : result.rects) {
        const qreal key = qRound64(r.top() * 1000) / 1000.0;
        rowRight[key] = qMax(rowRight.value(key, 0.0), r.right());
        if (rowHeight.contains(key)) QVERIFY(near(rowHeight[key], r.height(), 1e-6)); // equal heights in a row
        rowHeight[key] = r.height();
    }
    QVERIFY(rowRight.size() >= 2);
    const QList<qreal> rights = rowRight.values();
    for (int i = 1; i < rights.size() - 1; ++i) QVERIFY(near(rights[i], rights[0], 1e-6));
    QVERIFY(rights.last() <= rights[0] + 1e-6);
}

void TestGigaReferenceLayout::testPackedKeepsSizes()
{
    const QVector<QSizeF> sizes = mixedSizes();
    Options options;
    options.style = Style::Packed;
    options.spacing = 5;
    const Result result = arrange(sizes, options);
    for (int i = 0; i < sizes.size(); ++i) QCOMPARE(result.rects[i].size(), sizes[i]);
}

void TestGigaReferenceLayout::testAutoPicksAStyle()
{
    Options options;
    options.style = Style::Auto;
    options.targetAspect = 1.0;
    // Equal squares: a grid-like square layout scores (nearly) perfectly.
    QVector<QSizeF> squares(16, QSizeF(100, 100));
    const Result result = arrange(squares, options);
    QVERIFY(result.usedStyle != Style::Auto);
    QVERIFY(score(result.rects, 1.0) > 0.95);
}

void TestGigaReferenceLayout::testReadingOrder()
{
    // Two rows, given out of order.
    const QVector<QRectF> rects = {
        QRectF(200, 110, 100, 100), // row 2, right
        QRectF(0, 0, 100, 100),     // row 1, left
        QRectF(0, 105, 100, 100),   // row 2, left
        QRectF(110, 5, 100, 100),   // row 1, right (slightly lower)
    };
    QCOMPARE(readingOrder(rects), QVector<int>({1, 3, 2, 0}));
}

void TestGigaReferenceLayout::testEmptyAndInvalid()
{
    QVERIFY(arrange({}, Options()).rects.isEmpty());
    QVERIFY(normalize({}, NormalizeMode::SameArea).isEmpty());

    const QVector<QSizeF> withBad = {QSizeF(100, 100), QSizeF(0, 50), QSizeF(200, 100)};
    const QVector<QSizeF> n = normalize(withBad, NormalizeMode::SameHeight);
    QCOMPARE(n[1], QSizeF(0, 50)); // invalid kept as is
    QVERIFY(near(n[0].height(), 100) && near(n[2].height(), 100));

    for (int s = 0; s <= int(Style::Auto); ++s) {
        Options options;
        options.style = Style(s);
        QCOMPARE(arrange(withBad, options).rects.size(), 3);
        QCOMPARE(arrange({QSizeF(50, 80)}, options).rects.size(), 1);
    }
}

SIMPLE_TEST_MAIN(TestGigaReferenceLayout)

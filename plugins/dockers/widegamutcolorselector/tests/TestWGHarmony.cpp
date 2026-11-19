/*
 * SPDX-FileCopyrightText: 2026 Giga Sketch contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QTest>

#include "WGHarmony.h"

using namespace WGHarmony;

class TestWGHarmony : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testComplementary()
    {
        const QVector4D base(0.1f, 0.8f, 0.6f, 0.f);
        const QVector<QVector4D> c = harmony(Complementary, base, 0);
        QCOMPARE(c.size(), 2);
        QCOMPARE(c[0], base);
        QVERIFY(qAbs(c[1].x() - 0.6f) < 1e-5);
        QCOMPARE(c[1].y(), base.y());
        QCOMPARE(c[1].z(), base.z());
    }
    void testWrapAround()
    {
        const QVector<QVector4D> c = harmony(Triadic, QVector4D(0.9f, 1.f, 1.f, 0.f), 0);
        QCOMPARE(c.size(), 3);
        QVERIFY(qAbs(c[1].x() - (0.9f + 1.f / 3.f - 1.f)) < 1e-5); // 324° + 120° = 84°
        for (const QVector4D &v : c) QVERIFY(v.x() >= 0.f && v.x() < 1.f);
    }
    void testAnalogousBaseInMiddle()
    {
        const QVector4D base(0.5f, 0.5f, 0.5f, 0.f);
        const QVector<QVector4D> c = harmony(Analogous, base, 30);
        QCOMPARE(c.size(), 3);
        QCOMPARE(baseIndex(Analogous, base), 1);
        QCOMPARE(c[1], base);
        QVERIFY(qAbs(hueDelta(base.x(), c[0].x()) + 30) < 1e-3);
        QVERIFY(qAbs(hueDelta(base.x(), c[2].x()) - 30) < 1e-3);
    }
    void testSplit()
    {
        const QVector<QVector4D> c = harmony(SplitComplementary, QVector4D(0.f, 1.f, 1.f, 0.f), 150);
        QCOMPARE(c.size(), 3);
        QVERIFY(qAbs(hueDelta(0, c[1].x()) - 150) < 1e-3);
        QVERIFY(qAbs(hueDelta(0, c[2].x()) + 150) < 1e-3);
    }
    void testSquare()
    {
        QCOMPARE(harmony(Square, QVector4D(0.2f, 1.f, 1.f, 0.f), 0).size(), 4);
    }
    void testMonochrome_data()
    {
        QTest::addColumn<float>("z");
        QTest::newRow("dark") << 0.02f;
        QTest::newRow("mid") << 0.5f;
        QTest::newRow("light") << 0.97f;
        QTest::newRow("white") << 1.0f;
    }
    void testMonochrome()
    {
        QFETCH(float, z);
        const QVector4D base(0.3f, 0.7f, z, 0.f);
        const QVector<QVector4D> c = harmony(Monochrome, base, 0);
        QCOMPARE(c.size(), 5);
        const int b = baseIndex(Monochrome, base);
        QCOMPARE(c[b], base); // the base color itself is one of the swatches
        for (int i = 0; i < c.size(); i++) {
            QCOMPARE(c[i].x(), base.x());
            QCOMPARE(c[i].y(), base.y());
            QVERIFY(c[i].z() >= 0.f && c[i].z() <= 1.f);
            if (i > 0) QVERIFY(c[i].z() > c[i - 1].z()); // dark to light, no repeats
        }
    }
    void testSameColor()
    {
        QVERIFY(sameColor(QVector4D(0.999f, 0.5f, 0.5f, 0), QVector4D(0.001f, 0.5f, 0.5f, 0))); // across 0/1
        QVERIFY(!sameColor(QVector4D(0.2f, 0.5f, 0.5f, 0), QVector4D(0.25f, 0.5f, 0.5f, 0)));
        QVERIFY(sameColor(QVector4D(0.2f, 0.f, 0.5f, 0), QVector4D(0.7f, 0.f, 0.5f, 0))); // gray: hue ignored
    }
};

QTEST_GUILESS_MAIN(TestWGHarmony)
#include "TestWGHarmony.moc"

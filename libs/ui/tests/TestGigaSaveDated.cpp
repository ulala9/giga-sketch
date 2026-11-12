/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "TestGigaSaveDated.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <GigaSaveDated.h>
#include <simpletest.h>

namespace {
void touch(const QString &path)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
}
}

void TestGigaSaveDated::testFirstNameIsTheDate()
{
    QTemporaryDir dir;
    const QString path = GigaSaveDated::nextDatedPath(dir.path(), QDate(2026, 11, 12));
    QCOMPARE(QFileInfo(path).fileName(), QString("2026-11-12.jpg"));
    QCOMPARE(QFileInfo(path).absolutePath(), QFileInfo(dir.path()).absoluteFilePath());
}

void TestGigaSaveDated::testTakenNamesGetVersions()
{
    QTemporaryDir dir;
    const QDate day(2026, 1, 5);
    touch(dir.filePath("2026-01-05.jpg"));
    QCOMPARE(QFileInfo(GigaSaveDated::nextDatedPath(dir.path(), day)).fileName(), QString("2026-01-05_v2.jpg"));
    touch(dir.filePath("2026-01-05_v2.jpg"));
    touch(dir.filePath("2026-01-05_v3.jpg"));
    QCOMPARE(QFileInfo(GigaSaveDated::nextDatedPath(dir.path(), day)).fileName(), QString("2026-01-05_v4.jpg"));
}

SIMPLE_TEST_MAIN(TestGigaSaveDated)

/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaFontPickerTest.h"

#include <QLineEdit>
#include <QListWidget>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include <GigaFontPicker.h>
#include <simpletest.h>

namespace {
const QStringList FAMILIES = {"Alpha Sans", "Beta Serif", "Gamma Mono", "Delta Serif Display"};

int fontRows(QListWidget *list)
{
    int n = 0;
    for (int i = 0; i < list->count(); i++) {
        if (!list->item(i)->text().isEmpty() && (list->item(i)->flags() & Qt::ItemIsEnabled)) n++;
    }
    return n;
}
} // namespace

void GigaFontPickerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true); // keep recent/favourites out of real settings
}

void GigaFontPickerTest::testSearchFilters()
{
    QWidget anchor;
    anchor.show();
    GigaFontPicker picker;
    picker.popup(&anchor, FAMILIES, "Alpha Sans");
    QLineEdit *search = picker.findChild<QLineEdit *>();
    QListWidget *list = picker.findChild<QListWidget *>();
    QVERIFY(search && list);

    QTest::keyClicks(search, "serif");
    QCOMPARE(fontRows(list), 2); // Beta Serif, Delta Serif Display
    QCOMPARE(list->currentItem()->text(), QString("Beta Serif")); // first match ready for Enter
    picker.hide();
}

void GigaFontPickerTest::testEnterChoosesAndRemembersRecent()
{
    QWidget anchor;
    anchor.show();
    GigaFontPicker picker;
    QSignalSpy chosen(&picker, &GigaFontPicker::chosen);
    picker.popup(&anchor, FAMILIES, "Alpha Sans");
    QLineEdit *search = picker.findChild<QLineEdit *>();
    QTest::keyClicks(search, "gamma");
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(chosen.count(), 1);
    QCOMPARE(chosen.first().first().toString(), QString("Gamma Mono"));
    QVERIFY(!picker.isVisible());

    // Reopened: a Recent section lists it first.
    picker.popup(&anchor, FAMILIES, "Alpha Sans");
    QListWidget *list = picker.findChild<QListWidget *>();
    QCOMPARE(list->item(0)->text(), QString("Recent"));
    QCOMPARE(list->item(1)->text(), QString("Gamma Mono"));
    picker.hide();
}

void GigaFontPickerTest::testPreviewThenEscapeCancels()
{
    QWidget anchor;
    anchor.show();
    GigaFontPicker picker;
    QSignalSpy preview(&picker, &GigaFontPicker::previewRequested);
    QSignalSpy cancelled(&picker, &GigaFontPicker::previewCancelled);
    QSignalSpy chosen(&picker, &GigaFontPicker::chosen);
    picker.popup(&anchor, FAMILIES, "Alpha Sans");
    QLineEdit *search = picker.findChild<QLineEdit *>();
    QTest::keyClicks(search, "delta");
    QTest::keyClick(search, Qt::Key_Down); // stays on the only match; still previews it
    QListWidget *list = picker.findChild<QListWidget *>();
    list->setCurrentRow(0);
    QVERIFY(preview.count() >= 1);
    QCOMPARE(preview.last().first().toString(), QString("Delta Serif Display"));

    QTest::keyClick(search, Qt::Key_Escape);
    QCOMPARE(cancelled.count(), 1);
    QCOMPARE(chosen.count(), 0);
}

void GigaFontPickerTest::testNoPreviewForCurrentFont()
{
    QWidget anchor;
    anchor.show();
    GigaFontPicker picker;
    QSignalSpy preview(&picker, &GigaFontPicker::previewRequested);
    QSignalSpy cancelled(&picker, &GigaFontPicker::previewCancelled);
    picker.popup(&anchor, FAMILIES, "Beta Serif");
    QLineEdit *search = picker.findChild<QLineEdit *>();
    QTest::keyClicks(search, "beta"); // only the font already in use
    QCOMPARE(preview.count(), 0);
    QTest::keyClick(search, Qt::Key_Escape);
    QCOMPARE(cancelled.count(), 0); // nothing to undo
}

SIMPLE_TEST_MAIN(GigaFontPickerTest)

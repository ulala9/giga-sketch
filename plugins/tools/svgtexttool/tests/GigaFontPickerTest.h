/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_FONT_PICKER_TEST_H
#define GIGA_FONT_PICKER_TEST_H

#include <QObject>

class GigaFontPickerTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void testSearchFilters();
    void testEnterChoosesAndRemembersRecent();
    void testPreviewThenEscapeCancels();
    void testNoPreviewForCurrentFont();
};

#endif

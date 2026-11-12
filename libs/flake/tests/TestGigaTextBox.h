/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef TEST_GIGA_TEXT_BOX_H
#define TEST_GIGA_TEXT_BOX_H

#include <simpletest.h>

/// Giga Sketch text boxes: text flowing inside an invisible rectangle
/// (shape-inside) reflows when the rectangle is resized, and reports
/// overflow when it doesn't fit.
class TestGigaTextBox : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testNarrowerBoxMakesMoreLines();
    void testResizingTheBoxReflows();
    void testOverflowDetected();
    void testNoBoxNoOverflow();
    void testSaveAndReload();
};

#endif

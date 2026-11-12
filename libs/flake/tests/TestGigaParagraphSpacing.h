/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef TEST_GIGA_PARAGRAPH_SPACING_H
#define TEST_GIGA_PARAGRAPH_SPACING_H

#include <simpletest.h>

class TestGigaParagraphSpacing : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testSpaceAfterAddsGap();
    void testSpaceBeforeAndAfterAdd();
    void testIndentStartMovesText();
    void testIndentsNarrowWrapWidth();
    void testRoundTrip();
    void testUnusedPropertiesAreNotWritten();
};

#endif

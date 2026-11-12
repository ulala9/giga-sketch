/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_TEXT_PANELS_TEST_H
#define GIGA_TEXT_PANELS_TEST_H

#include <QObject>

/// Character / Paragraph panels: each field writes the right text property
/// (through the canvas resource provider) and shows it again when read back.
class GigaTextPanelsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testTrackingConversions();
    void testTrackingField();
    void testLeadingField();
    void testStyleToggles();
    void testCapsExcludeEachOther();
    void testJustifyLastCentred();
    void testFirstLineIndent();
};

#endif

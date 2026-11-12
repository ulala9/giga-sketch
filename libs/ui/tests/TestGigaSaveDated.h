/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef TEST_GIGA_SAVE_DATED_H
#define TEST_GIGA_SAVE_DATED_H

#include <QObject>

class TestGigaSaveDated : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testFirstNameIsTheDate();
    void testTakenNamesGetVersions();
};

#endif

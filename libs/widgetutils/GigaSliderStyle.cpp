/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "GigaSliderStyle.h"

#include <KSharedConfig>
#include <KConfigGroup>
#include <QApplication>
#include <QWidget>

namespace {
int s_enabled = -1; // -1 = not read yet; only touched on the GUI thread
}

bool GigaSliderStyle::enabled()
{
    if (s_enabled < 0) {
        s_enabled = KSharedConfig::openConfig()->group(QString()).readEntry("gigaSliderStyle", true) ? 1 : 0;
    }
    return s_enabled == 1;
}

void GigaSliderStyle::setEnabled(bool value)
{
    const int v = value ? 1 : 0;
    if (v == s_enabled) return;
    s_enabled = v;
    KSharedConfig::openConfig()->group(QString()).writeEntry("gigaSliderStyle", value);
    // Repaint every widget so open sliders switch at once.
    const auto widgets = QApplication::allWidgets();
    for (QWidget *w : widgets) {
        w->update();
    }
}

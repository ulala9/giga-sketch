/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef GIGA_SLIDER_STYLE_H
#define GIGA_SLIDER_STYLE_H

#include "kritawidgetutils_export.h"

/**
 * Giga Sketch: switch between the softer "Giga" look of Krita's slider spin
 * boxes (KisSliderSpinBox / KisDoubleSliderSpinBox) and Krita's original look.
 *
 * The value lives in the main config (key gigaSliderStyle, default on). It is
 * read once, the first time a slider paints; Preferences calls setEnabled() so
 * a change shows without a restart.
 */
namespace GigaSliderStyle {
KRITAWIDGETUTILS_EXPORT bool enabled();
KRITAWIDGETUTILS_EXPORT void setEnabled(bool value);
}

#endif

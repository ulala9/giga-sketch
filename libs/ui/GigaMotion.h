/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef GIGA_MOTION_H
#define GIGA_MOTION_H

#include "kritaui_export.h"

#include <QPixmap>

class QWidget;

/**
 * Giga Sketch: small interface animations.
 *
 * Qt can't make a child widget (our in-window panels) or, on Wayland, a popup
 * window partly transparent without slow effects. So instead of fading the
 * widget itself we lay a short-lived "cover" over it:
 *
 *  - reveal(): the cover is filled with the widget's own background color and
 *    fades away, so the content appears to fade in from the panel color.
 *  - crossFade(): the cover shows a picture of the window taken before a big
 *    change (interface mode switch) and fades away, showing the new layout.
 *
 * The cover ignores the mouse, deletes itself when done, and only runs on the
 * GUI thread. Everything is skipped when "Interface animations" is off
 * (config key gigaUiAnimations).
 */
namespace GigaMotion {

KRITAUI_EXPORT bool enabled();
KRITAUI_EXPORT void setEnabled(bool value);

/// Fade `w`'s content in over `ms` milliseconds (call right after showing it).
KRITAUI_EXPORT void reveal(QWidget *w, int ms = 120);

/// Picture of `window` to pass to crossFade() later. Empty when animations are off.
KRITAUI_EXPORT QPixmap snapshot(QWidget *window);

/// Fade from `before` (taken with snapshot()) to what `window` shows now.
KRITAUI_EXPORT void crossFade(QWidget *window, const QPixmap &before, int ms = 180);

}

#endif

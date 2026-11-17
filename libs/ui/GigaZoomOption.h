/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_ZOOM_OPTION_H
#define GIGA_ZOOM_OPTION_H

#include <QList>
#include <Qt>

#include "kritaui_export.h"

class QAction;
class QObject;

/**
 * Giga Sketch: View > "Ctrl+Space Zooms Toward Pointer".
 *
 * Krita's drag-zoom has two flavors: "Zoom Mode" (around the canvas
 * center) and "Relative Zoom Mode" (toward where you pressed, like the mouse
 * wheel); the same for the stepped "Discrete" zoom. This switches the
 * Space-based zoom bindings of the current input profile between them and
 * saves the profile, exactly as Settings > Canvas Input Settings would.
 */
namespace GigaZoomOption
{
/// The zoom-action mode a binding should have. Only bindings that use the
/// Space key and one of the drag-zoom modes change; others are returned as is.
KRITAUI_EXPORT uint modeFor(uint currentMode, const QList<Qt::Key> &keys, bool towardPointer);

/// Whether the current profile's Ctrl+Space zoom goes toward the pointer.
KRITAUI_EXPORT bool isTowardPointer();
KRITAUI_EXPORT void setTowardPointer(bool towardPointer);

/// The checkable View menu action, kept in sync with the profile.
QAction *createAction(QObject *parent);
}

#endif // GIGA_ZOOM_OPTION_H

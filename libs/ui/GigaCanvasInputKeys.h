/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_CANVAS_INPUT_KEYS_H
#define GIGA_CANVAS_INPUT_KEYS_H

#include <QList>
#include <QPointF>
#include <QSet>
#include <QVector>
#include <Qt>

#include "kritaui_export.h"

/**
 * Giga Sketch: lets other widgets (the Reference Board) reuse the canvas
 * navigation bindings from Settings > Canvas Input Settings, so changing
 * "Ctrl+Space = zoom" there changes it everywhere.
 *
 * The input profile classes aren't exported from kritaui, so this small
 * exported layer reads them and hands out plain values.
 */
namespace GigaCanvasInputKeys
{
enum class Kind {
    Zoom,           ///< drag zooms smoothly
    ZoomDiscrete,   ///< drag zooms in steps
    Rotate,         ///< drag rotates
    RotateDiscrete, ///< drag rotates in 15° steps
    RotateReset     ///< a key press puts the rotation back to 0
};

struct Binding {
    Kind kind;
    bool towardPointer = false;   ///< zoom: around the press point, not the center
    QList<Qt::Key> keys;          ///< all keys held, modifiers included (Qt::Key_Control...)
    Qt::MouseButtons buttons;     ///< drag kinds only
};

/// Zoom and rotate bindings of the current input profile (drag ones that
/// need at least one key, plus the rotation-reset keys). Falls back to
/// defaultBindings() when there is no profile.
KRITAUI_EXPORT QVector<Binding> currentBindings();

/// Ctrl+Space drag = zoom toward pointer, Ctrl+Alt+Space = stepped zoom,
/// Shift+Space = rotate, Shift+Alt+Space = stepped rotate, 5 = reset.
KRITAUI_EXPORT QVector<Binding> defaultBindings();

/// The keys held right now as Qt::Key values: the modifiers plus `otherKeys`
/// (non-modifier keys the caller tracked, like Space).
KRITAUI_EXPORT QSet<int> heldKeys(Qt::KeyboardModifiers modifiers, const QSet<int> &otherKeys);

/// Index of the drag binding started by pressing `button` with exactly
/// `held` keys down, or -1. Like the canvas, the key set must match exactly.
KRITAUI_EXPORT int matchDrag(const QVector<Binding> &bindings, const QSet<int> &held, Qt::MouseButton button);

/// Index of the RotateReset binding for exactly `held` keys, or -1.
KRITAUI_EXPORT int matchKey(const QVector<Binding> &bindings, const QSet<int> &held);

/// How far a zoom drag from `start` to `pos` went, in canvas "zoom pixels":
/// positive = zoom in. Follows the canvas settings (horizontal zoom drag,
/// inverted zoom). Smooth zoom = 2^(steps/100), stepped zoom = one step per 50.
KRITAUI_EXPORT qreal zoomDragSteps(const QPointF &start, const QPointF &pos);

/// Pure version of zoomDragSteps() for tests.
KRITAUI_EXPORT qreal zoomDragSteps(const QPointF &start, const QPointF &pos, bool horizontal, bool inverted);
}

#endif // GIGA_CANVAS_INPUT_KEYS_H

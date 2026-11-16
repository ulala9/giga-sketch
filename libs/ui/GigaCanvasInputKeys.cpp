/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaCanvasInputKeys.h"

#include "kis_config.h"
#include "input/kis_input_profile.h"
#include "input/kis_input_profile_manager.h"
#include "input/kis_rotate_canvas_action.h"
#include "input/kis_shortcut_configuration.h"
#include "input/kis_zoom_action.h"

namespace GigaCanvasInputKeys
{
namespace
{
Binding make(Kind kind, bool towardPointer, const QList<Qt::Key> &keys, Qt::MouseButtons buttons)
{
    Binding b;
    b.kind = kind;
    b.towardPointer = towardPointer;
    b.keys = keys;
    b.buttons = buttons;
    return b;
}

QSet<int> keySet(const QList<Qt::Key> &keys)
{
    QSet<int> set;
    for (Qt::Key k : keys) set.insert(int(k));
    return set;
}
} // namespace

QVector<Binding> defaultBindings()
{
    return {
        make(Kind::Zoom, true, {Qt::Key_Control, Qt::Key_Space}, Qt::LeftButton),
        make(Kind::ZoomDiscrete, true, {Qt::Key_Control, Qt::Key_Alt, Qt::Key_Space}, Qt::LeftButton),
        make(Kind::Rotate, false, {Qt::Key_Shift, Qt::Key_Space}, Qt::LeftButton),
        make(Kind::RotateDiscrete, false, {Qt::Key_Shift, Qt::Key_Alt, Qt::Key_Space}, Qt::LeftButton),
        make(Kind::RotateReset, false, {Qt::Key_5}, Qt::NoButton),
    };
}

QVector<Binding> currentBindings()
{
    KisInputProfileManager *manager = KisInputProfileManager::instance();
    KisInputProfile *profile = manager ? manager->currentProfile() : nullptr;
    if (!profile) return defaultBindings();

    QVector<Binding> result;
    Q_FOREACH (KisAbstractInputAction *action, manager->actions()) {
        const bool isZoom = dynamic_cast<KisZoomAction *>(action);
        const bool isRotate = dynamic_cast<KisRotateCanvasAction *>(action);
        if (!isZoom && !isRotate) continue;
        Q_FOREACH (KisShortcutConfiguration *s, profile->shortcutsForAction(action)) {
            const uint mode = s->mode();
            if (s->type() == KisShortcutConfiguration::MouseButtonType && !s->keys().isEmpty()) {
                // Key + drag. Button-only drags (middle drag...) stay the
                // widget's own (it pans with them).
                if (isZoom) {
                    if (mode == KisZoomAction::ZoomModeShortcut || mode == KisZoomAction::RelativeZoomModeShortcut) {
                        result.append(make(Kind::Zoom, mode == KisZoomAction::RelativeZoomModeShortcut, s->keys(), s->buttons()));
                    } else if (mode == KisZoomAction::DiscreteZoomModeShortcut
                               || mode == KisZoomAction::RelativeDiscreteZoomModeShortcut) {
                        result.append(make(Kind::ZoomDiscrete, mode == KisZoomAction::RelativeDiscreteZoomModeShortcut,
                                           s->keys(), s->buttons()));
                    }
                } else if (mode == KisRotateCanvasAction::RotateModeShortcut) {
                    result.append(make(Kind::Rotate, false, s->keys(), s->buttons()));
                } else if (mode == KisRotateCanvasAction::DiscreteRotateModeShortcut) {
                    result.append(make(Kind::RotateDiscrete, false, s->keys(), s->buttons()));
                }
            } else if (s->type() == KisShortcutConfiguration::KeyCombinationType && isRotate
                       && mode == KisRotateCanvasAction::RotateResetShortcut && !s->keys().isEmpty()) {
                result.append(make(Kind::RotateReset, false, s->keys(), Qt::NoButton));
            }
        }
    }
    return result;
}

QSet<int> heldKeys(Qt::KeyboardModifiers modifiers, const QSet<int> &otherKeys)
{
    QSet<int> held = otherKeys;
    if (modifiers & Qt::ControlModifier) held.insert(Qt::Key_Control);
    if (modifiers & Qt::ShiftModifier) held.insert(Qt::Key_Shift);
    if (modifiers & Qt::AltModifier) held.insert(Qt::Key_Alt);
    if (modifiers & Qt::MetaModifier) held.insert(Qt::Key_Meta);
    return held;
}

int matchDrag(const QVector<Binding> &bindings, const QSet<int> &held, Qt::MouseButton button)
{
    for (int i = 0; i < bindings.size(); ++i) {
        const Binding &b = bindings[i];
        if (b.kind == Kind::RotateReset) continue;
        if ((b.buttons & button) && keySet(b.keys) == held) return i;
    }
    return -1;
}

int matchKey(const QVector<Binding> &bindings, const QSet<int> &held)
{
    for (int i = 0; i < bindings.size(); ++i) {
        if (bindings[i].kind == Kind::RotateReset && keySet(bindings[i].keys) == held) return i;
    }
    return -1;
}

qreal zoomDragSteps(const QPointF &start, const QPointF &pos, bool horizontal, bool inverted)
{
    // Same as KisZoomAction: drag up (or right, with horizontal zoom) zooms in.
    const QPointF diff = -(pos - start);
    const qreal steps = horizontal ? -diff.x() : diff.y();
    return inverted ? -steps : steps;
}

qreal zoomDragSteps(const QPointF &start, const QPointF &pos)
{
    KisConfig cfg(true);
    return zoomDragSteps(start, pos, cfg.zoomHorizontal(), cfg.readEntry<bool>("InvertMiddleClickZoom", false));
}
} // namespace GigaCanvasInputKeys

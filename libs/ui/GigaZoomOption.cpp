/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaZoomOption.h"

#include <QAction>
#include <QSignalBlocker>

#include <klocalizedstring.h>

#include "input/kis_input_profile.h"
#include "input/kis_input_profile_manager.h"
#include "input/kis_shortcut_configuration.h"
#include "input/kis_zoom_action.h"

namespace
{
KisZoomAction *zoomAction()
{
    Q_FOREACH (KisAbstractInputAction *action, KisInputProfileManager::instance()->actions()) {
        if (KisZoomAction *zoom = dynamic_cast<KisZoomAction *>(action)) return zoom;
    }
    return nullptr;
}
}

uint GigaZoomOption::modeFor(uint currentMode, const QList<Qt::Key> &keys, bool towardPointer)
{
    if (!keys.contains(Qt::Key_Space)) return currentMode; // only the Ctrl+Space style bindings
    switch (currentMode) {
    case KisZoomAction::ZoomModeShortcut:
    case KisZoomAction::RelativeZoomModeShortcut:
        return towardPointer ? KisZoomAction::RelativeZoomModeShortcut : KisZoomAction::ZoomModeShortcut;
    case KisZoomAction::DiscreteZoomModeShortcut:
    case KisZoomAction::RelativeDiscreteZoomModeShortcut:
        return towardPointer ? KisZoomAction::RelativeDiscreteZoomModeShortcut : KisZoomAction::DiscreteZoomModeShortcut;
    default:
        return currentMode; // zoom in/out keys, fit, 100%...
    }
}

bool GigaZoomOption::isTowardPointer()
{
    KisInputProfile *profile = KisInputProfileManager::instance()->currentProfile();
    KisZoomAction *zoom = zoomAction();
    if (!profile || !zoom) return false;
    Q_FOREACH (KisShortcutConfiguration *shortcut, profile->shortcutsForAction(zoom)) {
        if (shortcut->keys().contains(Qt::Key_Space)
                && shortcut->mode() == uint(KisZoomAction::RelativeZoomModeShortcut)) {
            return true;
        }
    }
    return false;
}

void GigaZoomOption::setTowardPointer(bool towardPointer)
{
    KisInputProfileManager *manager = KisInputProfileManager::instance();
    KisInputProfile *profile = manager->currentProfile();
    KisZoomAction *zoom = zoomAction();
    if (!profile || !zoom) return;
    bool changed = false;
    Q_FOREACH (KisShortcutConfiguration *shortcut, profile->shortcutsForAction(zoom)) {
        const uint mode = modeFor(shortcut->mode(), shortcut->keys(), towardPointer);
        if (mode != shortcut->mode()) {
            shortcut->setMode(mode);
            changed = true;
        }
    }
    // Saves the profile to the user's data folder and makes the canvas
    // reload its bindings (same as the Canvas Input Settings page).
    if (changed) manager->saveProfiles();
}

QAction *GigaZoomOption::createAction(QObject *parent)
{
    QAction *action = new QAction(i18nc("@action:inmenu", "Ctrl+Space Zooms Toward Pointer"), parent);
    action->setCheckable(true);
    action->setToolTip(i18nc("@info:tooltip", "On: Ctrl+Space + drag zooms toward where you press, like the mouse wheel. "
                                              "Off: it zooms around the centre of the canvas."));
    action->setChecked(isTowardPointer());
    QObject::connect(action, &QAction::toggled, action, [](bool on) {
        if (on != isTowardPointer()) setTowardPointer(on);
    });
    // Follow the profile if it changes elsewhere (settings page, profile switch).
    QObject::connect(KisInputProfileManager::instance(), &KisInputProfileManager::currentProfileChanged, action, [action]() {
        const bool on = isTowardPointer();
        if (action->isChecked() != on) {
            QSignalBlocker blocker(action);
            action->setChecked(on);
        }
    });
    return action;
}

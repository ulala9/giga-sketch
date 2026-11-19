/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_ZOOM_MENU_H
#define GIGA_ZOOM_MENU_H

#include <QAction>
#include <QMenu>

#include <KoToolBase.h>

/**
 * Giga Sketch: the right-click menu of the Hand and Zoom tools (Photoshop-style):
 * Fit on Screen, Fit Width/Height, 100%, Zoom In/Out. It only lists Krita's own
 * view actions (found by name in the canvas's action collection), so their
 * names, shortcuts and behavior stay Krita's. Header-only so both the flake
 * Zoom tool and the Hand tool plugin can use it.
 */
inline QMenu *gigaFillZoomMenu(QMenu *menu, const KoToolBase *tool)
{
    if (!menu || !tool) return nullptr;
    menu->clear();
    const char *const groups[][4] = {
        {"zoom_to_fit", "zoom_to_fit_width", "zoom_to_fit_height", nullptr},
        {"zoom_to_100pct", nullptr, nullptr, nullptr},
        {"view_zoom_in", "view_zoom_out", nullptr, nullptr},
    };
    for (const auto &group : groups) {
        bool added = false;
        for (const char *name : group) {
            if (!name) break;
            if (QAction *action = tool->action(QLatin1String(name))) {
                menu->addAction(action);
                added = true;
            }
        }
        if (added) menu->addSeparator();
    }
    return menu->isEmpty() ? nullptr : menu;
}

#endif // GIGA_ZOOM_MENU_H

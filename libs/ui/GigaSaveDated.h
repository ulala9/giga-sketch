/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_SAVE_DATED_H
#define GIGA_SAVE_DATED_H

#include <QDate>
#include <QString>

#include "kritaui_export.h"

class KisMainWindow;

/**
 * Giga Sketch: File > Save Dated JPG.
 *
 * Saves a flattened JPG copy of the current image named after today's date
 * (2026-11-12.jpg; if taken, 2026-11-12_v2.jpg, _v3, ...) in the document's
 * folder. A never-saved image asks for a folder once and remembers it. The
 * document itself (its .kra, name and "modified" state) is not changed.
 * Built in from the user's "Save Dated JPG" Python extension.
 */
namespace GigaSaveDated
{
/// The first free "<date>.<ext>" / "<date>_vN.<ext>" path in @p folder.
KRITAUI_EXPORT QString nextDatedPath(const QString &folder, const QDate &date, const QString &extension = QStringLiteral("jpg"));

/// Save the active image of @p window as a dated JPG.
void saveDatedJpg(KisMainWindow *window);
}

#endif // GIGA_SAVE_DATED_H

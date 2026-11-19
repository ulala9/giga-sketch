/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_NOTE_ACTIONS_H
#define GIGA_NOTE_ACTIONS_H

#include <functional>

#include <QPointF>
#include <QString>
#include <QVector>

#include <kundo2magicstring.h>

#include "GigaNote.h"
#include "kritaui_export.h"

class QMenu;
class KisCanvas2;
class KisViewManager;
class KoSelection;

/**
 * Giga Sketch: the actions on sticky notes (GigaNote) that the Reference
 * Images tool and the Edit menu share. Interface thread only.
 */
namespace GigaNoteActions
{
/// Document points per screen pixel at the current zoom (rotation ignored).
KRITAUI_EXPORT qreal pointsPerScreenPixel(KisCanvas2 *canvas);

/// Where a new note goes: under the mouse pointer when it is over the
/// canvas, else the middle of the visible canvas. Document coordinates.
KRITAUI_EXPORT QPointF defaultPosition(KisCanvas2 *canvas);

/// Adds a note with its top-left corner at documentPos (one undo step) and
/// selects it on its own. The reference layer is created if needed.
/// startEditing: open the text box on it right away (an empty new note
/// disappears again if nothing is typed).
KRITAUI_EXPORT GigaNote *addNote(KisCanvas2 *canvas, const QPointF &documentPos,
                                 const QString &plainText = QString(), bool startEditing = false);

/// The selected notes (images in the selection are left out).
KRITAUI_EXPORT QVector<GigaNote *> selectedNotes(KoSelection *selection);

/// One undo step that applies edit() to the state of every note.
/// Does nothing when nothing changes.
KRITAUI_EXPORT void changeNotes(KisCanvas2 *canvas, const QVector<GigaNote *> &notes,
                                const KUndo2MagicString &undoText,
                                const std::function<void(GigaNote::State &)> &edit);

/// Sets the style of the notes (one undo step) and makes it the style for new notes.
KRITAUI_EXPORT void applyStyle(KisCanvas2 *canvas, const QVector<GigaNote *> &notes, int style);

/// Style, Color, Text Size and Collapse items for notes (used by the tool's
/// right-click menu).
KRITAUI_EXPORT void addNoteMenuItems(QMenu *menu, KisCanvas2 *canvas, const QVector<GigaNote *> &notes);

/// Copies of the notes, a little down and to the right (one undo step);
/// the copies become the selection.
KRITAUI_EXPORT void duplicateNotes(KisCanvas2 *canvas, const QVector<GigaNote *> &notes);

/// Edit > Add Note, from any tool: switches to the Reference Images tool and
/// adds a note at defaultPosition().
KRITAUI_EXPORT void addNoteFromMenu(KisViewManager *viewManager);

/// Keep readable notes of the canvas's document take this view's zoom
/// (same size on screen). Not an undo step: it is a view setting.
KRITAUI_EXPORT void applyKeepReadable(KisCanvas2 *canvas);
/// Calls applyKeepReadable() whenever the view pans/zooms/rotates (once per canvas).
KRITAUI_EXPORT void followZoom(KisCanvas2 *canvas);

/// View > Show Annotations: shows/hides every note and updates the menu check.
KRITAUI_EXPORT void setNotesShown(KisViewManager *viewManager, bool shown);

/// Asks for a custom color (QColorDialog). Returns an invalid color on cancel.
KRITAUI_EXPORT QColor askColor(const QColor &start);
}

#endif // GIGA_NOTE_ACTIONS_H

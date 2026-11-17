/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_NOTE_EDITOR_H
#define GIGA_NOTE_EDITOR_H

#include <QElapsedTimer>
#include <QPointer>
#include <QTextEdit>

#include "GigaNote.h"
#include "kritaui_export.h"

class KisCanvas2;
class KisDocument;

/**
 * Giga Sketch: the text box laid over a sticky note while you type in it.
 *
 * It is a child widget of the canvas, drawn at screen size and upright (even
 * on a rotated canvas). While it is open the note draws only its box; the
 * note's text and height follow the typing live. Esc, Ctrl+Enter or a click
 * outside finish it (a middle-button press, i.e. panning, does not), and the
 * whole edit becomes one undo step. Losing keyboard focus alone does not
 * finish it: menus and window switches move the focus around.
 *
 * While the box has the keyboard, it takes every key (so B or E type
 * letters instead of switching tools), except Ctrl+S / Ctrl+Shift+S.
 *
 * Interface thread only.
 */
class KRITAUI_EXPORT GigaNoteEditor : public QTextEdit
{
    Q_OBJECT
public:
    /// Opens the editor on note (finishing any other open one first).
    /// isNewNote: the note was just added; if it is still empty when the
    /// editor closes, it is removed (a normal remove step, never an undo).
    static GigaNoteEditor *edit(KisCanvas2 *canvas, GigaNote *note, bool isNewNote = false);
    /// The open editor, if any.
    static GigaNoteEditor *current();
    /// Closes the open editor (keeping the text).
    static void finishCurrent();

    GigaNote *note() const { return m_note; }

    /// Bold / italic / underline / strikethrough / bullet list toggles.
    void toggleBold();
    void toggleItalic();
    void toggleUnderline();
    void toggleStrikeOut();
    void toggleBulletList();

public Q_SLOTS:
    void finish();

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    GigaNoteEditor(KisCanvas2 *canvas, GigaNote *note, bool isNewNote);
    ~GigaNoteEditor() override;

    qreal screenPixelsPerUnit() const;
    void applyLook();
    void placeOnCanvas();
    void textChangedLive();
    bool noteStillExists() const;

    QPointer<KisCanvas2> m_canvas;
    QPointer<KisDocument> m_document;
    GigaNote *m_note = nullptr;
    GigaNote::State m_before;
    bool m_isNewNote = false;
    bool m_finishing = false;
    bool m_keepEvenIfEmpty = false; ///< closed by a click on the note itself (to move/resize it)
    QElapsedTimer m_opened;
};

#endif // GIGA_NOTE_EDITOR_H

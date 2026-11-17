/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_NOTE_H
#define GIGA_NOTE_H

#include <QColor>
#include <QMarginsF>
#include <QScopedPointer>
#include <QString>
#include <QVector>

#include <kundo2command.h>

#include "KisReferenceImage.h"
#include "kritaui_export.h"

class QTextDocument;
class KisReferenceImagesLayer;

/**
 * Giga Sketch: a sticky note ("Annotation" in the interface).
 *
 * A note is a special kind of reference image: it lives in the same hidden
 * reference layer, so moving, selecting, stacking, undo, sitting outside the
 * canvas and the Reference Board all work the same way. It draws a coloured
 * box with rich text instead of a picture, and saves itself as a
 * <giga-note> element, which stock Krita skips.
 *
 * Units: the text is laid out in "note units" (about one screen pixel at
 * the zoom the note was made at). contentScale() converts note units to
 * document points, so a note looks the same size on screen whatever the
 * document's resolution is. The shape's width is chosen by the user; the
 * height always follows the text.
 *
 * Everything here runs on the interface thread, except saveXml(), which
 * only reads plain values (it may run while saving in the background).
 */
class KRITAUI_EXPORT GigaNote : public KisReferenceImage
{
public:
    enum TextSize { Small = 0, Medium = 1, Large = 2 };
    /// Overall look; the note's colour applies in every style.
    enum Style { Sticky = 0, Card = 1, Bubble = 2, Outline = 3 };

    /// Everything about a note that the property commands change.
    struct KRITAUI_EXPORT State {
        QString html;
        QColor color;
        int textSize = Medium;
        bool collapsed = false;
        bool keepReadable = false;
        int style = Sticky;
        qreal width = 0.0; ///< note units (so it survives Keep readable rescaling)
        bool operator==(const State &o) const {
            return html == o.html && color == o.color && textSize == o.textSize
                && collapsed == o.collapsed && keepReadable == o.keepReadable && style == o.style
                && qFuzzyCompare(width + 1.0, o.width + 1.0);
        }
    };

    /// One undo step for content/colour/size/collapse/width changes of one
    /// or more notes.
    class KRITAUI_EXPORT StateCommand : public KUndo2Command
    {
    public:
        StateCommand(const KUndo2MagicString &text, const QVector<GigaNote *> &notes,
                     const QVector<State> &before, const QVector<State> &after,
                     KUndo2Command *parent = nullptr);
        /// The first redo() does nothing (use when the change is already
        /// applied, e.g. after live typing).
        void setSkipFirstRedo(bool skip) { m_skipRedo = skip; }
        void redo() override;
        void undo() override;
    private:
        QVector<GigaNote *> m_notes;
        QVector<State> m_before;
        QVector<State> m_after;
        bool m_skipRedo = false;
    };

    GigaNote();
    GigaNote(const GigaNote &rhs);
    ~GigaNote() override;

    /// A new empty note whose top-left corner is at documentPos.
    /// pointsPerScreenPixel = document points per screen pixel right now
    /// (so the note reads like normal interface text at this zoom).
    static GigaNote *create(const QPointF &documentPos, qreal pointsPerScreenPixel,
                            const QString &plainText = QString());

    KoShape *cloneShape() const override;
    bool isNote() const override { return true; }

    // --- content and look ----------------------------------------------------
    QString html() const;
    void setHtml(const QString &html);
    QString plainText() const;
    bool isEmpty() const;

    QColor color() const;
    void setColor(const QColor &color);
    /// Readable text colour on fillColor() (Outline: the note colour, lightened if dark).
    QColor textColor() const;
    /// What the box is filled with in this style (Outline: see-through dark).
    QColor fillColor() const;

    int style() const;
    void setStyle(int style);
    static QStringList styleNames();
    /// Style for new notes: the one picked last (KisConfig gigaNoteStyle).
    static int defaultStyle();
    static void setDefaultStyle(int style);

    int textSize() const;
    void setTextSize(int size);

    bool isCollapsed() const;
    void setCollapsed(bool collapsed);

    qreal contentScale() const;
    void setContentScale(qreal pointsPerUnit);

    /// Keep readable: the note stays the same size on screen at any zoom
    /// (its content scale follows the view; see GigaNoteActions::followZoom).
    bool keepReadable() const;
    void setKeepReadable(bool keep);

    State state() const;
    void setState(const State &state);

    /// While a note is being edited, the editor draws its text.
    void setEditing(bool editing);
    bool isEditing() const;

    /// The width is honoured (at least a minimum); the height follows the text.
    void setSize(const QSizeF &size) override;

    // --- geometry helpers (shape coordinates, i.e. document points before
    //     the shape's transform) ----------------------------------------------
    QRectF stripRect() const;   ///< darker band at the top, for dragging
    QRectF textRect() const;    ///< where the text is laid out
    qreal textPixelSize() const; ///< font size in note units

    // --- drawing, saving ------------------------------------------------------
    /// Does nothing once notes have their own top layer (GigaNotesDecoration),
    /// so the reference layer's pass (under the grid) skips them.
    void paint(QPainter &gc) const override;
    /// Draws the note in shape coordinates (used by GigaNotesDecoration).
    void paintOnTop(QPainter &gc) const;
    static void setDrawnOnTop(bool onTop);
    void paintInRect(QPainter &gc, const QRectF &target) const override;

    void saveXml(QDomDocument &document, QDomElement &parentElement, int id) override;
    bool saveImage(KoStore *store) const override;
    bool loadImage(KoStore *store) override;
    static GigaNote *fromXml(const QDomElement &elem);
    static QString xmlTagName();

    // --- presets, global options ----------------------------------------------
    static QVector<QColor> presetColors();
    static QStringList presetColorNames();
    static QColor defaultColor();

    /// View > Show Annotations (global, remembered in KisConfig).
    static bool notesShown();
    static void setNotesShown(bool shown, KisReferenceImagesLayer *layer);
    bool hiddenOnCanvas() const override;

    /// Notes of a layer, in layer order.
    static QVector<GigaNote *> notesOf(KisReferenceImagesLayer *layer);

    /// Builds a note-unit layout of the text (default font, wrap width). The
    /// editor uses the same settings so typing wraps where the note does.
    static void setupDocument(QTextDocument *doc, int textSize, qreal textWidthUnits);
    /// List indent in note units (the editor scales it to the screen).
    static qreal indentUnits();
    /// Removes explicit font sizes/families (pasted or written by toHtml),
    /// so the note's text size setting always wins.
    static void normalizeFonts(QTextDocument *doc);

private:
    void relayout();
    QTextDocument *layoutDocument() const;
    void paintNote(QPainter &gc, qreal scale) const;
    /// Space around the text in note units, for the style: left, top, right, bottom.
    QMarginsF insets() const;

    struct Private;
    QScopedPointer<Private> m_d;
};

#endif // GIGA_NOTE_H

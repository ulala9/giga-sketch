/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaNoteEditor.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTimer>
#include <cstdio>

#include <klocalizedstring.h>
#include <kundo2stack.h>

#include "GigaNoteActions.h"
#include "KisDocument.h"
#include "KisReferenceImagesLayer.h"
#include "KisView.h"
#include "kis_canvas2.h"
#include "kis_coordinates_converter.h"

namespace
{
QPointer<GigaNoteEditor> s_current;

/// toHtml() of a copy of doc with the explicit sizes/colours removed.
QString cleanHtml(const QTextDocument *doc)
{
    QScopedPointer<QTextDocument> copy(doc->clone());
    GigaNote::normalizeFonts(copy.data());
    return copy->isEmpty() ? QString() : copy->toHtml();
}
} // namespace

GigaNoteEditor *GigaNoteEditor::edit(KisCanvas2 *canvas, GigaNote *note, bool isNewNote)
{
    if (!canvas || !note || !canvas->canvasWidget()) return nullptr;
    if (s_current && s_current->m_note == note) return s_current;
    finishCurrent();
    GigaNoteEditor *editor = new GigaNoteEditor(canvas, note, isNewNote);
    s_current = editor;
    editor->show();
    editor->setFocus(Qt::OtherFocusReason);
    return editor;
}

GigaNoteEditor *GigaNoteEditor::current()
{
    return s_current;
}

void GigaNoteEditor::finishCurrent()
{
    if (s_current) s_current->finish();
}

GigaNoteEditor::GigaNoteEditor(KisCanvas2 *canvas, GigaNote *note, bool isNewNote)
    : QTextEdit(canvas->canvasWidget())
    , m_canvas(canvas)
    , m_document(canvas->imageView() ? canvas->imageView()->document() : nullptr)
    , m_note(note)
    , m_before(note->state())
    , m_isNewNote(isNewNote)
{
    setObjectName(QStringLiteral("GigaNoteEditor"));
    setFrameStyle(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAcceptRichText(true);
    setTabChangesFocus(false);
    setLineWrapMode(QTextEdit::FixedPixelWidth);


    note->setEditing(true); // the note now draws only its box

    applyLook();
    if (!m_before.html.isEmpty()) {
        setHtml(m_before.html);
        GigaNote::normalizeFonts(document()); // the editor's own (screen) size wins
    }
    moveCursor(QTextCursor::End);
    placeOnCanvas();

    connect(this, &QTextEdit::textChanged, this, &GigaNoteEditor::textChangedLive);
    // Watch for clicks outside only once the click that opened us (e.g. the
    // Add Note placement click, which still travels up to the canvas's
    // parents) is fully handled. m_opened adds a short grace period for the
    // mouse press a pen tap produces after its tablet press.
    m_opened.start();
    QTimer::singleShot(0, this, [this]() {
        if (!m_finishing) qApp->installEventFilter(this);
    });
    // Pan, zoom, rotate: follow the note.
    connect(canvas, &KisCanvas2::sigCanvasStateChanged, this, [this]() {
        applyLook();
        placeOnCanvas();
    });
}

GigaNoteEditor::~GigaNoteEditor()
{
    // Closed without finish() (e.g. the view went away): leave the note
    // drawable. Its text is already up to date.
    if (!m_finishing && noteStillExists()) m_note->setEditing(false);
}

bool GigaNoteEditor::noteStillExists() const
{
    if (!m_document || !m_note) return false;
    KisSharedPtr<KisReferenceImagesLayer> layer = m_document->referenceImagesLayer();
    return layer && layer->shapes().contains(m_note);
}

qreal GigaNoteEditor::screenPixelsPerUnit() const
{
    const qreal pointsPerPixel = GigaNoteActions::pointsPerScreenPixel(m_canvas);
    return pointsPerPixel > 0.0 ? m_note->contentScale() / pointsPerPixel : 1.0;
}

void GigaNoteEditor::applyLook()
{
    if (!noteStillExists()) return;
    const qreal z = screenPixelsPerUnit();

    // Same font as the note, scaled to the screen. Point size (not pixel
    // size) because it may be fractional.
    QFont font = QApplication::font();
    font.setHintingPreference(QFont::PreferNoHinting);
    const qreal pixels = m_note->textPixelSize() * z;
    font.setPointSizeF(qMax<qreal>(1.0, pixels * 72.0 / qMax(1, logicalDpiY())));
    document()->setDefaultFont(font);
    document()->setDocumentMargin(0.0);
    document()->setUseDesignMetrics(true);
    document()->setIndentWidth(GigaNote::indentUnits() * z);

    // The box's own fill; a see-through one (Outline) is shown over dark grey while typing.
    QColor background = m_note->fillColor();
    if (background.alpha() < 255) {
        const qreal a = background.alphaF();
        background = QColor(qRound(45 * (1 - a) + background.red() * a), qRound(45 * (1 - a) + background.green() * a),
                            qRound(45 * (1 - a) + background.blue() * a));
    }
    const QColor text = m_note->textColor();
    QPalette p = palette();
    p.setColor(QPalette::Base, background);
    p.setColor(QPalette::Window, background);
    p.setColor(QPalette::Text, text);
    QColor selection = text;
    selection.setAlphaF(0.25);
    p.setColor(QPalette::Highlight, selection);
    p.setColor(QPalette::HighlightedText, text);
    setPalette(p);
    viewport()->setPalette(p);
    viewport()->setAutoFillBackground(true);
}

void GigaNoteEditor::placeOnCanvas()
{
    if (!m_canvas || !noteStillExists()) return;
    const qreal z = screenPixelsPerUnit();
    const QRectF textRect = m_note->textRect();
    const qreal widthUnits = textRect.width() / m_note->contentScale();
    const int widthPx = qMax(20, qRound(widthUnits * z));
    setLineWrapColumnOrWidth(widthPx);

    // Upright at the corner where the note's text starts.
    const QPointF corner = m_note->absoluteTransformation().map(textRect.topLeft());
    const QPointF widgetPos = m_canvas->coordinatesConverter()->documentToWidget(corner);

    const int heightPx = qMax(qRound(document()->size().height()) + 2,
                              qRound(QFontMetricsF(document()->defaultFont()).lineSpacing()) + 2);
    setGeometry(QRect(widgetPos.toPoint(), QSize(widthPx + 2, heightPx)));
    verticalScrollBar()->setValue(0);
}

void GigaNoteEditor::textChangedLive()
{
    if (!noteStillExists()) return;
    // The note follows the typing, so its box grows with the text.
    m_note->update();
    m_note->setHtml(cleanHtml(document()));
    m_note->update();
    placeOnCanvas();
}

void GigaNoteEditor::finish()
{
    if (m_finishing) return;
    if (qEnvironmentVariableIsSet("GIGA_NOTE_TRACE")) fprintf(stderr, "GIGA_SELFTEST INFO editor finish()\n");
    m_finishing = true;
    if (s_current == this) s_current = nullptr;

    if (noteStillExists()) {
        m_note->setHtml(cleanHtml(document()));
        m_note->setEditing(false);
        m_note->update();
        const GigaNote::State after = m_note->state();

        if (m_isNewNote && m_note->isEmpty() && !m_keepEvenIfEmpty && m_canvas) {
            // Nothing typed into a new note: remove it. A normal remove
            // command, NOT undoStack()->undo(): the document's undo waits
            // (processing events, ignoring input) until the image is idle,
            // which froze the app on a big file right after the first note
            // had created the reference layer.
            if (KisSharedPtr<KisReferenceImagesLayer> layer = m_document->referenceImagesLayer()) {
                m_canvas->addCommand(layer->removeReferenceImages(m_document, {m_note}));
            }
        } else if (!(after == m_before) && m_canvas) {
            auto *cmd = new GigaNote::StateCommand(kundo2_i18n("Edit Note"), {m_note}, {m_before}, {after});
            cmd->setSkipFirstRedo(true); // already applied while typing
            m_canvas->addCommand(cmd);
        }
    }

    qApp->removeEventFilter(this);
    QWidget *canvasWidget = parentWidget();
    hide();
    deleteLater();
    if (canvasWidget) canvasWidget->setFocus(Qt::OtherFocusReason);
}

bool GigaNoteEditor::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        // Take every key while typing (B, E, Space... must not switch tools
        // or pan), but let saving through.
        QKeyEvent *key = static_cast<QKeyEvent *>(event);
        if (!key->matches(QKeySequence::Save) && !key->matches(QKeySequence::SaveAs)) {
            event->accept();
            return true;
        }
    }
    return QTextEdit::event(event);
}

void GigaNoteEditor::keyPressEvent(QKeyEvent *event)
{
    const Qt::KeyboardModifiers mods = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
    const bool ctrl = mods == Qt::ControlModifier;
    const bool ctrlShift = mods == (Qt::ControlModifier | Qt::ShiftModifier);

    if (event->key() == Qt::Key_Escape
        || (ctrl && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter))) {
        event->accept();
        finish();
        return;
    }
    if (ctrl && event->key() == Qt::Key_B) { toggleBold(); return; }
    if (ctrl && event->key() == Qt::Key_I) { toggleItalic(); return; }
    if (ctrl && event->key() == Qt::Key_U) { toggleUnderline(); return; }
    if (ctrlShift && event->key() == Qt::Key_X) { toggleStrikeOut(); return; }
    if (ctrlShift && (event->key() == Qt::Key_L || event->key() == Qt::Key_8 || event->key() == Qt::Key_Asterisk)) {
        toggleBulletList();
        return;
    }
    QTextEdit::keyPressEvent(event);
}

bool GigaNoteEditor::eventFilter(QObject *watched, QEvent *event)
{
    // Application-wide: a click anywhere outside the box finishes it (the
    // click itself still goes where it was aimed).
    if (m_finishing) return false;
    const QEvent::Type type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::TabletPress) return false;
    if (type == QEvent::MouseButtonPress && static_cast<QMouseEvent *>(event)->button() == Qt::MiddleButton) {
        return false; // panning
    }
    if (type == QEvent::TabletPress && static_cast<QTabletEvent *>(event)->button() == Qt::MiddleButton) {
        return false;
    }
    if (m_opened.elapsed() < 250) return false; // the press that opened us
    QWidget *widget = qobject_cast<QWidget *>(watched);
    if (!widget) return false; // the same press also arrives for its QWindow
    if (widget == this || isAncestorOf(widget)) return false;
    if (QApplication::activePopupWidget()) return false; // our right-click menu

    // A click on the note itself (or its resize handles just outside it)
    // means "move or resize it": finish typing but keep the note, even
    // if it is still empty. The click goes on to the tool as usual.
    if (widget == parentWidget() && m_canvas && noteStillExists()) {
        QPointF pos;
        if (type == QEvent::MouseButtonPress) pos = static_cast<QMouseEvent *>(event)->position();
        else pos = static_cast<QTabletEvent *>(event)->position();
        const QPointF doc = m_canvas->coordinatesConverter()->widgetToDocument(pos);
        const QPointF local = m_note->absoluteTransformation().inverted().map(doc);
        const qreal margin = 14.0 * GigaNoteActions::pointsPerScreenPixel(m_canvas);
        if (QRectF(QPointF(), m_note->size()).adjusted(-margin, -margin, margin, margin).contains(local)) {
            m_keepEvenIfEmpty = true;
        }
    }
    if (qEnvironmentVariableIsSet("GIGA_NOTE_TRACE")) {
        fprintf(stderr, "GIGA_SELFTEST INFO editor closed by press on %s (%s) type %d\n",
                watched->metaObject()->className(), qPrintable(watched->objectName()), int(type));
    }
    QTimer::singleShot(0, this, &GigaNoteEditor::finish);
    return false;
}

void GigaNoteEditor::contextMenuEvent(QContextMenuEvent *event)
{
    QScopedPointer<QMenu> menu(createStandardContextMenu(event->pos()));
    menu->addSeparator();
    const QTextCharFormat format = currentCharFormat();
    auto addToggle = [&](const QString &text, const QKeySequence &keys, bool checked, void (GigaNoteEditor::*slot)()) {
        QAction *action = menu->addAction(text);
        action->setShortcut(keys);
        action->setCheckable(true);
        action->setChecked(checked);
        connect(action, &QAction::triggered, this, slot);
    };
    addToggle(i18n("Bold"), QKeySequence(Qt::CTRL | Qt::Key_B), format.fontWeight() > QFont::Normal, &GigaNoteEditor::toggleBold);
    addToggle(i18n("Italic"), QKeySequence(Qt::CTRL | Qt::Key_I), format.fontItalic(), &GigaNoteEditor::toggleItalic);
    addToggle(i18n("Underline"), QKeySequence(Qt::CTRL | Qt::Key_U), format.fontUnderline(), &GigaNoteEditor::toggleUnderline);
    addToggle(i18n("Strikethrough"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X), format.fontStrikeOut(),
              &GigaNoteEditor::toggleStrikeOut);
    addToggle(i18n("Bullet List"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L), textCursor().currentList() != nullptr,
              &GigaNoteEditor::toggleBulletList);
    menu->addSeparator();
    QAction *done = menu->addAction(i18n("Done"));
    done->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(done, &QAction::triggered, this, &GigaNoteEditor::finish, Qt::QueuedConnection);
    menu->exec(event->globalPos());
}

void GigaNoteEditor::toggleBold()
{
    QTextCharFormat format;
    format.setFontWeight(currentCharFormat().fontWeight() > QFont::Normal ? QFont::Normal : QFont::Bold);
    mergeCurrentCharFormat(format);
}

void GigaNoteEditor::toggleItalic()
{
    QTextCharFormat format;
    format.setFontItalic(!currentCharFormat().fontItalic());
    mergeCurrentCharFormat(format);
}

void GigaNoteEditor::toggleUnderline()
{
    QTextCharFormat format;
    format.setFontUnderline(!currentCharFormat().fontUnderline());
    mergeCurrentCharFormat(format);
}

void GigaNoteEditor::toggleStrikeOut()
{
    QTextCharFormat format;
    format.setFontStrikeOut(!currentCharFormat().fontStrikeOut());
    mergeCurrentCharFormat(format);
}

void GigaNoteEditor::toggleBulletList()
{
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    if (QTextList *list = cursor.currentList()) {
        // Take the selected lines out of the list.
        const int start = qMin(cursor.anchor(), cursor.position());
        const int end = qMax(cursor.anchor(), cursor.position());
        for (QTextBlock block = document()->findBlock(start); block.isValid() && block.position() <= end;
             block = block.next()) {
            if (block.textList() == list) list->remove(block);
            QTextCursor blockCursor(block);
            QTextBlockFormat format = block.blockFormat();
            format.setIndent(0);
            blockCursor.setBlockFormat(format);
        }
    } else {
        cursor.createList(QTextListFormat::ListDisc);
    }
    cursor.endEditBlock();
}

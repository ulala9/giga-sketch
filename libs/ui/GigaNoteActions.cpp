/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaNoteActions.h"
#include "GigaNoteEditor.h"

#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QCursor>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QtMath>

#include <klocalizedstring.h>
#include <KoSelection.h>
#include <KoShapeManager.h>
#include <KoToolManager.h>

#include "KisDocument.h"
#include "KisReferenceImagesLayer.h"
#include "KisView.h"
#include "KisViewManager.h"
#include "kis_canvas2.h"
#include "kis_coordinates_converter.h"
#include "kis_icon_utils.h"
#include "kis_action.h"
#include "kis_action_manager.h"

namespace
{
QIcon swatchIcon(const QColor &color)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter gc(&pixmap);
    gc.setRenderHint(QPainter::Antialiasing);
    gc.setPen(color.darker(150));
    gc.setBrush(color);
    gc.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 3, 3);
    return QIcon(pixmap);
}

KisDocument *documentOf(KisCanvas2 *canvas)
{
    return (canvas && canvas->imageView()) ? canvas->imageView()->document() : nullptr;
}
} // namespace

namespace GigaNoteActions
{

qreal pointsPerScreenPixel(KisCanvas2 *canvas)
{
    if (!canvas || !canvas->coordinatesConverter()) return 1.0;
    // The transform's determinant is the area scale; its square root is the
    // length scale whatever the rotation or mirroring.
    const QTransform t = canvas->coordinatesConverter()->documentToWidgetTransform();
    const qreal pixelsPerPoint = qSqrt(qAbs(t.determinant()));
    return pixelsPerPoint > 1e-9 ? 1.0 / pixelsPerPoint : 1.0;
}

QPointF defaultPosition(KisCanvas2 *canvas)
{
    if (!canvas || !canvas->canvasWidget() || !canvas->coordinatesConverter()) return QPointF();
    QWidget *widget = canvas->canvasWidget();
    QPointF widgetPos = widget->mapFromGlobal(QCursor::pos());
    if (!widget->rect().contains(widgetPos.toPoint())) {
        // Middle of the view, moved up-left so the note's middle lands there.
        const qreal pad = 110.0;
        widgetPos = QPointF(widget->width() / 2.0 - pad, widget->height() / 2.0 - pad / 3.0);
    }
    return canvas->coordinatesConverter()->widgetToDocument(widgetPos);
}

GigaNote *addNote(KisCanvas2 *canvas, const QPointF &documentPos, const QString &plainText, bool startEditing)
{
    KisDocument *document = documentOf(canvas);
    if (!document) return nullptr;

    if (!GigaNote::notesShown()) setNotesShown(canvas->viewManager(), true); // a new note must be seen
    GigaNote *note = GigaNote::create(documentPos, pointsPerScreenPixel(canvas), plainText);
    if (document->referenceImagesLayer()) {
        note->setZIndex(document->referenceImagesLayer()->shapes().size());
    }
    canvas->addCommand(KisReferenceImagesLayer::addReferenceImages(document, {note}));
    followZoom(canvas);

    if (KisSharedPtr<KisReferenceImagesLayer> layer = document->referenceImagesLayer()) {
        if (KoShapeManager *manager = layer->shapeManager()) {
            manager->selection()->deselectAll();
            if (note->isVisible()) manager->selection()->select(note);
        }
    }
    if (startEditing && note->isVisible()) GigaNoteEditor::edit(canvas, note, true);
    return note;
}

QVector<GigaNote *> selectedNotes(KoSelection *selection)
{
    QVector<GigaNote *> notes;
    if (!selection) return notes;
    Q_FOREACH (KoShape *shape, selection->selectedEditableShapes()) {
        auto *reference = dynamic_cast<KisReferenceImage *>(shape);
        if (reference && reference->isNote()) notes.append(static_cast<GigaNote *>(reference));
    }
    return notes;
}

void changeNotes(KisCanvas2 *canvas, const QVector<GigaNote *> &notes, const KUndo2MagicString &undoText,
                 const std::function<void(GigaNote::State &)> &edit)
{
    if (!canvas || notes.isEmpty()) return;
    QVector<GigaNote::State> before;
    QVector<GigaNote::State> after;
    bool changed = false;
    for (GigaNote *note : notes) {
        GigaNote::State state = note->state();
        before.append(state);
        edit(state);
        changed |= !(state == before.last());
        after.append(state);
    }
    if (!changed) return;
    // addCommand runs redo(), which applies the new states.
    canvas->addCommand(new GigaNote::StateCommand(undoText, notes, before, after));
}

void applyStyle(KisCanvas2 *canvas, const QVector<GigaNote *> &notes, int style)
{
    GigaNote::setDefaultStyle(style); // new notes use the style picked last
    changeNotes(canvas, notes, kundo2_i18n("Change Note Style"), [style](GigaNote::State &s) { s.style = style; });
}

QColor askColor(const QColor &start)
{
    return QColorDialog::getColor(start, nullptr, i18n("Note Color"));
}

void addNoteMenuItems(QMenu *menu, KisCanvas2 *canvas, const QVector<GigaNote *> &notes)
{
    if (!menu || !canvas || notes.isEmpty()) return;
    // The menu is rebuilt every time it opens, so the actions live in it.
    QMenu *colorMenu = menu->addMenu(i18n("Note Color"));
    const QVector<QColor> colors = GigaNote::presetColors();
    const QStringList names = GigaNote::presetColorNames();
    for (int i = 0; i < colors.size(); ++i) {
        const QColor color = colors[i];
        QAction *action = colorMenu->addAction(swatchIcon(color), names.value(i));
        QObject::connect(action, &QAction::triggered, canvas, [canvas, notes, color]() {
            changeNotes(canvas, notes, kundo2_i18n("Change Note Color"),
                        [color](GigaNote::State &s) { s.color = color; });
        });
    }
    colorMenu->addSeparator();
    QAction *custom = colorMenu->addAction(i18n("Custom..."));
    QObject::connect(custom, &QAction::triggered, canvas, [canvas, notes]() {
        const QColor color = askColor(notes.first()->color());
        if (!color.isValid()) return;
        changeNotes(canvas, notes, kundo2_i18n("Change Note Color"),
                    [color](GigaNote::State &s) { s.color = color; });
    });

    QMenu *styleMenu = menu->addMenu(i18n("Note Style"));
    QActionGroup *styleGroup = new QActionGroup(styleMenu);
    const QStringList styleNames = GigaNote::styleNames();
    for (int style = GigaNote::Sticky; style <= GigaNote::Outline; ++style) {
        QAction *action = styleMenu->addAction(styleNames.value(style));
        action->setCheckable(true);
        action->setActionGroup(styleGroup);
        bool all = true;
        for (GigaNote *note : notes) all &= note->style() == style;
        action->setChecked(all);
        QObject::connect(action, &QAction::triggered, canvas, [canvas, notes, style]() { applyStyle(canvas, notes, style); });
    }

    QMenu *sizeMenu = menu->addMenu(i18n("Note Text Size"));
    QActionGroup *group = new QActionGroup(sizeMenu);
    const QStringList sizeNames = {i18nc("note text size", "Small"), i18nc("note text size", "Medium"),
                                   i18nc("note text size", "Large")};
    for (int size = GigaNote::Small; size <= GigaNote::Large; ++size) {
        QAction *action = sizeMenu->addAction(sizeNames[size]);
        action->setCheckable(true);
        action->setActionGroup(group);
        bool all = true;
        for (GigaNote *note : notes) all &= note->textSize() == size;
        action->setChecked(all);
        QObject::connect(action, &QAction::triggered, canvas, [canvas, notes, size]() {
            changeNotes(canvas, notes, kundo2_i18n("Change Note Text Size"),
                        [size](GigaNote::State &s) { s.textSize = size; });
        });
    }

    bool allCollapsed = true;
    for (GigaNote *note : notes) allCollapsed &= note->isCollapsed();
    QAction *collapse = menu->addAction(allCollapsed ? i18n("Expand Note") : i18n("Collapse Note"));
    QObject::connect(collapse, &QAction::triggered, canvas, [canvas, notes, allCollapsed]() {
        changeNotes(canvas, notes, allCollapsed ? kundo2_i18n("Expand Note") : kundo2_i18n("Collapse Note"),
                    [allCollapsed](GigaNote::State &s) { s.collapsed = !allCollapsed; });
    });

    bool allReadable = true;
    for (GigaNote *note : notes) allReadable &= note->keepReadable();
    QAction *readable = menu->addAction(i18n("Keep Readable at Any Zoom"));
    readable->setCheckable(true);
    readable->setChecked(allReadable);
    readable->setToolTip(i18n("The note stays the same size on screen when you zoom"));
    QObject::connect(readable, &QAction::toggled, canvas, [canvas, notes](bool keep) {
        changeNotes(canvas, notes, keep ? kundo2_i18n("Keep Note Readable") : kundo2_i18n("Note Follows Zoom"),
                    [keep](GigaNote::State &s) { s.keepReadable = keep; });
        applyKeepReadable(canvas);
    });

    QAction *duplicate = menu->addAction(KisIconUtils::loadIcon("duplicatelayer"), i18n("Duplicate Note"));
    QObject::connect(duplicate, &QAction::triggered, canvas, [canvas, notes]() { duplicateNotes(canvas, notes); });
}

void duplicateNotes(KisCanvas2 *canvas, const QVector<GigaNote *> &notes)
{
    KisDocument *document = documentOf(canvas);
    if (!document || notes.isEmpty()) return;
    const qreal offset = 16.0 * pointsPerScreenPixel(canvas);
    int z = document->referenceImagesLayer() ? document->referenceImagesLayer()->shapes().size() : 0;
    QList<KoShape *> copies;
    for (GigaNote *note : notes) {
        GigaNote *copy = static_cast<GigaNote *>(note->cloneShape());
        copy->setOnBoard(false);
        copy->setPosition(note->position() + QPointF(offset, offset));
        copy->setZIndex(z++);
        copies.append(copy);
    }
    canvas->addCommand(KisReferenceImagesLayer::addReferenceImages(document, copies));
    if (KisSharedPtr<KisReferenceImagesLayer> layer = document->referenceImagesLayer()) {
        KoSelection *selection = layer->shapeManager()->selection();
        selection->deselectAll();
        for (KoShape *copy : copies) {
            if (copy->isVisible()) selection->select(copy);
        }
    }
}

void applyKeepReadable(KisCanvas2 *canvas)
{
    KisDocument *document = documentOf(canvas);
    if (!document || !document->referenceImagesLayer()) return;
    const qreal target = pointsPerScreenPixel(canvas);
    for (GigaNote *note : GigaNote::notesOf(document->referenceImagesLayer().data())) {
        if (!note->keepReadable() || note->isOnBoard()) continue;
        if (qAbs(note->contentScale() - target) <= target * 1e-4) continue;
        note->update(); // old area
        note->setContentScale(target);
        note->update();
    }
}

void followZoom(KisCanvas2 *canvas)
{
    if (!canvas || canvas->property("gigaNotesFollowZoom").toBool()) return;
    canvas->setProperty("gigaNotesFollowZoom", true);
    QObject::connect(canvas, &KisCanvas2::sigCanvasStateChanged, canvas, [canvas]() { applyKeepReadable(canvas); });
}

void setNotesShown(KisViewManager *viewManager, bool shown)
{
    if (shown != GigaNote::notesShown()) {
        if (!shown) GigaNoteEditor::finishCurrent();
        GigaNote::setNotesShown(shown, nullptr);
    }
    if (viewManager && viewManager->actionManager()) {
        if (KisAction *action = viewManager->actionManager()->actionByName(QStringLiteral("giga_show_annotations"))) {
            const bool blocked = action->blockSignals(true);
            action->setChecked(shown);
            action->blockSignals(blocked);
        }
    }
}

void addNoteFromMenu(KisViewManager *viewManager)
{
    if (!viewManager || !viewManager->canvasBase()) return;
    KisCanvas2 *canvas = viewManager->canvasBase();
    const QPointF position = defaultPosition(canvas); // before the tool switch moves focus
    // Switching to the tool it is already in would restart the tool, which
    // closes the text box we are about to open.
    if (KoToolManager::instance()->activeToolId() != QLatin1String("ToolReferenceImages")) {
        KoToolManager::instance()->switchToolRequested(QStringLiteral("ToolReferenceImages"));
    }
    addNote(canvas, position, QString(), true);
}

} // namespace GigaNoteActions

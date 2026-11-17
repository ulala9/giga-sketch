/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaReferenceBoard.h"

#include "KisReferenceImage.h"
#include "KisReferenceImagesLayer.h"
#include "kis_canvas2.h"
#include "kis_config.h"

#include <KoSelection.h>
#include <KoShapeManager.h>
#include <kis_image.h>

#include <klocalizedstring.h>

#include <QEasingCurve>
#include <QSharedPointer>
#include <QVariantAnimation>
#include <QtMath>

#include <atomic>
#include <numeric>

namespace GigaReferenceBoard
{
namespace
{
// Mirrors of settings / panel state read while saving (maybe off the
// interface thread). -1 = not read yet.
std::atomic<int> s_shrinkOnSave{-1};
std::atomic<int> s_boardWidth{0};
std::atomic<int> s_boardHeight{0};

constexpr qreal RESAMPLE_HEADROOM = 1.5;

KisReferenceImagesLayer *layerOf(const KisReferenceImage *image)
{
    return image ? dynamic_cast<KisReferenceImagesLayer *>(image->parent()) : nullptr;
}

QTransform lerp(const QTransform &a, const QTransform &b, qreal t)
{
    auto m = [t](qreal x, qreal y) { return x + (y - x) * t; };
    return QTransform(m(a.m11(), b.m11()), m(a.m12(), b.m12()), m(a.m13(), b.m13()),
                      m(a.m21(), b.m21()), m(a.m22(), b.m22()), m(a.m23(), b.m23()),
                      m(a.m31(), b.m31()), m(a.m32(), b.m32()), m(a.m33(), b.m33()));
}

/// Maps rect `from` onto rect `to` (uniform scale by width, then move).
QTransform rectToRect(const QRectF &from, const QRectF &to)
{
    const qreal f = from.width() > 0.0 ? to.width() / from.width() : 1.0;
    return QTransform::fromTranslate(-from.x(), -from.y()) * QTransform::fromScale(f, f)
        * QTransform::fromTranslate(to.x(), to.y());
}

GeometryCommand *makeCommand(const KUndo2MagicString &text, const QVector<KisReferenceImage *> &images,
                             const QVector<ImageState> &before, const QVector<ImageState> &after)
{
    bool changed = false;
    for (int i = 0; i < images.size() && !changed; ++i) {
        changed = before[i].onBoard != after[i].onBoard || before[i].boardRect != after[i].boardRect
            || !qFuzzyCompare(before[i].transform, after[i].transform);
    }
    if (!changed) return nullptr;
    return new GeometryCommand(text, images, before, after);
}

QVector<ImageState> statesOf(const QVector<KisReferenceImage *> &images)
{
    QVector<ImageState> out;
    out.reserve(images.size());
    for (KisReferenceImage *image : images) out.append(stateOf(image));
    return out;
}

QVector<QRectF> boardRectsOf(const QVector<KisReferenceImage *> &images)
{
    QVector<QRectF> out;
    for (KisReferenceImage *image : images) out.append(image->boardRect());
    return out;
}

GigaReferenceLayout::Options layoutOptions(const Settings &settings, qreal aspect)
{
    GigaReferenceLayout::Options options;
    options.style = settings.style;
    options.spacing = settings.spacing;
    options.targetAspect = aspect;
    return options;
}

/// Lays `rects` (current places) out again, keeping the group's top-left.
QVector<QRectF> rearranged(const QVector<KisReferenceImage *> &images, const QVector<QRectF> &rects,
                           const Settings &settings, qreal aspect, KisReferenceImagesLayer *layer)
{
    const QVector<int> order = orderFor(images, rects, settings.order, layer);
    QVector<QSizeF> sizes;
    for (int i : order) sizes.append(rects[i].size());
    const GigaReferenceLayout::Result result = GigaReferenceLayout::arrange(sizes, layoutOptions(settings, aspect));

    const QPointF origin = GigaReferenceLayout::bounds(rects).topLeft();
    QVector<QRectF> out(rects.size());
    for (int k = 0; k < order.size(); ++k) out[order[k]] = result.rects[k].translated(origin);
    return out;
}
// Sticky notes (GigaNote) keep their own size: their width is the user's
// and their height follows the text.
QVector<KisReferenceImage *> withoutNotes(const QVector<KisReferenceImage *> &images)
{
    QVector<KisReferenceImage *> pictures;
    for (KisReferenceImage *image : images) {
        if (!image->isNote()) pictures.append(image);
    }
    return pictures;
}

} // namespace

// ---- Settings ---------------------------------------------------------------

Settings Settings::load()
{
    KisConfig cfg(true);
    Settings s;
    s.normalizeMode = GigaReferenceLayout::NormalizeMode(qBound(0, cfg.readEntry<int>("gigaRefBoardNormalize", 0), 3));
    s.style = GigaReferenceLayout::Style(qBound(0, cfg.readEntry<int>("gigaRefBoardStyle", 0), 4));
    s.order = Order(qBound(0, cfg.readEntry<int>("gigaRefBoardOrder", 0), 3));
    s.targetShape = TargetShape(qBound(0, cfg.readEntry<int>("gigaRefBoardShape", int(TargetShape::Board)), 2));
    s.spacing = qBound(0, cfg.readEntry<int>("gigaRefBoardSpacing", 16), 500);
    const QColor bg(cfg.readEntry<QString>("gigaRefBoardBackground", QStringLiteral("#262626")));
    if (bg.isValid()) s.background = bg;
    s.shadows = cfg.readEntry<bool>("gigaRefBoardShadows", true);
    s.roundedCorners = cfg.readEntry<bool>("gigaRefBoardRounded", true);
    s.captions = cfg.readEntry<bool>("gigaRefBoardCaptions", false);
    s.flipWithCanvas = cfg.readEntry<bool>("gigaRefBoardFlipWithCanvas", true);
    s.rotateWithCanvas = cfg.readEntry<bool>("gigaRefBoardRotateWithCanvas", true);
    s.dimOthers = cfg.readEntry<bool>("gigaRefBoardDim", true);
    s.shrinkOnSave = cfg.readEntry<bool>("gigaRefBoardShrinkOnSave", false);
    s.keepNotesInPlace = cfg.readEntry<bool>("gigaRefBoardKeepNotes", true);
    s_shrinkOnSave = s.shrinkOnSave ? 1 : 0;
    return s;
}

void Settings::save() const
{
    KisConfig cfg(false);
    cfg.writeEntry<int>("gigaRefBoardNormalize", int(normalizeMode));
    cfg.writeEntry<int>("gigaRefBoardStyle", int(style));
    cfg.writeEntry<int>("gigaRefBoardOrder", int(order));
    cfg.writeEntry<int>("gigaRefBoardShape", int(targetShape));
    cfg.writeEntry<int>("gigaRefBoardSpacing", spacing);
    cfg.writeEntry<QString>("gigaRefBoardBackground", background.name());
    cfg.writeEntry<bool>("gigaRefBoardShadows", shadows);
    cfg.writeEntry<bool>("gigaRefBoardRounded", roundedCorners);
    cfg.writeEntry<bool>("gigaRefBoardCaptions", captions);
    cfg.writeEntry<bool>("gigaRefBoardFlipWithCanvas", flipWithCanvas);
    cfg.writeEntry<bool>("gigaRefBoardRotateWithCanvas", rotateWithCanvas);
    cfg.writeEntry<bool>("gigaRefBoardKeepNotes", keepNotesInPlace);
    cfg.writeEntry<bool>("gigaRefBoardDim", dimOthers);
    cfg.writeEntry<bool>("gigaRefBoardShrinkOnSave", shrinkOnSave);
    s_shrinkOnSave = shrinkOnSave ? 1 : 0;
    Q_EMIT Notifier::instance()->settingsChanged();
}

// ---- Commands ---------------------------------------------------------------

ImageState stateOf(const KisReferenceImage *image)
{
    ImageState s;
    s.transform = image->transformation();
    s.onBoard = image->isOnBoard();
    s.boardRect = image->boardRect();
    return s;
}

GeometryCommand::GeometryCommand(const KUndo2MagicString &text, const QVector<KisReferenceImage *> &images,
                                 const QVector<ImageState> &before, const QVector<ImageState> &after,
                                 KUndo2Command *parent)
    : KUndo2Command(text, parent)
    , m_images(images)
    , m_before(before)
    , m_after(after)
{
}

void GeometryCommand::redo()
{
    apply(m_after);
}

void GeometryCommand::undo()
{
    apply(m_before);
}

void GeometryCommand::apply(const QVector<ImageState> &states)
{
    for (int i = 0; i < m_images.size(); ++i) {
        KisReferenceImage *image = m_images[i];
        const ImageState &s = states[i];
        image->update(); // old area on the canvas
        if (!qFuzzyCompare(image->transformation(), s.transform)) image->setTransformation(s.transform);
        image->setBoardRect(s.boardRect);
        if (image->isOnBoard() != s.onBoard) {
            if (s.onBoard) {
                if (KisReferenceImagesLayer *layer = layerOf(image)) {
                    layer->shapeManager()->selection()->deselect(image);
                }
            }
            image->setOnBoard(s.onBoard);
        }
        image->update(); // new area
    }
    Notifier::instance()->notify();
}

ResampleCommand::ResampleCommand(const QVector<KisReferenceImage *> &images, const QVector<QSize> &targets,
                                 KUndo2Command *parent)
    : KUndo2Command(kundo2_i18n("Resample Reference Images"), parent)
    , m_images(images)
{
    for (int i = 0; i < images.size(); ++i) {
        const QImage old = images[i]->getImage();
        m_old.append(old);
        m_new.append(old.scaled(targets[i], Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }
}

void ResampleCommand::redo()
{
    for (int i = 0; i < m_images.size(); ++i) {
        m_images[i]->setImage(m_new[i]);
        m_images[i]->update();
    }
    Notifier::instance()->notify();
}

void ResampleCommand::undo()
{
    for (int i = 0; i < m_images.size(); ++i) {
        m_images[i]->setImage(m_old[i]);
        m_images[i]->update();
    }
    Notifier::instance()->notify();
}

Notifier *Notifier::instance()
{
    static Notifier *s_instance = new Notifier();
    return s_instance;
}

// ---- Helpers ----------------------------------------------------------------

QSizeF displayedSize(const KisReferenceImage *image)
{
    const QTransform t = image->absoluteTransformation();
    const qreal sx = qSqrt(t.m11() * t.m11() + t.m12() * t.m12());
    const qreal sy = qSqrt(t.m21() * t.m21() + t.m22() * t.m22());
    return QSizeF(image->size().width() * sx, image->size().height() * sy);
}

QVector<KisReferenceImage *> canvasImages(KisReferenceImagesLayer *layer)
{
    QVector<KisReferenceImage *> out;
    if (!layer) return out;
    for (KisReferenceImage *image : layer->referenceImages()) {
        if (!image->isOnBoard()) out.append(image);
    }
    return out;
}

QVector<KisReferenceImage *> boardImages(KisReferenceImagesLayer *layer)
{
    QVector<KisReferenceImage *> out;
    if (!layer) return out;
    for (KisReferenceImage *image : layer->referenceImages()) {
        if (image->isOnBoard()) out.append(image);
    }
    return out;
}

QVector<int> orderFor(const QVector<KisReferenceImage *> &images, const QVector<QRectF> &currentRects, Order order,
                      KisReferenceImagesLayer *layer)
{
    QVector<int> idx(images.size());
    std::iota(idx.begin(), idx.end(), 0);
    switch (order) {
    case Order::Current:
        return GigaReferenceLayout::readingOrder(currentRects);
    case Order::Colour: {
        // Colourful images by hue, then greys from dark to light.
        auto key = [&](int i) {
            const QColor c = images[i]->averageColor().toHsv();
            const bool grey = c.hsvSaturationF() < 0.15 || c.hsvHueF() < 0.0;
            return qMakePair(grey ? 1.0 : 0.0, grey ? c.valueF() : c.hsvHueF());
        };
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return key(a) < key(b); });
        return idx;
    }
    case Order::Shape: {
        auto aspect = [&](int i) {
            const QSizeF s = currentRects[i].size();
            return s.height() > 0 ? s.width() / s.height() : 1.0;
        };
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return aspect(a) < aspect(b); });
        return idx;
    }
    case Order::DateAdded: {
        const QVector<KisReferenceImage *> all = layer ? layer->referenceImages() : images;
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return all.indexOf(images[a]) < all.indexOf(images[b]); });
        return idx;
    }
    }
    return idx;
}

qreal aspectFor(TargetShape shape, KoCanvasBase *canvas)
{
    switch (shape) {
    case TargetShape::Square:
        return 1.0;
    case TargetShape::Canvas:
        if (KisCanvas2 *kisCanvas = dynamic_cast<KisCanvas2 *>(canvas)) {
            const QRect bounds = kisCanvas->image()->bounds();
            if (bounds.height() > 0) return qreal(bounds.width()) / bounds.height();
        }
        return 1.0;
    case TargetShape::Board: {
        const QSize s = boardViewportSize();
        return (s.width() > 0 && s.height() > 0) ? qreal(s.width()) / s.height() : 1.6;
    }
    }
    return 1.0;
}

GeometryCommand *normalizeOnCanvas(const QVector<KisReferenceImage *> &allImages, GigaReferenceLayout::NormalizeMode mode,
                                   KisReferenceImage *matchTo)
{
    const QVector<KisReferenceImage *> images = withoutNotes(allImages);
    if (matchTo && matchTo->isNote()) matchTo = nullptr;
    if (images.size() < 2 && mode != GigaReferenceLayout::NormalizeMode::MatchSelected) return nullptr;
    QVector<QSizeF> sizes;
    for (KisReferenceImage *image : images) sizes.append(displayedSize(image));
    const QVector<QSizeF> wanted = GigaReferenceLayout::normalize(sizes, mode, images.indexOf(matchTo));

    const QVector<ImageState> before = statesOf(images);
    QVector<ImageState> after = before;
    for (int i = 0; i < images.size(); ++i) {
        if (sizes[i].width() <= 0.0) continue;
        const qreal f = wanted[i].width() / sizes[i].width();
        const QPointF c = images[i]->boundingRect().center();
        const QTransform scaleAboutCenter = QTransform::fromTranslate(-c.x(), -c.y()) * QTransform::fromScale(f, f)
            * QTransform::fromTranslate(c.x(), c.y());
        after[i].transform = before[i].transform * scaleAboutCenter;
    }
    return makeCommand(kundo2_i18n("Normalize Reference Sizes"), images, before, after);
}

GeometryCommand *arrangeOnCanvas(const QVector<KisReferenceImage *> &allImages, const Settings &settings,
                                 KoCanvasBase *canvas, KisReferenceImagesLayer *layer)
{
    const QVector<KisReferenceImage *> images = settings.keepNotesInPlace ? withoutNotes(allImages) : allImages;
    if (images.isEmpty()) return nullptr;
    QVector<QRectF> current;
    for (KisReferenceImage *image : images) current.append(image->boundingRect());
    const QVector<QRectF> wanted = rearranged(images, current, settings, aspectFor(settings.targetShape, canvas), layer);

    const QVector<ImageState> before = statesOf(images);
    QVector<ImageState> after = before;
    for (int i = 0; i < images.size(); ++i) {
        after[i].transform = before[i].transform * rectToRect(current[i], wanted[i]);
    }
    return makeCommand(kundo2_i18n("Arrange Reference Images"), images, before, after);
}

QVector<QRectF> placeNewOnBoard(const QVector<QSizeF> &sizes, const QVector<KisReferenceImage *> &alreadyOnBoard,
                                const Settings &settings)
{
    GigaReferenceLayout::Options options = layoutOptions(settings, aspectFor(TargetShape::Board, nullptr));
    const GigaReferenceLayout::Result result = GigaReferenceLayout::arrange(sizes, options);

    const QRectF existing = GigaReferenceLayout::bounds(boardRectsOf(alreadyOnBoard));
    const QPointF origin = existing.isNull()
        ? QPointF()
        : QPointF(existing.left(), existing.bottom() + qMax(settings.spacing, 16) * 2);
    QVector<QRectF> out;
    for (const QRectF &r : result.rects) out.append(r.translated(origin));
    return out;
}

GeometryCommand *sendToBoard(const QVector<KisReferenceImage *> &images, KisReferenceImagesLayer *layer,
                             const Settings &settings)
{
    QVector<KisReferenceImage *> toSend;
    for (KisReferenceImage *image : images) {
        if (!image->isOnBoard()) toSend.append(image);
    }
    if (toSend.isEmpty()) return nullptr;

    const QVector<ImageState> before = statesOf(toSend);
    QVector<ImageState> after = before;

    // Images that were on the Board before go back to their old spot; new
    // ones are laid out together under what is already there.
    QVector<int> fresh;
    QVector<QSizeF> freshSizes;
    for (int i = 0; i < toSend.size(); ++i) {
        after[i].onBoard = true;
        if (!before[i].boardRect.isValid()) {
            fresh.append(i);
            freshSizes.append(displayedSize(toSend[i]));
        }
    }
    if (!fresh.isEmpty()) {
        const QVector<QRectF> rects = placeNewOnBoard(freshSizes, boardImages(layer), settings);
        for (int k = 0; k < fresh.size(); ++k) after[fresh[k]].boardRect = rects[k];
    }
    return makeCommand(kundo2_i18n("Send to Reference Board"), toSend, before, after);
}

GeometryCommand *sendToCanvas(const QVector<KisReferenceImage *> &images)
{
    QVector<KisReferenceImage *> toSend;
    for (KisReferenceImage *image : images) {
        if (image->isOnBoard()) toSend.append(image);
    }
    if (toSend.isEmpty()) return nullptr;
    const QVector<ImageState> before = statesOf(toSend);
    QVector<ImageState> after = before;
    for (ImageState &s : after) s.onBoard = false; // canvas transform was never touched
    return makeCommand(kundo2_i18n("Send to Canvas"), toSend, before, after);
}

GeometryCommand *normalizeOnBoard(const QVector<KisReferenceImage *> &allImages, GigaReferenceLayout::NormalizeMode mode,
                                  KisReferenceImage *matchTo)
{
    const QVector<KisReferenceImage *> images = withoutNotes(allImages);
    if (matchTo && matchTo->isNote()) matchTo = nullptr;
    if (images.size() < 2 && mode != GigaReferenceLayout::NormalizeMode::MatchSelected) return nullptr;
    QVector<QSizeF> sizes;
    for (KisReferenceImage *image : images) sizes.append(image->boardRect().size());
    const QVector<QSizeF> wanted = GigaReferenceLayout::normalize(sizes, mode, images.indexOf(matchTo));
    const QVector<ImageState> before = statesOf(images);
    QVector<ImageState> after = before;
    for (int i = 0; i < images.size(); ++i) {
        QRectF r(QPointF(), wanted[i]);
        r.moveCenter(before[i].boardRect.center());
        after[i].boardRect = r;
    }
    return makeCommand(kundo2_i18n("Normalize Reference Sizes"), images, before, after);
}

GeometryCommand *arrangeOnBoard(const QVector<KisReferenceImage *> &allImages, const Settings &settings,
                                KoCanvasBase *canvas, KisReferenceImagesLayer *layer)
{
    const QVector<KisReferenceImage *> images = settings.keepNotesInPlace ? withoutNotes(allImages) : allImages;
    if (images.isEmpty()) return nullptr;
    const QVector<QRectF> current = boardRectsOf(images);
    const QVector<QRectF> wanted = rearranged(images, current, settings, aspectFor(settings.targetShape, canvas), layer);
    const QVector<ImageState> before = statesOf(images);
    QVector<ImageState> after = before;
    for (int i = 0; i < images.size(); ++i) after[i].boardRect = wanted[i];
    return makeCommand(kundo2_i18n("Arrange Reference Board"), images, before, after);
}

GeometryCommand *moveOnBoard(KisReferenceImage *image, const QRectF &newRect)
{
    if (!image) return nullptr;
    const QVector<ImageState> before = {stateOf(image)};
    QVector<ImageState> after = before;
    after[0].boardRect = newRect;
    return makeCommand(kundo2_i18n("Move on Reference Board"), {image}, before, after);
}

GeometryCommand *moveOnBoard(const QVector<KisReferenceImage *> &images, const QVector<QRectF> &newRects)
{
    if (images.isEmpty() || images.size() != newRects.size()) return nullptr;
    const QVector<ImageState> before = statesOf(images);
    QVector<ImageState> after = before;
    for (int i = 0; i < images.size(); ++i) after[i].boardRect = newRects[i];
    return makeCommand(kundo2_i18n("Move on Reference Board"), images, before, after);
}

GeometryCommand *scaleOnBoard(const QVector<KisReferenceImage *> &images, const QVector<QRectF> &newRects)
{
    if (images.isEmpty() || images.size() != newRects.size()) return nullptr;
    const QVector<ImageState> before = statesOf(images);
    QVector<ImageState> after = before;
    for (int i = 0; i < images.size(); ++i) after[i].boardRect = newRects[i];
    return makeCommand(kundo2_i18n("Scale on Reference Board"), images, before, after);
}

// ---- Canvas animation -------------------------------------------------------

void animateOnCanvas(GeometryCommand *command)
{
    if (!command) return;

    struct Item {
        KisReferenceImage *image;
        QTransform from, to;
        QTransform last; // what we set last; anything else means someone else moved it
        bool alive = true;
    };
    auto items = QSharedPointer<QVector<Item>>::create();
    KisReferenceImagesLayer *layer = nullptr;
    for (int i = 0; i < command->images().size(); ++i) {
        const ImageState &a = command->before()[i];
        const ImageState &b = command->after()[i];
        if (a.onBoard || b.onBoard || qFuzzyCompare(a.transform, b.transform)) continue;
        items->append({command->images()[i], a.transform, b.transform, b.transform, true});
        layer = layerOf(command->images()[i]);
    }
    if (items->isEmpty() || !layer) return;

    const quint64 generation = Notifier::instance()->generation();
    KisWeakSharedPtr<KisReferenceImagesLayer> weakLayer(layer);

    // Returns false when the animation must stop. It stops when the layer
    // goes away, and when another Board command (or an undo) is applied:
    // then images still where we left them jump to their final place, and
    // anything another command moved is left alone.
    auto step = [items, weakLayer, generation](qreal t) {
        KisSharedPtr<KisReferenceImagesLayer> l = weakLayer.toStrongRef();
        if (!l) return false;
        const QList<KoShape *> shapes = l->shapes();
        const bool interrupted = Notifier::instance()->generation() != generation;
        for (Item &item : *items) {
            if (!item.alive) continue;
            if (!shapes.contains(item.image) || !qFuzzyCompare(item.image->transformation(), item.last)) {
                item.alive = false;
                continue;
            }
            QTransform next;
            if (interrupted) {
                item.alive = false;
                if (qFuzzyCompare(item.last, item.from)) continue; // undone right at the start
                next = item.to;
            } else {
                next = lerp(item.from, item.to, t);
            }
            item.image->update();
            item.image->setTransformation(next);
            item.image->update();
            item.last = next;
        }
        return !interrupted;
    };

    QVariantAnimation *animation = new QVariantAnimation(Notifier::instance());
    animation->setDuration(200);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    QObject::connect(animation, &QVariantAnimation::valueChanged, animation, [animation, step](const QVariant &v) {
        if (!step(v.toReal())) animation->stop();
    });
    QObject::connect(animation, &QVariantAnimation::finished, animation, [step]() { step(1.0); });
    step(0.0);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

// ---- Resample ---------------------------------------------------------------

QSize resampleTarget(const KisReferenceImage *image)
{
    const QSize pixels = image->imagePixelSize();
    if (pixels.isEmpty()) return QSize();

    // Size on the canvas at 100 % zoom, in image pixels.
    qreal resolution = 1.0; // pixels per point
    if (KisReferenceImagesLayer *layer = layerOf(image)) {
        KisImageSP kisImage = layer->image().toStrongRef();
        if (kisImage) resolution = kisImage->xRes();
    }
    const qreal canvasWidth = displayedSize(image).width() * resolution;

    // Size it fills on the Board when focused.
    qreal focusWidth = 0.0;
    const QSize board = boardViewportSize();
    if (!board.isEmpty()) {
        const qreal aspect = qreal(pixels.width()) / pixels.height();
        focusWidth = qMin<qreal>(board.width(), board.height() * aspect);
    }

    const int targetWidth = qCeil(qMax(canvasWidth, focusWidth) * RESAMPLE_HEADROOM);
    // Only ever shrink, and only when it saves something worthwhile.
    if (targetWidth <= 0 || targetWidth >= pixels.width() * 0.9) return QSize();
    const int targetHeight = qMax(1, qRound(qreal(targetWidth) * pixels.height() / pixels.width()));
    return QSize(qMax(1, targetWidth), targetHeight);
}

ResampleCommand *resample(const QVector<KisReferenceImage *> &images)
{
    QVector<KisReferenceImage *> todo;
    QVector<QSize> targets;
    for (KisReferenceImage *image : images) {
        const QSize target = resampleTarget(image);
        if (!target.isValid()) continue;
        todo.append(image);
        targets.append(target);
    }
    if (todo.isEmpty()) return nullptr;
    return new ResampleCommand(todo, targets);
}

QString resampleSummary(const QVector<KisReferenceImage *> &images)
{
    int count = 0;
    qint64 before = 0, after = 0;
    QSize oneFrom, oneTo;
    for (KisReferenceImage *image : images) {
        const QSize target = resampleTarget(image);
        if (!target.isValid()) continue;
        ++count;
        oneFrom = image->imagePixelSize();
        oneTo = target;
        before += qint64(oneFrom.width()) * oneFrom.height() * 4;
        after += qint64(target.width()) * target.height() * 4;
    }
    if (count == 0) return QString();
    if (count == 1) {
        return i18nc("reference image pixel sizes before and after resampling", "%1 × %2 → %3 × %4",
                     oneFrom.width(), oneFrom.height(), oneTo.width(), oneTo.height());
    }
    auto mb = [](qint64 bytes) { return QString::number(qMax<qreal>(0.1, bytes / (1024.0 * 1024.0)), 'f', 1); };
    return i18nc("number of reference images, memory before and after resampling", "%1 images: %2 MB → %3 MB",
                 count, mb(before), mb(after));
}

QSize shrinkOnSaveTarget(const KisReferenceImage &image)
{
    if (s_shrinkOnSave < 0) {
        s_shrinkOnSave = KisConfig(true).readEntry<bool>("gigaRefBoardShrinkOnSave", false) ? 1 : 0;
    }
    if (s_shrinkOnSave != 1) return QSize();
    return resampleTarget(&image);
}

void setBoardViewportSize(const QSize &size)
{
    s_boardWidth = size.width();
    s_boardHeight = size.height();
}

QSize boardViewportSize()
{
    return QSize(s_boardWidth, s_boardHeight);
}

} // namespace GigaReferenceBoard

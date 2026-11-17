/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaReferenceBoardView.h"

#include <KisDocument.h>
#include <KisReferenceImage.h>
#include <KisReferenceImagesLayer.h>
#include <KisViewManager.h>
#include <KoCanvasResourceProvider.h>
#include <KoColor.h>
#include <KoColorSpaceRegistry.h>
#include <KoSelection.h>
#include <KoShapeManager.h>
#include <kis_canvas2.h>
#include <kis_coordinates_converter.h>
#include <KoCanvasController.h>
#include <kis_icon_utils.h>
#include <KisCursorOverrideLock.h>
#include <kis_action.h>
#include <kis_action_manager.h>
#include <kis_cursor.h>

#include <klocalizedstring.h>

#include <QAbstractSpinBox>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QtConcurrent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTimer>
#include <QTouchEvent>
#include <QVariantAnimation>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace
{
constexpr qreal MIN_ZOOM = 0.005;
constexpr qreal MAX_ZOOM = 64.0;
constexpr qreal FIT_MARGIN = 16.0;   // widget pixels around the overview
constexpr qreal FOCUS_MARGIN = 8.0;  // around a focused image
constexpr qreal DRAG_THRESHOLD = 4.0;
constexpr qreal ROTATE_STEP = 15.0;        // stepped rotate, same as the canvas
constexpr qreal ZOOM_DRAG_SMOOTH = 100.0;  // zoom drag: pixels per doubling (canvas value)
constexpr qreal ZOOM_DRAG_STEP = 50.0;     // stepped zoom drag: pixels per step (canvas value)

bool isModifierKey(int key)
{
    return key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta
        || key == Qt::Key_AltGr;
}

QRectF lerpRect(const QRectF &a, const QRectF &b, qreal t)
{
    return QRectF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t,
                  a.width() + (b.width() - a.width()) * t, a.height() + (b.height() - a.height()) * t);
}

bool isTextInput(QWidget *w)
{
    return qobject_cast<QLineEdit *>(w) || qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QTextEdit *>(w)
        || qobject_cast<QPlainTextEdit *>(w);
}

QString captionFor(KisReferenceImage *image)
{
    const QString name = QFileInfo(image->filename()).completeBaseName();
    return name.isEmpty() ? i18nc("caption of a pasted reference image without a file name", "Image") : name;
}
} // namespace

GigaReferenceBoardView::GigaReferenceBoardView(QWidget *parent)
    : QWidget(parent)
    , m_settings(GigaReferenceBoard::Settings::load())
{
    setObjectName(QStringLiteral("GigaReferenceBoardView"));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    setAttribute(Qt::WA_AcceptTouchEvents);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(120, 90);

    m_cameraAnimation = new QVariantAnimation(this);
    m_cameraAnimation->setStartValue(0.0);
    m_cameraAnimation->setEndValue(1.0);
    m_cameraAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_cameraAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        // Zoom moves geometrically, the board point at the centre linearly:
        // feels like one smooth fly-to.
        const qreal t = v.toReal();
        const QPointF c(width() / 2.0, height() / 2.0);
        const QPointF fromCenter = (c - m_cameraFrom.pan) / m_cameraFrom.zoom;
        const QPointF toCenter = (c - m_cameraTo.pan) / m_cameraTo.zoom;
        Camera cam;
        cam.zoom = m_cameraFrom.zoom * qPow(m_cameraTo.zoom / m_cameraFrom.zoom, t);
        const QPointF center = fromCenter + (toCenter - fromCenter) * t;
        cam.pan = c - center * cam.zoom;
        m_camera = cam;
        update();
    });

    // F1 (reset rotation): turn back to upright around the widget centre.
    m_rotationAnimation = new QVariantAnimation(this);
    m_rotationAnimation->setDuration(200);
    m_rotationAnimation->setStartValue(0.0);
    m_rotationAnimation->setEndValue(1.0);
    m_rotationAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_rotationAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        setRotation(m_rotationFrom * (1.0 - v.toReal()));
    });

    m_rectAnimation = new QVariantAnimation(this);
    m_rectAnimation->setDuration(200);
    m_rectAnimation->setStartValue(0.0);
    m_rectAnimation->setEndValue(1.0);
    m_rectAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_rectAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_rectT = v.toReal();
        update();
    });
    connect(m_rectAnimation, &QVariantAnimation::finished, this, [this]() {
        m_rectT = 1.0;
        m_animFrom.clear();
        update();
    });

    connect(GigaReferenceBoard::Notifier::instance(), &GigaReferenceBoard::Notifier::changed, this,
            &GigaReferenceBoardView::refresh);
    connect(GigaReferenceBoard::Notifier::instance(), &GigaReferenceBoard::Notifier::settingsChanged, this,
            &GigaReferenceBoardView::slotSettingsChanged);
}

GigaReferenceBoardView::~GigaReferenceBoardView()
{
}

// ---- Canvas / layer ---------------------------------------------------------

void GigaReferenceBoardView::setCanvas(KisCanvas2 *canvas)
{
    if (m_canvas == canvas) return;
    if (m_mirrorSource) m_mirrorSource->disconnect(this);
    m_mirrorSource = nullptr;
    if (m_canvas) {
        m_canvas->imageView()->document()->disconnect(this);
        m_canvas->image()->disconnect(this);
    }
    if (KisSharedPtr<KisReferenceImagesLayer> old = m_layer.toStrongRef()) {
        old->disconnect(this);
    }
    m_canvas = canvas;
    m_layer = nullptr;
    m_focused = nullptr;
    m_current = nullptr;
    m_selected.clear();
    m_moving.clear();
    m_drag = Drag::None;
    m_dropRects.clear();
    m_shownRects.clear();
    m_animFrom.clear();

    if (m_canvas) {
        connect(m_canvas->imageView()->document(), &KisDocument::sigReferenceImagesLayerChanged, this,
                &GigaReferenceBoardView::slotLayerChanged);
        connect(m_canvas->image().data(), SIGNAL(sigLayersChangedAsync()), this, SLOT(slotLayerChanged()));
        // Follow the canvas flip (Mirror View).
        if (KoCanvasController *controller = m_canvas->canvasController()) {
            m_mirrorSource = controller->proxyObject.data();
            connect(controller->proxyObject, &KoCanvasControllerProxyObject::documentMirrorStatusChanged, this,
                    &GigaReferenceBoardView::slotCanvasMirrorChanged);
            connect(controller->proxyObject, &KoCanvasControllerProxyObject::documentRotationChanged, this,
                    &GigaReferenceBoardView::slotCanvasRotationChanged);
        }
        m_canvasMirrorX = m_canvas->coordinatesConverter()->xAxisMirrored();
        m_canvasMirrorY = m_canvas->coordinatesConverter()->yAxisMirrored();
        m_canvasRotation = m_canvas->coordinatesConverter()->rotationAngle();
    } else {
        m_canvasMirrorX = m_canvasMirrorY = false;
        m_canvasRotation = 0.0;
    }
    m_autoFit = true;
    slotLayerChanged();
}

void GigaReferenceBoardView::slotLayerChanged()
{
    KisSharedPtr<KisReferenceImagesLayer> current =
        m_canvas ? m_canvas->imageView()->document()->referenceImagesLayer() : KisSharedPtr<KisReferenceImagesLayer>();
    if (current.data() != m_layer.toStrongRef().data()) {
        if (KisSharedPtr<KisReferenceImagesLayer> old = m_layer.toStrongRef()) old->disconnect(this);
        m_layer = current;
        if (current) {
            // Saturation changes, adds and removes all end up here.
            connect(current.data(), &KisReferenceImagesLayer::sigUpdateCanvas, this, &GigaReferenceBoardView::refresh);
        }
    }
    refresh();
}

KisReferenceImagesLayer *GigaReferenceBoardView::layer() const
{
    return m_layer.toStrongRef().data();
}

QVector<KisReferenceImage *> GigaReferenceBoardView::liveImages() const
{
    KisReferenceImagesLayer *l = layer();
    if (!l) return {};
    const QList<KoShape *> shapes = l->shapes();
    QVector<KisReferenceImage *> out;
    for (KisReferenceImage *image : m_images) {
        if (shapes.contains(image) && image->isOnBoard()) out.append(image);
    }
    return out;
}

int GigaReferenceBoardView::imageCount() const
{
    return liveImages().size();
}

int GigaReferenceBoardView::focusedIndex() const
{
    return m_focused ? int(liveImages().indexOf(m_focused)) : -1;
}

QPointF GigaReferenceBoardView::imageWidgetCenter(int index) const
{
    const QVector<KisReferenceImage *> images = liveImages();
    if (index < 0 || index >= images.size()) return QPointF(-1, -1);
    // Where it is drawn right now.
    return rotationTransform().map(cameraTransform().map(displayRect(images[index]).center()));
}

bool GigaReferenceBoardView::isImageSelected(int index) const
{
    const QVector<KisReferenceImage *> images = liveImages();
    return index >= 0 && index < images.size() && m_selected.contains(images[index]);
}

QPolygonF GigaReferenceBoardView::widgetPolygon(KisReferenceImage *image) const
{
    // Board -> view (camera), then view -> widget (flip + turn). Qt applies
    // the left transform of a product first.
    return (cameraTransform() * rotationTransform()).map(QPolygonF(displayRect(image)));
}

void GigaReferenceBoardView::refresh()
{
    QVector<KisReferenceImage *> images = GigaReferenceBoard::boardImages(layer());
    std::stable_sort(images.begin(), images.end(),
                     [](KisReferenceImage *a, KisReferenceImage *b) { return a->zIndex() < b->zIndex(); });

    // Animate images whose Board place changed (arrange, undo, ...), from
    // wherever they are drawn right now.
    QHash<KisReferenceImage *, QRectF> from;
    for (KisReferenceImage *image : images) {
        if (m_dropRects.contains(image)) continue; // a hand-dropped image never animates
        const auto it = m_shownRects.constFind(image);
        if (it != m_shownRects.constEnd() && it.value() != image->boardRect()) {
            from.insert(image, displayRect(image));
        }
    }
    const bool contentMoved = !from.isEmpty() || images != m_images;
    m_images = images;
    m_shownRects.clear();
    for (KisReferenceImage *image : images) m_shownRects.insert(image, image->boardRect());

    if (!from.isEmpty() && isVisible()) {
        m_animFrom = from;
        m_rectT = 0.0;
        m_rectAnimation->stop();
        m_rectAnimation->start();
    }

    for (auto it = m_dropRects.begin(); it != m_dropRects.end();) {
        if (!images.contains(it.key()) || it.key()->boardRect() == it.value()) {
            it = m_dropRects.erase(it); // landed (or gone)
        } else {
            ++it;
        }
    }
    if (m_focused && !images.contains(m_focused)) m_focused = nullptr;
    if (m_current && !images.contains(m_current)) m_current = nullptr;
    m_selected.erase(std::remove_if(m_selected.begin(), m_selected.end(),
                                    [&images](KisReferenceImage *i) { return !images.contains(i); }),
                     m_selected.end());
    m_marqueeBase.erase(std::remove_if(m_marqueeBase.begin(), m_marqueeBase.end(),
                                       [&images](KisReferenceImage *i) { return !images.contains(i); }),
                        m_marqueeBase.end());
    bool movingGone = false;
    for (KisReferenceImage *image : std::as_const(m_moving)) movingGone |= !images.contains(image);
    if ((m_pressImage && !images.contains(m_pressImage)) || movingGone) {
        // Something being dragged was deleted (undo...): drop the drag.
        m_pressImage = nullptr;
        m_moving.clear();
        if (m_drag == Drag::MoveImage || m_drag == Drag::Pending) m_drag = Drag::None;
    }

    if (contentMoved) {
        if (m_focused) {
            animateCamera(cameraFitting(m_focused->boardRect(), FOCUS_MARGIN), 200);
        } else if (m_autoFit) {
            QRectF all;
            for (KisReferenceImage *image : images) all |= image->boardRect();
            if (!all.isEmpty()) animateCamera(cameraFitting(all, FIT_MARGIN), 250);
        }
        Q_EMIT contentChanged();
    }
    update();
}

void GigaReferenceBoardView::slotCanvasMirrorChanged(bool mirrorX, bool mirrorY)
{
    if (mirrorX == m_canvasMirrorX && mirrorY == m_canvasMirrorY) return;
    m_canvasMirrorX = mirrorX;
    m_canvasMirrorY = mirrorY;
    update();
}

void GigaReferenceBoardView::slotCanvasRotationChanged(qreal angle)
{
    // Called while the canvas turns (Shift+Space drag, keys, slider), so
    // the Board turns live with it. Only a repaint: cheap.
    if (angle == m_canvasRotation) return;
    m_canvasRotation = angle;
    if (m_settings.rotateWithCanvas) update();
}

void GigaReferenceBoardView::slotSettingsChanged()
{
    m_settings = GigaReferenceBoard::Settings::load();
    update();
}

// ---- Camera -----------------------------------------------------------------

QTransform GigaReferenceBoardView::cameraTransform() const
{
    return QTransform(m_camera.zoom, 0, 0, m_camera.zoom, m_camera.pan.x(), m_camera.pan.y());
}

QPointF GigaReferenceBoardView::toBoard(const QPointF &widgetPos) const
{
    return (toView(widgetPos) - m_camera.pan) / m_camera.zoom;
}

QTransform GigaReferenceBoardView::rotationTransform() const
{
    // Qt applies the last call first: move the centre to 0, rotate, move back.
    const QPointF c(width() / 2.0, height() / 2.0);
    QTransform t;
    // Like the canvas: flip first, then rotate on screen.
    t.translate(c.x(), c.y());
    t.rotate(effectiveRotation());
    t.scale(isMirrored() ? -1.0 : 1.0, isMirroredVertically() ? -1.0 : 1.0);
    t.translate(-c.x(), -c.y());
    return t;
}

QPointF GigaReferenceBoardView::toView(const QPointF &widgetPos) const
{
    if (effectiveRotation() == 0.0 && !isMirrored() && !isMirroredVertically()) return widgetPos;
    return rotationTransform().inverted().map(widgetPos);
}

QPointF GigaReferenceBoardView::viewDelta(const QPointF &widgetDelta) const
{
    if (effectiveRotation() == 0.0 && !isMirrored() && !isMirroredVertically()) return widgetDelta;
    // Undo the rotation, then the flip (Qt applies the last call first).
    QTransform t;
    t.scale(isMirrored() ? -1.0 : 1.0, isMirroredVertically() ? -1.0 : 1.0);
    t.rotate(-effectiveRotation());
    return t.map(widgetDelta);
}

void GigaReferenceBoardView::setRotation(qreal degrees)
{
    m_rotation = std::remainder(degrees, 360.0); // -180..180, so a reset takes the short way
    if (qAbs(m_rotation) < 1e-9) m_rotation = 0.0;
    update();
}

void GigaReferenceBoardView::resetRotation()
{
    m_rotationAnimation->stop();
    if (m_rotation == 0.0) return;
    m_rotationFrom = m_rotation;
    if (!isVisible()) {
        setRotation(0.0);
        return;
    }
    m_rotationAnimation->start();
}

bool GigaReferenceBoardView::isResetRotationKey(QKeyEvent *event) const
{
    if (isModifierKey(event->key())) return false;
    // The canvas "Reset Canvas Rotation" shortcut (Keyboard Shortcuts, F1 here)...
    if (matchesActionShortcut(event, QStringLiteral("reset_canvas_rotation"))) return true;
    // ...and the reset keys of Canvas Input Settings > Rotate Canvas.
    return GigaCanvasInputKeys::matchKey(GigaCanvasInputKeys::currentBindings(),
                                         GigaCanvasInputKeys::heldKeys(event->modifiers(), {event->key()}))
        >= 0;
}

bool GigaReferenceBoardView::matchesActionShortcut(QKeyEvent *event, const QString &name) const
{
    if (isModifierKey(event->key()) || !m_canvas || !m_canvas->viewManager()) return false;
    // Read fresh every time, so a rebind in Keyboard Shortcuts applies at once.
    KisAction *action = m_canvas->viewManager()->actionManager()->actionByName(name);
    if (!action) return false;
    const QKeySequence pressed(QKeyCombination(event->modifiers() & ~Qt::KeypadModifier, Qt::Key(event->key())));
    for (const QKeySequence &shortcut : action->shortcuts()) {
        if (shortcut == pressed) return true;
    }
    return false;
}

bool GigaReferenceBoardView::isDeleteKey(QKeyEvent *event) const
{
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && mods == Qt::NoModifier) return true;
    // Whatever deletes on the canvas ("Clear", Ctrl+Q / Q for the user).
    return matchesActionShortcut(event, QStringLiteral("clear"));
}

void GigaReferenceBoardView::deleteImage(KisReferenceImage *image)
{
    KisReferenceImagesLayer *l = layer();
    if (!image || !l || !m_canvas || !liveImages().contains(image)) return;
    const bool wasFocused = image == m_focused;
    pushCommand(l->removeReferenceImages(m_canvas->imageView()->document(), {image}));
    // Zoomed in on it: fly back to the overview instead of staring at a gap.
    if (wasFocused) fitAll();
}

void GigaReferenceBoardView::deleteImages(const QVector<KisReferenceImage *> &images)
{
    KisReferenceImagesLayer *l = layer();
    if (!l || !m_canvas) return;
    const QVector<KisReferenceImage *> live = liveImages();
    QList<KoShape *> shapes;
    bool hadFocused = false;
    for (KisReferenceImage *image : images) {
        if (!live.contains(image)) continue;
        shapes.append(image);
        hadFocused |= image == m_focused;
    }
    if (shapes.isEmpty()) return;
    pushCommand(l->removeReferenceImages(m_canvas->imageView()->document(), shapes)); // one undo step
    if (hadFocused) fitAll();
}

void GigaReferenceBoardView::deleteCurrent()
{
    if (m_focused) {
        deleteImage(m_focused);
    } else if (!m_selected.isEmpty()) {
        deleteImages(m_selected);
    } else {
        deleteImage(m_current);
    }
}

void GigaReferenceBoardView::setSelection(const QVector<KisReferenceImage *> &images)
{
    if (images == m_selected) return;
    m_selected = images;
    update();
}

void GigaReferenceBoardView::selectAll()
{
    // Stop dimming the others so the outlines show; the camera stays put.
    m_focused = nullptr;
    setSelection(liveImages());
    update();
}

void GigaReferenceBoardView::clearSelection()
{
    setSelection({});
}

QVector<KisReferenceImage *> GigaReferenceBoardView::targetsFor(KisReferenceImage *image) const
{
    if (!image) return {};
    if (m_selected.contains(image)) {
        // In z order, like everything else on the Board.
        QVector<KisReferenceImage *> out;
        for (KisReferenceImage *i : liveImages()) {
            if (m_selected.contains(i)) out.append(i);
        }
        return out;
    }
    return {image};
}

void GigaReferenceBoardView::arrangeSelected()
{
    QVector<KisReferenceImage *> images;
    for (KisReferenceImage *i : liveImages()) {
        if (m_selected.contains(i)) images.append(i);
    }
    if (images.size() < 2) return;
    m_settings = GigaReferenceBoard::Settings::load();
    // Laid out from the selection's top-left corner, with the Board style.
    pushCommand(GigaReferenceBoard::arrangeOnBoard(images, m_settings, m_canvas, layer()));
}

QRectF GigaReferenceBoardView::displayRect(KisReferenceImage *image) const
{
    const auto dropped = m_dropRects.constFind(image);
    if (dropped != m_dropRects.constEnd()) return dropped.value();
    QRectF r = image->boardRect();
    const auto it = m_animFrom.constFind(image);
    if (it != m_animFrom.constEnd() && m_rectT < 1.0) r = lerpRect(it.value(), r, m_rectT);
    if (m_drag == Drag::MoveImage && m_moving.contains(image)) r.translate(m_dragOffset);
    return r;
}

KisReferenceImage *GigaReferenceBoardView::imageAt(const QPointF &widgetPos) const
{
    const QPointF p = toBoard(widgetPos);
    const QVector<KisReferenceImage *> images = liveImages();
    for (int i = images.size() - 1; i >= 0; --i) { // topmost first
        if (displayRect(images[i]).contains(p)) return images[i];
    }
    return nullptr;
}

GigaReferenceBoardView::Camera GigaReferenceBoardView::cameraFitting(const QRectF &rect, qreal margin) const
{
    Camera cam;
    if (rect.isEmpty() || width() <= 2 * margin || height() <= 2 * margin) {
        cam = m_camera;
        return cam;
    }
    // A turned view needs room for the rect's rotated outline.
    const qreal a = qDegreesToRadians(effectiveRotation());
    const qreal c = qAbs(std::cos(a)), s = qAbs(std::sin(a));
    const qreal w = rect.width() * c + rect.height() * s;
    const qreal h = rect.width() * s + rect.height() * c;
    cam.zoom = qBound(MIN_ZOOM, qMin((width() - 2 * margin) / w, (height() - 2 * margin) / h), MAX_ZOOM);
    cam.pan = QPointF(width() / 2.0, height() / 2.0) - rect.center() * cam.zoom;
    return cam;
}

void GigaReferenceBoardView::animateCamera(const Camera &target, int durationMs)
{
    if (!isVisible() || durationMs <= 0) {
        stopCameraAnimation();
        setCamera(target);
        return;
    }
    m_cameraAnimation->stop();
    m_cameraFrom = m_camera;
    m_cameraTo = target;
    m_cameraAnimation->setDuration(durationMs);
    m_cameraAnimation->start();
}

void GigaReferenceBoardView::setCamera(const Camera &camera)
{
    m_camera = camera;
    update();
}

void GigaReferenceBoardView::stopCameraAnimation()
{
    m_cameraAnimation->stop();
}

void GigaReferenceBoardView::zoomAt(const QPointF &widgetPos, qreal factor)
{
    stopCameraAnimation();
    const QPointF boardPoint = toBoard(widgetPos);
    m_camera.zoom = qBound(MIN_ZOOM, m_camera.zoom * factor, MAX_ZOOM);
    m_camera.pan = toView(widgetPos) - boardPoint * m_camera.zoom;
    m_autoFit = false;
    update();
}

void GigaReferenceBoardView::fitAll()
{
    m_focused = nullptr;
    m_autoFit = true;
    QRectF all;
    for (KisReferenceImage *image : liveImages()) all |= image->boardRect();
    if (!all.isEmpty()) animateCamera(cameraFitting(all, FIT_MARGIN), 300);
    update();
}

void GigaReferenceBoardView::focusImage(KisReferenceImage *image)
{
    if (!image || !liveImages().contains(image)) return;
    m_focused = image;
    m_current = image;
    m_selected = {image};
    m_autoFit = false;
    animateCamera(cameraFitting(image->boardRect(), FOCUS_MARGIN), 300);
    update();
}

void GigaReferenceBoardView::focusStep(int direction)
{
    const QVector<KisReferenceImage *> images = liveImages();
    if (images.isEmpty()) return;
    QVector<QRectF> rects;
    for (KisReferenceImage *image : images) rects.append(image->boardRect());
    const QVector<int> order = GigaReferenceLayout::readingOrder(rects);
    KisReferenceImage *from = m_focused ? m_focused : m_current;
    int at = -1;
    for (int k = 0; k < order.size(); ++k) {
        if (images[order[k]] == from) at = k;
    }
    const int next = at < 0 ? (direction > 0 ? 0 : order.size() - 1)
                            : (at + direction + order.size()) % order.size();
    focusImage(images[order[next]]);
}

void GigaReferenceBoardView::reportViewportSize()
{
    GigaReferenceBoard::setBoardViewportSize((QSizeF(size()) * devicePixelRatioF()).toSize());
}

void GigaReferenceBoardView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    reportViewportSize();
    stopCameraAnimation();
    if (m_focused) {
        setCamera(cameraFitting(m_focused->boardRect(), FOCUS_MARGIN));
    } else if (m_autoFit) {
        QRectF all;
        for (KisReferenceImage *image : liveImages()) all |= image->boardRect();
        if (!all.isEmpty()) setCamera(cameraFitting(all, FIT_MARGIN));
    }
}

void GigaReferenceBoardView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    reportViewportSize();
    refresh();
}

// ---- Painting -----------------------------------------------------------------

void GigaReferenceBoardView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), m_settings.background);

    const QVector<KisReferenceImage *> images = liveImages();
    const bool darkBackground = m_settings.background.lightnessF() < 0.5;
    if (images.isEmpty()) {
        p.setPen(darkBackground ? QColor(255, 255, 255, 120) : QColor(0, 0, 0, 140));
        const QString hint = m_canvas
            ? i18n("Drop or paste images here,\nor use Send to Board in the Reference Images tool.")
            : i18n("Open a document to use the Reference Board.");
        p.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap, hint);
        return;
    }

    // Everything below is drawn in view space; the painter turns it.
    const QTransform rotation = rotationTransform();
    p.setTransform(rotation);
    const QRectF visible = rotation.inverted().mapRect(QRectF(rect())).adjusted(-20, -20, 20, 20);
    const bool flipped = isMirrored() || isMirroredVertically();
    if (effectiveRotation() != 0.0 || flipped) p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QTransform camera = cameraTransform();
    const qreal radius = m_settings.roundedCorners ? qBound(2.0, 6.0 * qSqrt(m_camera.zoom), 10.0) : 0.0;
    p.setRenderHint(QPainter::Antialiasing, true);

    // Draw the dragged images last so they float above the others.
    QVector<KisReferenceImage *> order = images;
    if (m_drag == Drag::MoveImage) {
        for (KisReferenceImage *image : images) {
            if (m_moving.contains(image) && order.removeOne(image)) order.append(image);
        }
    }

    for (KisReferenceImage *image : order) {
        const QRectF boardRect = displayRect(image);
        const QRectF widgetRect = camera.mapRect(boardRect);
        if (!widgetRect.intersects(visible)) continue;

        const bool dimmed = m_settings.dimOthers && m_focused && image != m_focused;
        if (m_settings.shadows && !dimmed) {
            // A soft drop shadow from a few stacked translucent rectangles.
            p.setPen(Qt::NoPen);
            for (int i = 4; i >= 1; --i) {
                p.setBrush(QColor(0, 0, 0, darkBackground ? 28 : 18));
                p.drawRoundedRect(widgetRect.adjusted(-i, -i + 2, i, i + 2), radius + i, radius + i);
            }
        }

        p.save();
        if (radius > 0.0) {
            QPainterPath clip;
            clip.addRoundedRect(widgetRect, radius, radius);
            p.setClipPath(clip, Qt::IntersectClip);
        }
        if (dimmed) p.setOpacity(0.25);
        p.setTransform(camera, true);
        image->paintInRect(p, boardRect);
        p.restore();

        if (m_selected.contains(image) && !m_focused) {
            QPen pen(palette().color(QPalette::Highlight), 2.0);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(widgetRect.adjusted(-1, -1, 1, 1), radius + 1, radius + 1);
        }

        if (m_settings.captions && !dimmed && widgetRect.width() > 40) {
            QFont f = font();
            f.setPointSizeF(qMax(7.0, f.pointSizeF() * 0.85));
            p.setFont(f);
            const QFontMetrics fm(f);
            const QString text = fm.elidedText(captionFor(image), Qt::ElideMiddle, int(widgetRect.width()) - 8);
            const QRectF band(widgetRect.left(), widgetRect.bottom() - fm.height() - 4, widgetRect.width(), fm.height() + 4);
            p.save();
            if (radius > 0.0) {
                QPainterPath clip;
                clip.addRoundedRect(widgetRect, radius, radius);
                p.setClipPath(clip);
            }
            p.fillRect(band, QColor(0, 0, 0, 150));
            p.restore();
            p.setPen(QColor(255, 255, 255, 230));
            if (flipped) {
                // Keep the name readable: only the picture flips.
                p.save();
                p.translate(band.center());
                p.scale(isMirrored() ? -1.0 : 1.0, isMirroredVertically() ? -1.0 : 1.0);
                p.drawText(QRectF(-band.width() / 2.0 + 4, -band.height() / 2.0, band.width() - 8, band.height()),
                           Qt::AlignVCenter | Qt::AlignHCenter, text);
                p.restore();
            } else {
                p.drawText(band.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignHCenter, text);
            }
        }
    }

    if (m_drag == Drag::Marquee) {
        // The selection rectangle, upright on screen like on the canvas.
        p.resetTransform();
        p.setRenderHint(QPainter::Antialiasing, false);
        QColor fill = palette().color(QPalette::Highlight);
        QColor line = fill;
        fill.setAlpha(45);
        line.setAlpha(220);
        p.setPen(QPen(line, 1.0));
        p.setBrush(fill);
        p.drawRect(QRectF(m_pressPos, m_lastPos).normalized());
    }
}

// ---- Mouse, touch, keys ------------------------------------------------------

void GigaReferenceBoardView::pressAt(const QPointF &pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
    m_pressPos = pos;
    m_lastPos = pos;

    // Canvas zoom / rotate drags (Ctrl+Space, Shift+Space... as set in
    // Settings > Canvas Input Settings). Read fresh each press: cheap, and
    // always follows the settings.
    const QVector<GigaCanvasInputKeys::Binding> bindings = GigaCanvasInputKeys::currentBindings();
    const int nav = GigaCanvasInputKeys::matchDrag(bindings, GigaCanvasInputKeys::heldKeys(modifiers, m_heldKeys), button);
    if (nav >= 0) {
        using Kind = GigaCanvasInputKeys::Kind;
        m_navBinding = bindings[nav];
        stopCameraAnimation();
        m_rotationAnimation->stop();
        m_startZoom = m_camera.zoom;
        m_startRotation = m_rotation;
        m_rotateSnapping = false;
        m_zoomAnchor = m_navBinding.towardPointer ? pos : QPointF(width() / 2.0, height() / 2.0);
        switch (m_navBinding.kind) {
        case Kind::Zoom:
            m_drag = Drag::Zoom;
            setCursor(KisCursor::zoomSmoothCursor());
            break;
        case Kind::ZoomDiscrete:
            m_drag = Drag::Zoom;
            setCursor(KisCursor::zoomDiscreteCursor());
            break;
        case Kind::Rotate:
            m_drag = Drag::Rotate;
            setCursor(KisCursor::rotateCanvasSmoothCursor());
            break;
        default:
            m_drag = Drag::Rotate;
            setCursor(KisCursor::rotateCanvasDiscreteCursor());
            break;
        }
        return;
    }

    if (button == Qt::MiddleButton || (button == Qt::LeftButton && m_spaceHeld)) {
        m_drag = Drag::Pan;
        stopCameraAnimation();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (button != Qt::LeftButton) return;

    KisReferenceImage *image = imageAt(pos);
    if (image && (modifiers & Qt::ControlModifier)) {
        pickColor(image, pos); // Krita's Ctrl+click colour sampling
        m_drag = Drag::None;
        return;
    }
    // Grabbing the Board stops any camera fly-to, so what you grab stays
    // under the pen.
    stopCameraAnimation();
    m_pressImage = image;
    m_pressModifiers = modifiers;
    m_moving.clear();
    m_dragOffset = QPointF();
    m_drag = Drag::Pending;
}

void GigaReferenceBoardView::updateMarquee(const QPointF &pos)
{
    // Every image the rectangle touches (also turned/flipped ones: tested
    // against their outline on screen). Shift adds to the earlier selection.
    QPainterPath band;
    band.addRect(QRectF(m_pressPos, pos).normalized());
    QVector<KisReferenceImage *> selected = m_marqueeBase;
    for (KisReferenceImage *image : liveImages()) {
        if (selected.contains(image)) continue;
        QPainterPath outline;
        outline.addPolygon(widgetPolygon(image));
        outline.closeSubpath();
        if (band.intersects(outline)) selected.append(image);
    }
    m_selected = selected;
    update();
}

void GigaReferenceBoardView::moveAt(const QPointF &pos)
{
    const QPointF delta = pos - m_lastPos;
    m_lastPos = pos;
    switch (m_drag) {
    case Drag::Pending:
        if (QLineF(pos, m_pressPos).length() > DRAG_THRESHOLD) {
            if (m_pressImage) {
                // Grabbing a selected image drags the whole selection;
                // any other image is dragged alone (and becomes the selection).
                m_drag = Drag::MoveImage;
                m_current = m_pressImage;
                if (!m_selected.contains(m_pressImage)) m_selected = {m_pressImage};
                m_moving = m_focused ? QVector<KisReferenceImage *>{m_pressImage} : targetsFor(m_pressImage);
                m_dragOffset = viewDelta(pos - m_pressPos) / m_camera.zoom;
                setCursor(Qt::ClosedHandCursor);
            } else {
                // Dragging empty space draws a selection rectangle (pan is
                // middle-drag, Space+drag or the canvas navigation keys).
                m_drag = Drag::Marquee;
                m_marqueeBase = (m_pressModifiers & Qt::ShiftModifier) ? m_selected : QVector<KisReferenceImage *>();
                if (m_focused) {
                    m_focused = nullptr; // stop dimming the others, camera stays
                }
                updateMarquee(pos);
            }
            update();
        }
        break;
    case Drag::Pan:
        m_camera.pan += viewDelta(delta);
        m_autoFit = false;
        update();
        break;
    case Drag::MoveImage:
        m_dragOffset = viewDelta(pos - m_pressPos) / m_camera.zoom;
        update();
        break;
    case Drag::Marquee:
        updateMarquee(pos);
        break;
    case Drag::Zoom: {
        // Same feel as the canvas: up = in, 100 px doubles (stepped: one
        // step per 50 px), measured from where the drag started.
        const qreal steps = GigaCanvasInputKeys::zoomDragSteps(m_pressPos, pos);
        qreal target;
        if (m_navBinding.kind == GigaCanvasInputKeys::Kind::ZoomDiscrete) {
            target = m_startZoom * qPow(M_SQRT2, std::trunc(steps / ZOOM_DRAG_STEP));
        } else {
            target = m_startZoom * qPow(2.0, steps / ZOOM_DRAG_SMOOTH);
        }
        zoomAt(m_zoomAnchor, target / m_camera.zoom);
        break;
    }
    case Drag::Rotate: {
        // Angle swept around the widget centre since the press (canvas maths).
        const QPointF c(width() / 2.0, height() / 2.0);
        const QPointF a = m_pressPos - c, b = pos - c;
        // (Rotation is applied after the flip, so the screen turn = the angle
        // change even on a flipped Board: it follows the pen either way.)
        const qreal swept = qRadiansToDegrees(std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x()));
        qreal target = m_startRotation + swept; // own turn
        if (m_navBinding.kind == GigaCanvasInputKeys::Kind::RotateDiscrete) {
            // Don't jump on press: only snap once turned half a step. Snap
            // what you see (canvas angle + own turn) to 15° steps.
            if (!m_rotateSnapping && qAbs(std::remainder(swept, 360.0)) > 0.5 * ROTATE_STEP) m_rotateSnapping = true;
            const qreal base = effectiveRotation() - m_rotation;
            target = m_rotateSnapping ? std::round((base + target) / ROTATE_STEP) * ROTATE_STEP - base : m_startRotation;
        }
        setRotation(target);
        break;
    }
    default:
        updateCursor(pos);
        break;
    }
}

void GigaReferenceBoardView::releaseAt(const QPointF &pos)
{
    const Drag drag = m_drag;
    m_drag = Drag::None;
    KisReferenceImage *image = m_pressImage;
    m_pressImage = nullptr;
    const QVector<KisReferenceImage *> moving = m_moving;
    m_moving.clear();
    const bool shift = m_pressModifiers & Qt::ShiftModifier;

    if (drag == Drag::MoveImage && !moving.isEmpty()) {
        const QPointF offset = viewDelta(pos - m_pressPos) / m_camera.zoom;
        // Keep them drawn exactly where they were dropped, before and after
        // the move command lands: no animation from the old spot (that
        // looked like rubber-banding).
        const QVector<KisReferenceImage *> live = liveImages();
        QVector<KisReferenceImage *> images;
        QVector<QRectF> rects;
        for (KisReferenceImage *i : moving) {
            if (!live.contains(i)) continue;
            images.append(i);
            rects.append(i->boardRect().translated(offset));
            m_dropRects.insert(i, rects.last());
        }
        m_dragOffset = QPointF();
        pushCommand(GigaReferenceBoard::moveOnBoard(images, rects)); // one undo step
        for (KisReferenceImage *i : images) {
            const auto it = m_dropRects.constFind(i);
            if (it != m_dropRects.constEnd() && i->boardRect() == it.value()) m_dropRects.remove(i); // landed
        }
        if (!m_dropRects.isEmpty()) {
            // Safety net: never hold the drawn spots for long if the command
            // didn't land (it always should).
            QTimer::singleShot(1000, this, [this, images]() {
                for (KisReferenceImage *i : images) m_dropRects.remove(i);
                update();
            });
        }
    } else if (drag == Drag::Marquee) {
        m_marqueeBase.clear();
        if (!m_selected.isEmpty()) m_current = m_selected.last();
    } else if (drag == Drag::Pending && shift) {
        // Shift+click: add the image to the selection or take it out.
        if (image) {
            if (m_selected.contains(image)) {
                m_selected.removeOne(image);
            } else {
                m_selected.append(image);
                m_current = image;
            }
            if (m_focused) m_focused = nullptr; // show the outlines, camera stays
        }
    } else if (drag == Drag::Pending) {
        // A click: focus the image, or back to the overview on empty space.
        if (image) {
            if (image == m_focused) {
                fitAll();
            } else {
                focusImage(image);
            }
        } else {
            m_selected.clear();
            fitAll();
        }
    }
    updateCursor(pos);
    update();
}

void GigaReferenceBoardView::updateCursor(const QPointF &pos)
{
    if (m_spaceHeld) {
        setCursor(Qt::OpenHandCursor);
    } else if (imageAt(pos)) {
        setCursor(Qt::PointingHandCursor);
    } else {
        unsetCursor();
    }
}

void GigaReferenceBoardView::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);
    pressAt(event->position(), event->button(), event->modifiers());
    event->accept();
}

void GigaReferenceBoardView::mouseMoveEvent(QMouseEvent *event)
{
    moveAt(event->position());
}

void GigaReferenceBoardView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_drag == Drag::None) return;
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton
        || ((m_drag == Drag::Zoom || m_drag == Drag::Rotate) && (m_navBinding.buttons & event->button()))) {
        releaseAt(event->position());
    }
}

void GigaReferenceBoardView::wheelEvent(QWheelEvent *event)
{
    const bool touchpad = event->pointingDevice()
        && event->pointingDevice()->type() == QInputDevice::DeviceType::TouchPad;
    if (touchpad && !(event->modifiers() & Qt::ControlModifier) && !event->pixelDelta().isNull()) {
        // Two-finger scroll on a touchpad pans.
        stopCameraAnimation();
        m_camera.pan += viewDelta(QPointF(event->pixelDelta()));
        m_autoFit = false;
        update();
    } else {
        // Mouse wheel (or Ctrl + touchpad scroll) zooms toward the pointer.
        const qreal steps = event->angleDelta().y() / 120.0;
        if (!qFuzzyIsNull(steps)) zoomAt(event->position(), qPow(1.2, steps));
    }
    event->accept();
}

bool GigaReferenceBoardView::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::ShortcutOverride: {
        // Keep our keys from triggering window-wide shortcuts while the
        // Board has keyboard focus.
        QKeyEvent *key = static_cast<QKeyEvent *>(event);
        const int k = key->key();
        if (k == Qt::Key_Escape || k == Qt::Key_Home || k == Qt::Key_Left || k == Qt::Key_Right || k == Qt::Key_Space
            || key->matches(QKeySequence::Paste) || key->matches(QKeySequence::SelectAll) || isResetRotationKey(key)
            || isDeleteKey(key)) {
            event->accept();
            return true;
        }
        // Non-modifier keys used by the canvas zoom/rotate drags (if
        // someone binds e.g. R + drag), so they don't fire tool shortcuts.
        if (!isModifierKey(k)) {
            for (const GigaCanvasInputKeys::Binding &b : GigaCanvasInputKeys::currentBindings()) {
                if (b.kind != GigaCanvasInputKeys::Kind::RotateReset && b.keys.contains(Qt::Key(k))) {
                    event->accept();
                    return true;
                }
            }
        }
        break;
    }
    case QEvent::NativeGesture: {
        QNativeGestureEvent *gesture = static_cast<QNativeGestureEvent *>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            zoomAt(gesture->position(), 1.0 + gesture->value());
            return true;
        }
        break;
    }
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel: {
        QTouchEvent *touch = static_cast<QTouchEvent *>(event);
        const QList<QEventPoint> &points = touch->points();
        if (event->type() == QEvent::TouchCancel) {
            m_drag = Drag::None;
            m_pressImage = nullptr;
            m_moving.clear();
            if (!m_marqueeBase.isEmpty()) m_selected = m_marqueeBase;
            m_marqueeBase.clear();
            update();
            return true;
        }
        if (points.size() >= 2) {
            // Pinch zooms around the fingers, moving them pans.
            const QPointF a = points[0].position(), b = points[1].position();
            const QPointF center = (a + b) / 2.0;
            const qreal distance = QLineF(a, b).length();
            if (m_drag != Drag::Touch) {
                m_drag = Drag::Touch;
                m_pressImage = nullptr;
                m_moving.clear();
                m_marqueeBase.clear();
                stopCameraAnimation();
            } else if (m_touchDistance > 1.0) {
                m_camera.pan += viewDelta(center - m_touchCenter);
                zoomAt(center, distance / m_touchDistance);
            }
            m_touchCenter = center;
            m_touchDistance = distance;
            if (event->type() == QEvent::TouchEnd) m_drag = Drag::None;
            return true;
        }
        if (points.size() == 1) {
            const QPointF pos = points[0].position();
            if (event->type() == QEvent::TouchBegin) {
                pressAt(pos, Qt::LeftButton, touch->modifiers());
            } else if (event->type() == QEvent::TouchUpdate) {
                if (m_drag != Drag::Touch) moveAt(pos);
            } else {
                if (m_drag == Drag::Touch) {
                    m_drag = Drag::None;
                } else {
                    releaseAt(pos);
                }
            }
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QWidget::event(event);
}

void GigaReferenceBoardView::keyPressEvent(QKeyEvent *event)
{
    // Remember held keys only if a zoom/rotate drag uses them (Space...),
    // so a key let go somewhere else can't block the bindings.
    if (!event->isAutoRepeat() && !isModifierKey(event->key())) {
        for (const GigaCanvasInputKeys::Binding &b : GigaCanvasInputKeys::currentBindings()) {
            if (b.kind != GigaCanvasInputKeys::Kind::RotateReset && b.keys.contains(Qt::Key(event->key()))) {
                m_heldKeys.insert(event->key());
                break;
            }
        }
    }
    if (event->matches(QKeySequence::Paste)) {
        paste();
        return;
    }
    if (event->matches(QKeySequence::SelectAll)) {
        selectAll();
        return;
    }
    if (!event->isAutoRepeat() && isResetRotationKey(event)) {
        resetRotation();
        return;
    }
    if (isDeleteKey(event)) {
        if (!event->isAutoRepeat()) deleteCurrent(); // holding the key deletes one, not all
        return;
    }
    switch (event->key()) {
    case Qt::Key_Escape:
        // First Esc lets go of the selection, the next one shows everything.
        // (Zoomed in on an image, Esc still flies back out first.)
        if (!m_focused && !m_selected.isEmpty()) {
            clearSelection();
        } else {
            fitAll();
        }
        return;
    case Qt::Key_Home:
        fitAll();
        return;
    case Qt::Key_Left:
        focusStep(-1);
        return;
    case Qt::Key_Right:
        focusStep(+1);
        return;
    case Qt::Key_Space:
        if (!event->isAutoRepeat()) {
            m_spaceHeld = true;
            setCursor(Qt::OpenHandCursor);
        }
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void GigaReferenceBoardView::keyReleaseEvent(QKeyEvent *event)
{
    if (!event->isAutoRepeat()) m_heldKeys.remove(event->key());
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        updateCursor(mapFromGlobal(QCursor::pos()));
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void GigaReferenceBoardView::focusOutEvent(QFocusEvent *event)
{
    // Key releases won't reach us any more.
    m_heldKeys.clear();
    m_spaceHeld = false;
    QWidget::focusOutEvent(event);
}

void GigaReferenceBoardView::enterEvent(QEnterEvent *event)
{
    // Take keyboard focus while hovered so Space, arrows and Esc work
    // without a click, and give it back on leave. Never steal it from a
    // text field.
    QWidget *focused = QApplication::focusWidget();
    if (focused != this && !isTextInput(focused) && window()->isActiveWindow()) {
        m_focusBeforeHover = focused;
        setFocus(Qt::OtherFocusReason);
    }
    QWidget::enterEvent(event);
}

void GigaReferenceBoardView::leaveEvent(QEvent *event)
{
    m_spaceHeld = false;
    m_heldKeys.clear();
    if (hasFocus() && m_focusBeforeHover && m_drag == Drag::None) {
        m_focusBeforeHover->setFocus(Qt::OtherFocusReason);
    }
    m_focusBeforeHover = nullptr;
    QWidget::leaveEvent(event);
}

// ---- Actions ------------------------------------------------------------------

void GigaReferenceBoardView::pushCommand(KUndo2Command *command)
{
    if (!command) return;
    if (!m_canvas) {
        delete command;
        return;
    }
    m_canvas->addCommand(command);
}

void GigaReferenceBoardView::pickColor(KisReferenceImage *image, const QPointF &widgetPos)
{
    if (!m_canvas) return;
    const QRectF r = image->boardRect();
    const QPointF p = toBoard(widgetPos);
    const QPointF uv((p.x() - r.x()) / r.width(), (p.y() - r.y()) / r.height());
    const QColor c = image->pixelAtNormalized(uv);
    if (!c.isValid()) return;
    m_canvas->resourceManager()->setForegroundColor(KoColor(c, KoColorSpaceRegistry::instance()->rgb8()));
}

void GigaReferenceBoardView::arrange()
{
    m_settings = GigaReferenceBoard::Settings::load();
    m_autoFit = true;
    m_focused = nullptr;
    pushCommand(GigaReferenceBoard::arrangeOnBoard(liveImages(), m_settings, m_canvas, layer()));
}

void GigaReferenceBoardView::normalize(int mode)
{
    pushCommand(GigaReferenceBoard::normalizeOnBoard(liveImages(), GigaReferenceLayout::NormalizeMode(mode),
                                                     m_current ? m_current : m_focused));
}

void GigaReferenceBoardView::addFromCanvas()
{
    KisReferenceImagesLayer *l = layer();
    if (!l) return;
    QVector<KisReferenceImage *> images;
    Q_FOREACH (KoShape *shape, l->shapeManager()->selection()->selectedEditableShapes()) {
        if (auto *reference = dynamic_cast<KisReferenceImage *>(shape)) images.append(reference);
    }
    if (images.isEmpty()) images = GigaReferenceBoard::canvasImages(l);
    m_autoFit = true;
    pushCommand(GigaReferenceBoard::sendToBoard(images, l, GigaReferenceBoard::Settings::load()));
}

void GigaReferenceBoardView::sendAllToCanvas()
{
    pushCommand(GigaReferenceBoard::sendToCanvas(liveImages()));
}

namespace
{
bool isWebImageAddress(const QUrl &url)
{
    const QString scheme = url.scheme();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https")
        || (scheme == QLatin1String("data") && url.toString().startsWith(QLatin1String("data:image/")));
}

/// A dragged address from the image search browser may carry alternatives
/// in "#giga-alt=[...]" (servers never see the part after #).
QStringList candidatesFromDraggedUrl(const QUrl &url)
{
    if (url.scheme() == QLatin1String("data")) return {url.toString()};
    QStringList out;
    const QString fragment = url.fragment(QUrl::FullyDecoded);
    QUrl main = url;
    if (fragment.startsWith(QLatin1String("giga-alt="))) {
        main.setFragment(QString());
        out.append(main.toString());
        const QJsonArray alts = QJsonDocument::fromJson(fragment.mid(9).toUtf8()).array();
        for (const QJsonValue &v : alts) {
            if (!v.toString().isEmpty()) out.append(v.toString());
        }
    } else {
        out.append(url.toString());
    }
    return out;
}

/// Pictures from "data:image/png;base64,...." addresses (Google thumbnails).
QByteArray dataUrlBytes(const QString &url)
{
    const int comma = url.indexOf(QLatin1Char(','));
    if (comma < 0) return QByteArray();
    const QString header = url.left(comma);
    const QByteArray payload = url.mid(comma + 1).toLatin1();
    return header.endsWith(QLatin1String(";base64")) ? QByteArray::fromBase64(payload) : QByteArray::fromPercentEncoding(payload);
}

constexpr qint64 kMaxDownloadBytes = 150ll * 1024 * 1024;
} // namespace

void GigaReferenceBoardView::paste()
{
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime) return;
    QList<QImage> images;
    QStringList files;
    if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (!image.isNull()) images.append(image);
    }
    QList<QStringList> web;
    if (images.isEmpty() && mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile()) files.append(url.toLocalFile());
            else if (isWebImageAddress(url)) web.append(candidatesFromDraggedUrl(url)); // Giga: copied image address
        }
    }
    if (!web.isEmpty() && files.isEmpty()) {
        addWebImages(web, QUrl());
        return;
    }
    addImages(images, files, nullptr);
}

void GigaReferenceBoardView::addImages(const QList<QImage> &images, const QStringList &files, const QPointF *dropBoardPos)
{
    if (!m_canvas || (images.isEmpty() && files.isEmpty())) return;
    KisDocument *document = m_canvas->imageView()->document();

    QList<KisReferenceImage *> created;
    {
        KisCursorOverrideLock cursorLock(Qt::BusyCursor);
        for (const QString &file : files) {
            if (KisReferenceImage *reference = KisReferenceImage::fromFile(file, *m_canvas->coordinatesConverter(), this)) {
                created.append(reference);
            }
        }
        for (const QImage &image : images) {
            if (KisReferenceImage *reference = KisReferenceImage::fromQImage(*m_canvas->coordinatesConverter(), image)) {
                created.append(reference);
            }
        }
    }
    if (created.isEmpty()) return;

    // Similar size to what is already on the Board (median height).
    const QVector<KisReferenceImage *> existing = liveImages();
    QVector<qreal> heights;
    for (KisReferenceImage *image : existing) heights.append(image->boardRect().height());
    std::sort(heights.begin(), heights.end());
    const qreal wantedHeight = heights.isEmpty() ? 0.0 : heights[heights.size() / 2];

    QVector<QSizeF> sizes;
    for (KisReferenceImage *reference : created) {
        QSizeF s = reference->size();
        if (wantedHeight > 0.0 && s.height() > 0.0) s *= wantedHeight / s.height();
        sizes.append(s);
    }

    QVector<QRectF> rects;
    if (dropBoardPos) {
        // In a row, centred on the drop point.
        qreal total = 0.0;
        for (const QSizeF &s : sizes) total += s.width() + m_settings.spacing;
        qreal x = dropBoardPos->x() - total / 2.0;
        for (const QSizeF &s : sizes) {
            rects.append(QRectF(QPointF(x, dropBoardPos->y() - s.height() / 2.0), s));
            x += s.width() + m_settings.spacing;
        }
    } else {
        rects = GigaReferenceBoard::placeNewOnBoard(sizes, existing, m_settings);
    }

    int z = layer() ? layer()->shapes().size() : 0;
    QList<KoShape *> shapes;
    for (int i = 0; i < created.size(); ++i) {
        created[i]->setBoardRect(rects[i]);
        created[i]->setOnBoard(true); // straight onto the Board, never shown on the canvas
        created[i]->setZIndex(z++);
        shapes.append(created[i]);
    }
    if (!dropBoardPos) m_autoFit = true;
    pushCommand(KisReferenceImagesLayer::addReferenceImages(document, shapes));
}

void GigaReferenceBoardView::dragEnterEvent(QDragEnterEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (m_canvas && (mime->hasImage() || mime->hasUrls())) {
        event->acceptProposedAction();
    }
}


void GigaReferenceBoardView::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();
    QList<QImage> images;
    QStringList files;
    QVector<WebItem> webItems;
    if (mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile() && !QImageReader::imageFormat(url.toLocalFile()).isEmpty()) {
                files.append(url.toLocalFile());
            } else if (url.isLocalFile()) {
                files.append(url.toLocalFile()); // let Krita try (e.g. .kra)
            } else if (isWebImageAddress(url)) {
                // Giga Sketch: an image dragged from a web page (the image
                // search panel or another browser): download it.
                WebItem item;
                item.candidates = candidatesFromDraggedUrl(url);
                webItems.append(item);
            }
        }
    }
    QImage carried;
    if (mime->hasImage()) carried = qvariant_cast<QImage>(mime->imageData());
    const QPointF boardPos = toBoard(event->position());
    const QPointF *dropPos = liveImages().isEmpty() ? nullptr : &boardPos;
    if (!webItems.isEmpty() && files.isEmpty()) {
        // The browser's own picture (often the small preview) if every address fails.
        if (webItems.size() == 1 && !carried.isNull()) webItems[0].fallback = carried;
        startWebBatch(webItems, QUrl(), dropPos);
        event->acceptProposedAction();
        return;
    }
    if (files.isEmpty() && !carried.isNull()) images.append(carried);
    addImages(images, files, dropPos);
    event->acceptProposedAction();
}

void GigaReferenceBoardView::addWebImages(const QList<QStringList> &images, const QUrl &referrer)
{
    QVector<WebItem> items;
    for (const QStringList &candidates : images) {
        if (candidates.isEmpty()) continue;
        WebItem item;
        item.candidates = candidates;
        items.append(item);
    }
    startWebBatch(items, referrer, nullptr);
}

void GigaReferenceBoardView::startWebBatch(const QVector<WebItem> &items, const QUrl &referrer, const QPointF *dropBoardPos)
{
    if (!m_canvas || items.isEmpty()) return;
    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
        m_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    }
    const int id = m_nextBatchId++;
    WebBatch batch;
    batch.items = items;
    batch.pending = items.size();
    batch.referrer = referrer;
    batch.document = m_canvas->imageView()->document();
    if (dropBoardPos) {
        batch.hasDropPos = true;
        batch.dropPos = *dropBoardPos;
    }
    m_webBatches.insert(id, batch);
    if (m_webBatches.size() == 1) m_lastAdded = m_lastFailed = 0;
    updateFetchStatus();
    for (int i = 0; i < items.size(); ++i) fetchNext(id, i);
}

void GigaReferenceBoardView::fetchNext(int batchId, int index)
{
    auto it = m_webBatches.find(batchId);
    if (it == m_webBatches.end()) return;
    WebItem &item = it->items[index];
    if (item.candidates.isEmpty()) { // every address failed
        webItemDone(batchId, index, item.fallback);
        return;
    }
    const QString address = item.candidates.takeFirst();

    // Decoding a big photo takes a moment: do it off the interface thread.
    QPointer<GigaReferenceBoardView> self(this);
    auto decode = [self, batchId, index](const QByteArray &bytes) {
        if (!self) return;
        QFutureWatcher<QImage> *watcher = new QFutureWatcher<QImage>(self);
        connect(watcher, &QFutureWatcher<QImage>::finished, self, [self, watcher, batchId, index]() {
            const QImage image = watcher->result();
            watcher->deleteLater();
            if (!self) return;
            if (image.isNull()) self->fetchNext(batchId, index); // not a picture: try the next address
            else self->webItemDone(batchId, index, image);
        });
        watcher->setFuture(QtConcurrent::run([bytes]() { return QImage::fromData(bytes); }));
    };

    if (address.startsWith(QLatin1String("data:"))) {
        decode(dataUrlBytes(address));
        return;
    }

    const QUrl url(address);
    if (!url.isValid()) {
        fetchNext(batchId, index);
        return;
    }
    QNetworkRequest request(url);
    request.setTransferTimeout(20000);
    // A plain desktop browser identity; some image hosts refuse unknown clients.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36"));
    request.setRawHeader("Accept", "image/avif,image/webp,image/png,image/jpeg,image/*;q=0.8,*/*;q=0.5");
    if (it->referrer.isValid() && it->referrer.scheme().startsWith(QLatin1String("http"))) {
        request.setRawHeader("Referer", it->referrer.toEncoded());
    }
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > kMaxDownloadBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, batchId, index, decode]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fetchNext(batchId, index);
            return;
        }
        decode(reply->readAll());
    });
}

void GigaReferenceBoardView::webItemDone(int batchId, int index, const QImage &image)
{
    auto it = m_webBatches.find(batchId);
    if (it == m_webBatches.end()) return;
    it->items[index].image = image;
    if (--it->pending > 0) {
        updateFetchStatus();
        return;
    }
    // All of this batch is in: add the pictures together (one undo step).
    WebBatch batch = it.value();
    m_webBatches.erase(it);
    QList<QImage> images;
    for (const WebItem &item : batch.items) {
        if (!item.image.isNull()) images.append(item.image);
    }
    const int failed = batch.items.size() - images.size();
    const bool sameDocument = m_canvas && batch.document && batch.document == m_canvas->imageView()->document();
    if (sameDocument && !images.isEmpty()) {
        addImages(images, QStringList(), batch.hasDropPos ? &batch.dropPos : nullptr);
        m_lastAdded += images.size();
    }
    m_lastFailed += sameDocument ? failed : batch.items.size();
    updateFetchStatus();
}

void GigaReferenceBoardView::updateFetchStatus()
{
    int waiting = 0;
    for (const WebBatch &batch : std::as_const(m_webBatches)) waiting += batch.pending;
    if (waiting > 0) {
        Q_EMIT fetchStatusChanged(i18np("Adding 1 image…", "Adding %1 images…", waiting));
        return;
    }
    if (m_lastFailed == 0) {
        Q_EMIT fetchStatusChanged(QString());
        return;
    }
    const QString text = m_lastAdded > 0
        ? i18np("%2 added, 1 couldn't be downloaded", "%2 added, %1 couldn't be downloaded", m_lastFailed, m_lastAdded)
        : i18np("The image couldn't be downloaded", "%1 images couldn't be downloaded", m_lastFailed);
    Q_EMIT fetchStatusChanged(text);
    QPointer<GigaReferenceBoardView> self(this);
    QTimer::singleShot(5000, this, [self]() {
        if (self && self->m_webBatches.isEmpty()) Q_EMIT self->fetchStatusChanged(QString());
    });
}

void GigaReferenceBoardView::contextMenuEvent(QContextMenuEvent *event)
{
    m_drag = Drag::None;
    m_moving.clear();
    KisReferenceImage *image = imageAt(event->pos());
    QMenu menu(this);
    if (image) {
        // On a selected image the menu acts on the whole selection.
        if (!m_selected.contains(image)) m_selected = {image};
        m_current = image;
        update();
        const QVector<KisReferenceImage *> targets = targetsFor(image);
        const int n = targets.size();
        menu.addAction(i18n("Focus"), this, [this, image]() { focusImage(image); });
        menu.addAction(n > 1 ? i18n("Send %1 to Canvas", n) : i18n("Send to Canvas"), this, [this, targets]() {
            pushCommand(GigaReferenceBoard::sendToCanvas(targets));
        });
        if (n > 1) menu.addAction(i18n("Arrange Selected"), this, &GigaReferenceBoardView::arrangeSelected);
        const QString summary = GigaReferenceBoard::resampleSummary(targets);
        QAction *resample = menu.addAction(summary.isEmpty() ? i18n("Resample to Current Size (already small)")
                                                             : i18n("Resample to Current Size (%1)", summary),
                                           this, [this, targets]() {
            KUndo2Command *cmd = nullptr;
            {
                KisCursorOverrideLock cursorLock(Qt::BusyCursor);
                cmd = GigaReferenceBoard::resample(targets);
            }
            pushCommand(cmd);
        });
        resample->setEnabled(!summary.isEmpty());
        resample->setToolTip(i18n("Keeps the size it fills here and on the canvas sharp (×1.5). Detail is lost once the file is saved and closed."));
        menu.addSeparator();
        QAction *del = menu.addAction(KisIconUtils::loadIcon("edit-delete"), n > 1 ? i18n("Delete %1", n) : i18n("Delete"),
                                      this, [this, targets]() { deleteImages(targets); });
        del->setShortcut(QKeySequence(Qt::Key_Delete)); // shown as a hint; the key itself is handled in keyPressEvent
    } else {
        menu.addAction(KisIconUtils::loadIcon("zoom-fit"), i18n("Fit All"), this, &GigaReferenceBoardView::fitAll);
        menu.addAction(i18n("Arrange"), this, &GigaReferenceBoardView::arrange);
        menu.addAction(KisIconUtils::loadIcon("edit-paste"), i18n("Paste Image"), this, &GigaReferenceBoardView::paste);
        QAction *all = menu.addAction(i18n("Select All"), this, &GigaReferenceBoardView::selectAll);
        all->setShortcut(QKeySequence::SelectAll);
        menu.addSeparator();
        menu.addAction(i18n("Add from Canvas"), this, &GigaReferenceBoardView::addFromCanvas);
        menu.addAction(i18n("Send All to Canvas"), this, &GigaReferenceBoardView::sendAllToCanvas);
    }
    menu.exec(event->globalPos());
}

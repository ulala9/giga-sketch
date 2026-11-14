/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_REFERENCE_BOARD_VIEW_H
#define GIGA_REFERENCE_BOARD_VIEW_H

#include <QHash>
#include <QPointer>
#include <QRectF>
#include <QWidget>

#include <GigaReferenceBoard.h>
#include <kis_types.h>
#include <KisReferenceImagesLayer.h>

class KisCanvas2;
class KisReferenceImage;
class QVariantAnimation;

/**
 * Giga Sketch: draws the images that are "on the Board" with its own
 * camera (pan / zoom), independent of the painting canvas.
 *
 * Board coordinates are document points (the image's boardRect()). The
 * camera maps them to widget pixels: widget = board * zoom + pan.
 *
 * Everything runs on the interface thread; it only reads the reference
 * images and changes them through undo commands.
 */
class GigaReferenceBoardView : public QWidget
{
    Q_OBJECT
    // For the UI self-test (libs/ui/GigaSelfTest.cpp), which can't link this plugin.
    Q_PROPERTY(int imageCount READ imageCount)
    Q_PROPERTY(int focusedIndex READ focusedIndex)
public:
    explicit GigaReferenceBoardView(QWidget *parent = nullptr);
    ~GigaReferenceBoardView() override;

    void setCanvas(KisCanvas2 *canvas);

    int imageCount() const;
    KisReferenceImage *focusedImage() const { return m_focused; }
    int focusedIndex() const; ///< in z order, -1 = overview
    /// Widget position of the centre of image @p index (z order).
    Q_INVOKABLE QPointF imageWidgetCenter(int index) const;

public Q_SLOTS:
    void fitAll(); ///< overview of the whole Board (Home / Esc)
    void arrange();
    void normalize(int mode); ///< GigaReferenceLayout::NormalizeMode
    void addFromCanvas();     ///< selected canvas references (or all of them)
    void sendAllToCanvas();
    void paste();
    void focusImage(KisReferenceImage *image);
    void focusStep(int direction); ///< -1 previous, +1 next (arrangement order)

Q_SIGNALS:
    void contentChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool event(QEvent *event) override;

private Q_SLOTS:
    void refresh(); ///< re-reads which images are on the Board
    void slotLayerChanged();
    void slotSettingsChanged();

private:
    struct Camera {
        qreal zoom = 1.0;
        QPointF pan;
    };

    KisReferenceImagesLayer *layer() const;
    /// Board images that are really still in the layer (guards against a
    /// shape removed a moment ago).
    QVector<KisReferenceImage *> liveImages() const;
    QTransform cameraTransform() const;
    QRectF displayRect(KisReferenceImage *image) const;
    KisReferenceImage *imageAt(const QPointF &widgetPos) const;
    QPointF toBoard(const QPointF &widgetPos) const;

    Camera cameraFitting(const QRectF &boardRect, qreal margin) const;
    void animateCamera(const Camera &target, int durationMs);
    void setCamera(const Camera &camera);
    void zoomAt(const QPointF &widgetPos, qreal factor);
    void stopCameraAnimation();

    // Shared by mouse and one-finger touch.
    void pressAt(const QPointF &pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
    void moveAt(const QPointF &pos);
    void releaseAt(const QPointF &pos);

    void pickColor(KisReferenceImage *image, const QPointF &widgetPos);
    void addImages(const QList<QImage> &images, const QStringList &files, const QPointF *dropBoardPos);
    void pushCommand(KUndo2Command *command);
    void updateCursor(const QPointF &pos);
    void reportViewportSize();

    QPointer<KisCanvas2> m_canvas;
    KisWeakSharedPtr<KisReferenceImagesLayer> m_layer;
    QVector<KisReferenceImage *> m_images; // on the Board, in z order
    GigaReferenceBoard::Settings m_settings;

    Camera m_camera;
    bool m_autoFit = true; // keep the overview while the user hasn't moved the camera
    QVariantAnimation *m_cameraAnimation {nullptr};
    Camera m_cameraFrom, m_cameraTo;

    // Board rectangles animate (~200 ms) when they change (arrange, undo...).
    QHash<KisReferenceImage *, QRectF> m_shownRects;
    QHash<KisReferenceImage *, QRectF> m_animFrom;
    QVariantAnimation *m_rectAnimation {nullptr};
    qreal m_rectT = 1.0;

    KisReferenceImage *m_focused {nullptr};
    KisReferenceImage *m_current {nullptr}; // last clicked, for highlight

    enum class Drag { None, Pending, Pan, MoveImage, Touch };
    Drag m_drag = Drag::None;
    QPointF m_pressPos;
    QPointF m_lastPos;
    KisReferenceImage *m_pressImage {nullptr};
    QPointF m_dragOffset; // board units, for MoveImage
    KisReferenceImage *m_dropImage {nullptr}; // just dropped by hand: drawn at m_dropRect until its move lands
    QRectF m_dropRect;
    bool m_spaceHeld = false;

    // Two-finger touch
    QPointF m_touchCenter;
    qreal m_touchDistance = 0.0;

    QPointer<QWidget> m_focusBeforeHover;
};

#endif

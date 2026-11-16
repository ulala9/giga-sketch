/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef GIGA_TOOL_STROKE_POINTS_H
#define GIGA_TOOL_STROKE_POINTS_H

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QSet>
#include <QVector>

#include <KoToolFactoryBase.h>
#include <kis_tool.h>
#include <klocalizedstring.h>
#include <KoIcon.h>

class QWidget;
class QToolButton;
class QComboBox;
class KisSliderSpinBox;
class GigaLastStrokeTransform;

/**
 * Giga Sketch: "Edit Stroke Points", the Points mode of Edit > Transform
 * Last Stroke. Never in the toolbox: GigaLastStrokeTransform opens it.
 *
 * Shows dots along the last stroke. Drag a dot and the line bends smoothly
 * through it; when you let go, the stroke is repainted with its own brush
 * along the new path (in the background, see GigaLastStrokeTransform).
 *  - Shift+click: add/remove a dot from the selection (drag moves them all)
 *  - double-click the line: add a dot there
 *  - Enter or switching tools: apply; Esc: put the stroke back as drawn
 *  - Ctrl+Z / Ctrl+Shift+Z: step through the dot moves
 *  - Proportional (top bar): a drag also pulls the line around the dot,
 *    within a radius measured along the line, with a falloff curve (like
 *    Blender's proportional editing, "connected only"). Mouse wheel while
 *    dragging = bigger/smaller radius.
 *
 * The tool only edits the dots (GUI thread) and draws them, plus a thin line
 * of the new path while dragging; all painting is done by the controller.
 */
class GigaToolStrokePoints : public KisTool
{
    Q_OBJECT
public:
    explicit GigaToolStrokePoints(KoCanvasBase *canvas);
    ~GigaToolStrokePoints() override;

    void activate(const QSet<KoShape*> &shapes) override;
    void deactivate() override;

    void beginPrimaryAction(KoPointerEvent *event) override;
    void continuePrimaryAction(KoPointerEvent *event) override;
    void endPrimaryAction(KoPointerEvent *event) override;
    void beginPrimaryDoubleClickAction(KoPointerEvent *event) override;

    void beginAlternateAction(KoPointerEvent *event, AlternateAction action) override;
    void continueAlternateAction(KoPointerEvent *event, AlternateAction action) override;
    void endAlternateAction(KoPointerEvent *event, AlternateAction action) override;

    void mouseMoveEvent(KoPointerEvent *event) override;

    void requestStrokeEnd() override;
    void requestStrokeCancellation() override;

    void paint(QPainter &gc, const KoViewConverter &converter) override;
    QWidget *createOptionWidget() override;

    bool eventFilter(QObject *watched, QEvent *event) override;

    // For the self-test: dot count, a dot's place in widget coordinates.
    Q_INVOKABLE int gigaVertexCount() const;
    Q_INVOKABLE QPointF gigaVertexImagePos(int index) const;
    Q_INVOKABLE void gigaSetProportional(bool on, int radiusScreenPx, int falloff);
    Q_INVOKABLE int gigaRadius() const { return m_radius; }
    Q_INVOKABLE bool gigaWarping() const;

private Q_SLOTS:
    void slotSessionChanged();
    void slotSessionEnded();
    void slotSpacingChanged(int value);
    void slotModePixels();
    void slotProportionalToggled(bool on);
    void slotRadiusChanged(int value);
    void slotFalloffChanged(int index);

private:
    GigaLastStrokeTransform *session() const;
    qreal viewScale() const;           ///< screen px per image px
    int hitVertex(const QPointF &imagePos) const;
    void press(KoPointerEvent *event, bool toggleSelection);
    void drag(KoPointerEvent *event);
    void release();
    void updateOverlay();
    void connectSession();
    void setWheelCatcher(bool on);
    void changeRadius(int wheelSteps);
    /// Centres (s) the proportional preview is drawn around, or empty.
    QVector<qreal> proportionalCenters() const;
    /// While dragging: the last repaint, bent piece by piece to the new shape.
    void paintWarp(QPainter &gc, GigaLastStrokeTransform *s);
    void ensureWarpImage(GigaLastStrokeTransform *s);
    void paintCircles(QPainter &gc, GigaLastStrokeTransform *s);

    QPointer<GigaLastStrokeTransform> m_session;
    QSet<int> m_selected;
    int m_hover {-1};
    bool m_dragging {false};
    bool m_moved {false};
    QPointF m_dragStart;
    QVector<QPointF> m_startOffsets;   ///< offsets of all dots at drag start
    QRectF m_lastOverlay;              ///< image rect drawn last time

    QPointer<QWidget> m_options;
    QPointer<KisSliderSpinBox> m_spacing;
    QPointer<QToolButton> m_pixelsButton;
    QPointer<QToolButton> m_pointsButton;

    // Proportional editing
    bool m_proportional {false};
    int m_radius {120};                ///< screen px, along the line
    int m_falloff {0};                 ///< GigaStrokePath::Falloff
    bool m_propDrag {false};           ///< this drag is a proportional move
    bool m_wheelCatcher {false};
    QPointer<QToolButton> m_propButton;
    QPointer<KisSliderSpinBox> m_radiusBox;
    QPointer<QComboBox> m_falloffBox;
    QElapsedTimer m_radiusChanged;     ///< shows the "120 px" label for a moment

    // Bent preview: the last repaint as an image (at about screen resolution)
    QImage m_warpImage;
    int m_warpImageSerial {-2};
    qreal m_warpImageScale {1.0};      ///< image pixels per stroke pixel
    QPointF m_warpOrigin;              ///< stroke pixel at the image's top-left
};

class GigaToolStrokePointsFactory : public KoToolFactoryBase
{
public:
    GigaToolStrokePointsFactory()
        : KoToolFactoryBase("GigaToolStrokePoints")
    {
        setSection(QStringLiteral("giga-hidden")); // no toolbox button
        setToolTip(i18n("Edit Stroke Points"));
        setIconName(koIconNameCStr("krita_tool_transform"));
        setPriority(100);
        setActivationShapeId(KRITA_TOOL_ACTIVATION_ID);
    }
    ~GigaToolStrokePointsFactory() override {}

    KoToolBase *createTool(KoCanvasBase *canvas) override
    {
        return new GigaToolStrokePoints(canvas);
    }
};

#endif // GIGA_TOOL_STROKE_POINTS_H

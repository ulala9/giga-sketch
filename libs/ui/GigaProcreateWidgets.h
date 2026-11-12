/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_PROCREATE_WIDGETS_H
#define GIGA_PROCREATE_WIDGETS_H

#include <QFrame>
#include <QPointer>
#include <QWidget>

#include <functional>

class QLabel;
class QListWidget;
class QSlider;
class QToolButton;
class QVBoxLayout;
class KisViewManager;

/**
 * Giga Sketch Procreate mode widgets. Only used while Procreate mode is on
 * (GigaProcreateMode); they show Krita's real data (colours, layers, brush
 * presets) with a Procreate-like look.
 */

/// A rounded, shadowed popover card with a small arrow pointing at the
/// button that opened it. Closes when you click elsewhere.
class GigaPopover : public QWidget
{
public:
    explicit GigaPopover(QWidget *parent);
    void setContent(QWidget *content);
    /// Show under @p anchor (arrow pointing up at it).
    void showBelow(QWidget *anchor);
    std::function<void()> onHide;

protected:
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QVBoxLayout *m_layout {nullptr};
    int m_arrowX {40};
};

/// Procreate-like colour disc: hue ring around a saturation/brightness square.
class GigaColorDisc : public QWidget
{
public:
    explicit GigaColorDisc(QWidget *parent = nullptr);
    void setColor(const QColor &color);
    QColor color() const;
    std::function<void(const QColor &)> onColorPicked;   ///< while dragging
    std::function<void(const QColor &)> onColorReleased; ///< when you let go

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QRectF squareRect() const;
    void pick(const QPointF &pos);
    qreal m_hue {0}, m_sat {0}, m_val {0};
    enum Dragging { None, Ring, Square } m_dragging {None};
};

/// Popover content: colour disc + recent colours.
class GigaColorPanel : public QWidget
{
public:
    GigaColorPanel(KisViewManager *viewManager, QWidget *parent = nullptr);
private:
    void rebuildRecent();
    void rememberRecent(const QColor &color);
    QPointer<KisViewManager> m_viewManager;
    GigaColorDisc *m_disc {nullptr};
    QWidget *m_recentRow {nullptr};
};

/// Popover content: layer cards (thumbnail, name, visibility), "+" to add.
class GigaLayersPanel : public QWidget
{
public:
    GigaLayersPanel(KisViewManager *viewManager, QWidget *parent = nullptr);
private:
    void rebuild();
    QPointer<KisViewManager> m_viewManager;
    QVBoxLayout *m_cards {nullptr};
};

/// Popover content: brush sets on the left, brushes with previews on the right.
class GigaBrushLibrary : public QWidget
{
public:
    GigaBrushLibrary(KisViewManager *viewManager, QWidget *parent = nullptr);
private:
    void showSet(int row);
    QPointer<KisViewManager> m_viewManager;
    QListWidget *m_sets {nullptr};
    QListWidget *m_brushes {nullptr};
};

/// The floating, rounded side bar over the canvas (size, colour picker,
/// opacity, undo, redo). Drag it up/down; double-click to switch sides.
class GigaSideBarPill : public QFrame
{
public:
    explicit GigaSideBarPill(QWidget *overlayParent);
    QSlider *sizeSlider() const { return m_size; }
    QSlider *opacitySlider() const { return m_opacity; }
    QToolButton *samplerButton() const { return m_sampler; }
    QToolButton *undoButton() const { return m_undo; }
    QToolButton *redoButton() const { return m_redo; }
    /// Place the pill against the parent's edge (call on parent resize).
    void reposition();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QSlider *m_size {nullptr};
    QSlider *m_opacity {nullptr};
    QToolButton *m_sampler {nullptr};
    QToolButton *m_undo {nullptr};
    QToolButton *m_redo {nullptr};
    bool m_rightSide {false};
    qreal m_verticalFraction {0.3}; ///< top of the pill, as a fraction of the free height
    bool m_dragging {false};
    int m_dragOffsetY {0};
};

#endif // GIGA_PROCREATE_WIDGETS_H

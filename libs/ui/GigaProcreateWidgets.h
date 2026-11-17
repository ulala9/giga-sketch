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
 * (GigaProcreateMode); they show Krita's real data (colors, layers, brush
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
    /// Show to the left of @p anchor (arrow pointing right at it).
    void showLeftOf(QWidget *anchor);
    std::function<void()> onHide;

protected:
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QVBoxLayout *m_layout {nullptr};
    int m_arrowX {40};
    int m_arrowY {40};
    bool m_arrowOnRight {false};
};

/// Procreate-like color disc: hue ring around a saturation/brightness square.
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

/// Popover content: color disc + recent colors.
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

#endif // GIGA_PROCREATE_WIDGETS_H

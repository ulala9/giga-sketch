/*
 * SPDX-FileCopyrightText: 2026 Giga Sketch contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef WG_HARMONY_WHEEL_H
#define WG_HARMONY_WHEEL_H

#include "WGHarmony.h"
#include "WGConfig.h"
#include "WGSelectorWidgetBase.h"

#include <KoColor.h>
#include <KisVisualColorModel.h>

#include <QImage>
#include <QVector>
#include <QVector4D>

class QComboBox;

/**
 * Giga Sketch: Harmony Wheel in the Color panel.
 *
 * A hue/saturation disc with a big dot for the base color and small dots for
 * its harmony colors (complementary, analogous, ...), plus a row of swatches.
 * - Click a swatch: it becomes the active color (Shift+click: background).
 *   The harmony stays put, so you can go back and forth between its colors.
 * - Drag the big dot (or click the disc): new base color.
 * - Drag a small dot: Analogous / Split widen or narrow, the other modes turn.
 * - Picking a color anywhere else makes it the new base.
 *
 * Talks to the panel's KisVisualColorModel like WGShadeSelector does, so it
 * only runs on the GUI thread and never touches painting.
 */
class WGHarmonyWheel : public WGSelectorWidgetBase
{
    Q_OBJECT
    Q_PROPERTY(int mode READ mode WRITE setMode)
public:
    WGHarmonyWheel(WGSelectorDisplayConfigSP displayConfig, KisVisualColorModelSP colorModel, QWidget *parent = nullptr);

    void setModel(KisVisualColorModelSP colorModel) override;
    void updateSettings() override;

    int mode() const { return m_mode; }
    void setMode(int mode);

    // for the self-test
    Q_INVOKABLE int swatchCount() const { return m_colors.size(); }
    Q_INVOKABLE QColor swatchColor(int i) const;
    Q_INVOKABLE void pickSwatch(int i, bool background = false);
    Q_INVOKABLE int activeSwatch() const { return m_active; }
    Q_INVOKABLE qreal spread() const;
    Q_INVOKABLE void setSpread(qreal degrees);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    /// height that keeps the disc square at this width (set as fixed height)
    int heightForWidth(int w) const override;

Q_SIGNALS:
    /// Shift+click on a swatch: set this as the background color.
    void sigBackgroundColorPicked(const KoColor &color);

public Q_SLOTS:
    void slotChannelValuesChanged(const QVector4D &values);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;

private Q_SLOTS:
    void slotModelReset();
    void slotDisplayChanged();

private:
    void connectToModel();
    void recompute();
    void setActiveValues(const QVector4D &values);
    bool usable() const;
    QRectF discRect() const;
    QRectF swatchRect(int i) const;
    QPointF dotPos(const QVector4D &v) const;
    QVector4D valuesAt(const QPointF &pos, qreal z) const;
    int dotAt(const QPointF &pos) const;
    void renderDisc();
    QColor displayColor(const QVector4D &v) const;
    void saveSettings() const;

    KisVisualColorModelSP m_model;
    QComboBox *m_modeBox {nullptr};
    int m_mode {WGHarmony::Complementary};
    qreal m_spread[WGHarmony::ModeCount];
    QVector4D m_base;              ///< the harmony's base color (channel values)
    QVector<QVector4D> m_colors;   ///< harmony colors, swatch order
    int m_baseIndex {0};
    int m_active {0};              ///< swatch that matches the current color, -1 none
    bool m_haveBase {false};
    bool m_ownUpdate {false};      ///< true while we push a color into the model

    enum Drag { None, Base, Other };
    Drag m_drag {None};
    int m_dragIndex {-1};

    QImage m_disc;                 ///< cached hue/saturation disc
    qreal m_discZ {-1};
};

namespace WGConfig {
/// Giga Sketch: show the Harmony Wheel in the Color panel (default on).
extern const GenericSetting<bool> gigaHarmonyWheelEnabled;
}

#endif // WG_HARMONY_WHEEL_H

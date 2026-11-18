/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_BRUSH_SIZE_H
#define GIGA_BRUSH_SIZE_H

#include <QObject>

#include <kis_slider_spin_box.h>

#include "kritaui_export.h"

/**
 * Giga Sketch: optional "round brush sizes" (Preferences > General > Tools).
 *
 * When on, a brush size of 10 px and up becomes a whole number (90.9 -> 91) and a smaller
 * one keeps one decimal (2.47 -> 2.5). Rounding happens where the user *changes* the size
 * (top-bar slider, brush popup, Shift+drag on the canvas); sizes stored in presets are
 * left alone until they are changed. When off, Krita's behavior is unchanged.
 */
class KRITAUI_EXPORT GigaBrushSize : public QObject
{
    Q_OBJECT
public:
    static GigaBrushSize *instance();

    static bool roundingEnabled();
    static void setRoundingEnabled(bool enabled);

    /// The size the brush should get; unchanged when rounding is off.
    static qreal rounded(qreal size);

Q_SIGNALS:
    void roundingChanged();

private:
    GigaBrushSize() = default;
};

/**
 * Size slider that shows "91 px" instead of "90.90 px" while rounding is on (one decimal
 * below 10 px). With rounding off it formats exactly like KisDoubleSliderSpinBox.
 */
class KRITAUI_EXPORT GigaBrushSizeSpinBox : public KisDoubleSliderSpinBox
{
    Q_OBJECT
public:
    explicit GigaBrushSizeSpinBox(QWidget *parent = nullptr);

protected:
    QString textFromValue(double value) const override;
};

#endif

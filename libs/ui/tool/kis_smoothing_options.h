/*
 *  SPDX-FileCopyrightText: 2012 Boudewijn Rempt <boud@valdyas.org>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef KIS_SMOOTHING_OPTIONS_H
#define KIS_SMOOTHING_OPTIONS_H

#include <qglobal.h>
#include <QObject>
#include <QSharedPointer>
#include <QScopedPointer>
#include <kritaui_export.h>


class KRITAUI_EXPORT KisSmoothingOptions : public QObject
{
    Q_OBJECT
public:
    enum SmoothingType {
        NO_SMOOTHING = 0,
        SIMPLE_SMOOTHING,
        WEIGHTED_SMOOTHING,
        STABILIZER,
        PIXEL_PERFECT,
        /// Giga Sketch: speed-adaptive filter + centripetal Catmull-Rom curve
        /// fit, with an eased catch-up when the pen lifts. See KisCurveSmoother.
        /// Must stay last: the value is stored as an int in the config.
        CURVE_SMOOTHING
    };

public:

    KisSmoothingOptions(bool useSavedSmoothing = true);
    ~KisSmoothingOptions() override;

    SmoothingType smoothingType() const;
    void setSmoothingType(SmoothingType value);

    qreal smoothnessDistanceMin() const;
    void setSmoothnessDistanceMin(qreal value);

    qreal smoothnessDistanceMax() const;
    void setSmoothnessDistanceMax(qreal value);

    bool smoothnessDistanceKeepAspectRatio() const;
    void setSmoothnessDistanceKeepAspectRatio(bool value);

    qreal tailAggressiveness() const;
    void setTailAggressiveness(qreal value);

    bool smoothPressure() const;
    void setSmoothPressure(bool value);

    bool useScalableDistance() const;
    void setUseScalableDistance(bool value);

    qreal delayDistance() const;
    void setDelayDistance(qreal value);

    void setUseDelayDistance(bool value);
    bool useDelayDistance() const;

    void setFinishStabilizedCurve(bool value);
    bool finishStabilizedCurve() const;

    void setStabilizeSensors(bool value);
    bool stabilizeSensors() const;

    // Giga Sketch "Smooth Curve" mode settings (CURVE_SMOOTHING). All 0..1.

    /// How much the path is smoothed (0 = barely, 1 = very smooth and laggy).
    qreal curveAmount() const;
    void setCurveAmount(qreal value);

    /// How much drawing faster reduces smoothing (keeps fast flicks crisp).
    qreal curveSpeedInfluence() const;
    void setCurveSpeedInfluence(qreal value);

    /// How much a light touch increases smoothing.
    qreal curvePressureInfluence() const;
    void setCurvePressureInfluence(qreal value);

    /// When the pen lifts (or rests), let the line ease into the pen position.
    bool curveCatchUp() const;
    void setCurveCatchUp(bool value);

Q_SIGNALS:
    void sigSmoothingTypeChanged();

private Q_SLOTS:
    void slotWriteConfig();

private:
    struct Private;
    const QScopedPointer<Private> m_d;
};

typedef QSharedPointer<KisSmoothingOptions> KisSmoothingOptionsSP;

#endif // KIS_SMOOTHING_OPTIONS_H

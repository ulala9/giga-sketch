/*
 *  kis_tool_brush.cc - part of Krita
 *
 *  SPDX-FileCopyrightText: 2003-2004 Boudewijn Rempt <boud@valdyas.org>
 *  SPDX-FileCopyrightText: 2015 Moritz Molch <kde@moritzmolch.de>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_tool_brush.h"
#include <kis_icon.h>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>

#include <klocalizedstring.h>
#include <QAction>
#include <QLabel>
#include <kactioncollection.h>

#include <KoCanvasBase.h>
#include <KoCanvasController.h>

#include <kis_action_registry.h>
#include "kis_cursor.h"
#include "kis_config.h"
#include "kis_slider_spin_box.h"
#include "kundo2magicstring.h"

#include <KisUsageLogger.h>
#include "kis_types.h"
#include "kis_tool.h"
#include "kis_paintop_preset.h"
#include "kis_paintop_settings.h"
#include "kis_aspect_ratio_locker.h"
#include "kis_floating_message.h"
#include "canvas/kis_canvas2.h"
#include "KisViewManager.h"

#define MAXIMUM_SMOOTHNESS_DISTANCE 1000.0 // 0..1000.0 == weight in gui
#define MAXIMUM_MAGNETISM 1000


void KisToolBrush::addSmoothingAction(int enumId, const QString &id)
{
    /**
     * KisToolBrush is the base of several tools, but the actions
     * should be unique, so let's be careful with them
     */
    QAction *a = action(id);
    connect(a, SIGNAL(triggered()), &m_signalMapper, SLOT(map()));
    m_signalMapper.setMapping(a, enumId);
}

KisToolBrush::KisToolBrush(KoCanvasBase * canvas)
    : KisToolFreehand(canvas,
                      KisCursor::load("tool_freehand_cursor.xpm", 2, 2),
                      kundo2_i18n("Freehand Brush Stroke"))
{
    setObjectName("tool_brush");
    setIsOpacityPresetMode(true);

    connect(this, SIGNAL(smoothingTypeChanged()), this, SLOT(resetCursorStyle()));

    addSmoothingAction(KisSmoothingOptions::NO_SMOOTHING, "set_no_brush_smoothing");
    addSmoothingAction(KisSmoothingOptions::SIMPLE_SMOOTHING, "set_simple_brush_smoothing");
    addSmoothingAction(KisSmoothingOptions::WEIGHTED_SMOOTHING, "set_weighted_brush_smoothing");
    addSmoothingAction(KisSmoothingOptions::STABILIZER, "set_stabilizer_brush_smoothing");
    addSmoothingAction(KisSmoothingOptions::PIXEL_PERFECT, "set_pixel_perfect_smoothing");
}

KisToolBrush::~KisToolBrush()
{
}

void KisToolBrush::activate(const QSet<KoShape*> &shapes)
{
    KisToolFreehand::activate(shapes);
    connect(&m_signalMapper, SIGNAL(mapped(int)), SLOT(slotSetSmoothingType(int)), Qt::UniqueConnection);

    m_configGroup = KSharedConfig::openConfig()->group(toolId());
    optionWidgets(); // Ensure m_chkAssistant is initialized.
    QAction *toggleaction = action("toggle_assistant");
    connect(toggleaction, SIGNAL(triggered(bool)), m_chkAssistant, SLOT(toggle()), Qt::UniqueConnection);
}

void KisToolBrush::deactivate()
{
    disconnect(&m_signalMapper, 0, this, 0);
    QAction *toggleaction = action("toggle_assistant");
    disconnect(toggleaction, 0, m_chkAssistant, 0);

    KisToolFreehand::deactivate();
}

int KisToolBrush::smoothingType() const
{
    return smoothingOptions()->smoothingType();
}

bool KisToolBrush::smoothPressure() const
{
    return smoothingOptions()->smoothPressure();
}

int KisToolBrush::smoothnessQualityMin() const
{
    return smoothingOptions()->smoothnessDistanceMin();
}

int KisToolBrush::smoothnessQualityMax() const
{
    return smoothingOptions()->smoothnessDistanceMax();
}

qreal KisToolBrush::smoothnessFactor() const
{
    return smoothingOptions()->tailAggressiveness();
}

void KisToolBrush::slotSetSmoothingType(int index)
{
    /**
     * The slot can also be called from smoothing-type-switching
     * action that would mean the combo box will not be synchronized
     */
    if (m_cmbSmoothingType->currentIndex() != index) {
        m_cmbSmoothingType->setCurrentIndex(index);
    }

    switch (index) {
    case KisSmoothingOptions::NO_SMOOTHING:
        smoothingOptions()->setSmoothingType(KisSmoothingOptions::NO_SMOOTHING);
        m_lblSmoothnessDistanceMin->setVisible(false);
        m_sliderSmoothnessDistanceMin->setVisible(false);
        m_lblSmoothnessDistanceMax->setVisible(false);
        m_sliderSmoothnessDistanceMax->setVisible(false);
        m_distanceAspectButton->setVisible(false);
        showControl(m_sliderTailAggressiveness, false);
        showControl(m_chkSmoothPressure, false);
        showControl(m_chkUseScalableDistance, false);
        showControl(m_sliderDelayDistance, false);
        showControl(m_chkFinishStabilizedCurve, false);
        showControl(m_chkStabilizeSensors, false);
        break;
    case KisSmoothingOptions::SIMPLE_SMOOTHING:
        smoothingOptions()->setSmoothingType(KisSmoothingOptions::SIMPLE_SMOOTHING);
        m_lblSmoothnessDistanceMin->setVisible(false);
        m_sliderSmoothnessDistanceMin->setVisible(false);
        m_lblSmoothnessDistanceMax->setVisible(false);
        m_sliderSmoothnessDistanceMax->setVisible(false);
        m_distanceAspectButton->setVisible(false);
        showControl(m_sliderTailAggressiveness, false);
        showControl(m_chkSmoothPressure, false);
        showControl(m_chkUseScalableDistance, false);
        showControl(m_sliderDelayDistance, false);
        showControl(m_chkFinishStabilizedCurve, false);
        showControl(m_chkStabilizeSensors, false);
        break;
    case KisSmoothingOptions::WEIGHTED_SMOOTHING:
        smoothingOptions()->setSmoothingType(KisSmoothingOptions::WEIGHTED_SMOOTHING);
        m_lblSmoothnessDistanceMin->setVisible(true);
        m_sliderSmoothnessDistanceMin->setVisible(true);
        m_lblSmoothnessDistanceMax->setVisible(true);
        m_sliderSmoothnessDistanceMax->setVisible(true);
        m_distanceAspectButton->setVisible(true);
        showControl(m_sliderTailAggressiveness, true);
        showControl(m_chkSmoothPressure, true);
        showControl(m_chkUseScalableDistance, true);
        showControl(m_sliderDelayDistance, false);
        showControl(m_chkFinishStabilizedCurve, false);
        showControl(m_chkStabilizeSensors, false);
        break;
    case KisSmoothingOptions::STABILIZER:
        smoothingOptions()->setSmoothingType(KisSmoothingOptions::STABILIZER);
        m_lblSmoothnessDistanceMin->setVisible(true);
        m_sliderSmoothnessDistanceMin->setVisible(true);
        m_lblSmoothnessDistanceMax->setVisible(true);
        m_sliderSmoothnessDistanceMax->setVisible(true);
        m_distanceAspectButton->setVisible(true);
        showControl(m_sliderTailAggressiveness, false);
        showControl(m_chkSmoothPressure, false);
        showControl(m_sliderDelayDistance, true);
        showControl(m_chkFinishStabilizedCurve, true);
        showControl(m_chkStabilizeSensors, true);

        // scalable distance option is disabled due to bug 421314
        showControl(m_chkUseScalableDistance, false);
        break;
    case KisSmoothingOptions::CURVE_SMOOTHING:
        smoothingOptions()->setSmoothingType(KisSmoothingOptions::CURVE_SMOOTHING);
        m_lblSmoothnessDistanceMin->setVisible(false);
        m_sliderSmoothnessDistanceMin->setVisible(false);
        m_lblSmoothnessDistanceMax->setVisible(false);
        m_sliderSmoothnessDistanceMax->setVisible(false);
        m_distanceAspectButton->setVisible(false);
        showControl(m_sliderTailAggressiveness, false);
        showControl(m_chkSmoothPressure, false);
        showControl(m_chkUseScalableDistance, false);
        showControl(m_sliderDelayDistance, false);
        showControl(m_chkFinishStabilizedCurve, false);
        showControl(m_chkStabilizeSensors, false);
        break;
    case KisSmoothingOptions::PIXEL_PERFECT:
    default:
        smoothingOptions()->setSmoothingType(KisSmoothingOptions::PIXEL_PERFECT);
        m_lblSmoothnessDistanceMin->setVisible(false);
        m_sliderSmoothnessDistanceMin->setVisible(false);
        m_lblSmoothnessDistanceMax->setVisible(false);
        m_sliderSmoothnessDistanceMax->setVisible(false);
        m_distanceAspectButton->setVisible(false);
        showControl(m_sliderTailAggressiveness, false);
        showControl(m_chkSmoothPressure, false);
        showControl(m_chkUseScalableDistance, false);
        showControl(m_sliderDelayDistance, false);
        showControl(m_chkFinishStabilizedCurve, false);
        showControl(m_chkStabilizeSensors, false);
    }

    // Giga Sketch: the Smooth Curve controls belong to that mode only.
    {
        const bool curve = smoothingOptions()->smoothingType() == KisSmoothingOptions::CURVE_SMOOTHING;
        showControl(m_curveAmountRow, curve);
        showControl(m_curveSpeedInfluenceRow, curve);
        showControl(m_curvePressureInfluenceRow, curve);
        showControl(m_chkCurveCatchUp, curve);
        showControl(m_curveCatchUpTimeRow, curve);
        showControl(m_chkCurveShowLagLine, curve);
        showControl(m_btnResetCurveSmoothing, curve);
    }
    updateSmoothnessDistanceLabel();

    Q_EMIT smoothingTypeChanged();
}

void KisToolBrush::setCurveAmount(int percent)
{
    smoothingOptions()->setCurveAmount(percent / 100.0);
}

void KisToolBrush::setCurveSpeedInfluence(int percent)
{
    smoothingOptions()->setCurveSpeedInfluence(percent / 100.0);
}

void KisToolBrush::setCurvePressureInfluence(int percent)
{
    smoothingOptions()->setCurvePressureInfluence(percent / 100.0);
}

void KisToolBrush::setCurveCatchUp(bool value)
{
    smoothingOptions()->setCurveCatchUp(value);
}

void KisToolBrush::resetCurveSmoothing()
{
    // Setting the widgets fires their change signals, which update the
    // smoothing options and save them, same as moving them by hand.
    m_sliderCurveAmount->setValue(qRound(KisSmoothingOptions::defaultCurveAmount() * 100));
    m_sliderCurveSpeedInfluence->setValue(qRound(KisSmoothingOptions::defaultCurveSpeedInfluence() * 100));
    m_sliderCurvePressureInfluence->setValue(qRound(KisSmoothingOptions::defaultCurvePressureInfluence() * 100));
    m_chkCurveCatchUp->setChecked(KisSmoothingOptions::defaultCurveCatchUp());
    m_sliderCurveCatchUpTime->setValue(KisSmoothingOptions::defaultCurveCatchUpTime());
    m_chkCurveShowLagLine->setChecked(KisSmoothingOptions::defaultCurveShowLagLine());
}

void KisToolBrush::setCurveCatchUpTime(int ms)
{
    smoothingOptions()->setCurveCatchUpTime(ms);
}

void KisToolBrush::setCurveShowLagLine(bool value)
{
    smoothingOptions()->setCurveShowLagLine(value);
}

void KisToolBrush::updateSmoothnessDistanceLabel()
{
    const qreal oldValueMin = m_sliderSmoothnessDistanceMin->value();
    const qreal oldValueMax = m_sliderSmoothnessDistanceMax->value();

    if (smoothingType() == KisSmoothingOptions::STABILIZER) {
        m_lblSmoothnessDistanceMin->setText(i18n("Sample Count at Max Speed:"));
        m_sliderSmoothnessDistanceMin->setRange(3.0, MAXIMUM_SMOOTHNESS_DISTANCE, 0);
        m_sliderSmoothnessDistanceMin->setSingleStep(1);
        m_sliderSmoothnessDistanceMin->setExponentRatio(3.0); // help pick smaller values

        if (!qFuzzyCompare(m_sliderSmoothnessDistanceMin->value(), oldValueMin)) {
            // the slider will emit the change signal automatically
            m_sliderSmoothnessDistanceMin->setValue(qRound(oldValueMin));
        }

        m_lblSmoothnessDistanceMax->setText(i18n("Sample Count at Min Speed:"));
        m_sliderSmoothnessDistanceMax->setRange(3.0, MAXIMUM_SMOOTHNESS_DISTANCE, 0);
        m_sliderSmoothnessDistanceMax->setSingleStep(1);
        m_sliderSmoothnessDistanceMax->setExponentRatio(3.0); // help pick smaller values

        if (!qFuzzyCompare(m_sliderSmoothnessDistanceMax->value(), oldValueMax)) {
            // the slider will emit the change signal automatically
            m_sliderSmoothnessDistanceMax->setValue(qRound(oldValueMax));
        }
    } else {
        m_lblSmoothnessDistanceMin->setText(i18n("Distance at Max Speed:"));
        m_sliderSmoothnessDistanceMin->setRange(3.0, MAXIMUM_SMOOTHNESS_DISTANCE, 1);
        m_sliderSmoothnessDistanceMin->setSingleStep(1);
        m_sliderSmoothnessDistanceMin->setExponentRatio(3.0); // help pick smaller values

        if (!qFuzzyCompare(m_sliderSmoothnessDistanceMin->value(), oldValueMin)) {
            // the slider will emit the change signal automatically
            m_sliderSmoothnessDistanceMin->setValue(oldValueMin);
        }

        m_lblSmoothnessDistanceMax->setText(i18n("Distance at Min Speed:"));
        m_sliderSmoothnessDistanceMax->setRange(3.0, MAXIMUM_SMOOTHNESS_DISTANCE, 1);
        m_sliderSmoothnessDistanceMax->setSingleStep(1);
        m_sliderSmoothnessDistanceMax->setExponentRatio(3.0); // help pick smaller values

        if (!qFuzzyCompare(m_sliderSmoothnessDistanceMax->value(), oldValueMax)) {
            // the slider will emit the change signal automatically
            m_sliderSmoothnessDistanceMax->setValue(oldValueMax);
        }
    }
}

void KisToolBrush::slotSetSmoothnessDistanceMin(qreal distance)
{
    smoothingOptions()->setSmoothnessDistanceMin(distance);
    Q_EMIT smoothnessQualityChanged();
}

void KisToolBrush::slotSetSmoothnessDistanceMax(qreal distance)
{
    smoothingOptions()->setSmoothnessDistanceMax(distance);
    Q_EMIT smoothnessQualityChanged();
}

void KisToolBrush::slotSetSmoothnessDistanceKeepAspectRatio(bool value)
{
    smoothingOptions()->setSmoothnessDistanceKeepAspectRatio(value);
}

void KisToolBrush::slotSetTailAggressiveness(qreal argh_rhhrr)
{
    smoothingOptions()->setTailAggressiveness(argh_rhhrr);
    Q_EMIT smoothnessFactorChanged();
}

// used with weighted smoothing
void KisToolBrush::setSmoothPressure(bool value)
{
    smoothingOptions()->setSmoothPressure(value);
}

void KisToolBrush::slotSetMagnetism(int magnetism)
{
    m_magnetism = expf(magnetism / (double)MAXIMUM_MAGNETISM) / expf(1.0);
}

bool KisToolBrush::useScalableDistance() const
{
    return smoothingOptions()->useScalableDistance();
}

// used with weighted smoothing
void KisToolBrush::setUseScalableDistance(bool value)
{
    smoothingOptions()->setUseScalableDistance(value);

    Q_EMIT useScalableDistanceChanged();
}

void KisToolBrush::resetCursorStyle()
{
    KisConfig cfg(true);
    CursorStyle cursorStyle = cfg.newCursorStyle();

    // When the stabilizer is in use, we avoid using the brush outline cursor,
    // because it would hide the real position of the cursor to the user,
    // yielding unexpected results.
    if (smoothingOptions()->smoothingType() == KisSmoothingOptions::STABILIZER &&
            smoothingOptions()->useDelayDistance() &&
            cursorStyle == CURSOR_STYLE_NO_CURSOR) {

        useCursor(KisCursor::roundCursor());
    } else {
        KisToolFreehand::resetCursorStyle();
    }

    overrideCursorIfNotEditable();
}

// stabilizer brush settings
bool KisToolBrush::useDelayDistance() const
{
    return smoothingOptions()->useDelayDistance();
}

qreal KisToolBrush::delayDistance() const
{
    return smoothingOptions()->delayDistance();
}

void KisToolBrush::setUseDelayDistance(bool value)
{
    smoothingOptions()->setUseDelayDistance(value);
    m_sliderDelayDistance->setEnabled(value);
    enableControl(m_chkFinishStabilizedCurve, !value);

    Q_EMIT useDelayDistanceChanged();
}

void KisToolBrush::setDelayDistance(qreal value)
{
    smoothingOptions()->setDelayDistance(value);
    Q_EMIT delayDistanceChanged();
}

void KisToolBrush::setFinishStabilizedCurve(bool value)
{
    smoothingOptions()->setFinishStabilizedCurve(value);

    Q_EMIT finishStabilizedCurveChanged();
}

bool KisToolBrush::finishStabilizedCurve() const
{
    return smoothingOptions()->finishStabilizedCurve();
}

void KisToolBrush::setStabilizeSensors(bool value)
{
    smoothingOptions()->setStabilizeSensors(value);
    Q_EMIT stabilizeSensorsChanged();
}

bool KisToolBrush::stabilizeSensors() const
{
    return smoothingOptions()->stabilizeSensors();
}

void KisToolBrush::updateSettingsViews()
{
    m_cmbSmoothingType->setCurrentIndex(smoothingOptions()->smoothingType());

    m_sliderSmoothnessDistanceMin->setValue(smoothingOptions()->smoothnessDistanceMin());
    m_sliderSmoothnessDistanceMax->setValue(smoothingOptions()->smoothnessDistanceMax());
    m_chkDelayDistance->setChecked(smoothingOptions()->useDelayDistance());
    m_sliderDelayDistance->setValue(smoothingOptions()->delayDistance());
    m_sliderTailAggressiveness->setValue(smoothingOptions()->tailAggressiveness());
    m_chkSmoothPressure->setChecked(smoothingOptions()->smoothPressure());
    m_chkUseScalableDistance->setChecked(smoothingOptions()->useScalableDistance());
    m_cmbSmoothingType->setCurrentIndex((int)smoothingOptions()->smoothingType());
    m_chkStabilizeSensors->setChecked(smoothingOptions()->stabilizeSensors());

    Q_EMIT smoothnessQualityChanged();
    Q_EMIT smoothnessFactorChanged();
    Q_EMIT smoothPressureChanged();
    Q_EMIT smoothingTypeChanged();
    Q_EMIT useScalableDistanceChanged();
    Q_EMIT useDelayDistanceChanged();
    Q_EMIT delayDistanceChanged();
    Q_EMIT finishStabilizedCurveChanged();
    Q_EMIT stabilizeSensorsChanged();

    KisTool::updateSettingsViews();
}

QWidget * KisToolBrush::createOptionWidget()
{
    QWidget *optionsWidget = KisToolFreehand::createOptionWidget();
    optionsWidget->setObjectName(toolId() + "option widget");

    // See https://bugs.kde.org/show_bug.cgi?id=316896
    QWidget *specialSpacer = new QWidget(optionsWidget);
    specialSpacer->setObjectName("SpecialSpacer");
    specialSpacer->setFixedSize(0, 0);
    optionsWidget->layout()->addWidget(specialSpacer);

    // Line smoothing configuration
    m_cmbSmoothingType = new QComboBox(optionsWidget);
    m_cmbSmoothingType->addItems(QStringList()
                                 << i18nc("@item:inlistbox Brush Smoothing", "None")
                                 << i18nc("@item:inlistbox Brush Smoothing", "Basic")
                                 << i18nc("@item:inlistbox Brush Smoothing", "Weighted")
                                 << i18nc("@item:inlistbox Brush Smoothing", "Stabilizer")
                                 << i18nc("@item:inlistbox Brush Smoothing", "Pixel")
                                 << i18nc("@item:inlistbox Brush Smoothing", "Smooth Curve"));
    connect(m_cmbSmoothingType, SIGNAL(currentIndexChanged(int)), this, SLOT(slotSetSmoothingType(int)));
    addOptionWidgetOption(m_cmbSmoothingType, new QLabel(i18n("Brush Smoothing:"), optionsWidget));

    // Smoothness Distance
    m_sliderSmoothnessDistanceMin = new KisDoubleSliderSpinBox(optionsWidget);
    m_sliderSmoothnessDistanceMin->setEnabled(true);
    m_lblSmoothnessDistanceMin = new QLabel(optionsWidget);

    m_sliderSmoothnessDistanceMax = new KisDoubleSliderSpinBox(optionsWidget);
    m_sliderSmoothnessDistanceMax->setEnabled(true);
    m_lblSmoothnessDistanceMax = new QLabel(optionsWidget);

    updateSmoothnessDistanceLabel();
    // make sure that initialization of the value happens **after** we
    // have configured the slider's range in updateSmoothnessDistanceLabel()
    m_sliderSmoothnessDistanceMin->setValue(smoothingOptions()->smoothnessDistanceMin());
    m_sliderSmoothnessDistanceMax->setValue(smoothingOptions()->smoothnessDistanceMax());

    // Create KoAspectButton
    m_distanceAspectButton = new KoAspectButton(optionsWidget);
    m_distanceAspectButton->setKeepAspectRatio(smoothingOptions()->smoothnessDistanceKeepAspectRatio());
    m_distanceAspectButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_distanceAspectButton->setMinimumSize(QSize(24, 24));

    // Add distanceLabels
    QWidget* distanceLabelWidget = new QWidget(optionsWidget);
    QVBoxLayout* distanceLabelLayout = new QVBoxLayout(distanceLabelWidget);
    distanceLabelLayout->setContentsMargins(0,0,0,0);
    distanceLabelLayout->setSpacing(5);
    distanceLabelLayout->addWidget(m_lblSmoothnessDistanceMin);
    distanceLabelLayout->addWidget(m_lblSmoothnessDistanceMax);

    // Add distanceSliders
    QWidget* distanceSliderWidget = new QWidget(optionsWidget);
    QGridLayout* distanceSliderLayout = new QGridLayout(distanceSliderWidget);
    distanceSliderLayout->setContentsMargins(0,0,0,0);
    distanceSliderLayout->setHorizontalSpacing(0);
    distanceSliderLayout->setVerticalSpacing(5);
    distanceSliderLayout->addWidget(m_sliderSmoothnessDistanceMin, 0, 0, 1, 1);
    distanceSliderLayout->addWidget(m_sliderSmoothnessDistanceMax, 1, 0, 1, 1);
    distanceSliderLayout->addWidget(m_distanceAspectButton, 0, 1, 2, 1);

    addOptionWidgetOption(distanceSliderWidget, distanceLabelWidget);

    KisAspectRatioLocker *distanceAspectLocker = new KisAspectRatioLocker(this);
    distanceAspectLocker->connectSpinBoxes(m_sliderSmoothnessDistanceMin, m_sliderSmoothnessDistanceMax, m_distanceAspectButton);

    connect(distanceAspectLocker, &KisAspectRatioLocker::aspectButtonToggled,
        this, &KisToolBrush::slotSetSmoothnessDistanceKeepAspectRatio);

    connect(distanceAspectLocker, &KisAspectRatioLocker::sliderValueChanged, this, [this]() {
        slotSetSmoothnessDistanceMin(m_sliderSmoothnessDistanceMin->value());
        slotSetSmoothnessDistanceMax(m_sliderSmoothnessDistanceMax->value());
    });

    // Finish stabilizer curve
    m_chkFinishStabilizedCurve = new QCheckBox(optionsWidget);
    connect(m_chkFinishStabilizedCurve, SIGNAL(toggled(bool)), this, SLOT(setFinishStabilizedCurve(bool)));
    m_chkFinishStabilizedCurve->setChecked(smoothingOptions()->finishStabilizedCurve());
    m_chkFinishStabilizedCurve->setMinimumHeight(m_cmbSmoothingType->sizeHint().height()-3);

    // Delay Distance for Stabilizer
    QWidget* delayWidget = new QWidget(optionsWidget);
    QHBoxLayout* delayLayout = new QHBoxLayout(delayWidget);
    delayLayout->setContentsMargins(0,0,0,0);
    delayLayout->setSpacing(1);
    QLabel* delayLabel = new QLabel(i18n("Delay:"), optionsWidget);
    delayLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    delayLayout->addWidget(delayLabel);
    m_chkDelayDistance = new QCheckBox(optionsWidget);
    m_chkDelayDistance->setLayoutDirection(Qt::RightToLeft);
    delayWidget->setToolTip(i18n("Delay the brush stroke to make the line smoother"));
    connect(m_chkDelayDistance, SIGNAL(toggled(bool)), this, SLOT(setUseDelayDistance(bool)));
    delayLayout->addWidget(m_chkDelayDistance);
    m_sliderDelayDistance = new KisDoubleSliderSpinBox(optionsWidget);
    m_sliderDelayDistance->setToolTip(i18n("Radius where the brush is blocked"));
    m_sliderDelayDistance->setRange(0, 500);
    m_sliderDelayDistance->setExponentRatio(3.0); // help pick smaller values
    m_sliderDelayDistance->setSuffix(i18n(" px"));
    connect(m_sliderDelayDistance, SIGNAL(valueChanged(qreal)), SLOT(setDelayDistance(qreal)));

    addOptionWidgetOption(m_sliderDelayDistance, delayWidget);
    addOptionWidgetOption(m_chkFinishStabilizedCurve, new QLabel(i18n("Finish line:"), optionsWidget));

    m_sliderDelayDistance->setValue(smoothingOptions()->delayDistance());
    m_chkDelayDistance->setChecked(smoothingOptions()->useDelayDistance());
    // if the state is not flipped, then the previous line doesn't generate any signals
    setUseDelayDistance(m_chkDelayDistance->isChecked());

    // Stabilize sensors
    m_chkStabilizeSensors = new QCheckBox(optionsWidget);
    connect(m_chkStabilizeSensors, SIGNAL(toggled(bool)), this, SLOT(setStabilizeSensors(bool)));
    m_chkStabilizeSensors->setChecked(smoothingOptions()->stabilizeSensors());
    m_chkStabilizeSensors->setMinimumHeight(m_cmbSmoothingType->sizeHint().height()-3);
    addOptionWidgetOption(m_chkStabilizeSensors, new QLabel(i18n("Stabilize Sensors:"), optionsWidget));

    m_sliderTailAggressiveness = new KisDoubleSliderSpinBox(optionsWidget);
    m_sliderTailAggressiveness->setRange(0.0, 1.0, 2);
    m_sliderTailAggressiveness->setSingleStep(0.01);
    m_sliderTailAggressiveness->setEnabled(true);
    connect(m_sliderTailAggressiveness, SIGNAL(valueChanged(qreal)), SLOT(slotSetTailAggressiveness(qreal)));
    m_sliderTailAggressiveness->setValue(smoothingOptions()->tailAggressiveness());
    addOptionWidgetOption(m_sliderTailAggressiveness, new QLabel(i18n("Stroke Ending:"), optionsWidget));

    m_chkSmoothPressure = new QCheckBox(optionsWidget);
    m_chkSmoothPressure->setMinimumHeight(m_cmbSmoothingType->sizeHint().height()-3);
    m_chkSmoothPressure->setChecked(smoothingOptions()->smoothPressure());
    connect(m_chkSmoothPressure, SIGNAL(toggled(bool)), this, SLOT(setSmoothPressure(bool)));
    addOptionWidgetOption(m_chkSmoothPressure, new QLabel(QString("%1:").arg(i18n("Smooth Pressure")), optionsWidget));

    m_chkUseScalableDistance = new QCheckBox(optionsWidget);
    m_chkUseScalableDistance->setChecked(smoothingOptions()->useScalableDistance());
    m_chkUseScalableDistance->setMinimumHeight(m_cmbSmoothingType->sizeHint().height()-3);
    m_chkUseScalableDistance->setToolTip(i18nc("@info:tooltip",
                                               "Scalable distance takes zoom level "
                                               "into account and makes the distance "
                                               "be visually constant whatever zoom "
                                               "level is chosen"));
    connect(m_chkUseScalableDistance, SIGNAL(toggled(bool)), this, SLOT(setUseScalableDistance(bool)));
    addOptionWidgetOption(m_chkUseScalableDistance, new QLabel(QString("%1:").arg(i18n("Scalable Distance")), optionsWidget));

    // Giga Sketch "Smooth Curve" controls. Shown only for that mode
    // (see slotSetSmoothingType).
    // Puts a slider in a row with a small reset button (same icon as the
    // brush preset editor's reload button) that sets it back to `percent`.
    auto makeResettableRow = [optionsWidget](KisSliderSpinBox *slider, int defaultPercent) {
        QWidget *row = new QWidget(optionsWidget);
        QHBoxLayout *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        layout->addWidget(slider, 1);

        QToolButton *reset = new QToolButton(row);
        reset->setIcon(KisIconUtils::loadIcon("reload-preset-16"));
        reset->setAutoRaise(true);
        reset->setToolTip(i18nc("@info:tooltip", "Reset to default (%1)", slider->prefix() + QString::number(defaultPercent) + slider->suffix()));
        QObject::connect(reset, &QToolButton::clicked, slider, [slider, defaultPercent]() {
            slider->setValue(defaultPercent); // valueChanged updates the setting
        });
        layout->addWidget(reset);
        return row;
    };

    m_sliderCurveAmount = new KisSliderSpinBox(optionsWidget);
    m_sliderCurveAmount->setRange(0, 100);
    m_sliderCurveAmount->setSuffix(i18n("%"));
    m_sliderCurveAmount->setToolTip(i18nc("@info:tooltip", "How strongly the stroke path is smoothed. Higher values give smoother lines that trail further behind the pen."));
    m_sliderCurveAmount->setValue(qRound(smoothingOptions()->curveAmount() * 100));
    connect(m_sliderCurveAmount, SIGNAL(valueChanged(int)), SLOT(setCurveAmount(int)));
    m_curveAmountRow = makeResettableRow(m_sliderCurveAmount, qRound(KisSmoothingOptions::defaultCurveAmount() * 100));
    addOptionWidgetOption(m_curveAmountRow, new QLabel(i18n("Amount:"), optionsWidget));

    m_sliderCurveSpeedInfluence = new KisSliderSpinBox(optionsWidget);
    m_sliderCurveSpeedInfluence->setRange(0, 100);
    m_sliderCurveSpeedInfluence->setSuffix(i18n("%"));
    m_sliderCurveSpeedInfluence->setToolTip(i18nc("@info:tooltip", "How much drawing faster reduces smoothing, so quick flicks stay crisp while slow curves are smoothed."));
    m_sliderCurveSpeedInfluence->setValue(qRound(smoothingOptions()->curveSpeedInfluence() * 100));
    connect(m_sliderCurveSpeedInfluence, SIGNAL(valueChanged(int)), SLOT(setCurveSpeedInfluence(int)));
    m_curveSpeedInfluenceRow = makeResettableRow(m_sliderCurveSpeedInfluence, qRound(KisSmoothingOptions::defaultCurveSpeedInfluence() * 100));
    addOptionWidgetOption(m_curveSpeedInfluenceRow, new QLabel(i18n("Speed Influence:"), optionsWidget));

    m_sliderCurvePressureInfluence = new KisSliderSpinBox(optionsWidget);
    m_sliderCurvePressureInfluence->setRange(0, 100);
    m_sliderCurvePressureInfluence->setSuffix(i18n("%"));
    m_sliderCurvePressureInfluence->setToolTip(i18nc("@info:tooltip", "How much a light touch adds smoothing."));
    m_sliderCurvePressureInfluence->setValue(qRound(smoothingOptions()->curvePressureInfluence() * 100));
    connect(m_sliderCurvePressureInfluence, SIGNAL(valueChanged(int)), SLOT(setCurvePressureInfluence(int)));
    m_curvePressureInfluenceRow = makeResettableRow(m_sliderCurvePressureInfluence, qRound(KisSmoothingOptions::defaultCurvePressureInfluence() * 100));
    addOptionWidgetOption(m_curvePressureInfluenceRow, new QLabel(i18n("Pressure Influence:"), optionsWidget));

    m_chkCurveCatchUp = new QCheckBox(optionsWidget);
    m_chkCurveCatchUp->setMinimumHeight(m_cmbSmoothingType->sizeHint().height()-3);
    m_chkCurveCatchUp->setToolTip(i18nc("@info:tooltip", "When the pen rests or lifts, the line eases into the pen position instead of stopping short."));
    m_chkCurveCatchUp->setChecked(smoothingOptions()->curveCatchUp());
    connect(m_chkCurveCatchUp, SIGNAL(toggled(bool)), SLOT(setCurveCatchUp(bool)));
    addOptionWidgetOption(m_chkCurveCatchUp, new QLabel(i18n("Catch Up:"), optionsWidget));

    m_sliderCurveCatchUpTime = new KisSliderSpinBox(optionsWidget);
    m_sliderCurveCatchUpTime->setRange(0, 400);
    m_sliderCurveCatchUpTime->setSuffix(i18n(" ms"));
    m_sliderCurveCatchUpTime->setToolTip(i18nc("@info:tooltip", "How long the line takes to glide into place after the pen lifts. 0 lands instantly."));
    m_sliderCurveCatchUpTime->setValue(smoothingOptions()->curveCatchUpTime());
    connect(m_sliderCurveCatchUpTime, SIGNAL(valueChanged(int)), SLOT(setCurveCatchUpTime(int)));
    m_curveCatchUpTimeRow = makeResettableRow(m_sliderCurveCatchUpTime, KisSmoothingOptions::defaultCurveCatchUpTime());
    addOptionWidgetOption(m_curveCatchUpTimeRow, new QLabel(i18n("Catch-Up Time:"), optionsWidget));
    connect(m_chkCurveCatchUp, SIGNAL(toggled(bool)), m_sliderCurveCatchUpTime, SLOT(setEnabled(bool)));
    m_sliderCurveCatchUpTime->setEnabled(m_chkCurveCatchUp->isChecked());

    m_chkCurveShowLagLine = new QCheckBox(optionsWidget);
    m_chkCurveShowLagLine->setMinimumHeight(m_cmbSmoothingType->sizeHint().height()-3);
    m_chkCurveShowLagLine->setToolTip(i18nc("@info:tooltip", "While drawing, show a thin line from the painted stroke to the pen tip."));
    m_chkCurveShowLagLine->setChecked(smoothingOptions()->curveShowLagLine());
    connect(m_chkCurveShowLagLine, SIGNAL(toggled(bool)), SLOT(setCurveShowLagLine(bool)));
    addOptionWidgetOption(m_chkCurveShowLagLine, new QLabel(i18n("Show Lag Line:"), optionsWidget));

    m_btnResetCurveSmoothing = new QPushButton(i18n("Reset Smooth Curve"), optionsWidget);
    m_btnResetCurveSmoothing->setIcon(KisIconUtils::loadIcon("reload-preset-16"));
    m_btnResetCurveSmoothing->setToolTip(i18nc("@info:tooltip", "Set all Smooth Curve options back to their defaults"));
    connect(m_btnResetCurveSmoothing, SIGNAL(clicked()), SLOT(resetCurveSmoothing()));
    addOptionWidgetOption(m_btnResetCurveSmoothing);

    // Giga Sketch QuickShape: hold the pen still at the end of a stroke to
    // snap it to a line/arc/ellipse/rectangle/polygon (Shift = perfect).
    {
        KisConfig quickCfg(true);
        QCheckBox *quickShape = new QCheckBox(optionsWidget);
        quickShape->setMinimumHeight(m_cmbSmoothingType->sizeHint().height()-3);
        quickShape->setToolTip(i18nc("@info:tooltip", "Hold Ctrl and keep the pen still at the end of a stroke to turn it into a clean line, arc, ellipse, rectangle or polygon. Add Shift for a perfect circle, square or 15° line. Keep the pen down and drag to rotate or resize the shape before lifting."));
        quickShape->setChecked(quickCfg.readEntry<bool>("gigaQuickShape", true));
        connect(quickShape, &QCheckBox::toggled, this, [](bool on) {
            KisConfig(false).writeEntry<bool>("gigaQuickShape", on);
        });
        addOptionWidgetOption(quickShape, new QLabel(i18n("QuickShape:"), optionsWidget));

        KisSliderSpinBox *quickDelay = new KisSliderSpinBox(optionsWidget);
        quickDelay->setRange(150, 2000);
        quickDelay->setSingleStep(50);
        quickDelay->setSuffix(i18n(" ms"));
        quickDelay->setToolTip(i18nc("@info:tooltip", "How long to hold the pen still before the stroke snaps to a shape."));
        quickDelay->setValue(quickCfg.readEntry<int>("gigaQuickShapeDelay", 300));
        connect(quickDelay, QOverload<int>::of(&KisSliderSpinBox::valueChanged), this, [](int ms) {
            KisConfig(false).writeEntry<int>("gigaQuickShapeDelay", ms);
        });
        connect(quickShape, &QCheckBox::toggled, quickDelay, &QWidget::setEnabled);
        quickDelay->setEnabled(quickShape->isChecked());
        addOptionWidgetOption(quickDelay, new QLabel(i18n("Hold Time:"), optionsWidget));

        QCheckBox *needsCtrl = new QCheckBox(optionsWidget);
        needsCtrl->setMinimumHeight(m_cmbSmoothingType->sizeHint().height()-3);
        needsCtrl->setToolTip(i18nc("@info:tooltip", "Only snap while Ctrl is held. Turn off to snap whenever the pen is held still."));
        needsCtrl->setChecked(quickCfg.readEntry<bool>("gigaQuickShapeNeedsCtrl", true));
        connect(needsCtrl, &QCheckBox::toggled, this, [](bool on) {
            KisConfig(false).writeEntry<bool>("gigaQuickShapeNeedsCtrl", on);
        });
        connect(quickShape, &QCheckBox::toggled, needsCtrl, &QWidget::setEnabled);
        needsCtrl->setEnabled(quickShape->isChecked());
        addOptionWidgetOption(needsCtrl, new QLabel(i18n("Require Ctrl:"), optionsWidget));
    }

    // add a line spacer so we know that the next set of options are for different settings
    QFrame* line = new QFrame(optionsWidget);
    line->setObjectName(QString::fromUtf8("line"));
    line->setFrameStyle(QFrame::HLine | QFrame::Sunken);
    addOptionWidgetOption(line);

    // Drawing assistant configuration
    QWidget* assistantWidget = new QWidget(optionsWidget);
    QGridLayout* assistantLayout = new QGridLayout(assistantWidget);
    assistantLayout->setContentsMargins(10,0,0,0);
    assistantLayout->setSpacing(5);

    m_chkAssistant = new QCheckBox(optionsWidget);
    m_chkAssistant->setText(i18n("Snap to Assistants"));

    assistantWidget->setToolTip(i18n("You need to add Assistants before this tool will work."));
    connect(m_chkAssistant, SIGNAL(toggled(bool)), this, SLOT(setAssistant(bool)));
    addOptionWidgetOption(assistantWidget, m_chkAssistant);

    m_sliderMagnetism = new KisSliderSpinBox(optionsWidget);
    m_sliderMagnetism->setToolTip(i18n("Assistant Magnetism"));
    m_sliderMagnetism->setRange(0, MAXIMUM_MAGNETISM);

    m_sliderMagnetism->setValue(m_magnetism * MAXIMUM_MAGNETISM);
    connect(m_sliderMagnetism, SIGNAL(valueChanged(int)), SLOT(slotSetMagnetism(int)));

    QLabel* magnetismLabel = new QLabel(i18n("Magnetism:"), optionsWidget);
    addOptionWidgetOption(m_sliderMagnetism, magnetismLabel);

    QLabel* snapSingleLabel = new QLabel(i18n("Snap to Single Line"), optionsWidget);

    m_chkOnlyOneAssistant = new QCheckBox(optionsWidget);
    m_chkOnlyOneAssistant->setToolTip(i18nc("@info:tooltip","Make it only snap to a single assistant line, prevents snapping mess while using the infinite assistants."));
    m_chkOnlyOneAssistant->setCheckState(Qt::Checked); // turn on by default.
    connect(m_chkOnlyOneAssistant, SIGNAL(toggled(bool)), this, SLOT(setOnlyOneAssistantSnap(bool)));
    addOptionWidgetOption(m_chkOnlyOneAssistant, snapSingleLabel);

    QLabel* snapEraserLabel = new QLabel(i18n("Snap Eraser"), optionsWidget);

    m_chkSnapEraser = new QCheckBox(optionsWidget);
    m_chkSnapEraser->setToolTip(i18nc("@info:tooltip","Enable snapping when using erasers"));
    m_chkSnapEraser->setCheckState(Qt::Unchecked); // turn off by default.
    connect(m_chkSnapEraser, SIGNAL(toggled(bool)), this, SLOT(setSnapEraser(bool)));
    addOptionWidgetOption(m_chkSnapEraser, snapEraserLabel);

    // set the assistant snapping options to hidden by default and toggle their visibility based off snapping checkbox
    m_sliderMagnetism->setVisible(false);
    m_chkOnlyOneAssistant->setVisible(false);
    m_chkSnapEraser->setVisible(false);
    magnetismLabel->setVisible(false);
    snapSingleLabel->setVisible(false);
    snapEraserLabel->setVisible(false);

    connect(m_chkAssistant, SIGNAL(toggled(bool)), m_sliderMagnetism, SLOT(setVisible(bool)));
    connect(m_chkAssistant, SIGNAL(toggled(bool)), m_chkOnlyOneAssistant, SLOT(setVisible(bool)));
    connect(m_chkAssistant, SIGNAL(toggled(bool)), m_chkSnapEraser, SLOT(setVisible(bool)));
    connect(m_chkAssistant, SIGNAL(toggled(bool)), magnetismLabel, SLOT(setVisible(bool)));
    connect(m_chkAssistant, SIGNAL(toggled(bool)), snapSingleLabel, SLOT(setVisible(bool)));
    connect(m_chkAssistant, SIGNAL(toggled(bool)), snapEraserLabel, SLOT(setVisible(bool)));

    KisConfig cfg(true);

    slotSetSmoothingType(cfg.lineSmoothingType());

    return optionsWidget;
}

QList<QAction *> KisToolBrushFactory::createActionsImpl()
{
    KisActionRegistry *actionRegistry = KisActionRegistry::instance();

    QList<QAction *> actions = KisToolPaintFactoryBase::createActionsImpl();

    actions << actionRegistry->makeQAction("set_no_brush_smoothing", this);
    actions << actionRegistry->makeQAction("set_simple_brush_smoothing", this);
    actions << actionRegistry->makeQAction("set_weighted_brush_smoothing", this);
    actions << actionRegistry->makeQAction("set_stabilizer_brush_smoothing", this);
    actions << actionRegistry->makeQAction("set_pixel_perfect_smoothing", this);
    actions << actionRegistry->makeQAction("toggle_assistant", this);

    return actions;

}

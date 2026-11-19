/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaBrushSize.h"

#include <QLocale>
#include <QtMath>

#include <atomic>

#include "kis_config.h"

namespace {
const char kRoundKey[] = "gigaRoundBrushSize";
// -1 = not read from the config yet, 0 = off, 1 = on. Atomic because the size can also be
// read while a stroke runs; it is only ever written on the GUI thread.
std::atomic<int> s_rounding {-1};
}

GigaBrushSize *GigaBrushSize::instance()
{
    static GigaBrushSize *s_instance = new GigaBrushSize();
    return s_instance;
}

bool GigaBrushSize::roundingEnabled()
{
    int value = s_rounding.load(std::memory_order_relaxed);
    if (value < 0) {
        value = KisConfig(true).readEntry<bool>(kRoundKey, true) ? 1 : 0; // on by default
        s_rounding.store(value, std::memory_order_relaxed);
    }
    return value == 1;
}

void GigaBrushSize::setRoundingEnabled(bool enabled)
{
    const bool changed = roundingEnabled() != enabled;
    KisConfig(false).writeEntry<bool>(kRoundKey, enabled);
    s_rounding.store(enabled ? 1 : 0, std::memory_order_relaxed);
    if (changed) {
        Q_EMIT instance()->roundingChanged();
    }
}

qreal GigaBrushSize::rounded(qreal size)
{
    if (!roundingEnabled()) return size;
    if (size >= 9.95) {
        // 9.95 and up would show as "10.0" with one decimal, so it becomes a whole pixel too.
        return qMax<qreal>(10.0, qRound(size));
    }
    // Small brushes: one decimal, never 0 (the smallest Krita allows is 0.01).
    return qMax<qreal>(0.1, qRound(size * 10.0) / 10.0);
}

GigaBrushSizeSpinBox::GigaBrushSizeSpinBox(QWidget *parent)
    : KisDoubleSliderSpinBox(parent)
{
    // Re-format the shown text right away when the preference is toggled.
    // (Re-setting the suffix makes Qt rebuild the text from textFromValue().)
    connect(GigaBrushSize::instance(), &GigaBrushSize::roundingChanged, this, [this]() {
        setSuffix(suffix());
    });
}

QString GigaBrushSizeSpinBox::textFromValue(double value) const
{
    if (!GigaBrushSize::roundingEnabled()) {
        return KisDoubleSliderSpinBox::textFromValue(value);
    }
    QLocale loc = locale();
    loc.setNumberOptions(loc.numberOptions() | QLocale::OmitGroupSeparator);
    const qreal shown = GigaBrushSize::rounded(value);
    if (shown >= 10.0 || qFuzzyCompare(shown, qreal(qRound(shown)))) {
        return loc.toString(qRound(shown));  // "91", "5"
    }
    return loc.toString(shown, 'f', 1);      // "2.5"
}

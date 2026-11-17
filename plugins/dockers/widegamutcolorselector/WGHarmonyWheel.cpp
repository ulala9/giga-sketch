/*
 * SPDX-FileCopyrightText: 2026 Giga Sketch contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "WGHarmonyWheel.h"

#include "WGConfig.h"

#include <kis_display_color_converter.h>
#include <klocalizedstring.h>

#include <QComboBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

using namespace WGHarmony;

namespace {
constexpr int SWATCH_H = 22;    // swatch row height
constexpr int GAP = 6;          // space between combo, disc and swatches
constexpr int MAX_DISC = 220;   // the disc never gets bigger than this
constexpr int MIN_DISC = 90;
constexpr qreal BASE_DOT = 8.0; // dot radii
constexpr qreal DOT = 6.0;
constexpr int HUE_STEPS = 180;  // disc color grid (bilinear in between)
constexpr int SAT_STEPS = 16;
}

namespace WGConfig {
// Giga Sketch: Harmony Wheel settings (in the WideGamutColorSelector group).
const GenericSetting<bool> gigaHarmonyWheelEnabled {"gigaHarmonyWheelEnabled", true};
const NumericSetting<int> gigaHarmonyMode {"gigaHarmonyMode", WGHarmony::Complementary, 0,
                                          WGHarmony::ModeCount - 1, true};
const GenericSetting<qreal> gigaHarmonySpreadAnalogous {"gigaHarmonySpreadAnalogous", 30.0};
const GenericSetting<qreal> gigaHarmonySpreadSplit {"gigaHarmonySpreadSplit", 150.0};
}

WGHarmonyWheel::WGHarmonyWheel(WGSelectorDisplayConfigSP displayConfig, KisVisualColorModelSP colorModel, QWidget *parent)
    : WGSelectorWidgetBase(displayConfig, parent)
    , m_model(colorModel)
{
    setObjectName(QStringLiteral("GigaHarmonyWheel"));
    for (int i = 0; i < ModeCount; i++) m_spread[i] = defaultSpread(Mode(i));

    m_modeBox = new QComboBox(this);
    m_modeBox->setObjectName(QStringLiteral("GigaHarmonyMode"));
    m_modeBox->addItem(i18nc("color harmony", "Complementary"));
    m_modeBox->addItem(i18nc("color harmony", "Split Complementary"));
    m_modeBox->addItem(i18nc("color harmony", "Analogous"));
    m_modeBox->addItem(i18nc("color harmony", "Triadic"));
    m_modeBox->addItem(i18nc("color harmony", "Square"));
    m_modeBox->addItem(i18nc("color harmony", "Monochrome"));
    m_modeBox->setToolTip(i18n("Color harmony. Click a swatch to use it (Shift+click: background color).\n"
                               "Drag the big dot to change the base color, a small dot to widen or turn the harmony."));
    connect(m_modeBox, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        setMode(i);
        saveSettings();
    });

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(displayConfig.data(), SIGNAL(sigDisplayConfigurationChanged()), SLOT(slotDisplayChanged()));
    updateSettings();
    connectToModel();
    if (m_model && m_model->colorModel() != KisVisualColorModel::None) {
        slotChannelValuesChanged(m_model->channelValues());
    }
}

void WGHarmonyWheel::setModel(KisVisualColorModelSP colorModel)
{
    if (m_model) {
        m_model->disconnect(this);
        disconnect(m_model.data());
    }
    m_model = colorModel;
    connectToModel();
    m_haveBase = false;
    m_discZ = -1;
    if (m_model->colorModel() != KisVisualColorModel::None) {
        slotChannelValuesChanged(m_model->channelValues());
    }
    update();
}

void WGHarmonyWheel::connectToModel()
{
    if (!m_model) return;
    connect(m_model.data(), SIGNAL(sigColorModelChanged()), SLOT(slotModelReset()));
    connect(m_model.data(), SIGNAL(sigColorSpaceChanged()), SLOT(slotModelReset()));
    connect(m_model.data(), SIGNAL(sigChannelValuesChanged(QVector4D,quint32)),
            SLOT(slotChannelValuesChanged(QVector4D)));
    // Same path as the shade selector: the model turns channel values into
    // the panel's color, the panel then sets it on the canvas.
    connect(this, SIGNAL(sigChannelValuesChanged(QVector4D)),
            m_model.data(), SLOT(slotSetChannelValues(QVector4D)));
}

void WGHarmonyWheel::updateSettings()
{
    WGConfig::Accessor cfg;
    m_spread[Analogous] = qBound(minSpread(Analogous), cfg.get(WGConfig::gigaHarmonySpreadAnalogous), maxSpread(Analogous));
    m_spread[SplitComplementary] = qBound(minSpread(SplitComplementary), cfg.get(WGConfig::gigaHarmonySpreadSplit),
                                          maxSpread(SplitComplementary));
    setMode(cfg.get(WGConfig::gigaHarmonyMode));
}

void WGHarmonyWheel::saveSettings() const
{
    WGConfig::Accessor cfg(false);
    cfg.set(WGConfig::gigaHarmonyMode, m_mode);
    cfg.set(WGConfig::gigaHarmonySpreadAnalogous, m_spread[Analogous]);
    cfg.set(WGConfig::gigaHarmonySpreadSplit, m_spread[SplitComplementary]);
}

void WGHarmonyWheel::setMode(int mode)
{
    mode = qBound(0, mode, int(ModeCount) - 1);
    if (m_modeBox->currentIndex() != mode) m_modeBox->setCurrentIndex(mode);
    if (mode == m_mode && !m_colors.isEmpty()) return;
    m_mode = mode;
    recompute();
    // the current color may be one of the new harmony's colors
    m_active = -1;
    if (m_model && m_model->colorModel() != KisVisualColorModel::None) {
        const QVector4D cur = m_model->channelValues();
        for (int i = 0; i < m_colors.size(); i++) {
            if (sameColor(cur, m_colors[i])) { m_active = i; break; }
        }
    }
    update();
}

qreal WGHarmonyWheel::spread() const
{
    return m_spread[m_mode];
}

void WGHarmonyWheel::setSpread(qreal degrees)
{
    if (!hasSpread(Mode(m_mode))) return;
    m_spread[m_mode] = qBound(minSpread(Mode(m_mode)), degrees, maxSpread(Mode(m_mode)));
    recompute();
    update();
}

bool WGHarmonyWheel::usable() const
{
    return m_model && m_model->isHSXModel() && isEnabled();
}

void WGHarmonyWheel::recompute()
{
    m_colors = m_haveBase ? harmony(Mode(m_mode), m_base, m_spread[m_mode]) : QVector<QVector4D>();
    m_baseIndex = baseIndex(Mode(m_mode), m_base);
}

void WGHarmonyWheel::slotChannelValuesChanged(const QVector4D &values)
{
    if (m_ownUpdate) return; // our own color coming back
    if (!m_model || !m_model->isHSXModel()) { update(); return; }
    // The current color is one of ours (picked from a swatch, or the canvas
    // color coming back after 8-bit rounding): keep the harmony, light it up.
    if (m_haveBase) {
        for (int i = 0; i < m_colors.size(); i++) {
            if (sameColor(values, m_colors[i])) {
                m_active = i;
                update();
                return;
            }
        }
    }
    // Any other color becomes the new base.
    m_base = values;
    m_haveBase = true;
    recompute();
    m_active = m_baseIndex;
    update();
}

void WGHarmonyWheel::setActiveValues(const QVector4D &values)
{
    m_ownUpdate = true;
    Q_EMIT sigChannelValuesChanged(values);
    m_ownUpdate = false;
}

QColor WGHarmonyWheel::swatchColor(int i) const
{
    return (i >= 0 && i < m_colors.size()) ? displayColor(m_colors[i]) : QColor();
}

void WGHarmonyWheel::pickSwatch(int i, bool background)
{
    if (i < 0 || i >= m_colors.size() || !usable()) return;
    if (background) {
        Q_EMIT sigBackgroundColorPicked(m_model->convertChannelValuesToKoColor(m_colors[i]));
        return;
    }
    m_active = i;
    setActiveValues(m_colors[i]);
    update();
}

void WGHarmonyWheel::slotModelReset()
{
    m_discZ = -1; // new color space or model: repaint the disc
    m_haveBase = false;
    if (m_model && m_model->colorModel() != KisVisualColorModel::None) {
        slotChannelValuesChanged(m_model->channelValues());
    }
    update();
}

void WGHarmonyWheel::slotDisplayChanged()
{
    m_discZ = -1;
    update();
}

// ---- geometry --------------------------------------------------------------

int WGHarmonyWheel::heightForWidth(int w) const
{
    const int disc = qBound(MIN_DISC, w, MAX_DISC);
    return m_modeBox->sizeHint().height() + GAP + disc + GAP + SWATCH_H + 2;
}

QSize WGHarmonyWheel::sizeHint() const
{
    return QSize(MAX_DISC, heightForWidth(MAX_DISC));
}

QSize WGHarmonyWheel::minimumSizeHint() const
{
    return QSize(MIN_DISC, heightForWidth(MIN_DISC));
}

void WGHarmonyWheel::resizeEvent(QResizeEvent *event)
{
    WGSelectorWidgetBase::resizeEvent(event);
    m_modeBox->setGeometry(0, 0, width(), m_modeBox->sizeHint().height());
    const int wanted = heightForWidth(width());
    if (height() != wanted) setFixedHeight(wanted); // keep the disc square
    m_discZ = -1;
}

void WGHarmonyWheel::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::EnabledChange || event->type() == QEvent::PaletteChange) update();
    WGSelectorWidgetBase::changeEvent(event);
}

QRectF WGHarmonyWheel::discRect() const
{
    const qreal top = m_modeBox->sizeHint().height() + GAP;
    const qreal room = height() - top - GAP - SWATCH_H - 2;
    const qreal d = qMax<qreal>(10, qMin<qreal>(width() - 2, room));
    return QRectF((width() - d) / 2.0, top, d, d);
}

QRectF WGHarmonyWheel::swatchRect(int i) const
{
    const int n = qMax(1, m_colors.size());
    const qreal gap = 3;
    const qreal total = width() - 2;
    const qreal w = (total - gap * (n - 1)) / n;
    return QRectF(1 + i * (w + gap), height() - SWATCH_H - 1, w, SWATCH_H);
}

// Hue 0 (red) at the top, going clockwise; saturation grows outward.
QPointF WGHarmonyWheel::dotPos(const QVector4D &v) const
{
    const QRectF r = discRect();
    const qreal radius = r.width() / 2.0;
    const qreal a = v.x() * 2.0 * M_PI;
    const qreal s = qBound(0.0, qreal(v.y()), 1.0);
    return r.center() + QPointF(std::sin(a), -std::cos(a)) * radius * s;
}

QVector4D WGHarmonyWheel::valuesAt(const QPointF &pos, qreal z) const
{
    const QRectF r = discRect();
    const QPointF d = pos - r.center();
    const qreal radius = r.width() / 2.0;
    const qreal s = qMin(1.0, std::hypot(d.x(), d.y()) / radius);
    const qreal hue = wrapHue(std::atan2(d.x(), -d.y()) / (2.0 * M_PI));
    QVector4D v = m_base;
    v.setX(hue);
    v.setY(s);
    v.setZ(z);
    return v;
}

int WGHarmonyWheel::dotAt(const QPointF &pos) const
{
    if (!m_haveBase) return -1;
    // Monochrome: every dot sits on the base, only the base can be dragged.
    int best = -1;
    qreal bestDist = BASE_DOT + 4;
    for (int i = 0; i < m_colors.size(); i++) {
        if (m_mode == Monochrome && i != m_baseIndex) continue;
        const QPointF d = dotPos(m_colors[i]) - pos;
        const qreal dist = std::hypot(d.x(), d.y());
        // the base wins ties (it is drawn on top)
        if (dist < bestDist || (i == m_baseIndex && dist <= bestDist)) { best = i; bestDist = dist; }
    }
    return best;
}

// ---- painting --------------------------------------------------------------

QColor WGHarmonyWheel::displayColor(const QVector4D &v) const
{
    if (!m_model || m_model->colorModel() == KisVisualColorModel::None) return QColor();
    const KoColor c = m_model->convertChannelValuesToKoColor(v);
    return displayConverter()->toQColor(c, displayConfiguration()->previewInPaintingCS());
}

void WGHarmonyWheel::renderDisc()
{
    // Draw the disc at full brightness (HSV value 1, or mid lightness for
    // HSL/HSI/HSY), like a printed color wheel: a dark current color would
    // otherwise turn it black. The dots and swatches show the real colors.
    const qreal z = (m_model->colorModel() == KisVisualColorModel::HSV) ? 1.0 : 0.5;
    const QRectF r = discRect();
    const qreal dpr = devicePixelRatioF();
    const int size = qMax(1, qRound(r.width() * dpr));
    if (m_discZ == z && m_disc.width() == size) return;

    // Few real color conversions (they go through color management), then
    // cheap bilinear blending for each pixel.
    QVector<QColor> grid((HUE_STEPS + 1) * (SAT_STEPS + 1));
    QVector4D v = m_model->channelValues();
    v.setZ(z);
    for (int h = 0; h <= HUE_STEPS; h++) {
        for (int s = 0; s <= SAT_STEPS; s++) {
            v.setX(qreal(h % HUE_STEPS) / HUE_STEPS);
            v.setY(qreal(s) / SAT_STEPS);
            grid[h * (SAT_STEPS + 1) + s] = displayColor(v);
        }
    }

    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    const qreal radius = size / 2.0;
    for (int y = 0; y < size; y++) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < size; x++) {
            const qreal dx = x + 0.5 - radius, dy = y + 0.5 - radius;
            const qreal dist = std::hypot(dx, dy);
            const qreal alpha = qBound(0.0, radius - dist, 1.0); // smooth edge
            if (alpha <= 0) continue;
            const qreal hf = wrapHue(std::atan2(dx, -dy) / (2.0 * M_PI)) * HUE_STEPS;
            const qreal sf = qMin(1.0, dist / radius) * SAT_STEPS;
            const int h0 = qMin(int(hf), HUE_STEPS - 1), s0 = qMin(int(sf), SAT_STEPS - 1);
            const qreal th = hf - h0, ts = sf - s0;
            const QColor &c00 = grid[h0 * (SAT_STEPS + 1) + s0];
            const QColor &c01 = grid[h0 * (SAT_STEPS + 1) + s0 + 1];
            const QColor &c10 = grid[(h0 + 1) * (SAT_STEPS + 1) + s0];
            const QColor &c11 = grid[(h0 + 1) * (SAT_STEPS + 1) + s0 + 1];
            auto mix = [&](int (QColor::*ch)() const) {
                const qreal a = (c00.*ch)() * (1 - ts) + (c01.*ch)() * ts;
                const qreal b = (c10.*ch)() * (1 - ts) + (c11.*ch)() * ts;
                return a * (1 - th) + b * th;
            };
            const int rr = qRound(mix(&QColor::red) * alpha);
            const int gg = qRound(mix(&QColor::green) * alpha);
            const int bb = qRound(mix(&QColor::blue) * alpha);
            line[x] = qRgba(rr, gg, bb, qRound(alpha * 255));
        }
    }
    img.setDevicePixelRatio(dpr);
    m_disc = img;
    m_discZ = z;
}

void WGHarmonyWheel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF disc = discRect();

    if (!m_model || !m_model->isHSXModel()) {
        p.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        p.drawText(disc, Qt::AlignCenter | Qt::TextWordWrap,
                   i18n("Harmonies need a hue: pick an RGB color space."));
        return;
    }

    renderDisc();
    if (!isEnabled()) p.setOpacity(0.4);
    p.drawImage(disc.topLeft(), m_disc);
    if (!m_haveBase) return;

    const QPointF center = disc.center();
    const QColor ink(255, 255, 255, 200);
    const QColor shadow(0, 0, 0, 120);

    // spokes from the center to every dot, under the dots
    if (m_mode != Monochrome) {
        for (int i = 0; i < m_colors.size(); i++) {
            const QPointF d = dotPos(m_colors[i]);
            p.setPen(QPen(shadow, 3.0));
            p.drawLine(center, d);
            p.setPen(QPen(ink, 1.2));
            p.drawLine(center, d);
        }
    }
    auto drawDot = [&](const QVector4D &v, qreal radius, bool active) {
        const QPointF d = dotPos(v);
        p.setPen(QPen(shadow, active ? 4.5 : 3.5));
        p.setBrush(displayColor(v));
        p.drawEllipse(d, radius, radius);
        p.setPen(QPen(Qt::white, active ? 2.5 : 1.5));
        p.drawEllipse(d, radius, radius);
    };
    for (int i = 0; i < m_colors.size(); i++) {
        if (i == m_baseIndex || m_mode == Monochrome) continue;
        drawDot(m_colors[i], DOT, i == m_active);
    }
    if (m_baseIndex < m_colors.size()) {
        drawDot(m_colors[m_baseIndex], BASE_DOT, m_baseIndex == m_active);
    }

    // swatches
    const QColor accent = palette().color(QPalette::Highlight);
    for (int i = 0; i < m_colors.size(); i++) {
        const QRectF s = swatchRect(i);
        p.setPen(QPen(palette().color(QPalette::Window).darker(160), 1.0));
        p.setBrush(displayColor(m_colors[i]));
        p.drawRoundedRect(s.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        if (i == m_baseIndex) { // small dot = the base color
            p.setPen(QPen(shadow, 1.0));
            p.setBrush(Qt::white);
            p.drawEllipse(QPointF(s.center().x(), s.bottom() - 5), 2.2, 2.2);
        }
        if (i == m_active) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(accent, 2.0));
            p.drawRoundedRect(s.adjusted(1, 1, -1, -1), 3, 3);
        }
    }
}

// ---- mouse -----------------------------------------------------------------

void WGHarmonyWheel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !usable()) { event->ignore(); return; }
    const QPointF pos = event->position();
    for (int i = 0; i < m_colors.size(); i++) {
        if (swatchRect(i).contains(pos)) {
            pickSwatch(i, event->modifiers() & Qt::ShiftModifier);
            event->accept();
            return;
        }
    }
    const QRectF disc = discRect();
    const QPointF d = pos - disc.center();
    const int dot = dotAt(pos);
    if (dot < 0 && std::hypot(d.x(), d.y()) > disc.width() / 2.0 + 2) { event->ignore(); return; }

    Q_EMIT sigColorInteraction(true);
    if (dot >= 0 && dot != m_baseIndex) {
        m_drag = Other;
        m_dragIndex = dot;
        m_active = dot;
        setActiveValues(m_colors[dot]);
        update();
    } else {
        m_drag = Base;
        if (dot < 0) mouseMoveEvent(event); // clicked the disc: jump there
        else { m_active = m_baseIndex; setActiveValues(m_base); update(); }
    }
    event->accept();
}

void WGHarmonyWheel::mouseMoveEvent(QMouseEvent *event)
{
    if (m_drag == None) return;
    const QVector4D v = valuesAt(event->position(), m_base.z());
    if (m_drag == Base) {
        m_base = v;
        m_haveBase = true;
        recompute();
        m_active = m_baseIndex;
        setActiveValues(m_base);
    } else {
        const Mode mode = Mode(m_mode);
        m_base.setY(v.y()); // distance from the center = saturation of all
        if (hasSpread(mode)) {
            m_spread[mode] = qBound(minSpread(mode), qAbs(hueDelta(m_base.x(), v.x())), maxSpread(mode));
        } else {
            // fixed angles: dragging any dot turns the whole harmony
            const QVector<qreal> offsets = hueOffsets(mode, m_spread[mode]);
            if (m_dragIndex < offsets.size()) m_base.setX(wrapHue(v.x() - offsets[m_dragIndex] / 360.0));
        }
        recompute();
        m_active = m_dragIndex;
        if (m_dragIndex < m_colors.size()) setActiveValues(m_colors[m_dragIndex]);
    }
    update();
}

void WGHarmonyWheel::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_drag == None) { event->ignore(); return; }
    const bool spreadChanged = m_drag == Other && hasSpread(Mode(m_mode));
    m_drag = None;
    m_dragIndex = -1;
    if (spreadChanged) saveSettings();
    Q_EMIT sigColorInteraction(false);
    event->accept();
}

/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaProcreateWidgets.h"

#include <QAction>
#include <QCheckBox>
#include <QConicalGradient>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include <klocalizedstring.h>
#include <kactioncollection.h>

#include <KoColor.h>
#include <KisResourceTypes.h>
#include <KisTagFilterResourceProxyModel.h>
#include <KisTagModel.h>
#include <KisTag.h>
#include <brushengine/kis_paintop_preset.h>

#include "KisViewManager.h"
#include "kis_canvas_resource_provider.h"
#include "kis_config.h"
#include "kis_icon_utils.h"
#include "kis_image.h"
#include "kis_node.h"
#include "kis_node_manager.h"
#include "KisResourceServerProvider.h"

namespace {
const QColor CARD_BG(38, 38, 40, 248);
const QColor CARD_BORDER(70, 70, 74);
const int ARROW_H = 9;
const int RADIUS = 14;
}

// ============================================================================
// GigaPopover
// ============================================================================

GigaPopover::GigaPopover(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(14, 14 + ARROW_H, 14, 14);
}

void GigaPopover::setContent(QWidget *content)
{
    content->setParent(this);
    m_layout->addWidget(content);
}

void GigaPopover::showBelow(QWidget *anchor)
{
    adjustSize();
    QPoint pos(100, 100);
    if (anchor) {
        const QPoint anchorBottom = anchor->mapToGlobal(QPoint(anchor->width() / 2, anchor->height()));
        // Buttons on the right of the top bar: open to the left of the anchor.
        int x = anchorBottom.x() - width() + 48;
        if (QScreen *screen = anchor->screen()) {
            const QRect area = screen->availableGeometry();
            x = qBound(area.left() + 8, x, area.right() - width() - 8);
        }
        pos = QPoint(x, anchorBottom.y() + 2);
        m_arrowX = qBound(RADIUS + 8, anchorBottom.x() - x, width() - RADIUS - 8);
    }
    move(pos);
    show();
}

void GigaPopover::showLeftOf(QWidget *anchor)
{
    m_arrowOnRight = true;
    m_layout->setContentsMargins(14, 14, 14 + ARROW_H, 14);
    adjustSize();
    QPoint pos(100, 100);
    if (anchor) {
        const QPoint anchorLeft = anchor->mapToGlobal(QPoint(0, anchor->height() / 2));
        int y = anchorLeft.y() - 40;
        if (QScreen *screen = anchor->screen()) {
            const QRect area = screen->availableGeometry();
            y = qBound(area.top() + 8, y, area.bottom() - height() - 8);
        }
        pos = QPoint(anchorLeft.x() - width() - 2, y);
        m_arrowY = qBound(RADIUS + 8, anchorLeft.y() - y, height() - RADIUS - 8);
    }
    move(pos);
    show();
}

void GigaPopover::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF card = m_arrowOnRight ? QRectF(rect()).adjusted(4, 4, -4 - ARROW_H, -4)
                                       : QRectF(rect()).adjusted(4, 4 + ARROW_H, -4, -4);

    // Soft shadow: a few faint, growing rounded rectangles.
    for (int i = 4; i >= 1; i--) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 18));
        p.drawRoundedRect(card.adjusted(-i, -i + 2, i, i + 2), RADIUS + i, RADIUS + i);
    }

    QPainterPath shape;
    shape.addRoundedRect(card, RADIUS, RADIUS);
    QPainterPath arrow;
    if (m_arrowOnRight) {
        arrow.moveTo(card.right() - 1, m_arrowY - ARROW_H);
        arrow.lineTo(card.right() + ARROW_H, m_arrowY);
        arrow.lineTo(card.right() - 1, m_arrowY + ARROW_H);
    } else {
        arrow.moveTo(m_arrowX - ARROW_H, card.top() + 1);
        arrow.lineTo(m_arrowX, card.top() - ARROW_H);
        arrow.lineTo(m_arrowX + ARROW_H, card.top() + 1);
    }
    arrow.closeSubpath();
    shape = shape.united(arrow);

    p.setPen(QPen(CARD_BORDER, 1));
    p.setBrush(CARD_BG);
    p.drawPath(shape);
}

void GigaPopover::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (onHide) {
        auto callback = onHide;
        onHide = nullptr;
        callback();
    }
}

// ============================================================================
// GigaColorDisc
// ============================================================================

GigaColorDisc::GigaColorDisc(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(250, 250);
    setCursor(Qt::CrossCursor);
}

void GigaColorDisc::setColor(const QColor &color)
{
    float h, s, v;
    color.getHsvF(&h, &s, &v);
    if (h >= 0) m_hue = h; // keep hue for greys
    m_sat = s;
    m_val = v;
    update();
}

QColor GigaColorDisc::color() const
{
    return QColor::fromHsvF(m_hue, m_sat, m_val);
}

QRectF GigaColorDisc::squareRect() const
{
    // Largest square inside the inner circle of the ring.
    const qreal inner = width() / 2.0 - 26;
    const qreal half = inner / std::sqrt(2.0);
    const QPointF c = QRectF(rect()).center();
    return QRectF(c.x() - half, c.y() - half, 2 * half, 2 * half);
}

void GigaColorDisc::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
    const QPointF c = r.center();

    // Hue ring.
    QConicalGradient hueGradient(c, 0);
    for (int i = 0; i <= 12; i++) {
        hueGradient.setColorAt(i / 12.0, QColor::fromHsvF(fmod(1.0 - i / 12.0, 1.0), 1, 1));
    }
    QPainterPath ring;
    ring.addEllipse(r);
    ring.addEllipse(r.adjusted(20, 20, -20, -20));
    p.fillPath(ring, hueGradient);

    // Saturation (x) / brightness (y) square for the current hue.
    const QRectF sq = squareRect();
    QLinearGradient satGradient(sq.topLeft(), sq.topRight());
    satGradient.setColorAt(0, Qt::white);
    satGradient.setColorAt(1, QColor::fromHsvF(m_hue, 1, 1));
    p.fillRect(sq, satGradient);
    QLinearGradient valGradient(sq.topLeft(), sq.bottomLeft());
    valGradient.setColorAt(0, QColor(0, 0, 0, 0));
    valGradient.setColorAt(1, Qt::black);
    p.fillRect(sq, valGradient);

    // Markers.
    p.setBrush(Qt::NoBrush);
    const qreal ringRadius = r.width() / 2 - 10;
    const qreal angle = m_hue * 2 * M_PI;
    const QPointF huePos = c + QPointF(std::cos(angle), -std::sin(angle)) * ringRadius;
    p.setPen(QPen(Qt::white, 2.5));
    p.drawEllipse(huePos, 8, 8);
    const QPointF svPos(sq.left() + m_sat * sq.width(), sq.top() + (1 - m_val) * sq.height());
    p.setPen(QPen(m_val > 0.5 ? Qt::black : Qt::white, 2));
    p.drawEllipse(svPos, 7, 7);
}

void GigaColorDisc::pick(const QPointF &pos)
{
    const QPointF c = QRectF(rect()).center();
    if (m_dragging == Ring) {
        qreal angle = std::atan2(-(pos.y() - c.y()), pos.x() - c.x());
        if (angle < 0) angle += 2 * M_PI;
        m_hue = angle / (2 * M_PI);
    } else if (m_dragging == Square) {
        const QRectF sq = squareRect();
        m_sat = qBound(0.0, (pos.x() - sq.left()) / sq.width(), 1.0);
        m_val = qBound(0.0, 1.0 - (pos.y() - sq.top()) / sq.height(), 1.0);
    }
    update();
    if (onColorPicked) onColorPicked(color());
}

void GigaColorDisc::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    const qreal d = QLineF(pos, QRectF(rect()).center()).length();
    if (squareRect().contains(pos)) m_dragging = Square;
    else if (d > width() / 2.0 - 24 && d < width() / 2.0) m_dragging = Ring;
    else m_dragging = None;
    if (m_dragging != None) pick(pos);
}

void GigaColorDisc::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging != None) pick(event->position());
}

void GigaColorDisc::mouseReleaseEvent(QMouseEvent *)
{
    if (m_dragging != None && onColorReleased) onColorReleased(color());
    m_dragging = None;
}

// ============================================================================
// GigaColorPanel
// ============================================================================

GigaColorPanel::GigaColorPanel(KisViewManager *viewManager, QWidget *parent)
    : QWidget(parent)
    , m_viewManager(viewManager)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    QLabel *title = new QLabel(i18nc("Procreate mode popover", "Colors"), this);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    layout->addWidget(title);

    m_disc = new GigaColorDisc(this);
    layout->addWidget(m_disc, 0, Qt::AlignHCenter);
    if (m_viewManager) {
        m_disc->setColor(m_viewManager->canvasResourceProvider()->fgColor().toQColor());
    }
    m_disc->onColorPicked = [this](const QColor &c) {
        if (!m_viewManager) return;
        KisCanvasResourceProvider *provider = m_viewManager->canvasResourceProvider();
        provider->slotSetFGColor(KoColor(c, provider->fgColor().colorSpace()));
    };
    m_disc->onColorReleased = [this](const QColor &c) { rememberRecent(c); };

    QLabel *recentTitle = new QLabel(i18nc("Procreate mode popover", "History"), this);
    layout->addWidget(recentTitle);
    m_recentRow = new QWidget(this);
    new QHBoxLayout(m_recentRow);
    m_recentRow->layout()->setContentsMargins(0, 0, 0, 0);
    m_recentRow->layout()->setSpacing(6);
    layout->addWidget(m_recentRow);
    rebuildRecent();
}

void GigaColorPanel::rememberRecent(const QColor &color)
{
    KisConfig cfg(false);
    QStringList recent = cfg.readEntry<QStringList>("gigaRecentColors", QStringList());
    recent.removeAll(color.name());
    recent.prepend(color.name());
    while (recent.size() > 10) recent.removeLast();
    cfg.writeEntry<QStringList>("gigaRecentColors", recent);
    rebuildRecent();
}

void GigaColorPanel::rebuildRecent()
{
    QLayout *row = m_recentRow->layout();
    while (QLayoutItem *item = row->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QStringList recent = KisConfig(true).readEntry<QStringList>("gigaRecentColors", QStringList());
    for (const QString &name : recent) {
        QToolButton *swatch = new QToolButton(m_recentRow);
        swatch->setFixedSize(20, 20);
        swatch->setAutoRaise(true);
        swatch->setStyleSheet(QStringLiteral("QToolButton{background:%1;border-radius:10px;border:1px solid #666;}").arg(name));
        const QColor color(name);
        QObject::connect(swatch, &QToolButton::clicked, swatch, [this, color]() {
            m_disc->setColor(color);
            if (m_disc->onColorPicked) m_disc->onColorPicked(color);
        });
        row->addWidget(swatch);
    }
    static_cast<QHBoxLayout *>(row)->addStretch(1);
}

// ============================================================================
// GigaLayersPanel
// ============================================================================

GigaLayersPanel::GigaLayersPanel(KisViewManager *viewManager, QWidget *parent)
    : QWidget(parent)
    , m_viewManager(viewManager)
{
    setMinimumWidth(300);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout *header = new QHBoxLayout();
    QLabel *title = new QLabel(i18nc("Procreate mode popover", "Layers"), this);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    header->addWidget(title);
    header->addStretch(1);
    QToolButton *add = new QToolButton(this);
    add->setText(QStringLiteral("+"));
    add->setAutoRaise(true);
    add->setToolTip(i18nc("@info:tooltip", "New layer"));
    QFont addFont = add->font();
    addFont.setPointSizeF(addFont.pointSizeF() * 1.6);
    add->setFont(addFont);
    QObject::connect(add, &QToolButton::clicked, this, [this]() {
        if (!m_viewManager) return;
        if (QAction *action = m_viewManager->actionCollection()->action(QStringLiteral("add_new_paint_layer"))) {
            action->trigger();
        }
        QTimer::singleShot(50, this, [this]() { rebuild(); });
    });
    header->addWidget(add);
    layout->addLayout(header);

    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumHeight(300);
    QWidget *cardsHost = new QWidget(scroll);
    m_cards = new QVBoxLayout(cardsHost);
    m_cards->setContentsMargins(0, 0, 0, 0);
    m_cards->setSpacing(4);
    scroll->setWidget(cardsHost);
    layout->addWidget(scroll);

    rebuild();
}

void GigaLayersPanel::rebuild()
{
    while (QLayoutItem *item = m_cards->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (!m_viewManager || !m_viewManager->image()) return;
    KisNodeSP active = m_viewManager->nodeManager()->activeNode();

    // Top of the stack first, like the Layers panel; groups indent children.
    std::function<void(KisNodeSP, int)> addNode = [&](KisNodeSP node, int depth) {
        for (KisNodeSP child = node->lastChild(); child; child = child->prevSibling()) {
            if (child->isFakeNode()) continue; // e.g. selection decorations

            QFrame *card = new QFrame();
            const bool selected = child == active;
            card->setStyleSheet(selected
                ? QStringLiteral("QFrame{background:#2f5f9a;border-radius:8px;}")
                : QStringLiteral("QFrame{background:#303033;border-radius:8px;}"));
            QHBoxLayout *row = new QHBoxLayout(card);
            row->setContentsMargins(6 + depth * 14, 4, 8, 4);

            QLabel *thumb = new QLabel(card);
            thumb->setFixedSize(56, 42);
            thumb->setPixmap(QPixmap::fromImage(child->createThumbnail(56, 42, Qt::KeepAspectRatio)));
            thumb->setStyleSheet(QStringLiteral("background:#d8d8d8;border-radius:3px;"));
            thumb->setAttribute(Qt::WA_TransparentForMouseEvents); // taps reach the card
            row->addWidget(thumb);

            QLabel *name = new QLabel(child->name(), card);
            name->setStyleSheet(QStringLiteral("background:transparent;"));
            name->setAttribute(Qt::WA_TransparentForMouseEvents);
            row->addWidget(name, 1);

            QCheckBox *visible = new QCheckBox(card);
            visible->setChecked(child->visible());
            visible->setToolTip(i18nc("@info:tooltip", "Visible"));
            KisNodeSP nodeRef = child;
            QObject::connect(visible, &QCheckBox::toggled, card, [nodeRef](bool on) mutable {
                nodeRef->setVisible(on);
                nodeRef->setDirty();
            });
            row->addWidget(visible);

            // Tap a card to make that layer active.
            card->setCursor(Qt::PointingHandCursor);
            QPushButton *hit = new QPushButton(card);
            hit->setFlat(true);
            hit->setStyleSheet(QStringLiteral("QPushButton{background:transparent;border:0;}"));
            hit->setGeometry(0, 0, 4000, 60); // invisible, covers the card behind the other widgets
            hit->lower();
            QObject::connect(hit, &QPushButton::clicked, this, [this, nodeRef]() {
                if (m_viewManager) m_viewManager->nodeManager()->slotNonUiActivatedNode(nodeRef);
                QTimer::singleShot(0, this, [this]() { rebuild(); });
            });

            m_cards->addWidget(card);
            if (child->childCount() > 0) addNode(child, depth + 1);
        }
    };
    addNode(m_viewManager->image()->root(), 0);
    m_cards->addStretch(1);
}

// ============================================================================
// GigaBrushLibrary
// ============================================================================

GigaBrushLibrary::GigaBrushLibrary(KisViewManager *viewManager, QWidget *parent)
    : QWidget(parent)
    , m_viewManager(viewManager)
{
    setMinimumSize(520, 420);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    QLabel *title = new QLabel(i18nc("Procreate mode popover", "Brush Library"), this);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    layout->addWidget(title);

    QHBoxLayout *columns = new QHBoxLayout();
    m_sets = new QListWidget(this);
    m_sets->setFixedWidth(170);
    m_sets->setFrameShape(QFrame::NoFrame);
    m_brushes = new QListWidget(this);
    m_brushes->setFrameShape(QFrame::NoFrame);
    m_brushes->setIconSize(QSize(64, 64));
    m_brushes->setSpacing(2);
    columns->addWidget(m_sets);
    columns->addWidget(m_brushes, 1);
    layout->addLayout(columns);

    // Brush sets = Krita's brush preset tags (plus "All").
    KisTagModel tags(ResourceType::PaintOpPresets);
    for (int row = 0; row < tags.rowCount(); row++) {
        KisTagSP tag = tags.tagForIndex(tags.index(row, 0));
        if (!tag) continue;
        QListWidgetItem *item = new QListWidgetItem(tag->name(), m_sets);
        item->setData(Qt::UserRole, row);
    }
    QObject::connect(m_sets, &QListWidget::currentRowChanged, this, [this](int row) { showSet(row); });
    QObject::connect(m_brushes, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!m_viewManager) return;
        KisPaintOpPresetSP preset = KisResourceServerProvider::instance()->paintOpPresetServer()
                ->resource(QString(), QString(), item->data(Qt::UserRole).toString());
        if (preset && preset->valid()) {
            m_viewManager->canvasResourceProvider()->setPaintOpPreset(preset);
        }
    });

    const int last = KisConfig(true).readEntry<int>("gigaBrushLibrarySet", 0);
    m_sets->setCurrentRow(qBound(0, last, qMax(0, m_sets->count() - 1)));
}

void GigaBrushLibrary::showSet(int row)
{
    m_brushes->clear();
    if (row < 0) return;
    KisConfig(false).writeEntry<int>("gigaBrushLibrarySet", row);

    KisTagModel tags(ResourceType::PaintOpPresets);
    KisTagSP tag = tags.tagForIndex(tags.index(m_sets->item(row)->data(Qt::UserRole).toInt(), 0));
    if (!tag) return;
    KisTagFilterResourceProxyModel presets(ResourceType::PaintOpPresets);
    presets.setTagFilter(tag);

    KisPaintOpPresetSP current = m_viewManager ? m_viewManager->canvasResourceProvider()->currentPreset() : KisPaintOpPresetSP();
    for (int i = 0; i < presets.rowCount(); i++) {
        KisPaintOpPresetSP preset = presets.resourceForIndex(presets.index(i, 0)).dynamicCast<KisPaintOpPreset>();
        if (!preset) continue;
        QListWidgetItem *item = new QListWidgetItem(QIcon(QPixmap::fromImage(preset->image())), preset->name(), m_brushes);
        item->setData(Qt::UserRole, preset->name());
        item->setSizeHint(QSize(0, 70));
        if (current && current->name() == preset->name()) {
            m_brushes->setCurrentItem(item);
        }
    }
}

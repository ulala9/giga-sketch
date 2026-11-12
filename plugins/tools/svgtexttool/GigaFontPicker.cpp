/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaFontPicker.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <KConfigGroup>
#include <KSharedConfig>
#include <klocalizedstring.h>

#include <KisResourceModel.h>
#include <KisResourceModelProvider.h>
#include <KisResourceTypes.h>
#include <KoResource.h>

namespace {

const int ROW_HEIGHT = 26;
const int STAR_WIDTH = 22;
const int PREVIEW_WIDTH = 110;
const int MAX_RECENT = 8;

enum Roles {
    FamilyRole = Qt::UserRole + 1, ///< font family name (empty for headers)
    HeaderRole,                    ///< true for section titles
};

KConfigGroup pickerConfig()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("GigaFontPicker"));
}

/// Krita's preview is black text on a white 256x256 square. Turn it into
/// text-coloured pixels on transparent, cropped to the text, so it reads on
/// any theme.
QImage tintPreview(const QImage &source, const QColor &color)
{
    if (source.isNull()) return QImage();
    const QImage gray = source.convertToFormat(QImage::Format_Grayscale8);
    int left = gray.width(), right = -1, top = gray.height(), bottom = -1;
    for (int y = 0; y < gray.height(); y++) {
        const uchar *line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); x++) {
            if (line[x] < 245) {
                left = qMin(left, x); right = qMax(right, x);
                top = qMin(top, y); bottom = qMax(bottom, y);
            }
        }
    }
    if (right < left || bottom < top) return QImage();

    const QRect box(QPoint(left, top), QPoint(right, bottom));
    QImage out(box.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < box.height(); y++) {
        const uchar *in = gray.constScanLine(box.top() + y) + box.left();
        QRgb *o = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < box.width(); x++) {
            const int alpha = 255 - in[x];
            o[x] = qPremultiply(qRgba(color.red(), color.green(), color.blue(), alpha));
        }
    }
    return out;
}

/// Draws one row: star, family name, preview on the right. Headers are
/// small grey section titles.
class FontRowDelegate : public QStyledItemDelegate
{
public:
    FontRowDelegate(GigaFontPicker *picker, std::function<QImage(const QString &)> images, const QStringList *favourites)
        : QStyledItemDelegate(picker), m_images(std::move(images)), m_favourites(favourites)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Q_UNUSED(option);
        return QSize(260, index.data(HeaderRole).toBool() ? 20 : ROW_HEIGHT);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QRect r = option.rect;
        const QPalette &pal = option.palette;
        p->save();
        if (index.data(HeaderRole).toBool()) {
            QFont f = option.font;
            f.setPointSizeF(f.pointSizeF() * 0.85);
            f.setBold(true);
            p->setFont(f);
            p->setPen(pal.color(QPalette::Disabled, QPalette::Text));
            p->drawText(r.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignBottom, index.data(Qt::DisplayRole).toString());
            p->restore();
            return;
        }

        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        if (selected) {
            p->fillRect(r, pal.highlight());
        } else if (hovered) {
            QColor h = pal.highlight().color();
            h.setAlpha(70);
            p->fillRect(r, h);
        }
        const QColor textColor = selected ? pal.highlightedText().color() : pal.text().color();
        const QString family = index.data(FamilyRole).toString();

        // Star (favourite).
        const bool fav = m_favourites->contains(family);
        p->setPen(fav ? QColor(240, 190, 60) : pal.color(QPalette::Disabled, QPalette::Text));
        p->drawText(QRect(r.left(), r.top(), STAR_WIDTH, r.height()), Qt::AlignCenter,
                    fav ? QStringLiteral("★") : QStringLiteral("☆"));

        // Name.
        p->setPen(textColor);
        const QRect nameRect(r.left() + STAR_WIDTH, r.top(), r.width() - STAR_WIDTH - PREVIEW_WIDTH - 8, r.height());
        p->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                    option.fontMetrics.elidedText(family, Qt::ElideRight, nameRect.width()));

        // Preview, scaled to fit the row, right-aligned.
        const QImage img = m_images(family);
        if (!img.isNull()) {
            const QRect area(r.right() - PREVIEW_WIDTH - 4, r.top() + 4, PREVIEW_WIDTH, r.height() - 8);
            QSize s = img.size().scaled(area.size(), Qt::KeepAspectRatio);
            const QRect target(area.left(), area.top() + (area.height() - s.height()) / 2, s.width(), s.height());
            p->setRenderHint(QPainter::SmoothPixmapTransform, true);
            if (selected) {
                // The cached preview is tinted for normal rows; re-tint on the highlight.
                QImage t = img;
                QPainter tp(&t);
                tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
                tp.fillRect(t.rect(), textColor);
                tp.end();
                p->drawImage(target, t);
            } else {
                p->drawImage(target, img);
            }
        }
        p->restore();
    }

private:
    std::function<QImage(const QString &)> m_images;
    const QStringList *m_favourites;
};

} // namespace

GigaFontPicker::GigaFontPicker(QWidget *parent)
    : QFrame(parent, Qt::Popup)
{
    setFrameShape(QFrame::StyledPanel);
    setAttribute(Qt::WA_DeleteOnClose, false);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(i18nc("@info:placeholder", "Search fonts"));
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this); // arrow keys / Enter drive the list
    connect(m_search, &QLineEdit::textChanged, this, [this]() { rebuild(); });
    layout->addWidget(m_search);

    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(false);
    m_list->setMouseTracking(true);
    m_list->viewport()->setMouseTracking(true);
    m_list->viewport()->installEventFilter(this); // star clicks
    m_list->setItemDelegate(new FontRowDelegate(this, [this](const QString &f) { return previewImage(f); }, &m_favourites));
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(m_list, &QListWidget::itemEntered, this, [this](QListWidgetItem *item) { preview(item); });
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) { preview(item); });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { choose(item); });
    layout->addWidget(m_list);

    resize(380, 460);
}

void GigaFontPicker::popup(QWidget *anchor, const QStringList &families, const QString &current)
{
    m_families = families;
    m_current = current;
    m_lastPreview.clear();
    m_chosen = false;
    m_favourites = pickerConfig().readEntry("favourites", QStringList());

    m_search->blockSignals(true);
    m_search->clear();
    m_search->blockSignals(false);
    rebuild();

    // Below the font field, kept on screen.
    QPoint pos = anchor->mapToGlobal(QPoint(0, anchor->height()));
    if (QScreen *screen = anchor->screen()) {
        const QRect avail = screen->availableGeometry();
        pos.setX(qBound(avail.left(), pos.x(), avail.right() - width()));
        if (pos.y() + height() > avail.bottom()) pos.setY(anchor->mapToGlobal(QPoint(0, 0)).y() - height());
    }
    move(pos);
    show();
    m_search->setFocus();
}

void GigaFontPicker::addRecent(const QString &family)
{
    if (family.isEmpty()) return;
    KConfigGroup cfg = pickerConfig();
    QStringList recent = cfg.readEntry("recent", QStringList());
    recent.removeAll(family);
    recent.prepend(family);
    while (recent.size() > MAX_RECENT) recent.removeLast();
    cfg.writeEntry("recent", recent);
}

void GigaFontPicker::addHeader(const QString &title)
{
    QListWidgetItem *item = new QListWidgetItem(title, m_list);
    item->setData(HeaderRole, true);
    item->setFlags(Qt::NoItemFlags);
}

void GigaFontPicker::addFont(const QString &family)
{
    QListWidgetItem *item = new QListWidgetItem(family, m_list);
    item->setData(FamilyRole, family);
    item->setToolTip(family);
}

void GigaFontPicker::rebuild()
{
    m_list->blockSignals(true); // don't preview while rebuilding
    m_list->clear();
    const QString filter = m_search->text().trimmed();
    QListWidgetItem *currentItem = nullptr;

    auto addFamily = [&](const QString &family) {
        addFont(family);
        if (!currentItem && family.compare(m_current, Qt::CaseInsensitive) == 0) currentItem = m_list->item(m_list->count() - 1);
    };

    if (filter.isEmpty()) {
        QStringList favourites;
        Q_FOREACH (const QString &f, m_favourites) if (m_families.contains(f)) favourites << f;
        QStringList recent;
        Q_FOREACH (const QString &f, pickerConfig().readEntry("recent", QStringList())) if (m_families.contains(f)) recent << f;

        if (!favourites.isEmpty()) {
            addHeader(i18nc("font picker section", "Favourites"));
            Q_FOREACH (const QString &f, favourites) addFamily(f);
        }
        if (!recent.isEmpty()) {
            addHeader(i18nc("font picker section", "Recent"));
            Q_FOREACH (const QString &f, recent) addFamily(f);
        }
        if (!favourites.isEmpty() || !recent.isEmpty()) {
            addHeader(i18nc("font picker section", "All Fonts"));
        }
        currentItem = nullptr; // prefer the entry in the full list
        Q_FOREACH (const QString &f, m_families) addFamily(f);
    } else {
        Q_FOREACH (const QString &f, m_families) {
            if (f.contains(filter, Qt::CaseInsensitive)) addFamily(f);
        }
    }

    if (currentItem) {
        m_list->setCurrentItem(currentItem);
        m_list->scrollToItem(currentItem, QAbstractItemView::PositionAtCenter);
    } else if (!filter.isEmpty()) {
        // While searching, the first match is ready for Enter.
        for (int i = 0; i < m_list->count(); i++) {
            if (!m_list->item(i)->data(HeaderRole).toBool()) { m_list->setCurrentRow(i); break; }
        }
    }
    m_list->blockSignals(false);
    // While open (i.e. while searching), show the highlighted match live.
    preview(m_list->currentItem());
}

void GigaFontPicker::preview(QListWidgetItem *item)
{
    if (!item || !isVisible()) return;
    const QString family = item->data(FamilyRole).toString();
    if (family.isEmpty() || family == m_lastPreview) return;
    // Nothing previewed yet and it's the font already in use: nothing to show.
    if (m_lastPreview.isEmpty() && family.compare(m_current, Qt::CaseInsensitive) == 0) return;
    m_lastPreview = family;
    Q_EMIT previewRequested(family);
}

void GigaFontPicker::choose(QListWidgetItem *item)
{
    if (!item) return;
    const QString family = item->data(FamilyRole).toString();
    if (family.isEmpty()) return;
    m_chosen = true;
    addRecent(family);
    hide();
    Q_EMIT chosen(family);
}

QImage GigaFontPicker::previewImage(const QString &family)
{
    auto it = m_imageCache.constFind(family);
    if (it != m_imageCache.constEnd()) return it.value();

    QImage tinted;
    if (KisAllResourcesModel *model = KisResourceModelProvider::resourceModel(ResourceType::FontFamilies)) {
        const QVector<KoResourceSP> found = model->resourcesForName(family);
        if (!found.isEmpty() && found.first()) {
            tinted = tintPreview(found.first()->image(), palette().color(QPalette::Text));
        }
    }
    m_imageCache.insert(family, tinted);
    return tinted;
}

bool GigaFontPicker::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        QKeyEvent *key = static_cast<QKeyEvent *>(event);
        switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown: {
            // Move through the list, skipping section titles.
            const int step = (key->key() == Qt::Key_Up || key->key() == Qt::Key_PageUp) ? -1 : 1;
            const int jump = (key->key() == Qt::Key_PageUp || key->key() == Qt::Key_PageDown) ? 10 : 1;
            int row = m_list->currentRow();
            for (int moved = 0; moved < jump; moved++) {
                int next = row + step;
                while (next >= 0 && next < m_list->count() && m_list->item(next)->data(HeaderRole).toBool()) next += step;
                if (next < 0 || next >= m_list->count()) break;
                row = next;
            }
            if (row >= 0) {
                m_list->setCurrentRow(row);
                m_list->scrollToItem(m_list->item(row));
            }
            return true;
        }
        case Qt::Key_Return:
        case Qt::Key_Enter:
            choose(m_list->currentItem());
            return true;
        case Qt::Key_Escape:
            hide();
            return true;
        default:
            break;
        }
    } else if (watched == m_list->viewport() && event->type() == QEvent::MouseButtonPress) {
        // Clicking the star toggles the favourite instead of picking.
        QMouseEvent *mouse = static_cast<QMouseEvent *>(event);
        QListWidgetItem *item = m_list->itemAt(mouse->position().toPoint());
        if (item && !item->data(HeaderRole).toBool() && mouse->position().x() < STAR_WIDTH) {
            const QString family = item->data(FamilyRole).toString();
            if (m_favourites.contains(family)) m_favourites.removeAll(family);
            else m_favourites.append(family);
            pickerConfig().writeEntry("favourites", m_favourites);
            m_list->viewport()->update();
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void GigaFontPicker::hideEvent(QHideEvent *event)
{
    QFrame::hideEvent(event);
    if (!m_chosen && !m_lastPreview.isEmpty()) {
        Q_EMIT previewCancelled(); // closed without picking: undo the live preview
    }
    m_lastPreview.clear();
}

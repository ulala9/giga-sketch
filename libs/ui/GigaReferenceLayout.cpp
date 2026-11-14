/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaReferenceLayout.h"

#include <QtMath>
#include <algorithm>
#include <limits>
#include <numeric>

namespace GigaReferenceLayout
{
namespace
{

bool valid(const QSizeF &s)
{
    return s.width() > 0.0 && s.height() > 0.0 && qIsFinite(s.width()) && qIsFinite(s.height());
}

qreal median(QVector<qreal> values)
{
    if (values.isEmpty()) return 1.0;
    std::sort(values.begin(), values.end());
    const int n = values.size();
    return (n % 2) ? values[n / 2] : 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

/// Invalid sizes are laid out as tiny squares so every index still gets a
/// rectangle (the callers skip them anyway).
QVector<QSizeF> sanitized(const QVector<QSizeF> &sizes)
{
    QVector<QSizeF> out = sizes;
    for (QSizeF &s : out) {
        if (!valid(s)) s = QSizeF(1.0, 1.0);
    }
    return out;
}

/// Candidate total widths between `from` and `to`, spaced geometrically.
QVector<qreal> candidateWidths(qreal from, qreal to, int steps = 48)
{
    QVector<qreal> out;
    from = qMax(from, 1e-6);
    to = qMax(to, from);
    out.append(from);
    if (to > from) {
        const qreal ratio = qPow(to / from, 1.0 / steps);
        qreal w = from;
        for (int i = 0; i < steps; ++i) {
            w *= ratio;
            out.append(w);
        }
    }
    return out;
}

// ---- Justified rows ------------------------------------------------------

QVector<QRectF> justifiedRows(const QVector<QSizeF> &sizes, qreal gap, qreal rowWidth)
{
    const int n = sizes.size();
    QVector<qreal> heights;
    for (const QSizeF &s : sizes) heights.append(s.height());
    const qreal h0 = median(heights);

    QVector<qreal> natural(n); // width of each image at the nominal row height
    for (int i = 0; i < n; ++i) natural[i] = sizes[i].width() / sizes[i].height() * h0;

    // Greedy rows: close a row when it reaches rowWidth, picking whether the
    // last image goes in or starts the next row by which keeps the row's
    // height closer to h0.
    QVector<QVector<int>> rows;
    QVector<int> row;
    qreal rowSum = 0.0;
    auto fillScale = [&](qreal sum, int count) {
        return (rowWidth - gap * (count - 1)) / qMax(sum, 1e-9);
    };
    for (int i = 0; i < n; ++i) {
        row.append(i);
        rowSum += natural[i];
        const qreal widthWithGaps = rowSum + gap * (row.size() - 1);
        if (widthWithGaps >= rowWidth) {
            if (row.size() > 1) {
                const qreal withIt = fillScale(rowSum, row.size());
                const qreal without = fillScale(rowSum - natural[i], row.size() - 1);
                if (qAbs(qLn(without)) < qAbs(qLn(withIt))) {
                    row.removeLast();
                    rows.append(row);
                    row = {i};
                    rowSum = natural[i];
                    continue;
                }
            }
            rows.append(row);
            row.clear();
            rowSum = 0.0;
        }
    }

    QVector<QRectF> rects(n);
    qreal y = 0.0;
    auto placeRow = [&](const QVector<int> &r, qreal scale) {
        qreal x = 0.0;
        const qreal h = h0 * scale;
        for (int i : r) {
            const qreal w = natural[i] * scale;
            rects[i] = QRectF(x, y, w, h);
            x += w + gap;
        }
        y += h + gap;
    };
    for (const QVector<int> &r : rows) {
        qreal sum = 0.0;
        for (int i : r) sum += natural[i];
        placeRow(r, fillScale(sum, r.size()));
    }
    if (!row.isEmpty()) {
        // Last, unfilled row: keep the nominal height (left aligned) unless
        // it is nearly full, then stretch it like the others.
        const qreal scale = fillScale(rowSum, row.size());
        placeRow(row, scale < 1.25 ? scale : 1.0);
    }
    return rects;
}

// ---- Masonry -------------------------------------------------------------

QVector<QRectF> masonry(const QVector<QSizeF> &sizes, qreal gap, int columns)
{
    QVector<qreal> widths;
    for (const QSizeF &s : sizes) widths.append(s.width());
    const qreal cw = median(widths);

    QVector<qreal> colHeight(columns, 0.0);
    QVector<QRectF> rects(sizes.size());
    for (int i = 0; i < sizes.size(); ++i) {
        const int col = int(std::min_element(colHeight.begin(), colHeight.end()) - colHeight.begin());
        const qreal h = cw * sizes[i].height() / sizes[i].width();
        rects[i] = QRectF(col * (cw + gap), colHeight[col], cw, h);
        colHeight[col] += h + gap;
    }
    return rects;
}

// ---- Grid ----------------------------------------------------------------

QVector<QRectF> grid(const QVector<QSizeF> &sizes, qreal gap, int columns)
{
    QVector<qreal> widths, heights;
    for (const QSizeF &s : sizes) {
        widths.append(s.width());
        heights.append(s.height());
    }
    const qreal cellW = median(widths);
    const qreal cellH = median(heights);

    QVector<QRectF> rects(sizes.size());
    for (int i = 0; i < sizes.size(); ++i) {
        const int col = i % columns;
        const int rowIndex = i / columns;
        const qreal s = qMin(cellW / sizes[i].width(), cellH / sizes[i].height());
        const QSizeF fitted = sizes[i] * s;
        const QPointF cellTopLeft(col * (cellW + gap), rowIndex * (cellH + gap));
        rects[i] = QRectF(cellTopLeft + QPointF((cellW - fitted.width()) / 2, (cellH - fitted.height()) / 2), fitted);
    }
    return rects;
}

// ---- Packed (skyline, bottom-left) ---------------------------------------

struct Segment {
    qreal x, y, w;
};

QVector<QRectF> packed(const QVector<QSizeF> &sizes, qreal gap, qreal binWidth)
{
    const int n = sizes.size();
    QVector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    // Tallest first packs tightest.
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return sizes[a].height() > sizes[b].height(); });

    QVector<Segment> sky = {{0.0, 0.0, binWidth}};
    QVector<QRectF> rects(n);

    for (int idx : order) {
        const qreal w = sizes[idx].width() + gap;
        const qreal h = sizes[idx].height() + gap;

        qreal bestY = std::numeric_limits<qreal>::max();
        qreal bestX = 0.0;
        for (int s = 0; s < sky.size(); ++s) {
            const qreal x = sky[s].x;
            if (x + w > binWidth + 1e-6 && x > 0.0) continue;
            // Resting height over the span [x, x + w).
            qreal y = 0.0;
            for (int t = s; t < sky.size() && sky[t].x < x + w - 1e-9; ++t) y = qMax(y, sky[t].y);
            if (y < bestY - 1e-9 || (qAbs(y - bestY) <= 1e-9 && x < bestX)) {
                bestY = y;
                bestX = x;
            }
        }
        rects[idx] = QRectF(bestX, bestY, sizes[idx].width(), sizes[idx].height());

        // Raise the skyline over [bestX, bestX + w).
        const qreal x0 = bestX, x1 = bestX + w;
        QVector<Segment> next;
        for (const Segment &seg : sky) {
            const qreal s0 = seg.x, s1 = seg.x + seg.w;
            if (s1 <= x0 + 1e-9 || s0 >= x1 - 1e-9) {
                next.append(seg);
                continue;
            }
            if (s0 < x0) next.append({s0, seg.y, x0 - s0});
            if (s1 > x1) next.append({x1, seg.y, s1 - x1});
        }
        next.append({x0, bestY + h, w});
        std::sort(next.begin(), next.end(), [](const Segment &a, const Segment &b) { return a.x < b.x; });
        sky.clear();
        for (const Segment &seg : next) {
            if (!sky.isEmpty() && qAbs(sky.last().y - seg.y) < 1e-9) {
                sky.last().w += seg.w;
            } else {
                sky.append(seg);
            }
        }
    }
    return rects;
}

template<typename F>
QVector<QRectF> bestOf(int count, qreal targetAspect, F layoutFor)
{
    QVector<QRectF> best;
    qreal bestScore = -1.0;
    for (int i = 0; i < count; ++i) {
        QVector<QRectF> rects = layoutFor(i);
        const qreal s = score(rects, targetAspect);
        if (s > bestScore) {
            bestScore = s;
            best = rects;
        }
    }
    return best;
}

QVector<QRectF> arrangeStyle(const QVector<QSizeF> &sizes, Style style, qreal gap, qreal aspect)
{
    const int n = sizes.size();
    switch (style) {
    case Style::JustifiedRows:
    case Style::Packed: {
        qreal maxNatural = 0.0, total = 0.0;
        QVector<qreal> heights;
        for (const QSizeF &s : sizes) heights.append(s.height());
        const qreal h0 = median(heights);
        for (const QSizeF &s : sizes) {
            const qreal w = (style == Style::Packed) ? s.width() : s.width() / s.height() * h0;
            maxNatural = qMax(maxNatural, w);
            total += w + gap;
        }
        // Packed can't shrink images, so its bin is at least the widest one.
        // Justified rows may go narrower: a too-wide image then gets a row
        // of its own, scaled down.
        QVector<qreal> naturals;
        for (const QSizeF &s : sizes) naturals.append(s.width() / s.height() * h0);
        const qreal from = (style == Style::Packed) ? maxNatural + gap : median(naturals);
        const QVector<qreal> widths = candidateWidths(from, total);
        return bestOf(widths.size(), aspect, [&](int i) {
            return style == Style::Packed ? packed(sizes, gap, widths[i]) : justifiedRows(sizes, gap, widths[i]);
        });
    }
    case Style::Masonry:
        return bestOf(n, aspect, [&](int i) { return masonry(sizes, gap, i + 1); });
    case Style::Grid:
        return bestOf(n, aspect, [&](int i) { return grid(sizes, gap, i + 1); });
    case Style::Auto:
        break;
    }
    return {};
}

qreal totalArea(const QVector<QRectF> &rects)
{
    qreal a = 0.0;
    for (const QRectF &r : rects) a += r.width() * r.height();
    return a;
}

/// Styles that scale images (all but Packed) can drift the group's total
/// size (a lone image stretched across a row, say). Scale the inputs and
/// lay out again so the images keep about the same total area as before.
QVector<QRectF> arrangeKeepingArea(const QVector<QSizeF> &sizes, Style style, qreal gap, qreal aspect)
{
    QVector<QRectF> rects = arrangeStyle(sizes, style, gap, aspect);
    if (style == Style::Packed) return rects;
    qreal inArea = 0.0;
    for (const QSizeF &s : sizes) inArea += s.width() * s.height();
    QVector<QSizeF> scaled = sizes;
    for (int pass = 0; pass < 2; ++pass) {
        const qreal outArea = totalArea(rects);
        if (outArea <= 0.0) break;
        const qreal k = qSqrt(inArea / outArea);
        if (qAbs(k - 1.0) < 0.01) break;
        for (QSizeF &s : scaled) s *= k;
        rects = arrangeStyle(scaled, style, gap, aspect);
    }
    return rects;
}

} // namespace

QVector<QSizeF> normalize(const QVector<QSizeF> &sizes, NormalizeMode mode, int matchIndex)
{
    QVector<QSizeF> out = sizes;
    qreal sumArea = 0.0, sumW = 0.0, sumH = 0.0;
    int count = 0;
    for (const QSizeF &s : sizes) {
        if (!valid(s)) continue;
        sumArea += s.width() * s.height();
        sumW += s.width();
        sumH += s.height();
        ++count;
    }
    if (count == 0) return out;

    if (mode == NormalizeMode::MatchSelected && (matchIndex < 0 || matchIndex >= sizes.size() || !valid(sizes[matchIndex]))) {
        mode = NormalizeMode::SameArea;
    }
    const qreal targetArea = (mode == NormalizeMode::MatchSelected)
        ? sizes[matchIndex].width() * sizes[matchIndex].height()
        : sumArea / count;

    for (QSizeF &s : out) {
        if (!valid(s)) continue;
        qreal factor = 1.0;
        switch (mode) {
        case NormalizeMode::SameArea:
        case NormalizeMode::MatchSelected:
            factor = qSqrt(targetArea / (s.width() * s.height()));
            break;
        case NormalizeMode::SameHeight:
            factor = (sumH / count) / s.height();
            break;
        case NormalizeMode::SameWidth:
            factor = (sumW / count) / s.width();
            break;
        }
        s *= factor;
    }
    return out;
}

Result arrange(const QVector<QSizeF> &input, const Options &options)
{
    Result result;
    result.usedStyle = options.style;
    if (input.isEmpty()) return result;

    const QVector<QSizeF> sizes = sanitized(input);
    const qreal gap = qMax<qreal>(0.0, options.spacing);
    const qreal aspect = (options.targetAspect > 0.0 && qIsFinite(options.targetAspect)) ? options.targetAspect : 1.0;

    if (options.style != Style::Auto) {
        result.rects = arrangeKeepingArea(sizes, options.style, gap, aspect);
        return result;
    }

    qreal bestScore = -1.0;
    for (Style s : {Style::JustifiedRows, Style::Masonry, Style::Packed, Style::Grid}) {
        QVector<QRectF> rects = arrangeKeepingArea(sizes, s, gap, aspect);
        // Slight preference for the default look when scores are close.
        const qreal sc = score(rects, aspect) * (s == Style::JustifiedRows ? 1.02 : 1.0);
        if (sc > bestScore) {
            bestScore = sc;
            result.rects = rects;
            result.usedStyle = s;
        }
    }
    return result;
}

qreal score(const QVector<QRectF> &rects, qreal targetAspect)
{
    const QRectF b = bounds(rects);
    if (b.width() <= 0.0 || b.height() <= 0.0) return 0.0;
    qreal area = 0.0;
    for (const QRectF &r : rects) area += r.width() * r.height();
    const qreal fill = qMin<qreal>(1.0, area / (b.width() * b.height()));
    const qreal a = b.width() / b.height();
    const qreal t = targetAspect > 0.0 ? targetAspect : 1.0;
    const qreal aspectFit = qMin(a / t, t / a);
    return fill * aspectFit;
}

QRectF bounds(const QVector<QRectF> &rects)
{
    QRectF b;
    for (const QRectF &r : rects) b = b.isNull() ? r : b.united(r);
    return b;
}

QVector<int> readingOrder(const QVector<QRectF> &rects)
{
    const int n = rects.size();
    QVector<int> byY(n);
    std::iota(byY.begin(), byY.end(), 0);
    if (n < 2) return byY;

    QVector<qreal> heights;
    for (const QRectF &r : rects) heights.append(r.height());
    const qreal tolerance = 0.5 * median(heights);

    std::stable_sort(byY.begin(), byY.end(), [&](int a, int b) { return rects[a].center().y() < rects[b].center().y(); });

    QVector<int> out;
    QVector<int> row;
    qreal rowCenter = 0.0;
    auto flush = [&]() {
        std::stable_sort(row.begin(), row.end(), [&](int a, int b) { return rects[a].center().x() < rects[b].center().x(); });
        out += row;
        row.clear();
    };
    for (int i : byY) {
        const qreal cy = rects[i].center().y();
        if (!row.isEmpty() && qAbs(cy - rowCenter) > tolerance) flush();
        row.append(i);
        rowCenter = 0.0;
        for (int j : row) rowCenter += rects[j].center().y();
        rowCenter /= row.size();
    }
    flush();
    return out;
}

} // namespace GigaReferenceLayout

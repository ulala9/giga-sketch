/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaNote.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QDomDocument>
#include <QDomElement>
#include <QPainter>
#include <QPainterPath>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

#include <klocalizedstring.h>
#include <KoSelection.h>
#include <KoShapeManager.h>
#include <SvgUtil.h>
#include <libs/flake/svg/parsers/SvgTransformParser.h>
#include <kis_dom_utils.h>

#include "KisDocument.h"
#include "KisPart.h"
#include "KisReferenceImagesLayer.h"
#include "kis_config.h"

namespace
{
// Layout in note units (about one screen pixel at the zoom the note was
// made at).
constexpr qreal STRIP = 14.0;      ///< height of the darker top band
constexpr qreal PAD = 10.0;        ///< left/right/bottom padding
constexpr qreal PAD_TOP = 7.0;     ///< gap between the band and the text
constexpr qreal RADIUS = 7.0;      ///< corner rounding
constexpr qreal SHADOW = 3.0;      ///< Sticky: shadow offset (kept inside the shape)
constexpr qreal CARD_BAR = 5.0;    ///< Card: coloured bar on the left
constexpr qreal TAIL = 12.0;       ///< Bubble: height of the tail under the body
const char *CONFIG_STYLE = "gigaNoteStyle";
constexpr qreal MIN_WIDTH = 80.0;
constexpr qreal DEFAULT_WIDTH = 220.0;
constexpr qreal INDENT = 18.0;     ///< list indent (Qt's default is 40)

const char *CONFIG_SHOWN = "gigaShowAnnotations";

bool s_shownLoaded = false;
bool s_shown = true;

qreal pixelSizeFor(int textSize)
{
    switch (textSize) {
    case GigaNote::Small: return 12.0;
    case GigaNote::Large: return 20.0;
    default: return 15.0;
    }
}
} // namespace

struct GigaNote::Private {
    QString html;
    QColor color = GigaNote::defaultColor();
    int textSize = GigaNote::Medium;
    bool collapsed = false;
    bool keepReadable = false;
    int style = GigaNote::Sticky;
    qreal scale = 1.0; ///< document points per note unit
    bool editing = false;

    // Layout cache (never copied, rebuilt on demand).
    mutable QScopedPointer<QTextDocument> doc;
    mutable bool docDirty = true;
};

// ---- StateCommand --------------------------------------------------------------

GigaNote::StateCommand::StateCommand(const KUndo2MagicString &text, const QVector<GigaNote *> &notes,
                                     const QVector<State> &before, const QVector<State> &after,
                                     KUndo2Command *parent)
    : KUndo2Command(text, parent)
    , m_notes(notes)
    , m_before(before)
    , m_after(after)
{
}

void GigaNote::StateCommand::redo()
{
    if (m_skipRedo) {
        m_skipRedo = false;
        return;
    }
    for (int i = 0; i < m_notes.size(); ++i) m_notes[i]->setState(m_after[i]);
}

void GigaNote::StateCommand::undo()
{
    for (int i = 0; i < m_notes.size(); ++i) m_notes[i]->setState(m_before[i]);
}

// ---- GigaNote ------------------------------------------------------------------

GigaNote::GigaNote()
    : KisReferenceImage()
    , m_d(new Private())
{
    // The width is free; the height follows the text.
    setKeepAspectRatio(false);
    setName(i18n("Note"));
    KoShape::setSize(QSizeF(DEFAULT_WIDTH, STRIP + PAD_TOP + PAD + pixelSizeFor(Medium) * 1.4));
}

GigaNote::GigaNote(const GigaNote &rhs)
    : KisReferenceImage(rhs)
    , m_d(new Private())
{
    m_d->html = rhs.m_d->html;
    m_d->color = rhs.m_d->color;
    m_d->textSize = rhs.m_d->textSize;
    m_d->collapsed = rhs.m_d->collapsed;
    m_d->keepReadable = rhs.m_d->keepReadable;
    m_d->style = rhs.m_d->style;
    m_d->scale = rhs.m_d->scale;
    // editing is a per-shape interface state, not copied
}

GigaNote::~GigaNote()
{
}

KoShape *GigaNote::cloneShape() const
{
    return new GigaNote(*this);
}

GigaNote *GigaNote::create(const QPointF &documentPos, qreal pointsPerScreenPixel, const QString &plainText)
{
    GigaNote *note = new GigaNote();
    note->m_d->scale = pointsPerScreenPixel > 0.0 ? pointsPerScreenPixel : 1.0;
    note->m_d->style = defaultStyle();
    if (!plainText.isEmpty()) {
        QTextDocument doc;
        doc.setPlainText(plainText);
        note->m_d->html = doc.toHtml();
    }
    note->KoShape::setSize(QSizeF(DEFAULT_WIDTH * note->m_d->scale, 10.0));
    note->relayout();
    note->setPosition(documentPos);
    note->setOnBoard(false); // applies View > Show Annotations
    return note;
}

QString GigaNote::html() const
{
    return m_d->html;
}

void GigaNote::setHtml(const QString &html)
{
    if (m_d->html == html) return;
    m_d->html = html;
    m_d->docDirty = true;
    relayout();
}

QString GigaNote::plainText() const
{
    return layoutDocument()->toPlainText();
}

bool GigaNote::isEmpty() const
{
    return plainText().trimmed().isEmpty();
}

QColor GigaNote::color() const
{
    return m_d->color;
}

void GigaNote::setColor(const QColor &color)
{
    if (color.isValid()) m_d->color = color;
}

namespace
{
qreal luminance(const QColor &c)
{
    return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
}
} // namespace

QColor GigaNote::fillColor() const
{
    switch (m_d->style) {
    case Card: {
        // A light card tinted by the note colour (the colour itself is the bar).
        const QColor c = m_d->color;
        return QColor((c.red() + 3 * 252) / 4, (c.green() + 3 * 252) / 4, (c.blue() + 3 * 250) / 4);
    }
    case Outline:
        return QColor(0, 0, 0, 115); // smoked glass: the art shows through, the text stays readable
    default:
        return m_d->color;
    }
}

QColor GigaNote::textColor() const
{
    if (m_d->style == Outline) {
        // The text is drawn in the note colour, lightened until it reads on the dark glass.
        QColor c = m_d->color;
        for (int i = 0; i < 6 && luminance(c) < 0.55; ++i) c = c.lighter(135);
        return c;
    }
    // Relative luminance (sRGB weights) decides dark or light text.
    return luminance(fillColor()) > 0.5 ? QColor(32, 32, 32) : QColor(244, 244, 244);
}

int GigaNote::style() const
{
    return m_d->style;
}

void GigaNote::setStyle(int style)
{
    style = qBound<int>(Sticky, style, Outline);
    if (m_d->style == style) return;
    update();
    m_d->style = style;
    relayout();
    update();
}

QStringList GigaNote::styleNames()
{
    return {i18nc("note style", "Sticky"), i18nc("note style", "Card"), i18nc("note style", "Speech Bubble"),
            i18nc("note style", "Outline")};
}

int GigaNote::defaultStyle()
{
    KisConfig cfg(true);
    return qBound<int>(Sticky, cfg.readEntry<int>(CONFIG_STYLE, Sticky), Outline);
}

void GigaNote::setDefaultStyle(int style)
{
    KisConfig cfg(false);
    cfg.writeEntry<int>(CONFIG_STYLE, qBound<int>(Sticky, style, Outline));
}

QMarginsF GigaNote::insets() const
{
    switch (m_d->style) {
    case Sticky:
        return QMarginsF(PAD, STRIP + PAD_TOP, PAD + SHADOW, PAD + SHADOW);
    case Card:
        return QMarginsF(PAD + CARD_BAR, PAD, PAD, PAD);
    case Bubble:
        return QMarginsF(PAD + 2.0, PAD, PAD + 2.0, PAD + TAIL);
    default:
        return QMarginsF(PAD, PAD, PAD, PAD);
    }
}

int GigaNote::textSize() const
{
    return m_d->textSize;
}

void GigaNote::setTextSize(int size)
{
    size = qBound<int>(Small, size, Large);
    if (m_d->textSize == size) return;
    m_d->textSize = size;
    m_d->docDirty = true;
    relayout();
}

bool GigaNote::isCollapsed() const
{
    return m_d->collapsed;
}

void GigaNote::setCollapsed(bool collapsed)
{
    if (m_d->collapsed == collapsed) return;
    m_d->collapsed = collapsed;
    relayout();
}

qreal GigaNote::contentScale() const
{
    return m_d->scale;
}

void GigaNote::setContentScale(qreal pointsPerUnit)
{
    if (pointsPerUnit <= 0.0 || qFuzzyCompare(pointsPerUnit, m_d->scale)) return;
    const qreal widthUnits = size().width() / m_d->scale;
    m_d->scale = pointsPerUnit;
    KoShape::setSize(QSizeF(widthUnits * m_d->scale, size().height()));
    relayout();
}

bool GigaNote::keepReadable() const
{
    return m_d->keepReadable;
}

void GigaNote::setKeepReadable(bool keep)
{
    m_d->keepReadable = keep;
}

GigaNote::State GigaNote::state() const
{
    State s;
    s.html = m_d->html;
    s.color = m_d->color;
    s.textSize = m_d->textSize;
    s.collapsed = m_d->collapsed;
    s.keepReadable = m_d->keepReadable;
    s.style = m_d->style;
    s.width = size().width() / m_d->scale;
    return s;
}

void GigaNote::setState(const State &s)
{
    update(); // old area
    m_d->html = s.html;
    m_d->color = s.color.isValid() ? s.color : defaultColor();
    m_d->textSize = qBound<int>(Small, s.textSize, Large);
    m_d->collapsed = s.collapsed;
    m_d->keepReadable = s.keepReadable;
    m_d->style = qBound<int>(Sticky, s.style, Outline);
    m_d->docDirty = true;
    if (s.width > 0.0) KoShape::setSize(QSizeF(s.width * m_d->scale, size().height()));
    relayout();
    update(); // new area
}

void GigaNote::setEditing(bool editing)
{
    if (m_d->editing == editing) return;
    update(); // old area (a collapsed note opens up while it is edited)
    m_d->editing = editing;
    relayout();
    update();
}

bool GigaNote::isEditing() const
{
    return m_d->editing;
}

void GigaNote::setSize(const QSizeF &newSize)
{
    // Only the width is the user's; relayout() derives the height.
    const qreal minWidth = MIN_WIDTH * m_d->scale;
    KoShape::setSize(QSizeF(qMax(minWidth, newSize.width()), size().height()));
    relayout();
}

qreal GigaNote::textPixelSize() const
{
    return pixelSizeFor(m_d->textSize);
}

QRectF GigaNote::stripRect() const
{
    // The band you double-click to collapse: the top inset (Sticky: its strip).
    const qreal band = m_d->style == Sticky ? STRIP : insets().top();
    return QRectF(0.0, 0.0, size().width(), band * m_d->scale);
}

QRectF GigaNote::textRect() const
{
    const qreal s = m_d->scale;
    const QMarginsF m = insets();
    return QRectF(m.left() * s, m.top() * s,
                  qMax<qreal>(1.0, size().width() - (m.left() + m.right()) * s),
                  qMax<qreal>(1.0, size().height() - (m.top() + m.bottom()) * s));
}

void GigaNote::setupDocument(QTextDocument *doc, int textSize, qreal textWidthUnits)
{
    QFont font = QApplication::font();
    font.setPixelSize(qRound(pixelSizeFor(textSize)));
    // No hinting: the layout then scales linearly, so the editor (drawn at
    // screen size) wraps lines exactly where the note does.
    font.setHintingPreference(QFont::PreferNoHinting);
    doc->setDefaultFont(font);
    doc->setDocumentMargin(0.0);
    doc->setUseDesignMetrics(true);
    doc->setIndentWidth(INDENT);
    doc->setTextWidth(qMax<qreal>(1.0, textWidthUnits));
}

qreal GigaNote::indentUnits()
{
    return INDENT;
}

void GigaNote::normalizeFonts(QTextDocument *doc)
{
    struct Range {
        int start;
        int length;
        QTextCharFormat format;
    };
    QVector<Range> ranges;
    const QVector<int> props = {QTextFormat::FontPointSize, QTextFormat::FontPixelSize,
                                QTextFormat::FontFamilies, QTextFormat::FontStyleName,
                                QTextFormat::FontSizeAdjustment, QTextFormat::ForegroundBrush,
                                QTextFormat::BackgroundBrush};
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) continue;
            QTextCharFormat format = fragment.charFormat();
            bool changed = false;
            for (int p : props) {
                if (format.hasProperty(p)) {
                    format.clearProperty(p);
                    changed = true;
                }
            }
            if (changed) ranges.append({fragment.position(), fragment.length(), format});
        }
    }
    // Applied afterwards: changing formats while walking would merge
    // fragments under the iterator.
    QTextCursor cursor(doc);
    for (const Range &r : ranges) {
        cursor.setPosition(r.start);
        cursor.setPosition(r.start + r.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(r.format);
    }
}

QTextDocument *GigaNote::layoutDocument() const
{
    const QMarginsF m = insets();
    const qreal textWidth = size().width() / m_d->scale - m.left() - m.right();
    if (!m_d->doc) {
        m_d->doc.reset(new QTextDocument());
        m_d->docDirty = true;
    }
    if (m_d->docDirty) {
        setupDocument(m_d->doc.data(), m_d->textSize, textWidth);
        if (m_d->html.isEmpty()) {
            m_d->doc->clear();
        } else {
            m_d->doc->setHtml(m_d->html);
            normalizeFonts(m_d->doc.data());
        }
        m_d->docDirty = false;
    } else if (!qFuzzyCompare(m_d->doc->textWidth(), qMax<qreal>(1.0, textWidth))) {
        m_d->doc->setTextWidth(qMax<qreal>(1.0, textWidth));
    }
    return m_d->doc.data();
}

void GigaNote::relayout()
{
    QTextDocument *doc = layoutDocument();
    qreal textHeight = doc->size().height();
    const qreal lineHeight = QFontMetricsF(doc->defaultFont()).lineSpacing();
    if (m_d->collapsed && !m_d->editing) {
        textHeight = lineHeight;
        const QTextBlock first = doc->firstBlock();
        if (first.isValid() && first.layout() && first.layout()->lineCount() > 0) {
            textHeight = first.layout()->lineAt(0).height();
        }
    }
    textHeight = qMax(textHeight, lineHeight);
    const QMarginsF m = insets();
    const qreal heightUnits = m.top() + textHeight + m.bottom();
    KoShape::setSize(QSizeF(size().width(), heightUnits * m_d->scale));
}

void GigaNote::paintNote(QPainter &gc, qreal pointsPerUnit) const
{
    QTextDocument *doc = layoutDocument();

    gc.save();
    gc.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    gc.scale(pointsPerUnit, pointsPerUnit);
    const QRectF box(0.0, 0.0, size().width() / m_d->scale, size().height() / m_d->scale);

    const QColor color = m_d->color;
    QPen border(color.darker(135));
    border.setWidthF(1.0);
    border.setCosmetic(true);

    switch (m_d->style) {
    case Sticky: {
        // Soft shadow (inside the shape's area), box, darker top strip.
        const QRectF body = box.adjusted(0.0, 0.0, -SHADOW, -SHADOW);
        QPainterPath shadow;
        shadow.addRoundedRect(body.translated(SHADOW, SHADOW), RADIUS, RADIUS);
        gc.fillPath(shadow, QColor(0, 0, 0, 45));
        QPainterPath shape;
        shape.addRoundedRect(body, RADIUS, RADIUS);
        gc.fillPath(shape, color);
        gc.save();
        gc.setClipPath(shape);
        gc.fillRect(QRectF(body.left(), body.top(), body.width(), STRIP), color.darker(112));
        gc.restore();
        gc.setPen(border);
        gc.setBrush(Qt::NoBrush);
        gc.drawPath(shape);
        break;
    }
    case Card: {
        // Flat light card with the colour as a bar down the left edge.
        QPainterPath shape;
        shape.addRoundedRect(box, 3.0, 3.0);
        gc.fillPath(shape, fillColor());
        gc.save();
        gc.setClipPath(shape);
        gc.fillRect(QRectF(box.left(), box.top(), CARD_BAR, box.height()), color);
        gc.restore();
        QPen thin(QColor(0, 0, 0, 60));
        thin.setWidthF(1.0);
        thin.setCosmetic(true);
        gc.setPen(thin);
        gc.setBrush(Qt::NoBrush);
        gc.drawPath(shape);
        break;
    }
    case Bubble: {
        // Rounded body with a tail at the bottom-left.
        const QRectF body = box.adjusted(0.0, 0.0, 0.0, -TAIL);
        const qreal r = qMin<qreal>(12.0, qMin(body.width(), body.height()) / 2.0);
        QPainterPath shape;
        shape.addRoundedRect(body, r, r);
        QPainterPath tail;
        const qreal x = body.left() + qMin<qreal>(22.0, body.width() * 0.25);
        tail.moveTo(x, body.bottom() - 1.0);
        tail.lineTo(x - 6.0, box.bottom());
        tail.lineTo(x + 12.0, body.bottom() - 1.0);
        tail.closeSubpath();
        shape = shape.united(tail);
        gc.fillPath(shape, color);
        gc.setPen(border);
        gc.setBrush(Qt::NoBrush);
        gc.drawPath(shape);
        break;
    }
    default: {
        // Outline: see-through dark glass with a coloured border.
        QPainterPath shape;
        shape.addRoundedRect(box.adjusted(1.0, 1.0, -1.0, -1.0), RADIUS, RADIUS);
        gc.fillPath(shape, fillColor());
        QPen line(textColor());
        line.setWidthF(1.6);
        line.setCosmetic(true);
        gc.setPen(line);
        gc.setBrush(Qt::NoBrush);
        gc.drawPath(shape);
        break;
    }
    }

    if (!m_d->editing) {
        const QMarginsF m = insets();
        const QRectF text(m.left(), m.top(), box.width() - m.left() - m.right(), box.height() - m.top() - m.bottom());
        gc.translate(text.topLeft());
        const QRectF clip(QPointF(0.0, 0.0), text.size() + QSizeF(0.0, 1.0));
        gc.setClipRect(clip, Qt::IntersectClip);
        if (doc->isEmpty()) {
            QColor faded = textColor();
            faded.setAlphaF(0.45);
            gc.setPen(faded);
            gc.setFont(doc->defaultFont());
            gc.drawText(clip, Qt::AlignLeft | Qt::AlignTop, i18n("Note"));
        } else {
            QAbstractTextDocumentLayout::PaintContext context;
            context.palette = QApplication::palette();
            context.palette.setColor(QPalette::Text, textColor());
            context.clip = clip;
            doc->documentLayout()->draw(&gc, context);
        }
    }
    gc.restore();
}

void GigaNote::paint(QPainter &gc) const
{
    paintNote(gc, m_d->scale);
}

void GigaNote::paintInRect(QPainter &gc, const QRectF &target) const
{
    if (target.isEmpty() || size().isEmpty()) return;
    // Uniform scale so the text is never stretched; the Board keeps the
    // note's proportions, so this fills the target.
    const qreal f = qMin(target.width() / size().width(), target.height() / size().height());
    gc.save();
    gc.setClipRect(target, Qt::IntersectClip);
    gc.translate(target.topLeft());
    gc.scale(f, f);
    paintNote(gc, m_d->scale);
    gc.restore();
}

// ---- saving ----------------------------------------------------------------------

QString GigaNote::xmlTagName()
{
    return QStringLiteral("giga-note");
}

void GigaNote::saveXml(QDomDocument &document, QDomElement &parentElement, int id)
{
    Q_UNUSED(id);
    QDomElement element = document.createElement(xmlTagName());

    const QSizeF &shapeSize = size();
    element.setAttribute("width", KisDomUtils::toString(shapeSize.width()));
    element.setAttribute("height", KisDomUtils::toString(shapeSize.height()));
    element.setAttribute("transform", SvgUtil::transformToString(transform()));
    element.setAttribute("opacity", KisDomUtils::toString(1.0 - transparency()));
    element.setAttribute("color", m_d->color.name(QColor::HexArgb));
    element.setAttribute("textSize", QString::number(m_d->textSize));
    element.setAttribute("collapsed", m_d->collapsed ? "true" : "false");
    element.setAttribute("style", QString::number(m_d->style));
    if (m_d->keepReadable) element.setAttribute("keepReadable", "true");
    element.setAttribute("scale", KisDomUtils::toString(m_d->scale));

    // Same optional Board attributes as reference images.
    if (isOnBoard()) {
        element.setAttribute("giga-onBoard", "true");
    }
    const QRectF board = boardRect();
    if (board.isValid()) {
        element.setAttribute("giga-boardRect", QString("%1 %2 %3 %4")
                             .arg(KisDomUtils::toString(board.x()), KisDomUtils::toString(board.y()),
                                  KisDomUtils::toString(board.width()), KisDomUtils::toString(board.height())));
    }

    QDomElement text = document.createElement("html");
    text.appendChild(document.createTextNode(m_d->html));
    element.appendChild(text);

    parentElement.appendChild(element);
}

bool GigaNote::saveImage(KoStore *store) const
{
    Q_UNUSED(store);
    return true; // no picture to write
}

bool GigaNote::loadImage(KoStore *store)
{
    Q_UNUSED(store);
    return true; // no picture to read
}

GigaNote *GigaNote::fromXml(const QDomElement &elem)
{
    GigaNote *note = new GigaNote();
    note->m_d->scale = qMax<qreal>(1e-6, KisDomUtils::toDouble(elem.attribute("scale", "1")));
    const QColor color(elem.attribute("color"));
    if (color.isValid()) note->m_d->color = color;
    note->m_d->textSize = qBound<int>(Small, elem.attribute("textSize", "1").toInt(), Large);
    note->m_d->collapsed = elem.attribute("collapsed").toLower() == "true";
    note->m_d->keepReadable = elem.attribute("keepReadable").toLower() == "true";
    note->m_d->style = qBound<int>(Sticky, elem.attribute("style", "0").toInt(), Outline);
    note->m_d->html = elem.firstChildElement("html").text();
    note->m_d->docDirty = true;

    const qreal width = KisDomUtils::toDouble(elem.attribute("width", "100"));
    const qreal height = KisDomUtils::toDouble(elem.attribute("height", "40"));
    note->KoShape::setSize(QSizeF(width, height));
    note->relayout();

    note->setTransformation(SvgTransformParser(elem.attribute("transform")).transform());
    note->setTransparency(1.0 - KisDomUtils::toDouble(elem.attribute("opacity", "1")));

    const QStringList boardRect = elem.attribute("giga-boardRect").split(' ', Qt::SkipEmptyParts);
    if (boardRect.size() == 4) {
        const QRectF r(KisDomUtils::toDouble(boardRect[0]), KisDomUtils::toDouble(boardRect[1]),
                       KisDomUtils::toDouble(boardRect[2]), KisDomUtils::toDouble(boardRect[3]));
        if (r.isValid()) note->setBoardRect(r);
    }
    note->setOnBoard(elem.attribute("giga-onBoard").toLower() == "true" && note->boardRect().isValid());
    return note;
}

// ---- presets, global options -----------------------------------------------------------

QVector<QColor> GigaNote::presetColors()
{
    return {QColor(255, 230, 128), QColor(255, 182, 200), QColor(189, 232, 165),
            QColor(168, 212, 255), QColor(213, 189, 255), QColor(212, 212, 212)};
}

QStringList GigaNote::presetColorNames()
{
    return {i18nc("note colour", "Yellow"), i18nc("note colour", "Pink"), i18nc("note colour", "Green"),
            i18nc("note colour", "Blue"), i18nc("note colour", "Purple"), i18nc("note colour", "Grey")};
}

QColor GigaNote::defaultColor()
{
    return presetColors().first();
}

bool GigaNote::notesShown()
{
    if (!s_shownLoaded) {
        KisConfig cfg(true);
        s_shown = cfg.readEntry<bool>(CONFIG_SHOWN, true);
        s_shownLoaded = true;
    }
    return s_shown;
}

void GigaNote::setNotesShown(bool shown, KisReferenceImagesLayer *onlyLayer)
{
    s_shown = shown;
    s_shownLoaded = true;
    {
        KisConfig cfg(false);
        cfg.writeEntry<bool>(CONFIG_SHOWN, shown);
    }

    QVector<KisReferenceImagesLayer *> layers;
    if (onlyLayer) {
        layers.append(onlyLayer);
    } else {
        Q_FOREACH (QPointer<KisDocument> document, KisPart::instance()->documents()) {
            if (document && document->referenceImagesLayer()) layers.append(document->referenceImagesLayer().data());
        }
    }
    for (KisReferenceImagesLayer *layer : layers) {
        for (GigaNote *note : notesOf(layer)) {
            note->update();
            if (!shown && layer->shapeManager()) layer->shapeManager()->selection()->deselect(note);
            note->setOnBoard(note->isOnBoard()); // re-applies the visibility
            note->update();
        }
    }
}

bool GigaNote::hiddenOnCanvas() const
{
    return !notesShown();
}

QVector<GigaNote *> GigaNote::notesOf(KisReferenceImagesLayer *layer)
{
    QVector<GigaNote *> notes;
    if (!layer) return notes;
    for (KisReferenceImage *image : layer->referenceImages()) {
        if (image->isNote()) notes.append(static_cast<GigaNote *>(image));
    }
    return notes;
}

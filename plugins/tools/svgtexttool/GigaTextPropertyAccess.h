/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_TEXT_PROPERTY_ACCESS_H
#define GIGA_TEXT_PROPERTY_ACCESS_H

#include <QColor>
#include <QFont>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>

#include <KoSvgTextProperties.h>
#include <kis_signal_auto_connection.h>

#include <functional>

class KisCanvasResourceProvider;
class KoCanvasResourceProvider;

/**
 * Giga Sketch: reading and changing the selected text's properties, shared
 * by the text options bar and the Character / Paragraph panels.
 *
 * Krita keeps the selected text's properties in the canvas resource
 * provider: textPropertyData() for the whole text (or the next new text when
 * none is selected) and characterTextPropertyData() for a selected range.
 * KisTextPropertiesManager applies any change written there back to the text
 * as an undoable edit. This class only reads and writes those two values.
 */
class GigaTextPropertyAccess : public QObject
{
    Q_OBJECT
public:
    explicit GigaTextPropertyAccess(QObject *parent = nullptr);

    void setProviders(KisCanvasResourceProvider *provider, KoCanvasResourceProvider *canvasResources);
    bool hasProvider() const;

    /// True when a range of characters is selected (edits go to that range).
    bool isCharacterSelection() const;
    /// Fully resolved properties of the selection (or whole text).
    KoSvgTextProperties characterProperties() const;
    KoSvgTextProperties paragraphProperties() const;
    /// The selection has different values for @p id (show the field blank).
    bool characterMixed(KoSvgTextProperties::PropertyId id) const;
    bool paragraphMixed(KoSvgTextProperties::PropertyId id) const;

    /// Change character-level properties of the selection (or whole text).
    void applyCharacter(const QSet<KoSvgTextProperties::PropertyId> &ids,
                        const std::function<void(KoSvgTextProperties &)> &change);
    /// Change paragraph-level properties of the whole text.
    void applyParagraph(const QSet<KoSvgTextProperties::PropertyId> &ids,
                        const std::function<void(KoSvgTextProperties &)> &change);

    /// Text colour: the text's own fill, else the foreground colour.
    QColor textColor() const;
    /// Krita colours text through the foreground colour.
    void setForegroundColor(const QColor &color);

    // --- font family with live preview (font picker) ---
    void setFamily(const QString &family);
    /// Remember the current font before previewing others; false when the
    /// selection mixes fonts (then no live preview).
    bool beginFontPreview();
    void previewFamily(const QString &family);
    void cancelFontPreview();

    // --- font lists ---
    struct StyleEntry {
        QString label;
        int weight;
        int width;
        QFont::Style slant;
    };
    static QStringList fontFamilies();
    static QList<StyleEntry> stylesFor(const QString &family);
    static int bestStyle(const QList<StyleEntry> &styles, int weight, QFont::Style slant);

    // --- unit conversions (Photoshop units <-> CSS properties) ---
    /// Tracking in 1/1000 em (Photoshop) from letter-spacing.
    static qreal trackingFromProperties(const KoSvgTextProperties &resolved);
    static void setTracking(KoSvgTextProperties &p, qreal thousandthsEm);
    /// Leading in pt, or a negative number for Auto.
    static qreal leadingFromProperties(const KoSvgTextProperties &resolved);
    static void setLeading(KoSvgTextProperties &p, qreal points); ///< points < 0: Auto
    /// Baseline shift in pt (0 when none or super/sub).
    static qreal baselineShiftFromProperties(const KoSvgTextProperties &resolved);
    static void setBaselineShift(KoSvgTextProperties &p, qreal points);

Q_SIGNALS:
    /// The selected text or its properties changed.
    void changed();

private:
    QPointer<KisCanvasResourceProvider> m_provider;
    QPointer<KoCanvasResourceProvider> m_canvasResources;
    KisSignalAutoConnectionsStore m_connections;
    QStringList m_previewOriginal;
    bool m_previewAllowed {false};
};

#endif // GIGA_TEXT_PROPERTY_ACCESS_H

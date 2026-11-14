/*
 * SPDX-FileCopyrightText: 2017 Boudewijn Rempt <boud@valdyas.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KISREFERENCEIMAGE_H
#define KISREFERENCEIMAGE_H

#include <QSharedDataPointer>

#include <KoColor.h>
#include <KoShape.h>
#include <kis_types.h>
#include <kritaui_export.h>
#include <kundo2command.h>

class QImage;
class QPointF;
class QPainter;
class QRectF;
class KoStore;
class KisCoordinatesConverter;
class KisCanvas2;

/**
 * @brief The KisReferenceImage class represents a single reference image
 */
class KRITAUI_EXPORT KisReferenceImage : public KoShape
{
public:
    struct KRITAUI_EXPORT SetSaturationCommand : public KUndo2Command {
        QVector<KisReferenceImage*> images;
        QVector<qreal> oldSaturations;
        qreal newSaturation;

        explicit SetSaturationCommand(const QList<KoShape *> &images, qreal newSaturation, KUndo2Command *parent = 0);
        void undo() override;
        void redo() override;
    };

    KisReferenceImage();
    KisReferenceImage(const KisReferenceImage &rhs);
    ~KisReferenceImage();

    KoShape *cloneShape() const override;

    /**
     * Load a reference image from specified file.
     * If parent is provided and the image cannot be loaded, a warning message will be displayed to user.
     * @return reference image or null if one could not be loaded
     */
    static KisReferenceImage * fromFile(const QString &filename, const KisCoordinatesConverter &converter, QWidget *parent /*= nullptr*/);
    static KisReferenceImage * fromClipboard(const KisCoordinatesConverter &converter);
    static KisReferenceImage * fromQImage(const KisCoordinatesConverter &converter, const QImage &img);

    /**
     * Load a reference image from specified paint device.
     * @return reference image or null if one could not be loaded
     */
    static KisReferenceImage *
    fromPaintDevice(KisPaintDeviceSP src, const KisCoordinatesConverter &converter, QWidget *parent);

    void setSaturation(qreal saturation);
    qreal saturation() const;

    void setEmbed(bool embed);
    bool embed();
    bool hasLocalFile();

    void setFilename(const QString &filename);
    QString filename() const;
    QString internalFile() const;

    void paint(QPainter &gc) const override;

    QColor getPixel(QPointF position);

    void saveXml(QDomDocument &document, QDomElement &parentElement, int id);
    bool saveImage(KoStore *store) const;

    static KisReferenceImage * fromXml(const QDomElement &elem);
    bool loadImage(KoStore *store);

    QImage getImage();

    // --- Giga Sketch: Reference Board -------------------------------------
    // An image "on the Board" is hidden from the painting canvas (its canvas
    // placement is kept untouched) and drawn by the Reference Board panel at
    // boardRect() instead. Saved as optional giga-* XML attributes that
    // stock Krita ignores.

    bool isOnBoard() const;
    /// Also hides/shows the shape on the canvas. Call update() around it.
    void setOnBoard(bool onBoard);
    QRectF boardRect() const;
    void setBoardRect(const QRectF &rect);

    QSize imagePixelSize() const;
    /// Replaces the stored pixels (Resample to Current Size). The shape keeps
    /// its size on the canvas.
    void setImage(const QImage &image);

    /// Draws the image (with its saturation) stretched into target, using
    /// the same mipmaps as the canvas. Used by the Board.
    void paintInRect(QPainter &gc, const QRectF &target) const;
    /// Colour at uv (0..1 across the image), for Ctrl+click on the Board.
    QColor pixelAtNormalized(const QPointF &uv);
    /// Cheap average colour (cached), for "sort by colour".
    QColor averageColor() const;

private:
    struct Private;
    QSharedDataPointer<Private> d;
};

#endif // KISREFERENCEIMAGE_H

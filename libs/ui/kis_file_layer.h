/*
 *  SPDX-FileCopyrightText: 2013 Boudewijn Rempt <boud@valdyas.org>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef KIS_FILE_LAYER_H
#define KIS_FILE_LAYER_H

#include "kritaui_export.h"

#include "kis_external_layer_iface.h"
#include "kis_safe_document_loader.h"
#include "kis_signal_auto_connection.h"

/**
 * @brief The KisFileLayer class loads a particular file as a layer into the layer stack.
 */
class KRITAUI_EXPORT KisFileLayer : public KisExternalLayer
{
    Q_OBJECT
public:

    enum ScalingMethod {
        None,
        ToImageSize,
        ToImagePPI
    };

    KisFileLayer(KisImageWSP image, const QString &name, quint8 opacity);
    /**
     * @brief KisFileLayer create a new file layer with the given file
     * @param image the image the file layer will belong to
     * @param basePath the path to the image, if it has been saved before.
     * @param filename the path to the file, relative to the basePath
     * @param scalingMethod @see ScalingMethod
     * @param scalingFilter the ID of the KisFilterStrategy to be used if scaling
     * @param name the name of the layer
     * @param opacity the opacity of the layer
     */
    KisFileLayer(KisImageWSP image, const QString& basePath, const QString &filename, ScalingMethod scalingMethod, QString scalingFilter, const QString &name, quint8 opacity, const KoColorSpace *fallbackColorSpace = 0);
    ~KisFileLayer() override;
    KisFileLayer(const KisFileLayer& rhs);

    QIcon icon() const override;

    void resetCache(const KoColorSpace *colorSpace = 0) override;

    KisPaintDeviceSP original() const override;
    KisPaintDeviceSP paintDevice() const override;
    void setSectionModelProperties(const KisBaseNode::PropertyList &properties) override;
    KisBaseNode::PropertyList sectionModelProperties() const override;

    /**
     * @brief setFileName replace the existing file with a new one
     * @param basePath the path to the image, if it has been saved before.
     * @param filename the path to the file, relative to the basePath
     */
    void setFileName(const QString &basePath, const QString &filename);
    QString fileName() const;
    QString path() const;


    ScalingMethod scalingMethod() const;
    void setScalingMethod(ScalingMethod method);

    /**
     * Giga Sketch: a "Blender layer" (made by GigaBlenderBridge). Its picture
     * is also saved inside the .kra, so it still shows when the PNG is gone,
     * and a missing PNG is not reported as an error.
     */
    bool gigaIsBlenderLayer() const;
    void gigaSetBlenderLayer(bool value);
    /// The picture saved in the .kra was loaded.
    void gigaSetHasSavedPicture(bool value);
    /**
     * Set the PNG path without loading or watching it. Blender layers get
     * their picture from the bridge (gigaSetPicture) or the .kra, never by
     * reading the PNG, so a Send isn't loaded twice and never blocks.
     */
    void gigaSetBlenderSource(const QString &basePath, const QString &filename);
    /// Show @p device as the layer's picture (scaled to the image if needed). GUI thread.
    void gigaSetPicture(KisPaintDeviceSP device);

    QString scalingFilter() const;
    void setScalingFilter(QString method);

    KisNodeSP clone() const override;
    bool allowAsChild(KisNodeSP) const override;

    bool accept(KisNodeVisitor&) override;
    void accept(KisProcessingVisitor &visitor, KisUndoAdapter *undoAdapter) override;

    KUndo2Command* crop(const QRect & rect) override;
    KUndo2Command* transform(const QTransform &transform) override;

    void setImage(KisImageWSP image) override;

private Q_SLOTS:
    void slotLoadingFinished(KisPaintDeviceSP projection, qreal xRes, qreal yRes, const QSize &size);
    void slotLoadingFailed();
    void slotFileExistsStateChanged(bool exists);
    void openFile() const;
    void slotImageSizeChanged();
    void slotImageResolutionChanged();

Q_SIGNALS:
    void sigRequestOpenFile();

private:
    enum State {
        FileLoaded,
        FileNotFound,
        FileLoadingFailed
    };

    void changeState(State newState);

private:
    QString m_basePath;
    QString m_filename;
    ScalingMethod m_scalingMethod {None};
    QString m_scalingFilter;
    bool m_gigaBlenderLayer {false};
    bool m_gigaHasSavedPicture {false};

    KisPaintDeviceSP m_paintDevice;
    KisSafeDocumentLoader m_loader;
    QSize m_generatedForImageSize;
    qreal m_generatedForXRes = 0.0;
    qreal m_generatedForYRes = 0.0;

    State m_state = FileNotFound;

    KisSignalAutoConnectionsStore m_imageConnections;
};

#endif // KIS_FILE_LAYER_H

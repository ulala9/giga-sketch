/*
 * SPDX-FileCopyrightText: 2017 Boudewijn Rempt <boud@valdyas.org>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef TOOL_REFERENCE_IMAGES_H
#define TOOL_REFERENCE_IMAGES_H

#include <QPointer>

#include <KoToolFactoryBase.h>
#include <KoIcon.h>

#include <kis_tool.h>
#include "kis_painting_assistant.h"
#include <kis_icon.h>
#include <kis_canvas2.h>

#include <defaulttool/DefaultTool.h>
#include <defaulttool/DefaultToolFactory.h>

class ToolReferenceImagesWidget;
class KisReferenceImagesLayer;
class KisReferenceImage;

class ToolReferenceImages : public DefaultTool
{
    Q_OBJECT

public:
    ToolReferenceImages(KoCanvasBase * canvas);
    ~ToolReferenceImages() override;

    virtual quint32 priority() {
        return 3;
    }

    void mouseDoubleClickEvent(KoPointerEvent *event) override; // Giga Sketch: edit/collapse notes

    // Giga Sketch: sticky notes (click to place after Add Note, Esc cancels)
    void mousePressEvent(KoPointerEvent *event) override;
    void mouseMoveEvent(KoPointerEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

    bool hasSelection() override;

    void deleteSelection() override;

    QMenu* popupActionsMenu() override;

protected:
    QList<QPointer<QWidget>> createOptionWidgets() override;
    QWidget *createOptionWidget() override;

    bool isValidForCurrentLayer() const override;
    KoShapeManager *shapeManager() const override;
    KoSelection *koSelection() const override;

    void updateDistinctiveActions(const QList<KoShape*> &editableShapes) override;

public Q_SLOTS:
    void activate(const QSet<KoShape*> &shapes) override;
    void deactivate() override;

    void addReferenceImage();
    void pasteReferenceImage();
    void addReferenceImageFromLayer();
    void addReferenceImageFromVisible();
    void removeSelectedReferenceImages();
    void removeAllReferenceImages();
    void saveReferenceImages();
    void loadReferenceImages();

    // Giga Sketch: Reference Board (act on the selected images, or on all
    // canvas images when nothing is selected)
    void gigaNormalize();
    void gigaArrange();
    void gigaSendToBoard();
    void gigaSendAllToCanvas();
    void gigaResample();

    // Giga Sketch: sticky notes
    void gigaAddNote();          ///< next click on the canvas places a note
    void gigaDeleteSelected();

    void slotNodeAdded(KisNodeSP node);
    void slotNodeAdded(KisNodeSP node, KisNodeAdditionFlags flags);
    void slotSelectionChanged();

    void cut() override;
    void copy() const override;
    bool paste() override;

    bool selectAll() override;
    void deselect() override;


private:
    friend class ToolReferenceImagesWidget;
    ToolReferenceImagesWidget *m_optionsWidget = nullptr;
    KisWeakSharedPtr<KisReferenceImagesLayer> m_layer;

    KisDocument *document() const;
    QVector<KisReferenceImage*> gigaTargets() const;
    void gigaSetPlacingNote(bool placing);
    bool m_gigaPlacingNote = false;
    void setReferenceImageLayer(KisSharedPtr<KisReferenceImagesLayer> layer);
};


class ToolReferenceImagesFactory : public DefaultToolFactory
{
public:
    ToolReferenceImagesFactory()
    : DefaultToolFactory("ToolReferenceImages") {
        setToolTip(i18n("Reference Images Tool"));
        setSection(ToolBoxSection::View);
        setIconName(koIconNameCStr("krita_tool_reference_images"));
        setPriority(2);
        setActivationShapeId(KRITA_TOOL_ACTIVATION_ID);
    };


    ~ToolReferenceImagesFactory() override {}

    KoToolBase * createTool(KoCanvasBase * canvas) override {
        return new ToolReferenceImages(canvas);
    }

    QList<QAction *> createActionsImpl() override;

};


#endif


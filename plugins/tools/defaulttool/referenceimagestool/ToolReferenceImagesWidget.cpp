/*
 *  SPDX-FileCopyrightText: 2017 Eugene Ingerman
 *  SPDX-FileCopyrightText: 2022 L. E. Segovia <amy@amyspark.me>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ToolReferenceImagesWidget.h"

#include "ui_WdgToolOptions.h"

#include <KoSelection.h>
#include <KoShapeTransparencyCommand.h>
#include <KoShapeKeepAspectRatioCommand.h>
#include <kis_config.h>
#include <kis_signals_blocker.h>
#include <kis_signal_compressor.h>
#include <KisReferenceImage.h>
#include <KisSpinBoxI18nHelper.h>
#include <kis_clipboard.h>

#include <QApplication>
#include <QStandardItemModel>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

#include <GigaReferenceBoard.h>

#include "ToolReferenceImages.h"

struct ToolReferenceImagesWidget::Private {
    Private(ToolReferenceImages *tool)
    : tool(tool)
    {
    }

    Ui_WdgToolOptions *ui {nullptr};
    ToolReferenceImages *tool {nullptr};

    // Giga Sketch: Reference Board section
    QComboBox *normalizeMode {nullptr};
    QComboBox *style {nullptr};
    QComboBox *order {nullptr};
    QComboBox *shape {nullptr};
    QSpinBox *spacing {nullptr};
    QPushButton *resample {nullptr};
};

ToolReferenceImagesWidget::ToolReferenceImagesWidget(ToolReferenceImages *tool, KisCanvasResourceProvider */*provider*/, QWidget *parent)
    : QWidget(parent),
      d(new Private(tool))
{
    d->ui = new Ui_WdgToolOptions();
    d->ui->setupUi(this);

    d->ui->opacitySlider->setRange(0, 100);
    d->ui->opacitySlider->setTextTemplates(
        i18nc("{n} is the number value, % is the percent sign", "Opacity: {n}%"),
        i18nc("{n} is the number value, % is the percent sign", "Opacity [*varies*]: {n}%"));
    d->ui->opacitySlider->setValueGetter(
        [](KoShape *s){ return 100.0 * (1.0 - s->transparency()); }
    );

    d->ui->saturationSlider->setRange(0, 100);
    d->ui->saturationSlider->setTextTemplates(
        i18nc("{n} is the number value, % is the percent sign", "Saturation: {n}%"),
        i18nc("{n} is the number value, % is the percent sign", "Saturation [*varies*]: {n}%"));
    d->ui->saturationSlider->setValueGetter(
        [](KoShape *s){
            auto *r = dynamic_cast<KisReferenceImage*>(s);
            KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(r, 0.0);
            return 100.0 * r->saturation();
        }
    );

    d->ui->bnDeleteSelectedImages->setToolTip(i18n("Delete Selected Reference Images"));
    d->ui->bnDeleteSelectedImages->setIcon(KisIconUtils::loadIcon("edit-delete"));
    d->ui->bnDeleteSelectedImages->setIconSize(QSize(16, 16));

    d->ui->bnAddReferenceImage->setToolTip(i18n("Add Reference Image From File"));
    d->ui->bnAddReferenceImage->setIcon(KisIconUtils::loadIcon("view-preview"));
    d->ui->bnAddReferenceImage->setIconSize(QSize(16, 16));
    
    d->ui->bnPasteReferenceImage->setToolTip(i18n("Paste Reference Image From System Clipboard"));
    d->ui->bnPasteReferenceImage->setIcon(KisIconUtils::loadIcon("edit-paste-16"));
    d->ui->bnPasteReferenceImage->setIconSize(QSize(16, 16));
    d->ui->bnPasteReferenceImage->setEnabled(KisClipboard::instance()->hasClip() || KisClipboard::instance()->hasUrls());

    d->ui->bnAddReferenceImageFromCurrentLayer->setToolTip(i18n("Create Reference Image from Active Layer"));
    d->ui->bnAddReferenceImageFromCurrentLayer->setIcon(KisIconUtils::loadIcon("current-layer"));
    
    d->ui->bnAddReferenceImageFromVisible->setToolTip(i18n("Create Reference Image from Visible Canvas"));
    d->ui->bnAddReferenceImageFromVisible->setIcon(KisIconUtils::loadIcon("all-layers"));

    d->ui->bnDelete->setToolTip(i18n("Delete all Reference Images"));
    d->ui->bnDelete->setIcon(KisIconUtils::loadIcon("edit-delete"));
    d->ui->bnDelete->setIconSize(QSize(16, 16));

    d->ui->bnLoad->setToolTip(i18n("Load Reference Images Set"));
    d->ui->bnLoad->setIcon(KisIconUtils::loadIcon("folder"));
    d->ui->bnLoad->setIconSize(QSize(16, 16));

    d->ui->bnSave->setToolTip(i18n("Export Reference Images Set"));
    d->ui->bnSave->setIcon(KisIconUtils::loadIcon("document-save-16"));
    d->ui->bnSave->setIconSize(QSize(16, 16));


	connect(d->ui->bnDeleteSelectedImages, SIGNAL(clicked()), tool, SLOT(removeSelectedReferenceImages()));
    connect(d->ui->bnAddReferenceImage, SIGNAL(clicked()), tool, SLOT(addReferenceImage()));
    connect(d->ui->bnPasteReferenceImage, SIGNAL(clicked()), tool, SLOT(pasteReferenceImage()));
    connect(d->ui->bnAddReferenceImageFromCurrentLayer, SIGNAL(clicked()), tool, SLOT(addReferenceImageFromLayer()));
    connect(d->ui->bnAddReferenceImageFromVisible, SIGNAL(clicked()), tool, SLOT(addReferenceImageFromVisible()));

    connect(KisClipboard::instance(), &KisClipboard::clipChanged, this, [&]() {
        d->ui->bnPasteReferenceImage->setEnabled(KisClipboard::instance()->hasClip() || KisClipboard::instance()->hasUrls());
    });

    connect(d->ui->bnDelete, SIGNAL(clicked()), tool, SLOT(removeAllReferenceImages()));
    connect(d->ui->bnSave, SIGNAL(clicked()), tool, SLOT(saveReferenceImages()));
    connect(d->ui->bnLoad, SIGNAL(clicked()), tool, SLOT(loadReferenceImages()));

    connect(d->ui->chkKeepAspectRatio, SIGNAL(stateChanged(int)), this, SLOT(slotKeepAspectChanged()));

    KisSignalCompressor *compressor = new KisSignalCompressor(100 /* ms */, KisSignalCompressor::POSTPONE, this);
    connect(compressor, SIGNAL(timeout()), this, SLOT(slotImageValuesChanged()));

    connect(d->ui->saturationSlider, SIGNAL(valueChanged(qreal)), compressor, SLOT(start()));
    connect(d->ui->opacitySlider, SIGNAL(valueChanged(qreal)), compressor, SLOT(start()));

    d->ui->referenceImageLocationCombobox->addItem(i18n("Embed to .KRA"));
    d->ui->referenceImageLocationCombobox->addItem(i18n("Link to Image"));
    connect(d->ui->referenceImageLocationCombobox, SIGNAL(currentIndexChanged(int)), this, SLOT(slotSaveLocationChanged(int)));

    gigaSetupBoardSection();

    updateVisibility(false); // no selection when we start
}

void ToolReferenceImagesWidget::gigaSetupBoardSection()
{
    // Giga Sketch: PureRef-style tidy-up tools and the Reference Board. The
    // choices are shared with the Reference Board panel.
    QFrame *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    d->ui->verticalLayout->addWidget(line);

    QLabel *title = new QLabel(i18n("Arrange and Board:"), this);
    d->ui->verticalLayout->addWidget(title);

    QWidget *box = new QWidget(this);
    QGridLayout *grid = new QGridLayout(box);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(4);

    d->normalizeMode = new QComboBox(box);
    d->normalizeMode->addItem(i18n("Same area"));
    d->normalizeMode->addItem(i18n("Same height"));
    d->normalizeMode->addItem(i18n("Same width"));
    d->normalizeMode->addItem(i18n("Match selected"));
    d->normalizeMode->setToolTip(i18n("Same area: wide and tall images look equally big.\n"
                                      "Match selected: size everything like the image selected last."));
    QPushButton *normalize = new QPushButton(i18n("Normalize"), box);
    normalize->setToolTip(i18n("Make the images similar in size (one undo step)"));
    grid->addWidget(d->normalizeMode, 0, 0);
    grid->addWidget(normalize, 0, 1);

    d->style = new QComboBox(box);
    d->style->addItem(i18n("Justified rows"));
    d->style->addItem(i18n("Masonry"));
    d->style->addItem(i18n("Packed"));
    d->style->addItem(i18n("Grid"));
    d->style->addItem(i18n("Auto"));
    d->style->setToolTip(i18n("Justified rows: equal-height rows with a straight right edge.\n"
                              "Masonry: equal-width columns.\n"
                              "Packed: tightest fit, sizes kept.\n"
                              "Grid: equal cells.\n"
                              "Auto: tries them all and keeps the tidiest."));
    QPushButton *arrange = new QPushButton(i18n("Arrange"), box);
    arrange->setToolTip(i18n("Lay the images out as a tidy group (one undo step)"));
    grid->addWidget(d->style, 1, 0);
    grid->addWidget(arrange, 1, 1);
    d->ui->verticalLayout->addWidget(box);

    QWidget *optionsBox = new QWidget(this);
    QFormLayout *form = new QFormLayout(optionsBox);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(4);
    d->order = new QComboBox(optionsBox);
    d->order->addItem(i18n("Current"));
    d->order->addItem(i18n("Colour"));
    d->order->addItem(i18n("Shape"));
    d->order->addItem(i18n("Date added"));
    form->addRow(i18n("Order:"), d->order);
    d->shape = new QComboBox(optionsBox);
    d->shape->addItem(i18n("Square"));
    d->shape->addItem(i18n("Canvas"));
    d->shape->addItem(i18n("Board panel"));
    d->shape->setToolTip(i18n("Overall shape the arranged group aims for"));
    form->addRow(i18n("Shape:"), d->shape);
    d->spacing = new QSpinBox(optionsBox);
    d->spacing->setRange(0, 500);
    d->spacing->setSuffix(i18n(" px"));
    form->addRow(i18n("Spacing:"), d->spacing);
    d->ui->verticalLayout->addWidget(optionsBox);

    QWidget *boardBox = new QWidget(this);
    QGridLayout *boardGrid = new QGridLayout(boardBox);
    boardGrid->setContentsMargins(0, 0, 0, 0);
    boardGrid->setSpacing(4);
    QPushButton *toBoard = new QPushButton(KisIconUtils::loadIcon("krita_tool_reference_images"), i18n("Send to Board"), boardBox);
    toBoard->setToolTip(i18n("Move the images off the canvas into the Reference Board panel.\n"
                             "They remember where they were on the canvas."));
    QPushButton *toCanvas = new QPushButton(i18n("All Back to Canvas"), boardBox);
    toCanvas->setToolTip(i18n("Put every Board image back exactly where it was on the canvas"));
    d->resample = new QPushButton(i18n("Resample to Current Size"), boardBox);
    boardGrid->addWidget(toBoard, 0, 0);
    boardGrid->addWidget(toCanvas, 0, 1);
    boardGrid->addWidget(d->resample, 1, 0, 1, 2);
    d->ui->verticalLayout->addWidget(boardBox);

    QLabel *hint = new QLabel(i18n("These act on the selected images, or on all of them if none are selected."), this);
    hint->setWordWrap(true);
    hint->setEnabled(false); // greyed: just a hint
    d->ui->verticalLayout->addWidget(hint);

    connect(normalize, SIGNAL(clicked()), d->tool, SLOT(gigaNormalize()));
    connect(arrange, SIGNAL(clicked()), d->tool, SLOT(gigaArrange()));
    connect(toBoard, SIGNAL(clicked()), d->tool, SLOT(gigaSendToBoard()));
    connect(toCanvas, SIGNAL(clicked()), d->tool, SLOT(gigaSendAllToCanvas()));
    connect(d->resample, SIGNAL(clicked()), d->tool, SLOT(gigaResample()));

    gigaLoadSettings();
    for (QComboBox *combo : {d->normalizeMode, d->style, d->order, d->shape}) {
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() { gigaSaveSettings(); });
    }
    connect(d->spacing, qOverload<int>(&QSpinBox::valueChanged), this, [this]() { gigaSaveSettings(); });
    // The Board panel changes the same settings.
    connect(GigaReferenceBoard::Notifier::instance(), &GigaReferenceBoard::Notifier::settingsChanged, this,
            [this]() { gigaLoadSettings(); });
}

void ToolReferenceImagesWidget::gigaLoadSettings()
{
    const GigaReferenceBoard::Settings s = GigaReferenceBoard::Settings::load();
    KisSignalsBlocker blocker(d->normalizeMode, d->style, d->order, d->shape, d->spacing);
    d->normalizeMode->setCurrentIndex(int(s.normalizeMode));
    d->style->setCurrentIndex(int(s.style));
    d->order->setCurrentIndex(int(s.order));
    d->shape->setCurrentIndex(int(s.targetShape));
    d->spacing->setValue(s.spacing);
}

void ToolReferenceImagesWidget::gigaSaveSettings()
{
    GigaReferenceBoard::Settings s = GigaReferenceBoard::Settings::load();
    s.normalizeMode = GigaReferenceLayout::NormalizeMode(d->normalizeMode->currentIndex());
    s.style = GigaReferenceLayout::Style(d->style->currentIndex());
    s.order = GigaReferenceBoard::Order(d->order->currentIndex());
    s.targetShape = GigaReferenceBoard::TargetShape(d->shape->currentIndex());
    s.spacing = d->spacing->value();
    s.save();
}

ToolReferenceImagesWidget::~ToolReferenceImagesWidget()
{
    delete d->ui;
    d->ui = nullptr;
}

void ToolReferenceImagesWidget::selectionChanged(KoSelection *selection)
{
    QList<KoShape*> shapes = selection->selectedEditableShapes();

    d->ui->opacitySlider->setSelection(shapes);
    d->ui->saturationSlider->setSelection(shapes);

    bool anyKeepingAspectRatio = false;
    bool anyNotKeepingAspectRatio = false;
    bool anyEmbedded = false;
    bool anyLinked = false;
    // bool anyNonLinkable = false;
    bool anySelected = selection->count() > 0;

    Q_FOREACH(KoShape *shape, shapes) {
        KisReferenceImage *reference = dynamic_cast<KisReferenceImage*>(shape);

        anyKeepingAspectRatio |= shape->keepAspectRatio();
        anyNotKeepingAspectRatio |= !shape->keepAspectRatio();

        if (reference) {
            anyEmbedded |= reference->embed();
            anyLinked |= !reference->embed();
            // anyNonLinkable |= !reference->hasLocalFile();
        }
    }

    KisSignalsBlocker blocker(
        d->ui->chkKeepAspectRatio,
        d->ui->referenceImageLocationCombobox
    );

    d->ui->chkKeepAspectRatio->setCheckState(
        (anyKeepingAspectRatio && anyNotKeepingAspectRatio) ? Qt::PartiallyChecked :
         anyKeepingAspectRatio ? Qt::Checked : Qt::Unchecked);


    // set save location combobox
    bool imagesEmbedded = anyEmbedded && !anyLinked;
    int comboBoxIndex = imagesEmbedded ? 0 : 1; // maps state to combobox index
    d->ui->referenceImageLocationCombobox->setCurrentIndex(comboBoxIndex);


    updateVisibility(anySelected);

    // Giga Sketch: show what Resample would do before you press it.
    if (d->resample) {
        const QString summary = GigaReferenceBoard::resampleSummary(d->tool->gigaTargets());
        d->resample->setEnabled(!summary.isEmpty());
        d->resample->setToolTip(summary.isEmpty()
            ? i18n("Already about their shown size: nothing to shrink")
            : i18n("Shrink the stored pixels to the size they are shown at (×1.5 headroom): %1.\n"
                   "Smaller .kra files and less memory. Detail is lost once the file is saved and closed.", summary));
    }
}

void ToolReferenceImagesWidget::slotKeepAspectChanged()
{
    KoSelection *selection = d->tool->koSelection();
    QList<KoShape*> shapes = selection->selectedEditableShapes();

    KUndo2Command *cmd =
            new KoShapeKeepAspectRatioCommand(shapes, d->ui->chkKeepAspectRatio->isChecked());

    d->tool->canvas()->addCommand(cmd);
}

void ToolReferenceImagesWidget::slotOpacitySliderChanged(qreal newOpacity)
{
    QList<KoShape*> shapes = d->ui->opacitySlider->selection();
    if (shapes.isEmpty()) return;

    KUndo2Command *cmd =
        new KoShapeTransparencyCommand(shapes, 1.0 - newOpacity / 100.0);

    d->tool->canvas()->addCommand(cmd);
}

void ToolReferenceImagesWidget::slotSaturationSliderChanged(qreal newSaturation)
{
    QList<KoShape*> shapes = d->ui->saturationSlider->selection();
    if (shapes.isEmpty()) return;

    KUndo2Command *cmd =
            new KisReferenceImage::SetSaturationCommand(shapes, newSaturation / 100.0);

    d->tool->canvas()->addCommand(cmd);
}

void ToolReferenceImagesWidget::slotSaveLocationChanged(int index)
{
    KoSelection *selection = d->tool->koSelection();
    QList<KoShape*> shapes = selection->selectedEditableShapes();


    Q_FOREACH(KoShape *shape, shapes) {
        KisReferenceImage *reference = dynamic_cast<KisReferenceImage*>(shape);
        KIS_SAFE_ASSERT_RECOVER_RETURN(reference);

        if (index == 0) { // embed to KRA
            reference->setEmbed(true);
        } else { // link to file
            if (reference->hasLocalFile()) {
                reference->setEmbed(false);
            } else {
                //In the case no local file is found, switch back to embed file data.
                d->ui->referenceImageLocationCombobox->setCurrentIndex(0);
            }
        }
    }
}

void ToolReferenceImagesWidget::slotImageValuesChanged()
{
    slotSaturationSliderChanged(d->ui->saturationSlider->value());
    slotOpacitySliderChanged(d->ui->opacitySlider->value());
}

void ToolReferenceImagesWidget::updateVisibility(bool hasSelection)
{
    // hide UI elements if nothing is selected.
    d->ui->referenceImageLocationCombobox->setVisible(hasSelection);
    d->ui->chkKeepAspectRatio->setVisible(hasSelection);
    d->ui->saveLocationLabel->setVisible(hasSelection);
    d->ui->opacitySlider->setVisible(hasSelection);
    d->ui->saturationSlider->setVisible(hasSelection);
    d->ui->bnDeleteSelectedImages->setVisible(hasSelection);

    // show a label indicating that a selection is required to show options
    d->ui->referenceImageOptionsLabel->setVisible(!hasSelection);

    if (hasSelection) {
        KoSelection* selection = d->tool->koSelection();
        QList<KoShape*> shapes = selection->selectedEditableShapes();
        bool usesLocalFile = true;

        Q_FOREACH(KoShape *shape, shapes) {
            KisReferenceImage *reference = dynamic_cast<KisReferenceImage*>(shape);

            if (reference) {
                usesLocalFile &= reference->hasLocalFile();
            }
        }

        QStandardItemModel* model = dynamic_cast<QStandardItemModel*>(d->ui->referenceImageLocationCombobox->model());

        if (model) {
            QStandardItem* item = model->item(1);
            item->setFlags(usesLocalFile ? item->flags() | Qt::ItemIsEnabled :
                                           item->flags() & ~Qt::ItemIsEnabled);
        }
    }
}

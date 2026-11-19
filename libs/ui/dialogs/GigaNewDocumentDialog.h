/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_NEW_DOCUMENT_DIALOG_H
#define GIGA_NEW_DOCUMENT_DIALOG_H

#include <QDialog>

#include "kritaui_export.h"

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class KisDocument;

/**
 * Giga Sketch: a Photoshop-style "New Document" dialog.
 *
 * Presets in groups on the left (Paper, Screen, Social, Illustration),
 * document details on the right (name, size with units, orientation,
 * resolution, color mode, background). "More Options..." opens Krita's
 * full dialog, so templates and advanced settings stay available.
 */
class KRITAUI_EXPORT GigaNewDocumentDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GigaNewDocumentDialog(QWidget *parent = nullptr);

private Q_SLOTS:
    void slotPresetChosen();
    void slotUnitChanged(int index);
    void slotSwapOrientation();
    void slotCreate();
    void slotMoreOptions();

private:
    /// Size in pixels from the width/height fields, using the current unit.
    QSize sizeInPixels() const;
    KisDocument *createDocument();

    QListWidget *m_presets {nullptr};
    QLineEdit *m_name {nullptr};
    QDoubleSpinBox *m_width {nullptr};
    QDoubleSpinBox *m_height {nullptr};
    QComboBox *m_unit {nullptr};
    QDoubleSpinBox *m_resolution {nullptr};
    QComboBox *m_colorMode {nullptr};
    QComboBox *m_background {nullptr};
    int m_currentUnit {0};
};

#endif // GIGA_NEW_DOCUMENT_DIALOG_H

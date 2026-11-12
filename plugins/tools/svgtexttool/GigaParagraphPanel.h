/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_PARAGRAPH_PANEL_H
#define GIGA_PARAGRAPH_PANEL_H

#include <QWidget>

class QButtonGroup;
class QDoubleSpinBox;
class GigaTextPropertyAccess;

/**
 * Giga Sketch: Photoshop-style Paragraph panel: alignment (left, centre,
 * right) and justification (last line left, centre, right, or all), left /
 * right / first-line indents, and space before / after paragraphs. Applies
 * to the whole text as undoable steps.
 */
class GigaParagraphPanel : public QWidget
{
    Q_OBJECT
public:
    enum Align { AlignLeft = 0, AlignCenter, AlignRight, JustifyLeft, JustifyCenter, JustifyRight, JustifyAll, AlignCount };

    explicit GigaParagraphPanel(GigaTextPropertyAccess *access, QWidget *parent = nullptr);

    /// For tests: "align" (QButtonGroup is not a widget, so see alignGroup()),
    /// "indentLeft", "indentRight", "firstLine", "spaceBefore", "spaceAfter".
    QDoubleSpinBox *spinBox(const QString &name) const;
    QButtonGroup *alignGroup() const { return m_align; }

public Q_SLOTS:
    void updateFromProperties();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void applyAlign(int align);

    GigaTextPropertyAccess *m_access;
    bool m_updating {false};
    QButtonGroup *m_align {nullptr};
    QDoubleSpinBox *m_indentLeft {nullptr};
    QDoubleSpinBox *m_indentRight {nullptr};
    QDoubleSpinBox *m_firstLine {nullptr};
    QDoubleSpinBox *m_spaceBefore {nullptr};
    QDoubleSpinBox *m_spaceAfter {nullptr};
};

#endif // GIGA_PARAGRAPH_PANEL_H

/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_CHARACTER_PANEL_H
#define GIGA_CHARACTER_PANEL_H

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QToolButton;
class GigaFontComboBox;
class GigaFontPicker;
class GigaTextPropertyAccess;

/**
 * Giga Sketch: Photoshop-style Character panel.
 *
 * One compact two-column panel: font family (with the font picker), style,
 * size, leading, kerning, tracking, baseline shift, color, a row of style
 * toggles (faux bold, italic, all caps, small caps, superscript, subscript,
 * underline, strikethrough) and anti-aliasing. Edits apply to the selected
 * characters, or the whole text when nothing is selected, as undoable steps.
 * A blank field means the selection has mixed values.
 */
class GigaCharacterPanel : public QWidget
{
    Q_OBJECT
public:
    explicit GigaCharacterPanel(GigaTextPropertyAccess *access, QWidget *parent = nullptr);

    /// For tests: the panel's fields, by name ("size", "leading", ...).
    QWidget *field(const QString &name) const;

public Q_SLOTS:
    void updateFromProperties();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void fillFamilies();
    void fillStyles(const QString &family);
    void applySize();
    void applyLeading();
    void applyTracking();
    void applyBaseline();

    GigaTextPropertyAccess *m_access;
    bool m_updating {false};
    QStringList m_families;

    GigaFontComboBox *m_family {nullptr};
    GigaFontPicker *m_picker {nullptr};
    QComboBox *m_style {nullptr};
    QComboBox *m_size {nullptr};
    QComboBox *m_leading {nullptr};
    QComboBox *m_kerning {nullptr};
    QComboBox *m_tracking {nullptr};
    QDoubleSpinBox *m_baseline {nullptr};
    QToolButton *m_color {nullptr};
    QToolButton *m_bold {nullptr};
    QToolButton *m_italic {nullptr};
    QToolButton *m_allCaps {nullptr};
    QToolButton *m_smallCaps {nullptr};
    QToolButton *m_super {nullptr};
    QToolButton *m_sub {nullptr};
    QToolButton *m_underline {nullptr};
    QToolButton *m_strike {nullptr};
    QComboBox *m_antiAlias {nullptr};
};

/// Parse "12", "12 pt", "12,5" etc. Returns false when it isn't a number.
bool gigaParseNumber(QString text, qreal &value);

#endif // GIGA_CHARACTER_PANEL_H

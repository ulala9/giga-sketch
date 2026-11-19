/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_FONT_PICKER_H
#define GIGA_FONT_PICKER_H

#include <QComboBox>
#include <QFrame>
#include <QHash>
#include <QImage>
#include <QStringList>

class QLineEdit;
class QListWidget;
class QListWidgetItem;

/**
 * Giga Sketch: the font family field of the text options bar. Typing still
 * searches as before; opening the dropdown asks for the GigaFontPicker
 * instead of Qt's plain list.
 */
class GigaFontComboBox : public QComboBox
{
    Q_OBJECT
public:
    using QComboBox::QComboBox;
    void showPopup() override { Q_EMIT pickerRequested(); }
Q_SIGNALS:
    void pickerRequested();
};

/**
 * Giga Sketch: Photoshop-style font menu.
 *
 * A popup with a search field and a list of font families. Each row shows
 * a star (favorite), the family name and a preview of the font (Krita's
 * own preview image for that font resource). With no search text the list
 * is grouped into Favorites, Recent and All Fonts.
 *
 * Moving over a font (mouse or arrow keys) emits previewRequested() so the
 * text can show it live; picking one emits chosen(); closing without a pick
 * emits previewCancelled() so the text can go back to its font.
 */
class GigaFontPicker : public QFrame
{
    Q_OBJECT
public:
    explicit GigaFontPicker(QWidget *parent = nullptr);

    /// Open under @p anchor with @p families (all fonts) and @p current marked.
    void popup(QWidget *anchor, const QStringList &families, const QString &current);

    /// Remember @p family as recently used (most recent first).
    static void addRecent(const QString &family);

Q_SIGNALS:
    void previewRequested(const QString &family);
    void chosen(const QString &family);
    void previewCancelled();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void rebuild();
    void addHeader(const QString &title);
    void addFont(const QString &family);
    void choose(QListWidgetItem *item);
    void preview(QListWidgetItem *item);
    QImage previewImage(const QString &family);

    QLineEdit *m_search {nullptr};
    QListWidget *m_list {nullptr};
    QStringList m_families;
    QStringList m_favorites;
    QString m_current;
    QString m_lastPreview;
    bool m_chosen {false};
    QHash<QString, QImage> m_imageCache;
};

#endif // GIGA_FONT_PICKER_H

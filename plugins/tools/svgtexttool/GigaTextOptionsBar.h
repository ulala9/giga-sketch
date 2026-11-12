/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_TEXT_OPTIONS_BAR_H
#define GIGA_TEXT_OPTIONS_BAR_H

#include <QPointer>
#include <QSet>
#include <QToolBar>

#include <KoSvgTextProperties.h>
#include <kis_signal_auto_connection.h>

#include <functional>

class QComboBox;
class QToolButton;
class KisCanvasResourceProvider;
class KoCanvasResourceProvider;

/**
 * Giga Sketch: a Photoshop-style options bar for the Text tool.
 *
 * A horizontal toolbar along the top of the window with the most-used text
 * settings: orientation, font family, font style, size, anti-aliasing,
 * alignment, colour, a button to open the Character/Paragraph panels, and a
 * button to finish editing.
 *
 * How it talks to the text: exactly like the Text Properties docker. Krita
 * keeps the selected text's properties in the canvas resource provider
 * (textPropertyData for the whole text/paragraph, characterTextPropertyData
 * for a selected range). KisTextPropertiesManager copies the selection into
 * those, and applies any change made to them back to the text as an undoable
 * edit. With no text selected, they hold the settings for the next new text.
 * So this bar only ever reads and writes those two values.
 */
class GigaTextOptionsBar : public QToolBar
{
    Q_OBJECT
public:
    explicit GigaTextOptionsBar(QWidget *parent = nullptr);
    ~GigaTextOptionsBar() override;

    /// Connect to a canvas's resources (pass nullptrs to disconnect).
    void setResourceProviders(KisCanvasResourceProvider *provider, KoCanvasResourceProvider *canvasResources);

Q_SIGNALS:
    /// "Commit" (checkmark) pressed: finish editing the current text.
    void commitRequested();

private Q_SLOTS:
    void updateFromProperties();
    void slotFamilyChosen();
    void slotStyleChosen(int index);
    void slotSizeChosen();
    void slotAntiAliasChosen(int index);
    void slotAlignmentClicked(int alignment);
    void slotOrientationClicked();
    void slotColorClicked();
    void slotOpenPanels();

private:
    /// Properties of the current target: selected characters if a range is
    /// selected, otherwise the whole text (or the next new text).
    KoSvgTextProperties currentCharacterProperties() const;
    KoSvgTextProperties currentParagraphProperties() const;
    bool isCharacterSelection() const;

    /// Change character-level properties (font, size, style...).
    void applyCharacter(const QSet<KoSvgTextProperties::PropertyId> &ids,
                        const std::function<void(KoSvgTextProperties &)> &change);
    /// Change paragraph-level properties (alignment, writing mode...).
    void applyParagraph(const QSet<KoSvgTextProperties::PropertyId> &ids,
                        const std::function<void(KoSvgTextProperties &)> &change);

    void fillFamilies();
    void fillStyles(const QString &family);

private:
    QPointer<KisCanvasResourceProvider> m_provider;
    QPointer<KoCanvasResourceProvider> m_canvasResources;
    KisSignalAutoConnectionsStore m_connections;
    bool m_updating {false};
    bool m_familiesFilled {false};

    QToolButton *m_orientation {nullptr};
    QComboBox *m_family {nullptr};
    QComboBox *m_style {nullptr};
    QComboBox *m_size {nullptr};
    QComboBox *m_antiAlias {nullptr};
    QToolButton *m_alignLeft {nullptr};
    QToolButton *m_alignCenter {nullptr};
    QToolButton *m_alignRight {nullptr};
    QToolButton *m_alignJustify {nullptr};
    QToolButton *m_color {nullptr};
    QToolButton *m_panels {nullptr};
    QToolButton *m_commit {nullptr};
};

#endif // GIGA_TEXT_OPTIONS_BAR_H

/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_BRUSH_POPUP_H
#define GIGA_BRUSH_POPUP_H

#include <QFrame>
#include <QPointer>

#include <brushengine/kis_paintop_preset.h>

#include "KisPopupWidgetInterface.h"
#include "kritaui_export.h"

class KisCanvas2;
class KisViewManager;
class KisDoubleSliderSpinBox;
class KisPaintOpPresetsChooserPopup;
class QHBoxLayout;
class QLabel;

/**
 * Giga Sketch: the brush popup that right-click opens for paint tools
 * (instead of Krita's round popup palette, which stays available as an option).
 *
 * A small panel at the pointer with Size, Hardness, the recently used brushes and
 * Krita's brush preset list (tags + search). Click a brush to pick it and keep the
 * popup open; double-click, Enter, Esc or a click outside closes it.
 *
 * It is a child widget of the canvas widget, like the popup palette, and is found
 * again by object name, so KisCanvas2 doesn't need a new member. All of this runs
 * on the GUI thread: it only changes canvas resources (size, fade, preset), the
 * same way the top bar sliders do, so it never touches the paint thread.
 */
class KRITAUI_EXPORT GigaBrushPopup : public QFrame, public KisPopupWidgetInterface
{
    Q_OBJECT
    Q_INTERFACES(KisPopupWidgetInterface)
public:
    /// What right-click opens for paint tools (Preferences > General > Tools).
    enum RightClickMode { BrushPopup = 0, PopupPalette = 1 };
    static RightClickMode rightClickMode();
    static void setRightClickMode(RightClickMode mode);

    /// The popup for this canvas (made on first use). Null without a canvas widget.
    static GigaBrushPopup *forCanvas(KisCanvas2 *canvas);

    /// Remember a brush the user switched to, for the "recent" row (newest first).
    static void noteRecentPreset(KisPaintOpPresetSP preset);
    static QStringList recentPresetNames();

    /// Open Krita's popup palette under the pointer (the "Show Popup Palette" action).
    static void showPopupPalette(KisViewManager *viewManager);

    explicit GigaBrushPopup(KisCanvas2 *canvas, QWidget *parent);
    ~GigaBrushPopup() override;

    // KisPopupWidgetInterface
    void popup(const QPoint &position) override;
    void dismiss() override;
    bool onScreen() override;

    /// For tests: the widgets inside.
    KisDoubleSliderSpinBox *sizeSlider() const { return m_size; }
    KisDoubleSliderSpinBox *hardnessSlider() const { return m_hardness; }

Q_SIGNALS:
    void finished() override;

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private Q_SLOTS:
    void slotCanvasResourceChanged(int key, const QVariant &value);
    void slotPresetChosen(KoResourceSP resource);

private:
    void syncFromCanvas();
    void rebuildRecentRow();
    void placeAt(const QPoint &position);
    void choosePreset(KoResourceSP resource);

    QPointer<KisCanvas2> m_canvas;
    KisDoubleSliderSpinBox *m_size {nullptr};
    KisDoubleSliderSpinBox *m_hardness {nullptr};
    QLabel *m_hardnessLabel {nullptr};
    QWidget *m_recentRow {nullptr};
    QHBoxLayout *m_recentLayout {nullptr};
    KisPaintOpPresetsChooserPopup *m_presets {nullptr};
    bool m_syncing {false};
    bool m_watchingApp {false};
};

#endif // GIGA_BRUSH_POPUP_H

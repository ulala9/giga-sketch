/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_REFERENCE_BOARD_DOCK_H
#define GIGA_REFERENCE_BOARD_DOCK_H

#include <QDockWidget>
#include <QPointer>

#include <KoCanvasObserverBase.h>

class KisCanvas2;
class QLabel;
class QMenu;
class QSplitter;
class QToolButton;
class GigaReferenceBoardView;

/**
 * Giga Sketch: the Reference Board panel. A header row of buttons
 * (Arrange, Fit, Add from Canvas, Back to Canvas, Look) over a
 * GigaReferenceBoardView, which has its own pan/zoom camera.
 *
 * Search (when built with Qt WebEngine) splits the panel: the image search
 * browser (GigaImageSearchView) on one side, the Board on the other. The
 * browser is only made on the first click, so it costs nothing until then;
 * closing the split keeps it for a quick reopen ("Close Browser" frees it).
 */
class GigaReferenceBoardDock : public QDockWidget, public KoCanvasObserverBase
{
    Q_OBJECT
public:
    GigaReferenceBoardDock();
    ~GigaReferenceBoardDock() override;
    QString observerName() override { return QStringLiteral("GigaReferenceBoardDock"); }
    void setCanvas(KoCanvasBase *canvas) override;
    void unsetCanvas() override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void setSearchOpen(bool open);
    void closeBrowser();
    void updateSplitOrientation();
    void buildArrangeMenu();
    void buildLookMenu(); // both menus are rebuilt from the saved settings each time they open

    GigaReferenceBoardView *m_view {nullptr};
    QPointer<KisCanvas2> m_canvas;
    QMenu *m_arrangeMenu {nullptr};
    QMenu *m_lookMenu {nullptr};
    QSplitter *m_split {nullptr};
    QToolButton *m_searchButton {nullptr};
    QLabel *m_status {nullptr};
    QPointer<QWidget> m_search; // a GigaImageSearchView (plain QWidget here so builds without WebEngine work)
};

#endif

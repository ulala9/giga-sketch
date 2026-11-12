/*
 * SPDX-FileCopyrightText: 2015 Friedrich W. H. Kossebau <kossebau@kde.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef _KO_TOOLBOXBUTTON_H_
#define _KO_TOOLBOXBUTTON_H_

#include <QToolButton>

class KoToolAction;

class KoToolBoxButton : public QToolButton
{
    Q_OBJECT
public:
    explicit KoToolBoxButton(KoToolAction *toolAction, QWidget * parent);
    void attachAction(QAction *action);
    void setHighlightColor();

    KoToolAction *toolAction() const { return m_toolAction; }

    /// Giga Sketch: draw a small corner triangle meaning "more tools here
    /// (press and hold or right-click)", like Photoshop's grouped tools.
    void setGroupIndicator(bool show);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void setDataFromToolAction(QAction *action); // Generates tooltips.
private Q_SLOTS:
    void slotUpdateActionData();
private:
    KoToolAction *m_toolAction;
    bool m_groupIndicator {false};
};

#endif // _KO_TOOLBOXBUTTON_H_

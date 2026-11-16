/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_IMAGE_SEARCH_VIEW_H
#define GIGA_IMAGE_SEARCH_VIEW_H

#include <QList>
#include <QPointer>
#include <QStringList>
#include <QUrl>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QToolButton;
class QWebEngineProfile;
class QWebEngineView;
class GigaImageSearchPage;

/**
 * Giga Sketch: the Reference Board's image search browser.
 *
 * A small web browser (Qt WebEngine = Chromium in its own process, so a slow
 * page never slows the painting): site picker + search box + back/forward/
 * reload over the real Google Images / Bing / Pinterest page. Images get to
 * the Board by dragging them over, right-click > Add to Board, or Pick mode
 * (click several, then "Add selected").
 *
 * This widget only finds image addresses. It hands them to the Board view
 * (addRequested), which downloads them in the background. Each image comes
 * as a list of addresses, best first: the full-size one(s), then the one shown.
 *
 * The browser has its own logins/cookies in Giga Sketch's data folder
 * ("image-search"), separate from other browsers and from stock Krita.
 */
class GigaImageSearchView : public QWidget
{
    Q_OBJECT
    // For the UI self-test (libs/ui/GigaSelfTest.cpp), which can't link this plugin.
    Q_PROPERTY(bool pageLoaded READ isPageLoaded)
    Q_PROPERTY(int pickCount READ pickCount)
    Q_PROPERTY(bool pickMode READ isPickMode WRITE setPickMode)
public:
    explicit GigaImageSearchView(QWidget *parent = nullptr);
    ~GigaImageSearchView() override;

    bool isPageLoaded() const { return m_loaded; }
    int pickCount() const { return m_pickCount; }
    bool isPickMode() const { return m_pickMode; }
    void setPickMode(bool on);

    /// Deletes cookies, logins and the cache of the search browser.
    static void clearBrowsingData(QWidget *parentForMessage);

    // Self-test hooks (also usable from code).
    Q_INVOKABLE void loadUrl(const QUrl &url);
    Q_INVOKABLE void addImageByIndex(int index); ///< like right-click > Add to Board on document.images[index]
    Q_INVOKABLE void togglePickByIndex(int index); ///< like a click in Pick mode
    Q_INVOKABLE QString runScriptForTest(const QString &js); ///< waits for the result (max 3 s)

public Q_SLOTS:
    void search();       ///< runs the text in the search box on the chosen site
    void addSelected();  ///< Pick mode: add every marked image, then unmark them

Q_SIGNALS:
    /// One entry per image: addresses to try in order. @p referrer = the page.
    void addRequested(const QList<QStringList> &images, const QUrl &referrer);

private Q_SLOTS:
    void slotLoadFinished(bool ok);
    void slotConsoleMessage(const QString &message);
    void slotSiteChanged(int index);

private:
    void showContextMenu(const QPoint &pos);
    void addCandidatesFromScriptResult(const QVariant &result);
    void updatePickUi();
    static QWebEngineProfile *profile();

    QComboBox *m_site {nullptr};
    QLineEdit *m_query {nullptr};
    QToolButton *m_back {nullptr};
    QToolButton *m_forward {nullptr};
    QToolButton *m_reload {nullptr};
    QToolButton *m_pick {nullptr};
    QToolButton *m_addSelected {nullptr};
    QWebEngineView *m_web {nullptr};
    QPointer<GigaImageSearchPage> m_page;
    bool m_loaded = false;
    bool m_pickMode = false;
    int m_pickCount = 0;
};

#endif

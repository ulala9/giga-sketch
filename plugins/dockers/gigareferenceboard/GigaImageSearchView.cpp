/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaImageSearchView.h"

#include <kis_icon_utils.h>
#include <GigaBackgroundMemoryTrimmer.h>

#include <klocalizedstring.h>
#include <kconfiggroup.h>
#include <ksharedconfig.h>

#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWebEngineContextMenuRequest>
#include <QWebEngineCookieStore>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineView>

namespace
{
struct Site {
    const char *name;
    const char *home;
    const char *search; // %1 = the search words
};

// Image search address of each site. Only the addresses the sites' own
// search boxes go to; nothing is scraped, the user browses the real page.
const Site kSites[] = {
    {"Google Images", "https://images.google.com/", "https://www.google.com/search?udm=2&q=%1"},
    {"Bing Images", "https://www.bing.com/images", "https://www.bing.com/images/search?q=%1"},
    {"Pinterest", "https://www.pinterest.com/", "https://www.pinterest.com/search/pins/?q=%1"},
};
constexpr int kSiteCount = int(sizeof(kSites) / sizeof(kSites[0]));

KConfigGroup searchConfig()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("GigaImageSearch"));
}

QString pageScript()
{
    QFile f(QStringLiteral(":/gigaimagesearch/GigaImageSearchScript.js"));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

/// Turns the script's answer (a JS array of arrays of strings) into lists.
QList<QStringList> toCandidateLists(const QVariant &value)
{
    QList<QStringList> out;
    for (const QVariant &entry : value.toList()) {
        QStringList urls;
        for (const QVariant &u : entry.toList()) {
            const QString s = u.toString();
            if (!s.isEmpty()) urls.append(s);
        }
        if (!urls.isEmpty()) out.append(urls);
    }
    return out;
}
} // namespace

/**
 * The web page: reports the injected script's console messages and opens
 * "new window" links in the same view (the panel has no tabs).
 */
class GigaImageSearchPage : public QWebEnginePage
{
    Q_OBJECT
public:
    GigaImageSearchPage(QWebEngineProfile *profile, QObject *parent)
        : QWebEnginePage(profile, parent)
    {
    }

Q_SIGNALS:
    void gigaMessage(const QString &message);

protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel, const QString &message, int, const QString &) override
    {
        if (message.startsWith(QLatin1String("giga:"))) Q_EMIT gigaMessage(message.mid(5));
    }

    QWebEnginePage *createWindow(WebWindowType) override
    {
        return this; // links that want a new window open here
    }
};

/**
 * The web view: adds "Add to Board" to the page's right-click menu.
 */
class GigaImageSearchWebView : public QWebEngineView
{
public:
    std::function<void(const QPoint &)> onContextMenu;
    using QWebEngineView::QWebEngineView;

protected:
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        if (onContextMenu) onContextMenu(event->pos());
    }
};

#include "GigaImageSearchView.moc"

QWebEngineProfile *GigaImageSearchView::profile()
{
    static QPointer<QWebEngineProfile> s_profile;
    if (!s_profile) {
        // A named profile = kept on disk (logins survive restarts), in Giga
        // Sketch's own data folder.
        s_profile = new QWebEngineProfile(QStringLiteral("gigasketch-image-search"), qApp);
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/image-search");
        QDir().mkpath(base);
        s_profile->setPersistentStoragePath(base + QStringLiteral("/storage"));
        s_profile->setCachePath(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/image-search"));
        s_profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
        s_profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
        // Look like the plain Chromium it is: some sites serve a basic page
        // to browsers they don't recognise (the "QtWebEngine/x.y" part).
        QString agent = s_profile->httpUserAgent();
        agent.remove(QRegularExpression(QStringLiteral("QtWebEngine/[0-9.]+ ?")));
        s_profile->setHttpUserAgent(agent);

        QWebEngineScript script;
        script.setName(QStringLiteral("giga-image-search"));
        script.setSourceCode(pageScript());
        script.setInjectionPoint(QWebEngineScript::DocumentReady);
        script.setWorldId(QWebEngineScript::ApplicationWorld);
        script.setRunsOnSubFrames(false);
        s_profile->scripts()->insert(script);
    }
    return s_profile;
}

GigaImageSearchView::GigaImageSearchView(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("GigaImageSearchView"));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget *bar = new QWidget(this);
    QHBoxLayout *row = new QHBoxLayout(bar);
    row->setContentsMargins(4, 2, 4, 2);
    row->setSpacing(2);

    auto button = [bar](const QString &icon, const QString &tip) {
        QToolButton *b = new QToolButton(bar);
        b->setIcon(KisIconUtils::loadIcon(icon));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    m_back = button(QStringLiteral("go-previous"), i18n("Back"));
    m_forward = button(QStringLiteral("go-next"), i18n("Forward"));
    m_reload = button(QStringLiteral("view-refresh"), i18n("Reload"));

    m_site = new QComboBox(bar);
    m_site->setObjectName(QStringLiteral("GigaImageSearchSite"));
    for (int i = 0; i < kSiteCount; ++i) m_site->addItem(QString::fromLatin1(kSites[i].name));
    m_site->setToolTip(i18n("Where to search"));

    m_query = new QLineEdit(bar);
    m_query->setPlaceholderText(i18n("Search images…"));
    m_query->setClearButtonEnabled(true);

    m_pick = new QToolButton(bar);
    m_pick->setText(i18nc("@action:button pick several images", "Pick"));
    m_pick->setCheckable(true);
    m_pick->setAutoRaise(true);
    m_pick->setToolTip(i18n("Pick mode: click images to mark them (instead of opening them), then Add selected"));

    m_addSelected = new QToolButton(bar);
    m_addSelected->setAutoRaise(true);
    m_addSelected->setToolTip(i18n("Download the marked images and lay them out on the Board"));

    row->addWidget(m_back);
    row->addWidget(m_forward);
    row->addWidget(m_reload);
    row->addWidget(m_site);
    row->addWidget(m_query, 1);
    row->addWidget(m_pick);
    row->addWidget(m_addSelected);

    GigaImageSearchWebView *web = new GigaImageSearchWebView(this);
    m_web = web;
    m_page = new GigaImageSearchPage(profile(), m_web);
    m_web->setPage(m_page);
    web->onContextMenu = [this](const QPoint &pos) { showContextMenu(pos); };

    layout->addWidget(bar);
    layout->addWidget(m_web, 1);

    connect(m_back, &QToolButton::clicked, m_web, &QWebEngineView::back);
    connect(m_forward, &QToolButton::clicked, m_web, &QWebEngineView::forward);
    connect(m_reload, &QToolButton::clicked, m_web, &QWebEngineView::reload);
    connect(m_query, &QLineEdit::returnPressed, this, &GigaImageSearchView::search);
    connect(m_site, qOverload<int>(&QComboBox::activated), this, &GigaImageSearchView::slotSiteChanged);
    connect(m_pick, &QToolButton::toggled, this, &GigaImageSearchView::setPickMode);
    connect(m_addSelected, &QToolButton::clicked, this, &GigaImageSearchView::addSelected);
    connect(m_page, &QWebEnginePage::loadStarted, this, [this]() {
        m_loaded = false;
        m_pickCount = 0;
        updatePickUi();
    });
    connect(m_page, &QWebEnginePage::loadFinished, this, &GigaImageSearchView::slotLoadFinished);
    connect(m_page, &GigaImageSearchPage::gigaMessage, this, &GigaImageSearchView::slotConsoleMessage);
    connect(m_page, &QWebEnginePage::urlChanged, this, [this]() {
        m_back->setEnabled(m_web->history()->canGoBack());
        m_forward->setEnabled(m_web->history()->canGoForward());
    });

    // Giga Sketch: when the app frees memory in the background, a browser that
    // is not on screen drops its page (its web process lets go of the memory).
    // Qt reloads the page by itself as soon as the browser is shown again.
    if (GigaBackgroundMemoryTrimmer *trimmer = GigaBackgroundMemoryTrimmer::instance()) {
        connect(trimmer, &GigaBackgroundMemoryTrimmer::sigTrimmed, this, [this]() {
            if (m_page && !m_web->isVisible()
                && m_page->lifecycleState() == QWebEnginePage::LifecycleState::Active) {
                m_page->setLifecycleState(QWebEnginePage::LifecycleState::Discarded);
            }
        });
    }

    m_site->setCurrentIndex(qBound(0, searchConfig().readEntry("site", 0), kSiteCount - 1));
    updatePickUi();
    m_back->setEnabled(false);
    m_forward->setEnabled(false);

    if (!qEnvironmentVariableIsSet("GIGA_SELFTEST")) {
        loadUrl(QUrl(QString::fromLatin1(kSites[m_site->currentIndex()].home)));
    }
    m_query->setFocus();
}

GigaImageSearchView::~GigaImageSearchView()
{
    // The page is a child of the web view, so it goes with it; the profile
    // (owned by the application) outlives both, as WebEngine requires.
}

void GigaImageSearchView::loadUrl(const QUrl &url)
{
    m_loaded = false;
    m_web->load(url);
}

void GigaImageSearchView::search()
{
    const QString words = m_query->text().trimmed();
    const Site &site = kSites[m_site->currentIndex()];
    if (words.isEmpty()) {
        loadUrl(QUrl(QString::fromLatin1(site.home)));
        return;
    }
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(words));
    loadUrl(QUrl(QString::fromLatin1(site.search).arg(encoded)));
    m_web->setFocus();
}

void GigaImageSearchView::slotSiteChanged(int index)
{
    KConfigGroup cfg = searchConfig();
    cfg.writeEntry("site", index);
    search(); // same words on the newly chosen site (or its home page)
}

void GigaImageSearchView::slotLoadFinished(bool ok)
{
    m_loaded = ok;
    // A new page has a fresh copy of the script: tell it about Pick mode.
    setPickMode(m_pickMode);
}

void GigaImageSearchView::slotConsoleMessage(const QString &message)
{
    static const QString countTag = QStringLiteral("pick-count:");
    if (message.startsWith(countTag)) {
        m_pickCount = message.mid(countTag.size()).toInt();
        updatePickUi();
    }
}

void GigaImageSearchView::setPickMode(bool on)
{
    m_pickMode = on;
    if (m_pick->isChecked() != on) {
        const QSignalBlocker block(m_pick);
        m_pick->setChecked(on);
    }
    const QString accent = palette().color(QPalette::Highlight).name();
    m_page->runJavaScript(QStringLiteral("window.__giga && window.__giga.setPick(%1, '%2')")
                              .arg(on ? QStringLiteral("true") : QStringLiteral("false"), accent),
                          QWebEngineScript::ApplicationWorld);
    updatePickUi();
}

void GigaImageSearchView::updatePickUi()
{
    m_addSelected->setText(i18nc("@action:button", "Add selected (%1)", m_pickCount));
    m_addSelected->setEnabled(m_pickCount > 0);
    m_addSelected->setVisible(m_pickMode || m_pickCount > 0);
}

void GigaImageSearchView::addSelected()
{
    QPointer<GigaImageSearchView> self(this);
    m_page->runJavaScript(QStringLiteral("window.__giga ? window.__giga.picked() : []"), QWebEngineScript::ApplicationWorld,
                          [self](const QVariant &result) {
                              if (!self) return;
                              const QList<QStringList> images = toCandidateLists(result);
                              if (!images.isEmpty()) Q_EMIT self->addRequested(images, self->m_page->url());
                              self->m_page->runJavaScript(QStringLiteral("window.__giga && window.__giga.clearPicked()"),
                                                          QWebEngineScript::ApplicationWorld);
                          });
}

void GigaImageSearchView::addCandidatesFromScriptResult(const QVariant &result)
{
    QStringList urls;
    for (const QVariant &u : result.toList()) {
        if (!u.toString().isEmpty()) urls.append(u.toString());
    }
    if (!urls.isEmpty()) Q_EMIT addRequested({urls}, m_page->url());
}

void GigaImageSearchView::addImageByIndex(int index)
{
    QPointer<GigaImageSearchView> self(this);
    m_page->runJavaScript(QStringLiteral("window.__giga ? window.__giga.candidatesOf(%1) : []").arg(index),
                          QWebEngineScript::ApplicationWorld, [self](const QVariant &result) {
                              if (self) self->addCandidatesFromScriptResult(result);
                          });
}

void GigaImageSearchView::togglePickByIndex(int index)
{
    m_page->runJavaScript(QStringLiteral("window.__giga && window.__giga.togglePickOf(%1)").arg(index),
                          QWebEngineScript::ApplicationWorld);
}

QString GigaImageSearchView::runScriptForTest(const QString &js)
{
    QString answer;
    bool done = false;
    m_page->runJavaScript(js, QWebEngineScript::ApplicationWorld, [&answer, &done](const QVariant &result) {
        answer = result.toStringList().isEmpty() ? result.toString() : result.toStringList().join(QLatin1Char('\n'));
        done = true;
    });
    QElapsedTimer t;
    t.start();
    while (!done && t.elapsed() < 3000) QApplication::processEvents(QEventLoop::AllEvents, 20);
    return answer;
}

void GigaImageSearchView::showContextMenu(const QPoint &pos)
{
    QWebEngineContextMenuRequest *request = m_web->lastContextMenuRequest();
    // Widget pixels -> page (CSS) pixels.
    const qreal zoom = m_web->zoomFactor() > 0 ? m_web->zoomFactor() : 1.0;
    const QPointF cssPos = QPointF(pos) / zoom;

    QMenu *menu = m_web->createStandardContextMenu();
    QAction *first = menu->actions().isEmpty() ? nullptr : menu->actions().first();
    QAction *add = new QAction(KisIconUtils::loadIcon(QStringLiteral("list-add")), i18n("Add to Board"), menu);
    const bool onImage = request && request->mediaType() == QWebEngineContextMenuRequest::MediaTypeImage;
    const QUrl mediaUrl = request ? request->mediaUrl() : QUrl();
    QPointer<GigaImageSearchView> self(this);
    connect(add, &QAction::triggered, this, [self, cssPos, mediaUrl]() {
        if (!self) return;
        self->m_page->runJavaScript(
            QStringLiteral("window.__giga ? window.__giga.candidatesAt(%1, %2) : []").arg(cssPos.x()).arg(cssPos.y()),
            QWebEngineScript::ApplicationWorld, [self, mediaUrl](const QVariant &result) {
                if (!self) return;
                QStringList urls;
                for (const QVariant &u : result.toList()) urls.append(u.toString());
                // The browser's own idea of the image, as a last resort.
                if (mediaUrl.isValid() && !urls.contains(mediaUrl.toString())) urls.append(mediaUrl.toString());
                urls.removeAll(QString());
                if (!urls.isEmpty()) Q_EMIT self->addRequested({urls}, self->m_page->url());
            });
    });
    menu->insertAction(first, add);
    if (first) menu->insertSeparator(first);
    // Shown even off an image: result tiles are often a box over the picture.
    add->setEnabled(true);
    if (onImage) menu->setActiveAction(add);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(m_web->mapToGlobal(pos));
}

void GigaImageSearchView::clearBrowsingData(QWidget *parentForMessage)
{
    QWebEngineProfile *p = profile();
    p->cookieStore()->deleteAllCookies();
    p->clearHttpCache();
    p->clearAllVisitedLinks();
    QMessageBox::information(parentForMessage, i18n("Image Search"),
                             i18n("Logins, cookies and the cache of the image search browser were deleted."));
}

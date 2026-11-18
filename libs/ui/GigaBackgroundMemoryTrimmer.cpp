/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaBackgroundMemoryTrimmer.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPointer>
#include <QSet>

#include "KisDocument.h"
#include "KisMainWindow.h"
#include "KisPart.h"
#include "KisViewManager.h"
#include "kis_config.h"
#include "kis_config_notifier.h"
#include "kis_image.h"
#include "kis_layer_utils.h"
#include "kis_node.h"
#include "kis_paint_device.h"
#include "tiles3/swap/GigaTileMemoryTrim.h"

#include <cstdio>

// GIGA_MEMTRIM_TRACE=1: one line per trim/prewarm on stderr (qInfo is filtered)
#define gigaTrace(...) do { std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)

namespace
{
const char *KEY_ENABLED = "gigaBackgroundMemoryTrim";
const char *KEY_DELAY = "gigaBackgroundMemoryTrimDelay";
const int DEFAULT_DELAY = 30;     // seconds without focus before trimming
const int POLL_MS = 250;          // how often we look at the swapper's progress
const int BUSY_RETRY_MS = 10000;  // a save or a filter is running: try again later
const int MAX_POLL_ROUNDS = 4 * 120; // give up waiting for the swapper after ~2 min
}

GigaBackgroundMemoryTrimmer *GigaBackgroundMemoryTrimmer::instance()
{
    static QPointer<GigaBackgroundMemoryTrimmer> s_instance;
    if (!s_instance && qApp) {
        // parented to the application: lives until the app quits
        s_instance = new GigaBackgroundMemoryTrimmer(qApp);
    }
    return s_instance;
}

bool GigaBackgroundMemoryTrimmer::enabledInConfig()
{
    KisConfig cfg(true);
    return cfg.readEntry<bool>(KEY_ENABLED, true);
}

void GigaBackgroundMemoryTrimmer::setEnabledInConfig(bool on)
{
    KisConfig cfg(false);
    cfg.writeEntry<bool>(KEY_ENABLED, on);
}

int GigaBackgroundMemoryTrimmer::delaySecondsInConfig()
{
    KisConfig cfg(true);
    return qBound(5, cfg.readEntry<int>(KEY_DELAY, DEFAULT_DELAY), 3600);
}

void GigaBackgroundMemoryTrimmer::setDelaySecondsInConfig(int seconds)
{
    KisConfig cfg(false);
    cfg.writeEntry<int>(KEY_DELAY, qBound(5, seconds, 3600));
}

GigaBackgroundMemoryTrimmer::GigaBackgroundMemoryTrimmer(QObject *parent)
    : QObject(parent)
{
    m_trace = qEnvironmentVariableIntValue("GIGA_MEMTRIM_TRACE") > 0;

    m_delay.setSingleShot(true);
    connect(&m_delay, &QTimer::timeout, this, &GigaBackgroundMemoryTrimmer::slotDelayElapsed);

    m_poll.setInterval(POLL_MS);
    connect(&m_poll, &QTimer::timeout, this, &GigaBackgroundMemoryTrimmer::slotPoll);

    // one worker is enough: loading tiles back is limited by decompression
    m_prewarmPool.setMaxThreadCount(1);

    connect(qGuiApp, &QGuiApplication::applicationStateChanged,
            this, &GigaBackgroundMemoryTrimmer::slotApplicationStateChanged);
    connect(KisConfigNotifier::instance(), &KisConfigNotifier::configChanged,
            this, &GigaBackgroundMemoryTrimmer::slotConfigChanged);

    // quitting: stop the worker before the documents go away
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        m_prewarmCancel.storeRelease(1);
        GigaTileMemoryTrim::cancel();
        m_prewarmPool.waitForDone();
    });

    if (qGuiApp->applicationState() != Qt::ApplicationActive) {
        slotApplicationStateChanged(qGuiApp->applicationState());
    }
}

GigaBackgroundMemoryTrimmer::~GigaBackgroundMemoryTrimmer()
{
    m_prewarmCancel.storeRelease(1);
    GigaTileMemoryTrim::cancel();
    m_prewarmPool.waitForDone();
}

bool GigaBackgroundMemoryTrimmer::isBusy() const
{
    return m_phase == Phase::SwappingOut || m_prewarmPool.activeThreadCount() > 0;
}

void GigaBackgroundMemoryTrimmer::slotApplicationStateChanged(Qt::ApplicationState state)
{
    if (state == Qt::ApplicationActive) {
        comeBack();
        return;
    }

    // In the background now (another window has focus, or minimized).
    if (m_phase != Phase::Idle || !enabledInConfig()) return;
    // UI self-tests run unfocused; only the memtrim test trims (trimNow()).
    if (qEnvironmentVariableIsSet("GIGA_SELFTEST")) return;

    // Loading tiles back is pointless now; stop it so it does not undo the trim.
    m_prewarmCancel.storeRelease(1);

    m_phase = Phase::Waiting;
    m_delay.start(delaySecondsInConfig() * 1000);
}

void GigaBackgroundMemoryTrimmer::slotConfigChanged()
{
    if (!enabledInConfig() && m_phase == Phase::Waiting) {
        m_delay.stop();
        m_phase = Phase::Idle;
    }
}

void GigaBackgroundMemoryTrimmer::slotDelayElapsed()
{
    if (m_phase != Phase::Waiting) return;
    if (qGuiApp->applicationState() == Qt::ApplicationActive) {
        m_phase = Phase::Idle;
        return;
    }
    startSwapOut();
}

void GigaBackgroundMemoryTrimmer::trimNow()
{
    m_delay.stop();
    m_phase = Phase::Waiting;
    startSwapOut();
}

void GigaBackgroundMemoryTrimmer::restoreNow()
{
    comeBack();
}

bool GigaBackgroundMemoryTrimmer::documentsBusy() const
{
    if (m_prewarmPool.activeThreadCount() > 0) return true;

    Q_FOREACH (const QPointer<KisDocument> &doc, KisPart::instance()->documents()) {
        if (!doc) continue;
        // autosave and background saves read the image from another thread
        if (doc->isSaving() || doc->isAutosaving()) return true;
        KisImageSP image = doc->image();
        if (image && !image->isIdle()) return true;
    }
    return false;
}

void GigaBackgroundMemoryTrimmer::startSwapOut()
{
    if (documentsBusy()) {
        // a save or a filter is still running: look again a bit later
        m_delay.start(BUSY_RETRY_MS);
        return;
    }

    m_residentBefore = GigaTileMemoryTrim::residentMiB();
    m_clock.start();
    m_pollRounds = 0;
    m_phase = Phase::SwappingOut;

    if (m_trace) {
        gigaTrace("GIGA_MEMTRIM start: %d MiB resident, %d of %d tiles in memory",
              m_residentBefore, GigaTileMemoryTrim::tilesInMemory(), GigaTileMemoryTrim::tilesTotal());
    }

    // the swapper thread does the work; we just watch it
    GigaTileMemoryTrim::requestSwapOut();
    m_poll.start();
}

void GigaBackgroundMemoryTrimmer::slotPoll()
{
    if (m_phase != Phase::SwappingOut) {
        m_poll.stop();
        return;
    }

    if (++m_pollRounds > MAX_POLL_ROUNDS) {
        // the swapper never reported back; leave things as they are
        GigaTileMemoryTrim::cancel();
        m_poll.stop();
        m_phase = Phase::Trimmed;
        if (m_trace) gigaTrace("GIGA_MEMTRIM gave up waiting for the swapper");
        return;
    }

    if (!GigaTileMemoryTrim::swapOutFinished()) return;

    // A stroke/save started meanwhile (e.g. a Blender layer refresh): wait.
    if (documentsBusy()) return;

    m_poll.stop();
    finishTrim();
}

void GigaBackgroundMemoryTrimmer::finishTrim()
{
    const int swappedMs = int(m_clock.elapsed());
    const int tilesLeft = GigaTileMemoryTrim::tilesInMemory();

    const bool poolsReleased = GigaTileMemoryTrim::releaseMemory();
    const int after = GigaTileMemoryTrim::residentMiB();

    m_phase = Phase::Trimmed;

    if (m_trace) {
        gigaTrace("GIGA_MEMTRIM done in %d ms: %d -> %d MiB resident, %d tiles left in memory, pools %s",
              swappedMs, m_residentBefore, after, tilesLeft, poolsReleased ? "released" : "kept");
    }

    Q_EMIT sigTrimmed(poolsReleased, m_residentBefore, after);
}

void GigaBackgroundMemoryTrimmer::comeBack()
{
    m_delay.stop();
    m_poll.stop();

    const Phase was = m_phase;
    m_phase = Phase::Idle;

    if (was == Phase::SwappingOut || was == Phase::Trimmed) {
        // the swapper stops at its next step
        GigaTileMemoryTrim::cancel();
        startPrewarm();
    }
}

void GigaBackgroundMemoryTrimmer::startPrewarm()
{
    // Collect the devices on the GUI thread, most useful first: the layer the
    // user is working on, then what the canvas shows, then everything else.
    QList<KisPaintDeviceSP> devices;
    QSet<KisPaintDevice*> seen;
    auto add = [&](KisPaintDeviceSP dev) {
        if (dev && !seen.contains(dev.data())) {
            seen.insert(dev.data());
            devices << dev;
        }
    };

    KisMainWindow *window = KisPart::instance()->currentMainwindow();
    if (window && window->viewManager()) {
        KisNodeSP node = window->viewManager()->activeNode();
        if (node) add(node->paintDevice());
    }

    Q_FOREACH (const QPointer<KisDocument> &doc, KisPart::instance()->documents()) {
        if (!doc || !doc->image()) continue;
        add(doc->image()->projection());
    }

    Q_FOREACH (const QPointer<KisDocument> &doc, KisPart::instance()->documents()) {
        if (!doc || !doc->image()) continue;
        KisLayerUtils::recursiveApplyNodes(doc->image()->root(), [&](KisNodeSP n) {
            add(n->paintDevice());
            add(n->original());
            add(n->projection());
        });
    }

    if (devices.isEmpty()) return;

    m_prewarmCancel.storeRelease(0);
    const bool trace = m_trace;

    // The worker holds its own references to the devices, so a layer deleted
    // meanwhile stays valid until the worker is done with it.
    m_prewarmPool.start([this, devices, trace]() {
        QElapsedTimer clock;
        clock.start();
        const int tiles = GigaTileMemoryTrim::prewarm(devices, m_prewarmCancel);
        const int ms = int(clock.elapsed());
        if (trace) {
            gigaTrace("GIGA_MEMTRIM prewarm: %d tiles in %d ms%s, %d MiB resident", tiles, ms,
                  m_prewarmCancel.loadAcquire() ? " (stopped)" : "", GigaTileMemoryTrim::residentMiB());
        }
        QMetaObject::invokeMethod(this, [this, tiles, ms]() {
            Q_EMIT sigPrewarmFinished(tiles, ms);
        }, Qt::QueuedConnection);
    });
}

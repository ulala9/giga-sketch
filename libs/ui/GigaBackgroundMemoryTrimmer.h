/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_BACKGROUND_MEMORY_TRIMMER_H
#define GIGA_BACKGROUND_MEMORY_TRIMMER_H

#include <QAtomicInt>
#include <QElapsedTimer>
#include <QObject>
#include <QThreadPool>
#include <QTimer>

#include "kritaui_export.h"

/**
 * Giga Sketch: "Free memory in background" (Preferences > Performance).
 *
 * When no Giga Sketch window has had focus for a while, image tiles go to the
 * compressed swap file and the freed memory goes back to the system
 * (GigaTileMemoryTrim in libs/image does the work). When the app becomes
 * active again, the tiles are loaded back in a worker thread.
 *
 * Lives on the GUI thread; never blocks it for more than a few milliseconds
 * (the swapping runs in Krita's swapper thread).
 */
class KRITAUI_EXPORT GigaBackgroundMemoryTrimmer : public QObject
{
    Q_OBJECT
public:
    static GigaBackgroundMemoryTrimmer *instance();

    static bool enabledInConfig();
    static void setEnabledInConfig(bool on);
    static int delaySecondsInConfig();
    static void setDelaySecondsInConfig(int seconds);

    /// Trim now, ignoring the delay and the focus (self-test).
    void trimNow();
    /// Act as if the app became active again (self-test).
    void restoreNow();

    bool isTrimmed() const { return m_phase == Phase::Trimmed; }
    bool isBusy() const;

Q_SIGNALS:
    /// The memory has been handed back. Others may free their own caches.
    void sigTrimmed(bool poolsReleased, int residentBeforeMiB, int residentAfterMiB);
    void sigPrewarmFinished(int tiles, int milliseconds);

private Q_SLOTS:
    void slotApplicationStateChanged(Qt::ApplicationState state);
    void slotDelayElapsed();
    void slotPoll();
    void slotConfigChanged();

private:
    explicit GigaBackgroundMemoryTrimmer(QObject *parent);
    ~GigaBackgroundMemoryTrimmer() override;

    enum class Phase { Idle, Waiting, SwappingOut, Trimmed };

    bool documentsBusy() const;
    void startSwapOut();
    void finishTrim();
    void comeBack();
    void startPrewarm();

    Phase m_phase {Phase::Idle};
    QTimer m_delay;
    QTimer m_poll;
    QElapsedTimer m_clock;
    int m_residentBefore {-1};
    int m_pollRounds {0};
    bool m_trace {false};

    QThreadPool m_prewarmPool;
    QAtomicInt m_prewarmCancel;
};

#endif

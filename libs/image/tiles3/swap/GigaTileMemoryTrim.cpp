/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaTileMemoryTrim.h"

#include <QFile>

#include "kis_paint_device.h"
#include "kis_random_accessor_ng.h"
#include "tiles3/kis_tile_data.h"
#include "tiles3/kis_tile_data_store.h"

#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace
{
// 0 = nothing asked, 1 = swap-out requested, 2 = swap-out finished
QAtomicInt s_state;
}

namespace GigaTileMemoryTrim
{

void requestSwapOut()
{
    s_state.storeRelease(1);
    // kickPooler() also wakes the swapper thread (see KisTileDataStore)
    KisTileDataStore::instance()->kickPooler();
}

void cancel()
{
    s_state.storeRelease(0);
}

bool swapOutFinished()
{
    return s_state.loadAcquire() == 2;
}

bool swapOutRequested()
{
    return s_state.loadAcquire() == 1;
}

void markSwapOutFinished()
{
    // only if nobody cancelled in the meantime
    s_state.testAndSetOrdered(1, 2);
}

int tilesInMemory()
{
    return KisTileDataStore::instance()->numTilesInMemory();
}

int tilesTotal()
{
    return KisTileDataStore::instance()->numTiles();
}

bool releaseMemory()
{
    const int before = tilesInMemory();
    // Krita's own function: it does nothing when 100 or more tiles are still
    // in memory, and (Giga change) when it cannot prove the purge is safe.
    KisTileData::releaseInternalPools();
#ifdef __GLIBC__
    // give the freed heap pages back to the system
    malloc_trim(0);
#endif
    return before < 100;
}

int residentMiB()
{
#ifdef Q_OS_LINUX
    QFile f(QStringLiteral("/proc/self/statm"));
    if (f.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> parts = f.readAll().split(' ');
        if (parts.size() > 1) {
            return int(parts[1].toLongLong() * 4096 / (1024 * 1024));
        }
    }
#endif
    return -1;
}

int prewarm(const QList<PrewarmJob> &jobs, const QAtomicInt &cancelFlag)
{
    const int tw = KisTileData::WIDTH;
    const int th = KisTileData::HEIGHT;
    int touched = 0;

    for (const PrewarmJob &job : jobs) {
        if (!job.device) continue;
        QRect r = job.device->extent();
        if (!job.rect.isEmpty()) r &= job.rect;
        if (r.isEmpty()) continue;

        // A read-only accessor: moving it onto a tile loads that tile from
        // swap (if needed) and never creates new tiles. A tile that is
        // already in memory costs next to nothing, so the "whole device"
        // jobs after the "visible area" ones are cheap where they overlap.
        KisRandomConstAccessorSP acc = job.device->createRandomConstAccessorNG();
        const int x0 = r.left() - ((r.left() % tw) + tw) % tw;
        const int y0 = r.top() - ((r.top() % th) + th) % th;

        for (int y = y0; y <= r.bottom(); y += th) {
            for (int x = x0; x <= r.right(); x += tw) {
                if (cancelFlag.loadAcquire()) return touched;
                acc->moveTo(x, y);
                (void)acc->rawDataConst();
                touched++;
            }
        }
    }
    return touched;
}

}

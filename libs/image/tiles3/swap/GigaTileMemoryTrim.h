/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_TILE_MEMORY_TRIM_H
#define GIGA_TILE_MEMORY_TRIM_H

#include <QAtomicInt>

#include "kritaimage_export.h"
#include "kis_types.h"

/**
 * Giga Sketch: give memory back to the system while the app sits in the
 * background (driven by GigaBackgroundMemoryTrimmer in libs/ui).
 *
 * 1. requestSwapOut(): the swapper thread moves (nearly) every image tile into
 *    the compressed swap file, in small steps, until cancel() is called.
 * 2. releaseMemory(): once that is done, hands the freed tile memory back to the
 *    system (Krita's own pool release + malloc_trim). It only purges the pools
 *    when it can prove no tile still uses them (see kis_tile_data.cc).
 * 3. prewarm(): when the app is active again, loads the tiles of the given
 *    paint devices back from the swap file in a background thread, so the first
 *    stroke does not have to wait for them.
 */
namespace GigaTileMemoryTrim
{
/// Ask the swapper thread to swap out every tile it can. Never blocks.
KRITAIMAGE_EXPORT void requestSwapOut();

/// Stop a swap-out in progress (the app became active again). Never blocks.
KRITAIMAGE_EXPORT void cancel();

/// True after requestSwapOut() once the swapper has finished that request.
KRITAIMAGE_EXPORT bool swapOutFinished();

/// Image tiles still in RAM / swapped out (for logs and the self-test).
KRITAIMAGE_EXPORT int tilesInMemory();
KRITAIMAGE_EXPORT int tilesTotal();

/**
 * Release the tile pools and trim the heap. Call from the GUI thread, after
 * swapOutFinished(). Returns true when the tile pools were released.
 */
KRITAIMAGE_EXPORT bool releaseMemory();

/// Current resident memory of the process in MiB (Linux; -1 elsewhere).
KRITAIMAGE_EXPORT int residentMiB();

/**
 * Touch every tile of @p devices so they are loaded back from swap. Meant to
 * run in a worker thread; stops early when @p cancelFlag becomes non-zero.
 * Returns the number of tiles touched.
 */
KRITAIMAGE_EXPORT int prewarm(const QList<KisPaintDeviceSP> &devices, const QAtomicInt &cancelFlag);

// Used by KisTileDataSwapper only.
bool swapOutRequested();
void markSwapOutFinished();
}

#endif

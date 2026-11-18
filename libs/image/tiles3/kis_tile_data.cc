/*
 *  SPDX-FileCopyrightText: 2009 Dmitry Kazakov <dimula73@gmail.com>
 *  SPDX-FileCopyrightText: 2018 Andrey Kamakin <a.kamakin@icloud.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */


#include "kis_tile_data.h"
#include "kis_tile_data_store.h"

#include <kis_debug.h>

#include <boost/pool/singleton_pool.hpp>
#include <QReadWriteLock>
#include <QAtomicInt>
#include "kis_tile_data_store_iterators.h"

// BPP == bytes per pixel
#define TILE_SIZE_4BPP (4 * __TILE_DATA_WIDTH * __TILE_DATA_HEIGHT)
#define TILE_SIZE_8BPP (8 * __TILE_DATA_WIDTH * __TILE_DATA_HEIGHT)

typedef boost::singleton_pool<KisTileData, TILE_SIZE_4BPP, boost::default_user_allocator_new_delete, boost::details::pool::default_mutex, 256, 4096> BoostPool4BPP;
typedef boost::singleton_pool<KisTileData, TILE_SIZE_8BPP, boost::default_user_allocator_new_delete, boost::details::pool::default_mutex, 128, 2048> BoostPool8BPP;

const qint32 KisTileData::WIDTH = __TILE_DATA_WIDTH;
const qint32 KisTileData::HEIGHT = __TILE_DATA_HEIGHT;

SimpleCache KisTileData::m_cache;

namespace {
/**
 * Giga Sketch: makes releaseInternalPools() safe to call while other threads
 * are running (Giga Sketch calls it when the app sits in the background, see
 * GigaTileMemoryTrim).
 *
 * purge_memory() frees *every* chunk of the pools, so it must never run while
 * some chunk still belongs to a tile that releaseInternalPools() did not copy
 * aside (e.g. a tile that is being created right now and is not registered in
 * the store yet). We count the chunks that belong to tiles, and the purge only
 * happens when that count matches the tiles it found. The read lock is taken
 * by every allocateData()/freeData() (like SimpleCache's own lock), the write
 * lock only by releaseInternalPools(), so no chunk can change hands while it
 * checks and purges.
 */
QReadWriteLock s_gigaPoolLock;
QAtomicInt s_gigaPooledChunksInUse;

inline bool gigaIsPooledSize(qint32 pixelSize)
{
    return pixelSize == 4 || pixelSize == 8;
}
}

SimpleCache::~SimpleCache()
{
    clear();
}

void SimpleCache::clear()
{
    QWriteLocker l(&m_cacheLock);
    quint8 *ptr = 0;

    while (m_4Pool.pop(ptr)) {
        BoostPool4BPP::free(ptr);
    }

    while (m_8Pool.pop(ptr)) {
        BoostPool8BPP::free(ptr);
    }

    while (m_16Pool.pop(ptr)) {
        free(ptr);
    }
}


KisTileData::KisTileData(qint32 pixelSize, const quint8 *defPixel, KisTileDataStore *store, bool checkFreeMemory)
    : m_state(NORMAL),
      m_mementoFlag(0),
      m_age(0),
      m_usersCount(0),
      m_refCount(0),
      m_pixelSize(pixelSize),
      m_store(store)
{
    if (checkFreeMemory) {
        m_store->checkFreeMemory();
    }
    m_data = allocateData(m_pixelSize);

    fillWithPixel(defPixel);
}


/**
 * Duplicating tiledata
 * + new object loaded in memory
 * + it's unlocked and has refCount==0
 *
 * NOTE: the memory allocated by the pooler for clones is not counted
 * by the store in memoryHardLimit. The pooler has it's own slice of
 * memory and keeps track of the its size itself. So we should be able
 * to disable the memory check with checkFreeMemory, otherwise, there
 * is a deadlock.
 */
KisTileData::KisTileData(const KisTileData& rhs, bool checkFreeMemory)
    : m_state(NORMAL),
      m_mementoFlag(0),
      m_age(0),
      m_usersCount(0),
      m_refCount(0),
      m_pixelSize(rhs.m_pixelSize),
      m_store(rhs.m_store)
{
    if (checkFreeMemory) {
        m_store->checkFreeMemory();
    }
    m_data = allocateData(m_pixelSize);

    memcpy(m_data, rhs.data(), m_pixelSize * WIDTH * HEIGHT);
}


KisTileData::~KisTileData()
{
    releaseMemory();
}

void KisTileData::fillWithPixel(const quint8 *defPixel)
{
    quint8 *it = m_data;

    for (int i = 0; i < WIDTH * HEIGHT; i++, it += m_pixelSize) {
        memcpy(it, defPixel, m_pixelSize);
    }
}

void KisTileData::releaseMemory()
{
    if (m_data) {
        freeData(m_data, m_pixelSize);
        m_data = 0;
    }

    KisTileData *clone = 0;
    while (m_clonesStack.pop(clone)) {
        delete clone;
    }

    Q_ASSERT(m_clonesStack.isEmpty());
}

void KisTileData::allocateMemory()
{
    Q_ASSERT(!m_data);
    m_data = allocateData(m_pixelSize);
}

quint8* KisTileData::allocateData(const qint32 pixelSize)
{
    QReadLocker gigaPoolLocker(&s_gigaPoolLock);
    if (gigaIsPooledSize(pixelSize)) {
        s_gigaPooledChunksInUse.ref();
    }

    quint8 *ptr = 0;

    if (!m_cache.pop(pixelSize, ptr)) {
        switch (pixelSize) {
        case 4:
            ptr = (quint8*)BoostPool4BPP::malloc();
            break;
        case 8:
            ptr = (quint8*)BoostPool8BPP::malloc();
            break;
        default:
            ptr = (quint8*) malloc(pixelSize * WIDTH * HEIGHT);
            break;
        }
    }

    return ptr;
}

void KisTileData::freeData(quint8* ptr, const qint32 pixelSize)
{
    QReadLocker gigaPoolLocker(&s_gigaPoolLock);
    if (gigaIsPooledSize(pixelSize)) {
        s_gigaPooledChunksInUse.deref();
    }

    if (!m_cache.push(pixelSize, ptr)) {
        switch (pixelSize) {
        case 4:
            BoostPool4BPP::free(ptr);
            break;
        case 8:
            BoostPool8BPP::free(ptr);
            break;
        default:
            free(ptr);
            break;
        }
    }
}

//#define DEBUG_POOL_RELEASE

#ifdef DEBUG_POOL_RELEASE
#include <unistd.h>
#endif /* DEBUG_POOL_RELEASE */

void KisTileData::releaseInternalPools()
{
    const int maxMigratedTiles = 100;

    if (KisTileDataStore::instance()->numTilesInMemory() < maxMigratedTiles) {

        QVector<KisTileData*> dataObjects;
        QVector<QByteArray> memoryChunks;
        bool failedToLock = false;

        KisTileDataStoreIterator *iter = KisTileDataStore::instance()->beginIteration();

        /**
         * Giga Sketch: two steps instead of one loop. While we hold the
         * iteration lock no tile can be registered, swapped in or swapped
         * out, so the list stays valid between the steps.
         *
         * Step 1: release all the clones. Their memory goes back through
         * freeData(), which takes s_gigaPoolLock, so this must happen
         * before we lock the pools.
         */
        QVector<KisTileData*> allItems;
        while (iter->hasNext()) {
            KisTileData *item = iter->next();

            KisTileData *clone = 0;
            while (item->m_clonesStack.pop(clone)) {
                delete clone;
            }

            allItems << item;
        }

        /**
         * Step 2: with the pools locked, copy every pooled tile aside. Only
         * 4 and 8 bytes-per-pixel tiles live in the pools (bigger ones use
         * malloc() and are not touched by the purge).
         */
        QWriteLocker gigaPoolLocker(&s_gigaPoolLock);

        Q_FOREACH (KisTileData *item, allItems) {
            if (!gigaIsPooledSize(item->m_pixelSize)) continue;

            // check if the tile has been swapped out
            if (item->m_data) {
                const bool locked = item->m_swapLock.tryLockForWrite();
                if (!locked) {
                    failedToLock = true;
                    break;
                }

                const int chunkSize = item->m_pixelSize * WIDTH * HEIGHT;
                dataObjects << item;
                memoryChunks << QByteArray((const char*)item->m_data, chunkSize);
            }
        }

        const bool gigaChunksMatch =
            dataObjects.size() == s_gigaPooledChunksInUse.loadAcquire();

        if (!failedToLock && gigaChunksMatch) {
            // purge the pools memory
            m_cache.clear();
            BoostPool4BPP::purge_memory();
            BoostPool8BPP::purge_memory();
            s_gigaPooledChunksInUse.storeRelease(0);

            auto it = dataObjects.begin();
            auto chunkIt = memoryChunks.constBegin();

            for (; it != dataObjects.end(); ++it, ++chunkIt) {
                KisTileData *item = *it;
                const int chunkSize = item->m_pixelSize * WIDTH * HEIGHT;

                // allocateData() would take s_gigaPoolLock again; the cache
                // is empty now, so ask the pools directly
                item->m_data = item->m_pixelSize == 4
                    ? (quint8*)BoostPool4BPP::malloc()
                    : (quint8*)BoostPool8BPP::malloc();
                s_gigaPooledChunksInUse.ref();
                memcpy(item->m_data, chunkIt->data(), chunkSize);

                item->m_swapLock.unlock();
            }
        } else {
            Q_FOREACH (KisTileData *item, dataObjects) {
                item->m_swapLock.unlock();
            }

            if (failedToLock) {
                warnKrita << "WARNING: Failed to lock the tiles while trying to release the pooled memory";
            } else {
                dbgKrita << "DEBUG: releasing of the pooled memory has been cancelled:"
                         << "some pooled memory belongs to tiles that are being created";
            }
        }

        gigaPoolLocker.unlock();

        KisTileDataStore::instance()->endIteration(iter);

#ifdef DEBUG_POOL_RELEASE
        dbgKrita << "After purging unused memory:";

        char command[256];
        sprintf(command, "cat /proc/%d/status | grep -i vm", (int)getpid());
        printf("--- %s ---\n", command);
        (void)system(command);
#endif /* DEBUG_POOL_RELEASE */

    } else {
        dbgKrita << "DEBUG: releasing of the pooled memory has been cancelled:"
                 << "there are still"
                 << KisTileDataStore::instance()->numTilesInMemory()
                 << "tiles in memory";
    }
}

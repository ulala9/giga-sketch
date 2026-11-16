/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "GigaLastStroke.h"

#include "kis_resources_snapshot.h"
#include "kis_selection.h"
#include "kis_node.h"
#include "kis_image.h"

namespace {
// A stroke with more calls than this is not kept (memory guard: one call is
// a few hundred bytes, so this is roughly 100 MB at worst).
const int MaxJobs = 300000;
}

GigaLastStrokeRecordSP &GigaLastStroke::current()
{
    static GigaLastStrokeRecordSP s;
    return s;
}

GigaLastStrokeRecordSP &GigaLastStroke::finished()
{
    static GigaLastStrokeRecordSP s;
    return s;
}

void GigaLastStroke::begin(GigaLastStrokeRecordSP record)
{
    current() = record;
}

void GigaLastStroke::addJob(const GigaLastStrokeRecord::Job &job)
{
    GigaLastStrokeRecordSP rec = current();
    if (!rec || rec->tooLong) return;
    if (rec->jobs.size() >= MaxJobs) {
        rec->tooLong = true;
        rec->jobs.clear();
        rec->jobs.squeeze();
        return;
    }
    rec->jobs.append(job);
}

void GigaLastStroke::finish()
{
    GigaLastStrokeRecordSP rec = current();
    current().clear();
    if (!rec) return;
    rec->endTime = QTime::currentTime();
    finished() = rec;
}

void GigaLastStroke::abort()
{
    current().clear();
}

bool GigaLastStroke::isRecording()
{
    return bool(current());
}

GigaLastStrokeRecordSP GigaLastStroke::last()
{
    return finished();
}

void GigaLastStroke::clearLast()
{
    finished().clear();
}

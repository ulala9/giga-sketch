/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef GIGA_LAST_STROKE_H
#define GIGA_LAST_STROKE_H

#include <QPointF>
#include <QSharedPointer>
#include <QTime>
#include <QVector>

#include "kritaui_export.h"
#include "kis_types.h"
#include "kis_resources_snapshot.h"
#include "kundo2magicstring.h"
#include <brushengine/kis_paint_information.h>
#include <brushengine/kis_stroke_random_source.h>
#include <kis_distance_information.h>

/**
 * Giga Sketch (Transform Last Stroke): a recording of the most recent
 * freehand stroke, detailed enough to paint it again.
 *
 * KisToolFreehandHelper sends every dab of a stroke to the paint thread
 * through three calls (paintAt / paintLine / paintBezierCurve). The helper
 * copies each of those calls into this record as it goes (GUI thread only,
 * just a list append), together with the brush settings ("resources
 * snapshot"), where each mirrored copy of the stroke started, and the
 * stroke's random numbers. Repainting the jobs in order with those settings
 * gives back the same stroke, on its own.
 */
struct KRITAUI_EXPORT GigaLastStrokeRecord
{
    enum JobType { Point, Line, Curve };

    struct Job {
        JobType type {Point};
        int strokeInfoId {0};
        KisPaintInformation pi1;
        KisPaintInformation pi2;
        QPointF control1;
        QPointF control2;
    };

    KisImageWSP image;
    KisNodeSP node;                       ///< the layer the stroke was painted on
    KisResourcesSnapshotSP resources;     ///< brush, colour, opacity... at stroke start
    KisSelectionSP selection;             ///< selection that clipped the stroke (may be null)
    QVector<KisDistanceInformation> startDistances; ///< one per mirrored copy
    KisStrokeRandomSource randomSource;   ///< fresh copy, taken before any dab
    KUndo2MagicString name;               ///< the undo text ("Freehand Brush Stroke")
    QTime startTime;
    QTime endTime;
    QVector<Job> jobs;
    bool tooLong {false};                 ///< stopped recording (huge stroke)
    /// Set when Transform Last Stroke had to write the stroke back as a new
    /// undo entry: that entry (at that index) now stands for the stroke.
    const void *replacementCommand {nullptr};
    int replacementIndex {-1};
};

typedef QSharedPointer<GigaLastStrokeRecord> GigaLastStrokeRecordSP;

/**
 * Process-wide holder of the recording. Only touched from the GUI thread.
 */
class KRITAUI_EXPORT GigaLastStroke
{
public:
    /// The stroke helper starts a new recording (replaces any unfinished one).
    static void begin(GigaLastStrokeRecordSP record);
    /// Append one painting call to the recording in progress (if any).
    static void addJob(const GigaLastStrokeRecord::Job &job);
    /// The stroke ended normally: it becomes "the last stroke".
    static void finish();
    /// The stroke was cancelled: drop the recording in progress.
    static void abort();

    /// True while a stroke is being painted (not finished yet).
    static bool isRecording();

    /// The last finished stroke, or null.
    static GigaLastStrokeRecordSP last();
    /// Forget the last stroke (e.g. after it has been transformed).
    static void clearLast();

private:
    static GigaLastStrokeRecordSP &current();
    static GigaLastStrokeRecordSP &finished();
};

#endif // GIGA_LAST_STROKE_H

/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaLastStrokeTransform.h"

#include <QIcon>
#include <QMetaObject>
#include <QTimer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <cstring>
#include <functional>

#include <klocalizedstring.h>
#include <KoCompositeOpRegistry.h>
#include <KoToolManager.h>
#include <kundo2command.h>
#include <kundo2stack.h>

#include "KisDocument.h"
#include "KisPart.h"
#include "KisViewManager.h"
#include "kis_canvas2.h"
#include "kis_config.h"
#include "kis_floating_message.h"
#include "kis_image.h"
#include "kis_node_manager.h"
#include "kis_paint_device.h"
#include "kis_paint_layer.h"
#include "kis_painter.h"
#include "kis_processing_applicator.h"
#include "kis_random_accessor_ng.h"
#include "kis_transaction.h"
#include "kis_raster_keyframe_channel.h"
#include <brushengine/kis_paintop_preset.h>
#include "tool/kis_tool.h"
#include "tool/kis_resources_snapshot.h"
#include "tool/strokes/freehand_stroke.h"
#include "tool/strokes/KisFreehandStrokeInfo.h"
#include "tool/KisAsynchronousStrokeUpdateHelper.h"
#include <KisRunnableBasedStrokeStrategy.h>
#include <KisRunnableStrokeJobData.h>

namespace {

const char *ModeKey = "gigaLastStrokeTransformMode";
const int WarpMode = 1; // KisToolTransform::WarpTransformMode
const char *EditModeKey = "gigaLastStrokeEditMode";     // 0 Pixels, 1 Points
const char *PointSpacingKey = "gigaStrokePointSpacing"; // screen px
const char *PointsToolId = "GigaToolStrokePoints";

GigaLastStrokeTransform *s_pointsSession = nullptr;

/**
 * A stroke that just runs one function on the image's threads, with no
 * undo entry. Used to show the Points preview on the real layer: the layer
 * is set back to "lifted" and the repainted stroke is drawn on top. As a
 * stroke it runs in order with the repaint and never while the image is
 * being rendered from those pixels.
 */
class GigaNoUndoJobStrategy : public KisRunnableBasedStrokeStrategy
{
public:
    GigaNoUndoJobStrategy()
        : KisRunnableBasedStrokeStrategy(QLatin1String("GigaNoUndoJob"))
    {
        enableJob(KisSimpleStrokeStrategy::JOB_DOSTROKE);
        setClearsRedoOnStart(false);
        setRequestsOtherStrokesToEnd(false);
    }
};

void runNoUndo(KisImageSP image, std::function<void()> func)
{
    KisStrokeId id = image->startStroke(new GigaNoUndoJobStrategy());
    image->addJob(id, new KisRunnableStrokeJobData(func, KisStrokeJobData::SEQUENTIAL, KisStrokeJobData::EXCLUSIVE));
    image->endStroke(id);
}

/**
 * One undo entry that replaces the pixels of @p rect on a layer with the
 * pixels of another device (a saved copy). The first redo() does the copy
 * inside a KisTransaction, which remembers the old tiles; later undo/redo
 * just replay that transaction.
 */
class GigaCopyPixelsCommand : public KUndo2Command
{
public:
    GigaCopyPixelsCommand(KisPaintDeviceSP target, KisPaintDeviceSP source,
                          const QRect &rect, const KUndo2MagicString &name)
        : KUndo2Command(name), m_target(target), m_source(source), m_rect(rect) {}

    void redo() override {
        if (m_transaction) {
            m_transaction->redo();
            return;
        }
        KisTransaction transaction(m_target);
        KisPainter::copyAreaOptimized(m_rect.topLeft(), m_source, m_target, m_rect);
        m_transaction.reset(transaction.endAndTake());
        // A fresh transaction ignores its first redo() (the change is already
        // made); use that up now so a later redo really re-applies it.
        m_transaction->redo();
        m_target->setDirty(m_rect);
        m_source.clear(); // not needed any more
    }

    void undo() override {
        if (m_transaction) m_transaction->undo();
    }

private:
    KisPaintDeviceSP m_target;
    KisPaintDeviceSP m_source;
    QRect m_rect;
    QScopedPointer<KUndo2Command> m_transaction;
};

/// Brush engines that read the pixels they paint over: they can't be
/// repainted on an empty layer and look the same.
bool readsCanvas(const QString &paintOpId)
{
    return paintOpId == QLatin1String("colorsmudge")
        || paintOpId == QLatin1String("deformbrush")
        || paintOpId == QLatin1String("filter")
        || paintOpId == QLatin1String("duplicate");
}

/**
 * Wait (keeping the window alive) until the image has finished all its
 * work, at most @p timeoutMs. Unlike KisImage::waitForDone() this never
 * hangs when some tool keeps a stroke open: it just returns false.
 */
bool waitIdle(KisImageSP image, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!image->isIdle()) {
        if (timer.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
        QThread::msleep(2);
    }
    return true;
}

bool closeTimes(const QTime &a, const QTime &b, int toleranceMs)
{
    if (!a.isValid() || !b.isValid()) return false;
    return qAbs(a.msecsTo(b)) <= toleranceMs;
}

} // namespace

GigaLastStrokeTransform::GigaLastStrokeTransform(QObject *parent)
    : QObject(parent)
{
    // The recording holds the image alive; drop it when documents close.
    connect(KisPart::instance(), SIGNAL(sigDocumentRemoved(QString)), this, SLOT(slotDocumentRemoved()));
}

GigaLastStrokeTransform::~GigaLastStrokeTransform()
{
    GigaLastStroke::clearLast();
}

void GigaLastStrokeTransform::slotDocumentRemoved()
{
    if (m_points) {
        // The document of a Points session closed: just forget the session.
        KisImageSP sessionImage = m_image;
        bool open = false;
        Q_FOREACH (QPointer<KisDocument> d, KisPart::instance()->documents()) {
            if (d && sessionImage && d->image().data() == sessionImage.data()) open = true;
        }
        if (!open) {
            endPoints(false);
            endSession();
        }
    }
    GigaLastStrokeRecordSP rec = GigaLastStroke::last();
    if (!rec) return;
    KisImageSP image = rec->image;
    bool stillOpen = false;
    Q_FOREACH (QPointer<KisDocument> doc, KisPart::instance()->documents()) {
        if (doc && image && doc->image().data() == image.data()) stillOpen = true;
    }
    if (!stillOpen) GigaLastStroke::clearLast();
}

void GigaLastStrokeTransform::trigger(KisViewManager *view)
{
    if (!view) return;
    m_pendingView = view;
    tryTrigger(0);
}

void GigaLastStrokeTransform::tryTrigger(int attempt)
{
    KisViewManager *view = m_pendingView;
    if (!view) return;

    auto fail = [view](const QString &message) {
        view->showFloatingMessage(message, QIcon(), 3500, KisFloatingMessage::Medium);
    };

    if (m_active) {
        // Pressed again while open: switch between Pixels and Points.
        if (m_points) {
            pointsSwitchToPixels();
            return;
        }
        KisTool *transform = dynamic_cast<KisTool*>(
            KoToolManager::instance()->toolById(view->canvasBase(), QStringLiteral("KisToolTransform")));
        if (!transform || m_reopenInPoints) {
            fail(i18n("Already transforming the last stroke. Apply or cancel it first."));
            return;
        }
        // The warp can't be turned into dots: drop it, reopen with dots.
        m_reopenInPoints = true;
        transform->requestStrokeCancellation();
        return;
    }
    // The waits below keep the window responsive, so the shortcut could be
    // pressed again meanwhile: ignore that.
    if (m_preparing) return;
    struct PreparingFlag {
        bool &flag;
        explicit PreparingFlag(bool &f) : flag(f) { flag = true; }
        ~PreparingFlag() { flag = false; }
    };

    // A Smooth Curve catch-up tail may still be landing right after the pen
    // lifted: wait for that stroke to finish (up to ~2 seconds).
    if (GigaLastStroke::isRecording()) {
        if (attempt < 20) {
            QTimer::singleShot(100, this, [this, attempt]() { tryTrigger(attempt + 1); });
        } else {
            fail(i18n("Finish the stroke first."));
        }
        return;
    }

    PreparingFlag preparing(m_preparing);

    KisImageSP image = view->image();
    KisDocument *doc = view->document();
    GigaLastStrokeRecordSP rec = GigaLastStroke::last();
    if (!image || !doc || !rec || rec->image.data() != image.data()) {
        fail(i18n("Transform Last Stroke: draw a stroke first."));
        return;
    }
    if (rec->tooLong || rec->jobs.isEmpty()) {
        fail(i18n("Transform Last Stroke: that stroke is too long to transform."));
        return;
    }

    // Everything painted so far (and its undo entry) must be finished.
    if (!waitIdle(image, 2000)) {
        fail(i18n("Transform Last Stroke: the image is busy. Finish what the current tool is doing first."));
        return;
    }

    KisPaintLayer *layer = dynamic_cast<KisPaintLayer*>(rec->node.data());
    if (!layer || !layer->parent() || layer->image().data() != image.data()) {
        fail(i18n("Transform Last Stroke: the stroke's layer is gone."));
        return;
    }
    if (!layer->isEditable(true) || !layer->visible(true)) {
        fail(i18n("Transform Last Stroke: the layer is locked or hidden."));
        return;
    }
    if (layer->alphaLocked()) {
        fail(i18n("Transform Last Stroke: turn off alpha lock on the layer first."));
        return;
    }
    if (layer->paintDevice()->keyframeChannel()) {
        fail(i18n("Transform Last Stroke doesn't work on animated layers."));
        return;
    }

    KisResourcesSnapshotSP res = rec->resources;
    KisPaintOpPresetSP preset = res ? res->currentPaintOpPreset() : KisPaintOpPresetSP();
    if (!res || !preset) {
        fail(i18n("Transform Last Stroke: the stroke can't be repainted."));
        return;
    }
    if (res->compositeOpId() != COMPOSITE_OVER) {
        fail(i18n("Transform Last Stroke only works with Normal blending (not erasers or other blend modes)."));
        return;
    }
    if (readsCanvas(preset->paintOp().id())) {
        fail(i18n("Transform Last Stroke doesn't work with smudge, deform, filter or clone brushes."));
        return;
    }

    // The newest undo entry must be this very stroke: same name, same time.
    KUndo2Stack *stack = doc->undoStack();
    const int index = stack->index();
    const KUndo2Command *top = index > 0 ? stack->command(index - 1) : nullptr;
    const bool isOriginal = top && top->text().toString() == rec->name.toString()
            && closeTimes(top->time(), rec->startTime, 1500)
            && closeTimes(top->endTime(), rec->endTime, 5000);
    const bool isReplacement = top && rec->replacementCommand == static_cast<const void *>(top)
            && rec->replacementIndex == index && top->text().toString() == rec->name.toString();
    if (!isOriginal && !isReplacement) {
        fail(i18n("Transform Last Stroke: something else was done after the stroke."));
        return;
    }

    if (editMode() == PointsMode) {
        startPoints(view, image, doc, layer, rec);
        return;
    }

    KisTool *tool = dynamic_cast<KisTool*>(
        KoToolManager::instance()->toolById(view->canvasBase(), QStringLiteral("KisToolTransform")));
    if (!tool) {
        fail(i18n("Transform Last Stroke: the Transform tool is missing."));
        return;
    }

    // 1. The stroke on its own.
    KisPaintDeviceSP strokeOnly = renderStrokeOnly(rec, image, layer);
    if (!strokeOnly || strokeOnly->exactBounds().isEmpty()) {
        fail(i18n("Transform Last Stroke: the stroke is empty."));
        return;
    }

    // 2. A copy of the layer as drawn. Krita's tiles are copy-on-write, so
    //    this costs almost nothing until the layer changes.
    m_withStroke = new KisPaintDevice(*layer->paintDevice());

    // 3. The Transform tool places the image on every *selected* layer, so
    //    make the stroke's layer the active and only selected one.
    if (view->activeNode() != KisNodeSP(layer)) {
        view->nodeManager()->slotNonUiActivatedNode(layer);
    }
    view->nodeManager()->slotSetSelectedNodes(KisNodeList() << KisNodeSP(layer));

    // 4. Lift the stroke: undo it, so whatever it covered shows again.
    stack->undo();
    waitIdle(image, 5000);
    if (stack->index() != index - 1) {
        m_withStroke.clear();
        fail(i18n("Transform Last Stroke: couldn't lift the stroke (the image is busy)."));
        return;
    }

    m_active = true;
    m_doc = doc;
    m_image = image;
    m_layer = layer;
    m_record = rec;
    m_indexAfterLift = stack->index();

    openTransformTool(view, strokeOnly);
}

void GigaLastStrokeTransform::openTransformTool(KisViewManager *view, KisPaintDeviceSP strokeOnly)
{
    KisTool *tool = view ? dynamic_cast<KisTool*>(
        KoToolManager::instance()->toolById(view->canvasBase(), QStringLiteral("KisToolTransform"))) : nullptr;
    if (!tool) return;

    // Queued: the tool emits this from inside its own end/cancel code; doing
    // undo/redo work there would re-enter it.
    connect(tool, SIGNAL(gigaExternalSourceStrokeFinished(bool,int)),
            this, SLOT(slotTransformFinished(bool,int)),
            Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection));

    const int mode = KisConfig(true).readEntry<int>(ModeKey, WarpMode);
    QMetaObject::invokeMethod(tool, "gigaSetNextActivationMode", Q_ARG(int, mode));

    // 5. Same route as Edit > Paste and Transform.
    tool->newActivationWithExternalSource(strokeOnly);
}

KisPaintDeviceSP GigaLastStrokeTransform::renderStrokeOnly(GigaLastStrokeRecordSP rec, KisImageSP image, KisNodeSP layer)
{
    // A layer that is never added to the image: nothing shows on screen and
    // its painting has no undo entry (gigaSetNoUndo).
    KisPaintLayerSP hidden = new KisPaintLayer(image, QStringLiteral("giga-last-stroke"),
                                               OPACITY_OPAQUE_U8, layer->paintDevice()->colorSpace());

    KisResourcesSnapshotSP res = rec->resources;
    res->setCurrentNode(hidden);
    res->gigaSetNoUndo(true);
    res->setSelectionOverride(rec->selection); // clip like the original stroke

    QVector<KisFreehandStrokeInfo*> infos;
    Q_FOREACH (const KisDistanceInformation &dist, rec->startDistances) {
        infos << new KisFreehandStrokeInfo(dist);
    }
    if (infos.isEmpty()) {
        infos << new KisFreehandStrokeInfo(KisDistanceInformation());
    }

    FreehandStrokeStrategy *strategy = new FreehandStrokeStrategy(res, infos, rec->name, FreehandStrokeStrategy::None);
    strategy->gigaSetNoLod(true);
    strategy->gigaSetRandomSource(rec->randomSource);

    KisStrokeId id = image->startStroke(strategy);
    Q_FOREACH (const GigaLastStrokeRecord::Job &job, rec->jobs) {
        if (job.strokeInfoId < 0 || job.strokeInfoId >= infos.size()) continue;
        switch (job.type) {
        case GigaLastStrokeRecord::Point:
            image->addJob(id, new FreehandStrokeStrategy::Data(job.strokeInfoId, job.pi1));
            break;
        case GigaLastStrokeRecord::Line:
            image->addJob(id, new FreehandStrokeStrategy::Data(job.strokeInfoId, job.pi1, job.pi2));
            break;
        case GigaLastStrokeRecord::Curve:
            image->addJob(id, new FreehandStrokeStrategy::Data(job.strokeInfoId, job.pi1,
                                                               job.control1, job.control2, job.pi2));
            break;
        }
    }
    // Same final update as a normal stroke end (renders any queued dabs).
    image->addJob(id, new KisAsynchronousStrokeUpdateHelper::UpdateData(true));
    image->endStroke(id);
    const bool done = waitIdle(image, 20000);

    // The snapshot belongs to the record; point it back at the real layer.
    res->setCurrentNode(layer);

    return done ? hidden->paintDevice() : KisPaintDeviceSP();
}

void GigaLastStrokeTransform::slotTransformFinished(bool applied, int mode)
{
    if (!m_active) return;

    KisConfig(false).writeEntry<int>(ModeKey, mode);

    KisImageSP image = m_image;
    KisDocument *doc = m_doc;
    if (!image || !doc || !m_layer || !m_layer->parent()) {
        endSession();
        return;
    }

    KUndo2Stack *stack = doc->undoStack();
    if (!waitIdle(image, 5000)) {
        // Some tool started new work right away; leave the history as the
        // Transform tool made it (the stroke stays where it was placed).
        GigaLastStroke::clearLast();
        endSession();
        return;
    }

    if (applied && stack->index() == m_indexAfterLift + 1) {
        // The Transform tool added its entry. Take the result, step back to
        // the lifted state, and write the history as:
        //   <stroke as drawn>  then  "Transform Last Stroke".
        KisPaintDeviceSP result = new KisPaintDevice(*m_layer->paintDevice());
        stack->undo();
        waitIdle(image, 5000);

        if (stack->index() == m_indexAfterLift) {
            KisPaintDeviceSP lifted = m_layer->paintDevice();
            const QRect changed = differingRect(m_withStroke, result);
            pushPixels(image, m_layer, m_withStroke, differingRect(lifted, m_withStroke), m_record->name);
            if (changed.isEmpty()) {
                // Nothing was changed: the stroke is simply back, and can be
                // transformed again.
                rememberReplacement(image, stack);
            } else {
                pushPixels(image, m_layer, result, changed, kundo2_i18n("Transform Last Stroke"));
                GigaLastStroke::clearLast();
            }
        } else {
            // Couldn't step back (image busy): keep the tool's own entry.
            stack->redo();
            GigaLastStroke::clearLast();
        }
    } else if (!applied || stack->index() == m_indexAfterLift) {
        // Cancelled (or nothing was placed): bring the stroke back. Starting
        // the transform emptied the redo list (Krita always does: the old
        // redo data can't survive new edits of the layer), so the stroke is
        // written back from the saved copy as a new entry of the same name.
        pushPixels(image, m_layer, m_withStroke,
                   differingRect(m_layer->paintDevice(), m_withStroke), m_record->name);
        rememberReplacement(image, stack);
    } else {
        // Something unexpected happened to the history; leave it alone.
        GigaLastStroke::clearLast();
    }

    endSession();

    if (m_reopenInPoints) {
        // Pixels -> Points: the stroke is back as drawn; open it with dots.
        m_reopenInPoints = false;
        setEditMode(PointsMode);
        QTimer::singleShot(0, this, [this]() { tryTrigger(0); });
    }
}

void GigaLastStrokeTransform::pushPixels(KisImageSP image, KisNodeSP layer, KisPaintDeviceSP source,
                                         const QRect &rect, const KUndo2MagicString &name)
{
    if (rect.isEmpty() || !source) return;
    KisProcessingApplicator::runSingleCommandStroke(
        image, new GigaCopyPixelsCommand(layer->paintDevice(), source, rect, name),
        KisStrokeJobData::BARRIER, KisStrokeJobData::EXCLUSIVE);
}

void GigaLastStrokeTransform::rememberReplacement(KisImageSP image, KUndo2Stack *stack)
{
    // The entry just pushed now stands for the stroke (same pixels, same
    // name), so Transform Last Stroke keeps working on it.
    if (!waitIdle(image, 5000) || stack->index() <= 0 || !m_record) {
        GigaLastStroke::clearLast();
        return;
    }
    m_record->replacementCommand = stack->command(stack->index() - 1);
    m_record->replacementIndex = stack->index();
}

void GigaLastStrokeTransform::endSession()
{
    m_active = false;
    m_doc.clear();
    m_image = KisImageWSP();
    m_layer.clear();
    m_record.clear();
    m_withStroke.clear();
    m_indexAfterLift = -1;
    m_finishing = false;
    m_view.clear();
}

QRect GigaLastStrokeTransform::differingRect(KisPaintDeviceSP a, KisPaintDeviceSP b)
{
    if (!a || !b || a->pixelSize() != b->pixelSize()) return QRect();

    const QRect area = a->extent() | b->extent();
    if (area.isEmpty()) return QRect();

    const int pixelSize = a->pixelSize();
    KisRandomConstAccessorSP ia = a->createRandomConstAccessorNG();
    KisRandomConstAccessorSP ib = b->createRandomConstAccessorNG();

    QRect result;
    for (int y = area.top(); y <= area.bottom();) {
        const int rows = qMin(qMin(ia->numContiguousRows(y), ib->numContiguousRows(y)), area.bottom() - y + 1);
        for (int x = area.left(); x <= area.right();) {
            const int cols = qMin(qMin(ia->numContiguousColumns(x), ib->numContiguousColumns(x)), area.right() - x + 1);
            ia->moveTo(x, y);
            ib->moveTo(x, y);
            const quint8 *pa = ia->rawDataConst();
            const quint8 *pb = ib->rawDataConst();
            const int strideA = ia->rowStride(x, y);
            const int strideB = ib->rowStride(x, y);
            if (pa != pb) { // shared (copy-on-write) tiles are equal for sure
                for (int r = 0; r < rows; r++) {
                    if (std::memcmp(pa + r * strideA, pb + r * strideB, size_t(cols) * pixelSize) != 0) {
                        result |= QRect(x, y, cols, rows);
                        break;
                    }
                }
            }
            x += cols;
        }
        y += rows;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Points mode (feature/stroke-points)
// ---------------------------------------------------------------------------

GigaLastStrokeTransform::EditMode GigaLastStrokeTransform::editMode()
{
    return KisConfig(true).readEntry<int>(EditModeKey, PixelsMode) == PointsMode ? PointsMode : PixelsMode;
}

void GigaLastStrokeTransform::setEditMode(EditMode mode)
{
    KisConfig(false).writeEntry<int>(EditModeKey, int(mode));
}

QString GigaLastStrokeTransform::pointsToolId()
{
    return QLatin1String(PointsToolId);
}

GigaLastStrokeTransform *GigaLastStrokeTransform::pointsSession()
{
    return s_pointsSession;
}

int GigaLastStrokeTransform::pointSpacing()
{
    return qBound(10, KisConfig(true).readEntry<int>(PointSpacingKey, 40), 200);
}

void GigaLastStrokeTransform::setPointSpacing(int screenPixels)
{
    KisConfig(false).writeEntry<int>(PointSpacingKey, qBound(10, screenPixels, 200));
}

bool GigaLastStrokeTransform::interceptUndo(KisDocument *doc, bool redo)
{
    GigaLastStrokeTransform *session = s_pointsSession;
    if (!session || !doc || session->m_doc.data() != doc || session->m_finishing) return false;
    session->pointsUndo(redo);
    return true;
}

bool GigaLastStrokeTransform::startPoints(KisViewManager *view, KisImageSP image, KisDocument *doc,
                                          KisPaintLayer *layer, GigaLastStrokeRecordSP rec)
{
    auto fail = [view](const QString &message) {
        view->showFloatingMessage(message, QIcon(), 3500, KisFloatingMessage::Medium);
        return false;
    };

    GigaStrokePath path;
    QString error;
    if (!path.build(*rec, &error)) return fail(error);
    if (!KoToolManager::instance()->toolById(view->canvasBase(), pointsToolId())) {
        return fail(i18n("Edit Stroke Points: the points tool is missing."));
    }

    KUndo2Stack *stack = doc->undoStack();
    const int index = stack->index();

    // The layer as drawn, then lift the stroke to learn what it covered.
    m_withStroke = new KisPaintDevice(*layer->paintDevice());
    if (view->activeNode() != KisNodeSP(layer)) {
        view->nodeManager()->slotNonUiActivatedNode(layer);
    }
    stack->undo();
    waitIdle(image, 5000);
    if (stack->index() != index - 1) {
        m_withStroke.clear();
        return fail(i18n("Transform Last Stroke: couldn't lift the stroke (the image is busy)."));
    }
    m_lifted = new KisPaintDevice(*layer->paintDevice());
    m_drawnRect = differingRect(m_lifted, m_withStroke);
    m_previewRect = m_drawnRect;

    // Show the stroke again right away (no undo entry): while editing, the
    // layer is "lifted + stroke", and the history ends at the lifted state.
    {
        KisPaintDeviceSP target = layer->paintDevice();
        KisPaintDeviceSP drawn = m_withStroke;
        KisNodeSP node = layer;
        const QRect rect = m_drawnRect;
        runNoUndo(image, [target, drawn, node, rect]() mutable {
            if (rect.isEmpty()) return;
            KisPainter::copyAreaOptimized(rect.topLeft(), drawn, target, rect);
            node->setDirty(rect);
        });
    }

    m_active = true;
    m_points = true;
    m_doc = doc;
    m_image = image;
    m_layer = layer;
    m_record = rec;
    m_view = view;
    m_indexAfterLift = stack->index();
    m_path = path;
    m_lastRender.clear();
    m_renderBusy = false;
    m_renderDirty = false;
    m_editSerial = 0;
    m_renderedSerial = -1;
    m_undoShapes.clear();
    m_redoShapes.clear();
    s_pointsSession = this;

    // The points tool places the dots (it knows the zoom) when it opens.
    m_previousToolId = KoToolManager::instance()->activeToolId();
    if (m_previousToolId == pointsToolId()) m_previousToolId.clear();
    KoToolManager::instance()->switchToolRequested(pointsToolId());
    return true;
}

void GigaLastStrokeTransform::pointsBeginEdit()
{
    if (!m_points) return;
    m_undoShapes.push(m_path.vertices());
    m_redoShapes.clear();
}

void GigaLastStrokeTransform::pointsChanged()
{
    if (!m_points) return;
    ++m_editSerial;
    startPreviewRender();
    Q_EMIT sigPointsChanged();
}

void GigaLastStrokeTransform::pointsUndo(bool redo)
{
    if (!m_points) return;
    if (!redo) {
        if (m_undoShapes.isEmpty()) {
            // Nothing left to step back: Ctrl+Z puts the stroke back as drawn.
            pointsFinish(false, true);
            return;
        }
        m_redoShapes.push(m_path.vertices());
        m_path.setVertices(m_undoShapes.pop());
    } else {
        if (m_redoShapes.isEmpty()) return;
        m_undoShapes.push(m_path.vertices());
        m_path.setVertices(m_redoShapes.pop());
    }
    pointsChanged();
}

void GigaLastStrokeTransform::startPreviewRender()
{
    KisImageSP image = m_image;
    if (!m_points || !image || !m_layer || !m_record) return;
    if (m_renderBusy) {
        // One repaint at a time: the newest shape is painted right after.
        m_renderDirty = true;
        return;
    }
    m_renderBusy = true;
    m_renderDirty = false;
    const int serial = m_editSerial;

    // 1. Repaint the bent stroke on a layer that is not in the image (no
    //    undo entry, nothing on screen), like renderStrokeOnly() but without
    //    waiting for it.
    KisPaintLayerSP hidden = new KisPaintLayer(image, QStringLiteral("giga-stroke-points"),
                                               OPACITY_OPAQUE_U8, m_layer->paintDevice()->colorSpace());
    KisResourcesSnapshotSP res = m_record->resources;
    // Only one repaint is in flight, so pointing the shared snapshot at
    // this repaint's layer is safe; endPoints() points it back.
    res->setCurrentNode(hidden);
    res->gigaSetNoUndo(true);
    res->setSelectionOverride(m_record->selection);

    QVector<KisFreehandStrokeInfo*> infos;
    Q_FOREACH (const KisDistanceInformation &dist, m_record->startDistances) {
        infos << new KisFreehandStrokeInfo(dist);
    }
    if (infos.isEmpty()) infos << new KisFreehandStrokeInfo(KisDistanceInformation());

    FreehandStrokeStrategy *strategy = new FreehandStrokeStrategy(res, infos, m_record->name, FreehandStrokeStrategy::None);
    strategy->gigaSetNoLod(true);
    strategy->gigaSetRandomSource(m_record->randomSource);
    m_startingRepaint = true;
    KisStrokeId id = image->startStroke(strategy);
    m_startingRepaint = false;
    const QVector<GigaLastStrokeRecord::Job> jobs = m_path.bentJobs();
    for (const GigaLastStrokeRecord::Job &job : jobs) {
        if (job.strokeInfoId < 0 || job.strokeInfoId >= infos.size()) continue;
        if (job.type == GigaLastStrokeRecord::Point) {
            image->addJob(id, new FreehandStrokeStrategy::Data(job.strokeInfoId, job.pi1));
        } else {
            image->addJob(id, new FreehandStrokeStrategy::Data(job.strokeInfoId, job.pi1, job.pi2));
        }
    }
    image->addJob(id, new KisAsynchronousStrokeUpdateHelper::UpdateData(true));
    image->endStroke(id);

    // 2. Right after it (strokes run in order): put the layer back to
    //    "lifted" where the last preview was, draw the new stroke on top.
    KisPaintDeviceSP strokeDevice = hidden->paintDevice();
    KisPaintDeviceSP target = m_layer->paintDevice();
    KisPaintDeviceSP lifted = m_lifted;
    KisNodeSP node = m_layer;
    const QRect previous = m_previewRect;
    QPointer<GigaLastStrokeTransform> self(this);
    runNoUndo(image, [=]() mutable {
        const QRect strokeRect = strokeDevice->exactBounds();
        if (!previous.isEmpty()) {
            KisPainter::copyAreaOptimized(previous.topLeft(), lifted, target, previous);
        }
        if (!strokeRect.isEmpty()) {
            KisPainter gc(target);
            gc.setCompositeOpId(COMPOSITE_OVER);
            gc.bitBlt(strokeRect.topLeft(), strokeDevice, strokeRect);
        }
        node->setDirty(previous | strokeRect);
        // Back to the GUI thread (queued: this runs on a worker thread).
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, serial, strokeRect, strokeDevice]() {
            if (self) self->previewRendered(serial, strokeRect, strokeDevice);
        }, Qt::QueuedConnection);
    });
}

void GigaLastStrokeTransform::previewRendered(int serial, const QRect &rect, KisPaintDeviceSP strokeDevice)
{
    if (!m_points) return;
    m_renderBusy = false;
    m_previewRect = rect;
    m_lastRender = strokeDevice;
    m_renderedSerial = serial;
    // While finishing, only waitPreview() decides whether to paint again.
    if (!m_finishing && (m_renderDirty || m_renderedSerial != m_editSerial)) {
        m_renderDirty = false;
        startPreviewRender();
    }
    Q_EMIT sigPointsChanged();
}

bool GigaLastStrokeTransform::waitPreview(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (m_points) {
        if (!m_renderBusy && !m_renderDirty && m_renderedSerial == m_editSerial && m_lastRender) return true;
        if (!m_renderBusy) startPreviewRender();
        if (timer.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
        QThread::msleep(2);
    }
    return false;
}

void GigaLastStrokeTransform::restoreLifted()
{
    KisImageSP image = m_image;
    if (!image || !m_layer || !m_lifted) return;
    // A repaint still on its way would draw after this: let it land first
    // (m_previewRect then covers it).
    m_renderDirty = false;
    QElapsedTimer timer;
    timer.start();
    while (m_renderBusy && timer.elapsed() < 20000) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
        QThread::msleep(2);
    }
    KisPaintDeviceSP target = m_layer->paintDevice();
    KisPaintDeviceSP lifted = m_lifted;
    KisNodeSP node = m_layer;
    const QRect rect = m_previewRect;
    runNoUndo(image, [target, lifted, node, rect]() mutable {
        if (rect.isEmpty()) return;
        KisPainter::copyAreaOptimized(rect.topLeft(), lifted, target, rect);
        node->setDirty(rect);
    });
    m_previewRect = QRect();
    waitIdle(image, 5000);
}

void GigaLastStrokeTransform::pointsFinish(bool apply, bool switchBack)
{
    if (!m_points || m_finishing) return;
    m_finishing = true;

    KisImageSP image = m_image;
    KisDocument *doc = m_doc;
    if (!image || !doc || !m_layer || !m_layer->parent() || !m_record) {
        endPoints(switchBack);
        endSession();
        return;
    }

    const bool edited = apply && m_path.isEdited();
    if (edited) waitPreview(20000);
    waitIdle(image, 5000);

    KUndo2Stack *stack = doc->undoStack();
    if (stack->index() != m_indexAfterLift) {
        // Something else changed the history meanwhile: leave the pixels as
        // they are rather than undo someone else's work.
        GigaLastStroke::clearLast();
        endPoints(switchBack);
        endSession();
        return;
    }

    KisPaintDeviceSP result = edited ? new KisPaintDevice(*m_layer->paintDevice()) : KisPaintDeviceSP();
    const KUndo2MagicString name = m_record->name;

    // Back to the lifted state (no undo), then write the history:
    //   <stroke as drawn>  then (if changed) "Edit Stroke Points".
    restoreLifted();
    pushPixels(image, m_layer, m_withStroke, m_drawnRect, name);
    const QRect changed = result ? differingRect(m_withStroke, result) : QRect();
    if (!changed.isEmpty()) {
        pushPixels(image, m_layer, result, changed, kundo2_i18n("Edit Stroke Points"));
        waitIdle(image, 5000);
        GigaLastStroke::clearLast();
    } else {
        // Same pixels as drawn: it stays "the last stroke", can be opened again.
        rememberReplacement(image, stack);
    }

    endPoints(switchBack);
    endSession();
}

void GigaLastStrokeTransform::pointsSwitchToPixels()
{
    if (!m_points || m_finishing) return;
    KisViewManager *view = m_view;
    KisImageSP image = m_image;
    if (!view || !image || !m_layer) return;
    m_finishing = true;

    // The stroke as it is now, on its own (the dots' shape, repainted).
    if (!waitPreview(20000) || !m_lastRender) {
        m_finishing = false;
        view->showFloatingMessage(i18n("Edit Stroke Points: still repainting, try again."), QIcon(), 2500,
                                  KisFloatingMessage::Medium);
        return;
    }
    KisPaintDeviceSP strokeOnly = m_lastRender;
    restoreLifted();
    if (m_doc && m_doc->undoStack()->index() != m_indexAfterLift) {
        m_finishing = false;
        pointsFinish(false, true);
        return;
    }

    // Same session from here on, now in the Transform tool. The points
    // tool is left without a session, so its deactivation does nothing.
    endPoints(false);
    m_finishing = false;
    setEditMode(PixelsMode);
    // The Transform tool places the image on every selected layer.
    view->nodeManager()->slotSetSelectedNodes(KisNodeList() << KisNodeSP(m_layer));
    openTransformTool(view, strokeOnly);
}

void GigaLastStrokeTransform::endPoints(bool switchBack)
{
    if (!m_points) return;
    if (m_record && m_record->resources && m_layer) {
        m_record->resources->setCurrentNode(m_layer);
    }
    m_points = false;
    if (s_pointsSession == this) s_pointsSession = nullptr;
    m_path = GigaStrokePath();
    m_lifted.clear();
    m_lastRender.clear();
    m_previewRect = QRect();
    m_drawnRect = QRect();
    m_renderBusy = false;
    m_renderDirty = false;
    m_undoShapes.clear();
    m_redoShapes.clear();
    Q_EMIT sigPointsEnded();

    if (switchBack && KoToolManager::instance()->activeToolId() == pointsToolId()) {
        const QString back = m_previousToolId.isEmpty() ? QStringLiteral("KritaShape/KisToolBrush") : m_previousToolId;
        KoToolManager::instance()->switchToolRequested(back);
    }
    m_previousToolId.clear();
}

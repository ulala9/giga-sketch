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

namespace {

const char *ModeKey = "gigaLastStrokeTransformMode";
const int WarpMode = 1; // KisToolTransform::WarpTransformMode

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
        fail(i18n("Already transforming the last stroke. Apply or cancel it first."));
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

/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */

// Giga Sketch: a reference image set (.krf) keeps its sticky notes, and the
// pictures next to them still load.

#include <simpletest.h>
#include <kistest.h>

#include <QBuffer>
#include <QImage>

#include <GigaNote.h>
#include <KisReferenceImage.h>

#include "../referenceimagestool/KisReferenceImageCollection.h"

class TestGigaNoteCollection : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testRoundTrip();
};

void TestGigaNoteCollection::testRoundTrip()
{
    QImage pixels(8, 8, QImage::Format_ARGB32);
    pixels.fill(Qt::red);
    KisReferenceImage *picture = new KisReferenceImage();
    picture->setImage(pixels);
    picture->setSize(QSizeF(8, 8));

    GigaNote *note = GigaNote::create(QPointF(20, 30), 0.5, "Remember the light");
    GigaNote::State state = note->state();
    state.color = QColor(189, 232, 165);
    state.textSize = GigaNote::Small;
    state.keepReadable = true;
    note->setState(state);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    KisReferenceImageCollection saved({picture, note});
    QVERIFY(saved.save(&buffer));
    buffer.close();

    buffer.open(QIODevice::ReadOnly);
    KisReferenceImageCollection loaded;
    QVERIFY(loaded.load(&buffer));
    QCOMPARE(loaded.referenceImages().size(), 2);

    KisReferenceImage *first = loaded.referenceImages()[0];
    KisReferenceImage *second = loaded.referenceImages()[1];
    QVERIFY(!first->isNote());
    QCOMPARE(first->getImage().size(), QSize(8, 8));
    QVERIFY(second->isNote());
    GigaNote *loadedNote = static_cast<GigaNote *>(second);
    QCOMPARE(loadedNote->plainText(), QString("Remember the light"));
    QCOMPARE(loadedNote->color(), QColor(189, 232, 165));
    QCOMPARE(loadedNote->textSize(), int(GigaNote::Small));
    QVERIFY(loadedNote->keepReadable());
    QVERIFY(qAbs(loadedNote->position().x() - 20.0) < 1e-6);

    qDeleteAll(loaded.referenceImages());
    delete picture;
    delete note;
}

#include "TestGigaNoteCollection.moc"

KISTEST_MAIN(TestGigaNoteCollection)

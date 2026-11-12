/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaSaveDated.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>

#include <klocalizedstring.h>

#include "KisDocument.h"
#include "KisMainWindow.h"
#include "KisView.h"
#include "KisViewManager.h"
#include "kis_config.h"
#include "kis_properties_configuration.h"

namespace
{
const char *const FOLDER_KEY = "gigaSaveDatedFolder"; ///< for never-saved images
const int JPG_QUALITY = 90;                           ///< same as the old extension
}

QString GigaSaveDated::nextDatedPath(const QString &folder, const QDate &date, const QString &extension)
{
    const QDir dir(folder);
    const QString base = date.toString(QStringLiteral("yyyy-MM-dd"));
    QString path = dir.filePath(base + QLatin1Char('.') + extension);
    for (int version = 2; QFileInfo::exists(path); version++) {
        path = dir.filePath(QStringLiteral("%1_v%2.%3").arg(base).arg(version).arg(extension));
    }
    return path;
}

void GigaSaveDated::saveDatedJpg(KisMainWindow *window)
{
    KisView *view = window ? window->activeView() : nullptr;
    KisDocument *document = view ? view->document() : nullptr;
    if (!document) return;
    KisViewManager *viewManager = window->viewManager();
    auto tell = [viewManager](const QString &message) {
        if (viewManager) viewManager->showFloatingMessage(message, QIcon(), 2500, KisFloatingMessage::Medium);
    };

    // Next to the .kra; a never-saved image asks once and remembers.
    QString folder;
    if (!document->path().isEmpty()) {
        folder = QFileInfo(document->path()).absolutePath();
    } else {
        KisConfig cfg(true);
        const QString remembered = cfg.readEntry<QString>(FOLDER_KEY, QString());
        if (!remembered.isEmpty() && QDir(remembered).exists()) {
            folder = remembered;
        } else {
            folder = QFileDialog::getExistingDirectory(window, i18nc("@title:window", "Folder for Dated JPGs"),
                                                       QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
            if (folder.isEmpty()) return;
            KisConfig(false).writeEntry<QString>(FOLDER_KEY, folder);
        }
    }
    if (!QFileInfo(folder).isWritable()) {
        tell(i18nc("floating message", "Can't save the dated JPG: the folder is not writable"));
        return;
    }

    const QString path = nextDatedPath(folder, QDate::currentDate());
    // Your last-used JPEG export settings, at quality 90.
    KisPropertiesConfigurationSP config = KisConfig(true).exportConfiguration(QStringLiteral("image/jpeg"));
    if (!config) config = new KisPropertiesConfiguration();
    config->setProperty("quality", JPG_QUALITY);

    // Krita's own export (in the background, like File > Export); it
    // flattens the image and leaves the document untouched. "Batch mode"
    // skips Krita's JPEG options dialog; it is only consulted while the
    // export starts, so it's put back straight away (errors still show).
    const bool wasBatch = document->fileBatchMode();
    document->setFileBatchMode(true);
    const bool started = document->exportDocument(path, QByteArrayLiteral("image/jpeg"), false, false, config);
    document->setFileBatchMode(wasBatch);
    if (started) {
        tell(i18nc("floating message, %1 is a file name", "Saving %1", QFileInfo(path).fileName()));
    } else {
        tell(i18nc("floating message", "Couldn't save the dated JPG"));
    }
}

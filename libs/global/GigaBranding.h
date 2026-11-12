/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_BRANDING_H
#define GIGA_BRANDING_H

#include <QString>

/**
 * Giga Sketch identity strings, all kept in one place.
 *
 * Giga Sketch is a fork of Krita. To make sure it never reads or overwrites
 * the settings of a stock Krita installed on the same machine, every
 * user-visible location (config files, data folder, logs, the
 * single-instance socket, the Wayland app ID) uses the names below instead
 * of "krita".
 *
 * Read-only data that Giga Sketch installs (brushes, palettes, menus...)
 * still lives in <prefix>/share/krita, so the build files stay identical to
 * upstream. Only the *per-user* locations are renamed.
 *
 * Everything here is an inline function, so this header needs no .cpp file
 * and no export macro. Any library can include it.
 */
namespace GigaBranding
{

/// Internal app name. Qt builds the user paths from it:
///   ~/.config/gigasketchrc, ~/.local/share/gigasketch/, ~/.cache/gigasketch/
inline QString appName() { return QStringLiteral("gigasketch"); }

/// Name shown to the user in the title bar, about box, etc.
inline QString displayName() { return QStringLiteral("Giga Sketch"); }

/// Reverse-domain ID. Used as the Wayland app ID and the .desktop file name.
inline QString desktopFileName() { return QStringLiteral("io.github.ulala9.gigasketch"); }

inline QString homepage() { return QStringLiteral("https://github.com/ulala9/giga-sketch"); }
/// Reversed, this gives the "io.github.ulala9" prefix of desktopFileName().
inline QString organizationDomain() { return QStringLiteral("ulala9.github.io"); }

/// Prefix of the local socket name used to send files to a running instance.
/// Must differ from Krita's ("Krita5"), or files opened in Giga Sketch
/// would be sent to a running Krita window instead.
inline QString singleInstanceKeyPrefix() { return QStringLiteral("GigaSketch6"); }

/// Main settings file. Normally Qt derives it from appName(); this is for
/// the few places that open it by name.
inline QString mainConfigFile() { return QStringLiteral("gigasketchrc"); }

/// Early display/OpenGL settings (Krita: kritadisplayrc).
inline QString displayConfigFile() { return QStringLiteral("gigasketchdisplayrc"); }

/// Custom keyboard shortcuts (Krita: kritashortcutsrc).
inline QString shortcutsConfigFile() { return QStringLiteral("gigasketchshortcutsrc"); }

/// Usage logs in ~/.local/share (Krita: krita.log, krita-sysinfo.log).
inline QString usageLogFile() { return QStringLiteral("gigasketch.log"); }
inline QString sysInfoLogFile() { return QStringLiteral("gigasketch-sysinfo.log"); }

/// Default UI theme (krita/data/themes/GigaDark.colors).
inline QString defaultTheme() { return QStringLiteral("Giga Dark"); }

/// Default keyboard shortcut scheme: Krita's bundled Photoshop-compatible
/// one (krita/data/shortcuts/photoshop_compatible.shortcuts).
inline QString defaultShortcutScheme() { return QStringLiteral("photoshop_compatible"); }

/// Default canvas input profile (pan/zoom/rotate keys and mouse buttons),
/// Krita's bundled Photoshop-compatible one.
inline QString defaultInputProfile() { return QStringLiteral("Photoshop Compatible"); }

/// True for both the stock Krita name and ours. For upstream code that
/// checks "are we running inside the real app (not a unit test)?".
inline bool isMainApplicationName(const QString &name)
{
    return name == QLatin1String("krita") || name == appName();
}

} // namespace GigaBranding

#endif // GIGA_BRANDING_H

/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_SELF_TEST_H
#define GIGA_SELF_TEST_H

class KisMainWindow;

/**
 * Giga Sketch: developer self-tests that drive the real UI.
 *
 * Does nothing unless the environment variable GIGA_SELFTEST is set, e.g.
 *     GIGA_SELFTEST=panels krita
 * (giga-tools/selftest.sh runs it in a throw-away config). A few seconds
 * after the first window opens, the named test runs and prints lines like
 *     GIGA_SELFTEST PASS onto-layers: tabbed with Layers
 *     GIGA_SELFTEST FAIL ...
 *     GIGA_SELFTEST DONE 0 failed
 * then quits with the number of failures as exit code.
 */
namespace GigaSelfTest
{
/// Call once per main window; only the first one is tested.
void maybeRun(KisMainWindow *window);
}

#endif // GIGA_SELF_TEST_H

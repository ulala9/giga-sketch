/*
 * SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef GIGATOOLLASSOFILLFACTORY_H
#define GIGATOOLLASSOFILLFACTORY_H

#include <KisToolPaintFactoryBase.h>

#include "KisToolEncloseAndFill.h"

// Giga Sketch: "Lasso Fill" — the Enclose and Fill tool preset as a one-stroke
// lasso fill: draw a loop, the whole inside fills on pen lift. Eraser mode (E)
// erases instead. Its own tool id, so its settings are saved apart from
// Enclose and Fill's.
class GigaToolLassoFillFactory : public KisToolPaintFactoryBase
{
public:
    GigaToolLassoFillFactory()
        : KisToolPaintFactoryBase("GigaToolLassoFill")
    {
        setToolTip(i18n("Lasso Fill Tool"));
        setSection(ToolBoxSection::Fill);
        setActivationShapeId(KRITA_TOOL_ACTIVATION_ID);
        setIconName(koIconNameCStr("giga_tool_lasso_fill"));
        setPriority(16);
    }

    KoToolBase *createTool(KoCanvasBase *canvas) override
    {
        return new KisToolEncloseAndFill(canvas, true);
    }
};

#endif

/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.12
import org.krita.flake.text 1.0
import org.krita.components 1.0 as Kis

// Giga Sketch: Photoshop-style paragraph spacing and indents.
// Space Before/After add a gap between paragraphs (lines separated by
// Enter). Indent Start/End push the text in from the left/right edges
// (start/end for right-to-left and vertical text).
TextPropertyBase {
    propertyTitle: i18nc("@title:group", "Paragraph Spacing");
    propertyName: "giga-paragraph-spacing";
    propertyType: TextPropertyConfigModel.Paragraph;
    toolTip: i18nc("@info:tooltip",
                   "Space before and after paragraphs, and indents from the start and end edges.");
    searchTerms: i18nc("comma separated search terms for the paragraph spacing property, matching is case-insensitive",
                       "space, before, after, paragraph, indent, left, right, margin");

    property alias spaceBefore: spaceBeforeSpn.dValue;
    property alias spaceAfter: spaceAfterSpn.dValue;
    property alias indentStart: indentStartSpn.dValue;
    property alias indentEnd: indentEndSpn.dValue;

    Connections {
        target: properties;
        function onGigaSpaceBeforeChanged() { updateValues(); updateVisibility(); }
        function onGigaSpaceAfterChanged() { updateValues(); updateVisibility(); }
        function onGigaIndentStartChanged() { updateValues(); updateVisibility(); }
        function onGigaIndentEndChanged() { updateValues(); updateVisibility(); }
        function onGigaSpaceBeforeStateChanged() { updateVisibility(); }
        function onGigaSpaceAfterStateChanged() { updateVisibility(); }
        function onGigaIndentStartStateChanged() { updateVisibility(); }
        function onGigaIndentEndStateChanged() { updateVisibility(); }
    }
    onPropertiesChanged: {
        updateValues();
        setVisibleFromProperty();
    }

    function updateValues() {
        blockSignals = true;
        if (!spaceBeforeSpn.isDragging) spaceBefore = properties.gigaSpaceBefore;
        if (!spaceAfterSpn.isDragging) spaceAfter = properties.gigaSpaceAfter;
        if (!indentStartSpn.isDragging) indentStart = properties.gigaIndentStart;
        if (!indentEndSpn.isDragging) indentEnd = properties.gigaIndentEnd;
        blockSignals = false;
    }
    function updateVisibility() {
        propertyState = [properties.gigaSpaceBeforeState, properties.gigaSpaceAfterState,
                         properties.gigaIndentStartState, properties.gigaIndentEndState];
        setVisibleFromProperty();
    }

    onSpaceBeforeChanged: if (!blockSignals) properties.gigaSpaceBefore = spaceBefore;
    onSpaceAfterChanged: if (!blockSignals) properties.gigaSpaceAfter = spaceAfter;
    onIndentStartChanged: if (!blockSignals) properties.gigaIndentStart = indentStart;
    onIndentEndChanged: if (!blockSignals) properties.gigaIndentEnd = indentEnd;

    onEnableProperty: {
        properties.gigaSpaceBeforeState = KoSvgTextPropertiesModel.PropertySet;
        properties.gigaSpaceAfterState = KoSvgTextPropertiesModel.PropertySet;
        properties.gigaIndentStartState = KoSvgTextPropertiesModel.PropertySet;
        properties.gigaIndentEndState = KoSvgTextPropertiesModel.PropertySet;
    }

    GridLayout {
        columns: 2;
        columnSpacing: columnSpacing;
        width: parent.width;

        RevertPropertyButton {
            revertState: properties.gigaSpaceBeforeState;
            onClicked: properties.gigaSpaceBeforeState = KoSvgTextPropertiesModel.PropertyUnset;
        }
        Kis.DoubleSliderSpinBox {
            id: spaceBeforeSpn;
            prefix: i18nc("@label:spinbox", "Space Before: ");
            suffix: i18nc("@label:unit", " pt");
            dFrom: 0; dTo: 500; softDFrom: 0; softDTo: 72; softRangeActive: true;
            dStepSize: 1;
            Layout.fillWidth: true;
            blockUpdateSignalOnDrag: true;
        }

        RevertPropertyButton {
            revertState: properties.gigaSpaceAfterState;
            onClicked: properties.gigaSpaceAfterState = KoSvgTextPropertiesModel.PropertyUnset;
        }
        Kis.DoubleSliderSpinBox {
            id: spaceAfterSpn;
            prefix: i18nc("@label:spinbox", "Space After: ");
            suffix: i18nc("@label:unit", " pt");
            dFrom: 0; dTo: 500; softDFrom: 0; softDTo: 72; softRangeActive: true;
            dStepSize: 1;
            Layout.fillWidth: true;
            blockUpdateSignalOnDrag: true;
        }

        RevertPropertyButton {
            revertState: properties.gigaIndentStartState;
            onClicked: properties.gigaIndentStartState = KoSvgTextPropertiesModel.PropertyUnset;
        }
        Kis.DoubleSliderSpinBox {
            id: indentStartSpn;
            prefix: i18nc("@label:spinbox", "Indent Start: ");
            suffix: i18nc("@label:unit", " pt");
            dFrom: 0; dTo: 1000; softDFrom: 0; softDTo: 144; softRangeActive: true;
            dStepSize: 1;
            Layout.fillWidth: true;
            blockUpdateSignalOnDrag: true;
        }

        RevertPropertyButton {
            revertState: properties.gigaIndentEndState;
            onClicked: properties.gigaIndentEndState = KoSvgTextPropertiesModel.PropertyUnset;
        }
        Kis.DoubleSliderSpinBox {
            id: indentEndSpn;
            prefix: i18nc("@label:spinbox", "Indent End: ");
            suffix: i18nc("@label:unit", " pt");
            dFrom: 0; dTo: 1000; softDFrom: 0; softDTo: 144; softRangeActive: true;
            dStepSize: 1;
            Layout.fillWidth: true;
            blockUpdateSignalOnDrag: true;
        }
    }
}

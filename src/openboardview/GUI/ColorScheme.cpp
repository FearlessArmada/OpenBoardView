#include "ColorScheme.h"

#include <cstdint>
#include <cstdio>

#include "imgui/imgui.h"

#include "Board.h"

const std::vector<ColorScheme::ColorGroup> &ColorScheme::Groups() {
	static const std::vector<ColorGroup> groups = {
	    {"Board",
	     {
	         {"backgroundColor", "Background", &ColorScheme::backgroundColor},
	         {"boardFillColor", "Board fill", &ColorScheme::boardFillColor},
	         {"boardOutlineColor", "Board outline", &ColorScheme::boardOutlineColor},
	         {"silkscreenColor", "Silkscreen", &ColorScheme::silkscreenColor},
	         {"viaHoleColor", "Via hole", &ColorScheme::viaHoleColor},
	         {"traceSameNetHaloColor", "Trace halo (same net)", &ColorScheme::traceSameNetHaloColor},
	         {"gridMajorColor", "Grid (major)", &ColorScheme::gridMajorColor},
	         {"gridMinorColor", "Grid (minor)", &ColorScheme::gridMinorColor},
	         {"originMarkerColor", "Origin marker", &ColorScheme::originMarkerColor},
	     }},
	    {"Parts",
	     {
	         {"partOutlineColor", "Outline", &ColorScheme::partOutlineColor},
	         {"partHullColor", "Hull", &ColorScheme::partHullColor},
	         {"partFillColor", "Fill", &ColorScheme::partFillColor},
	         {"partShadowColor", "Shadow", &ColorScheme::partShadowColor},
	         {"partTextColor", "Text", &ColorScheme::partTextColor},
	         {"partTextBackgroundColor", "Text background", &ColorScheme::partTextBackgroundColor},
	         {"partHighlightedColor", "Selected", &ColorScheme::partHighlightedColor},
	         {"partHighlightedFillColor", "Fill (selected)", &ColorScheme::partHighlightedFillColor},
	         {"partHighlightedTextColor", "Text (selected)", &ColorScheme::partHighlightedTextColor},
	         {"partHighlightedTextBackgroundColor",
	          "Text background (selected)",
	          &ColorScheme::partHighlightedTextBackgroundColor},
	     }},
	    {"Part types",
	     {
	         {"partResistorColor", "Resistor", &ColorScheme::partResistorColor},
	         {"partCapacitorColor", "Capacitor", &ColorScheme::partCapacitorColor},
	         {"partInductorColor", "Inductor", &ColorScheme::partInductorColor},
	         {"partDiodeColor", "Diode", &ColorScheme::partDiodeColor},
	         {"partTransistorColor", "Transistor", &ColorScheme::partTransistorColor},
	         {"partICColor", "IC", &ColorScheme::partICColor},
	         {"partFuseColor", "Fuse", &ColorScheme::partFuseColor},
	         {"partConnectorColor", "Connector", &ColorScheme::partConnectorColor},
	         {"partCrystalColor", "Crystal", &ColorScheme::partCrystalColor},
	         {"partJellyBeanColor", "Jelly bean", &ColorScheme::partJellyBeanColor},
	         {"partNoStuffColor", "Not fitted", &ColorScheme::partNoStuffColor},
	     }},
	    {"Pins",
	     {
	         {"pinDefaultColor", "Default", &ColorScheme::pinDefaultColor, "pinDefaultFilled", &ColorScheme::pinDefaultFill},
	         {"pinDefaultTextColor", "Default text", &ColorScheme::pinDefaultTextColor},
	         {"pinTextBackgroundColor", "Text background", &ColorScheme::pinTextBackgroundColor},
	         {"pinGroundColor", "Ground", &ColorScheme::pinGroundColor, "pinGroundFilled", &ColorScheme::pinGroundFill},
	         {"pinNotConnectedColor",
	          "Not connected",
	          &ColorScheme::pinNotConnectedColor,
	          "pinNotConnectedFilled",
	          &ColorScheme::pinNotConnectedFill},
	         {"pinTestPadColor", "Test pad", &ColorScheme::pinTestPadColor, "pinTestPadFilled", &ColorScheme::pinTestPadFill},
	         {"pinTestPadFillColor", "Test pad fill", &ColorScheme::pinTestPadFillColor},
	         {"pinA1PadColor", "A1/1 pad", &ColorScheme::pinA1PadColor, "pinA1PadFilled", &ColorScheme::pinA1PadFill},
	     }},
	    {"Selection",
	     {
	         {"pinSelectedColor", "Selected", &ColorScheme::pinSelectedColor, "pinSelectedFilled", &ColorScheme::pinSelectedFill},
	         {"pinSelectedFillColor", "Selected fill", &ColorScheme::pinSelectedFillColor},
	         {"pinSelectedTextColor", "Selected text", &ColorScheme::pinSelectedTextColor},
	         {"pinSameNetColor", "Same net", &ColorScheme::pinSameNetColor, "pinSameNetFilled", &ColorScheme::pinSameNetFill},
	         {"pinSameNetFillColor", "Same net fill", &ColorScheme::pinSameNetFillColor},
	         {"pinSameNetTextColor", "Same net text", &ColorScheme::pinSameNetTextColor},
	         {"pinHaloColor", "Halo", &ColorScheme::pinHaloColor},
	         {"pinNetWebColor", "Net web strands", &ColorScheme::pinNetWebColor},
	         {"pinNetWebOSColor", "Net web (other side)", &ColorScheme::pinNetWebOSColor},
	     }},
	    {"Annotations",
	     {
	         {"annotationBoxColor", "Box", &ColorScheme::annotationBoxColor},
	         {"annotationStalkColor", "Stalk", &ColorScheme::annotationStalkColor},
	         {"annotationPartAliasColor", "Part alias", &ColorScheme::annotationPartAliasColor},
	         {"annotationPopupTextColor", "Popup text", &ColorScheme::annotationPopupTextColor},
	         {"annotationPopupBackgroundColor", "Popup background", &ColorScheme::annotationPopupBackgroundColor},
	     }},
	    {"Masks",
	     {
	         {"selectedMaskPins", "Pins", &ColorScheme::selectedMaskPins},
	         {"selectedMaskParts", "Parts", &ColorScheme::selectedMaskParts},
	         {"selectedMaskOutline", "Outline", &ColorScheme::selectedMaskOutline},
	         {"orMaskPins", "Boost (pins)", &ColorScheme::orMaskPins},
	         {"orMaskParts", "Boost (parts)", &ColorScheme::orMaskParts},
	         {"orMaskOutline", "Boost (outline)", &ColorScheme::orMaskOutline},
	     }},
	};
	return groups;
}

const char *ColorScheme::TraceColorKey(size_t i) {
	static char keys[traceColorCount][16];
	static bool built = false;
	if (!built) {
		for (size_t n = 0; n < traceColorCount; n++) snprintf(keys[n], sizeof(keys[n]), "traceColor%02zu", n);
		built = true;
	}
	return keys[i % traceColorCount];
}

uint32_t ColorScheme::PartTypeColor(int componentType) const {
	if (!partTypeColorsEnabled) return partFillColor;

	switch (componentType) {
		case Component::kComponentTypeResistor: return partResistorColor;
		case Component::kComponentTypeCapacitor: return partCapacitorColor;
		case Component::kComponentTypeInductor: return partInductorColor;
		case Component::kComponentTypeDiode: return partDiodeColor;
		case Component::kComponentTypeTransistor: return partTransistorColor;
		case Component::kComponentTypeIC: return partICColor;
		case Component::kComponentTypeFuse: return partFuseColor;
		case Component::kComponentTypeConnector: return partConnectorColor;
		case Component::kComponentTypeCrystal: return partCrystalColor;
		case Component::kComponentTypeJellyBean: return partJellyBeanColor;
		default: return partFillColor;
	}
}

uint32_t ColorScheme::byte4swap(uint32_t x) {
	/*
	 * used to convert RGBA -> ABGR etc
	 */
	return (((x & 0x000000ff) << 24) | ((x & 0x0000ff00) << 8) | ((x & 0x00ff0000) >> 8) | ((x & 0xff000000) >> 24));
}

void ColorScheme::ThemeSetStyle(const char *name) {
	baseStyle = (name && name[0]) ? name : "light";

	const bool dark    = baseStyle.find("dark") != std::string::npos;
	const bool classic = baseStyle.compare(0, 7, "classic") == 0;

	if (ImGui::GetCurrentContext()) {
		ImGuiStyle &style = ImGui::GetStyle();

		style.WindowBorderSize = 0.0f;

		if (dark) {
			style.Colors[ImGuiCol_Text]          = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
			style.Colors[ImGuiCol_TextDisabled]  = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
			style.Colors[ImGuiCol_WindowBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.70f);
			style.Colors[ImGuiCol_ChildBg]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
			style.Colors[ImGuiCol_PopupBg]       = ImVec4(0.05f, 0.05f, 0.10f, 0.90f);
			style.Colors[ImGuiCol_Border]        = ImVec4(0.70f, 0.70f, 0.70f, 0.65f);
			style.Colors[ImGuiCol_BorderShadow]  = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
			style.Colors[ImGuiCol_FrameBg] =
			ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
			style.Colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.90f, 0.80f, 0.80f, 0.40f);
			style.Colors[ImGuiCol_FrameBgActive]        = ImVec4(0.90f, 0.65f, 0.65f, 0.45f);
			style.Colors[ImGuiCol_TitleBg]              = ImVec4(0.27f, 0.27f, 0.54f, 0.83f);
			style.Colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.40f, 0.40f, 0.80f, 0.20f);
			style.Colors[ImGuiCol_TitleBgActive]        = ImVec4(0.32f, 0.32f, 0.63f, 0.87f);
			style.Colors[ImGuiCol_MenuBarBg]            = ImVec4(0.40f, 0.40f, 0.55f, 0.80f);
			style.Colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.20f, 0.25f, 0.30f, 0.60f);
			style.Colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.40f, 0.40f, 0.80f, 0.30f);
			style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.80f, 0.40f);
			style.Colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.80f, 0.50f, 0.50f, 0.40f);
			style.Colors[ImGuiCol_CheckMark]            = ImVec4(0.90f, 0.90f, 0.90f, 0.50f);
			style.Colors[ImGuiCol_CheckboxSelectedBg]   = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
			style.Colors[ImGuiCol_SliderGrab]           = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
			style.Colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.80f, 0.50f, 0.50f, 1.00f);
			style.Colors[ImGuiCol_Button]               = ImVec4(0.67f, 0.40f, 0.40f, 0.60f);
			style.Colors[ImGuiCol_ButtonHovered]        = ImVec4(0.67f, 0.40f, 0.40f, 1.00f);
			style.Colors[ImGuiCol_ButtonActive]         = ImVec4(0.80f, 0.50f, 0.50f, 1.00f);
			style.Colors[ImGuiCol_Header]               = ImVec4(0.40f, 0.40f, 0.90f, 0.45f);
			style.Colors[ImGuiCol_HeaderHovered]        = ImVec4(0.45f, 0.45f, 0.90f, 0.80f);
			style.Colors[ImGuiCol_HeaderActive]         = ImVec4(0.53f, 0.53f, 0.87f, 0.80f);
			style.Colors[ImGuiCol_Separator]            = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
			style.Colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.70f, 0.60f, 0.60f, 1.00f);
			style.Colors[ImGuiCol_SeparatorActive]      = ImVec4(0.90f, 0.70f, 0.70f, 1.00f);
			style.Colors[ImGuiCol_ResizeGrip]           = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
			style.Colors[ImGuiCol_ResizeGripHovered]    = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
			style.Colors[ImGuiCol_ResizeGripActive]     = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
			style.Colors[ImGuiCol_PlotLines]            = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
			style.Colors[ImGuiCol_PlotLinesHovered]     = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
			style.Colors[ImGuiCol_PlotHistogram]        = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
			style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
			style.Colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.00f, 0.00f, 1.00f, 0.35f);
			style.Colors[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
			style.Colors[ImGuiCol_TableRowBg]           = ImVec4(0.05f, 0.05f, 0.10f, 0.90f);
			style.Colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.10f, 0.10f, 0.20f, 0.90f);
		} else {
			style.Colors[ImGuiCol_Text]                 = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
			style.Colors[ImGuiCol_TextDisabled]         = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
			style.Colors[ImGuiCol_PopupBg]              = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
			style.Colors[ImGuiCol_WindowBg]             = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
			style.Colors[ImGuiCol_ChildBg]              = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
			style.Colors[ImGuiCol_Border]               = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
			style.Colors[ImGuiCol_BorderShadow]         = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
			style.Colors[ImGuiCol_FrameBg]              = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
			style.Colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
			style.Colors[ImGuiCol_FrameBgActive]        = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
			style.Colors[ImGuiCol_TitleBg]              = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
			style.Colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(1.00f, 1.00f, 1.00f, 0.51f);
			style.Colors[ImGuiCol_TitleBgActive]        = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
			style.Colors[ImGuiCol_MenuBarBg]            = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
			style.Colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.98f, 0.98f, 0.98f, 0.53f);
			style.Colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.69f, 0.69f, 0.69f, 0.80f);
			style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.49f, 0.49f, 0.49f, 0.80f);
			style.Colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.49f, 0.49f, 0.49f, 1.00f);
			style.Colors[ImGuiCol_CheckMark]            = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
			style.Colors[ImGuiCol_CheckboxSelectedBg]   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
			style.Colors[ImGuiCol_SliderGrab]           = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
			style.Colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
			style.Colors[ImGuiCol_Button]               = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
			style.Colors[ImGuiCol_ButtonHovered]        = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
			style.Colors[ImGuiCol_ButtonActive]         = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
			style.Colors[ImGuiCol_Header]               = ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
			style.Colors[ImGuiCol_HeaderHovered]        = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
			style.Colors[ImGuiCol_HeaderActive]         = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
			style.Colors[ImGuiCol_Separator]            = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
			style.Colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
			style.Colors[ImGuiCol_SeparatorActive]      = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
			style.Colors[ImGuiCol_ResizeGrip]           = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
			style.Colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
			style.Colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
			style.Colors[ImGuiCol_PlotLines]            = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
			style.Colors[ImGuiCol_PlotLinesHovered]     = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
			style.Colors[ImGuiCol_PlotHistogram]        = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
			style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
			style.Colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
			style.Colors[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
			style.Colors[ImGuiCol_TableRowBg]           = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
			style.Colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
		}
	}

	if (classic)
		SetClassicBoardColors(dark);
	else
		SetDefaultBoardColors(dark);
}

void ColorScheme::SetClassicBoardColors(bool dark) {
	if (dark) {
		this->backgroundColor          = byte4swap(0x000000ff);
		this->boardFillColor           = byte4swap(0x2a2a2aff);
		this->boardOutlineColor        = byte4swap(0xcc2222ff);
		this->viaHoleColor             = byte4swap(0x000000ff);
		this->traceSameNetHaloColor    = byte4swap(0xffffffcc);
		this->silkscreenColor          = byte4swap(0xb0b0b0ff);
		this->partHullColor            = byte4swap(0x80808080);
		this->partOutlineColor         = byte4swap(0x999999ff);
		this->partFillColor            = byte4swap(0x111111ff);
		this->partTextColor            = byte4swap(0x80808080);
		this->partTextBackgroundColor  = byte4swap(0x11111100);
		this->partHighlightedColor     = byte4swap(0xffffffff);
		this->partHighlightedFillColor = byte4swap(0x333333ff);
		this->partHighlightedTextColor           = byte4swap(0x000000ff);
		this->partHighlightedTextBackgroundColor = byte4swap(0xcccc22ff);
		this->pinDefaultColor          = byte4swap(0x4040ffff);
		this->pinDefaultTextColor      = byte4swap(0xccccccff);
		this->pinTextBackgroundColor   = byte4swap(0xffffff80);
		this->pinGroundColor           = byte4swap(0x0300C3ff);
		this->pinNotConnectedColor     = byte4swap(0xaaaaaaff);
		this->pinTestPadColor          = byte4swap(0x888888ff);
		this->pinTestPadFillColor      = byte4swap(0x6c5b1fff);
		this->pinA1PadColor            = byte4swap(0xdd0000ff);

		this->pinSelectedColor     = byte4swap(0x00ff00ff);
		this->pinSelectedFillColor = byte4swap(0x8888ffff);
		this->pinSelectedTextColor = byte4swap(0xffffffff);

		this->pinSameNetColor     = byte4swap(0x0000ffff);
		this->pinSameNetFillColor = byte4swap(0x9999ffff);
		this->pinSameNetTextColor = byte4swap(0x111111ff);

		this->pinHaloColor     = byte4swap(0xffffff88);
		this->pinNetWebColor   = byte4swap(0xff888888);
		this->pinNetWebOSColor = byte4swap(0x8888ff88);

		this->annotationPartAliasColor       = byte4swap(0xffff00ff);
		this->annotationBoxColor             = byte4swap(0xcccc88ff);
		this->annotationStalkColor           = byte4swap(0xaaaaaaff);
		this->annotationPopupBackgroundColor = byte4swap(0x888888ff);
		this->annotationPopupTextColor       = byte4swap(0xffffffff);
	} else {
		this->backgroundColor          = byte4swap(0xffffffff);
		this->boardFillColor           = byte4swap(0xddddddff);
		this->boardOutlineColor        = byte4swap(0x444444ff);
		this->viaHoleColor             = byte4swap(0x303030ff);
		this->traceSameNetHaloColor    = byte4swap(0x000000cc);
		this->silkscreenColor          = byte4swap(0x505050ff);
		this->partHullColor            = byte4swap(0x80808080);
		this->partOutlineColor         = byte4swap(0x444444ff);
		this->partFillColor            = byte4swap(0xffffff77);
		this->partTextColor            = byte4swap(0x80808080);
		this->partTextBackgroundColor  = byte4swap(0x11111100);
		this->partHighlightedColor     = byte4swap(0xff0000ff);
		this->partHighlightedFillColor = byte4swap(0xf0f0f0ff);
		this->partHighlightedTextColor           = byte4swap(0xff3030ff);
		this->partHighlightedTextBackgroundColor = byte4swap(0xffff00ff);
		this->pinDefaultColor          = byte4swap(0x22aa33ff);
		this->pinDefaultTextColor      = byte4swap(0x666688ff);
		this->pinTextBackgroundColor   = byte4swap(0xffffff80);
		this->pinGroundColor           = byte4swap(0x2222aaff);
		this->pinNotConnectedColor     = byte4swap(0xaaaaaaff);
		this->pinTestPadColor          = byte4swap(0x888888ff);
		this->pinTestPadFillColor      = byte4swap(0xd6c68dff);
		this->pinA1PadColor            = byte4swap(0xdd0000ff);

		this->pinSelectedColor     = byte4swap(0x00000000);
		this->pinSelectedFillColor = byte4swap(0x8888ffff);
		this->pinSelectedTextColor = byte4swap(0xffffffff);

		this->pinSameNetColor     = byte4swap(0x888888ff);
		this->pinSameNetFillColor = byte4swap(0x9999ffff);
		this->pinSameNetTextColor = byte4swap(0x111111ff);

		this->pinHaloColor     = byte4swap(0x22FF2288);
		this->pinNetWebColor   = byte4swap(0xff0000aa);
		this->pinNetWebOSColor = byte4swap(0x0000ff33);

		this->annotationPartAliasColor       = byte4swap(0xffff00ff);
		this->annotationBoxColor             = byte4swap(0xff0000aa);
		this->annotationStalkColor           = byte4swap(0x000000ff);
		this->annotationPopupBackgroundColor = byte4swap(0xeeeeeeff);
		this->annotationPopupTextColor       = byte4swap(0x000000ff);
	}

	SetCommonBoardColors(dark);

	this->partTypeColorsEnabled = false;
	this->pinDefaultFill        = false;
	this->pinGroundFill         = false;
	this->pinNotConnectedFill   = false;
	this->pinTestPadFill        = true;
	this->pinA1PadFill          = true;
	this->pinSelectedFill       = true;
	this->pinSameNetFill        = true;
}

void ColorScheme::SetDefaultBoardColors(bool dark) {
	if (dark) {
		this->backgroundColor          = byte4swap(0x000000ff);
		this->boardFillColor           = byte4swap(0x191817ff);
		this->boardOutlineColor        = byte4swap(0x313131ff);
		this->viaHoleColor             = byte4swap(0x0d0d0dff);
		this->traceSameNetHaloColor    = byte4swap(0xffffffcc);
		this->silkscreenColor          = byte4swap(0x838383ff);
		this->partHullColor            = byte4swap(0x80808080);
		this->partOutlineColor         = byte4swap(0x4a4a4aff);
		this->partFillColor            = byte4swap(0x111111bb);
		this->partShadowColor          = byte4swap(0x00000080);
		this->partTextColor            = byte4swap(0xc0c0c0ff);
		this->partTextBackgroundColor  = byte4swap(0x11111100);
		this->partHighlightedColor     = byte4swap(0xdaee00ff);
		this->partHighlightedFillColor = byte4swap(0x1b1b1bff);
		this->partHighlightedTextColor           = byte4swap(0x000000ff);
		this->partHighlightedTextBackgroundColor = byte4swap(0xe8eb22c6);

		this->partResistorColor   = byte4swap(0x222222ff);
		this->partCapacitorColor  = byte4swap(0x908064ff);
		this->partInductorColor   = byte4swap(0x848484ff);
		this->partDiodeColor      = byte4swap(0x722f2fff);
		this->partTransistorColor = byte4swap(0x333333ff);
		this->partICColor         = byte4swap(0x201e1eff);
		this->partFuseColor       = byte4swap(0xcececeff);
		this->partConnectorColor  = byte4swap(0x737373ff);
		this->partCrystalColor    = byte4swap(0x4a4a3aff);
		this->partJellyBeanColor  = byte4swap(0x2a2a2aff);
		this->partNoStuffColor    = byte4swap(0x00000000);

		this->pinDefaultColor        = byte4swap(0x525278ff);
		this->pinDefaultTextColor    = byte4swap(0xccccccff);
		this->pinTextBackgroundColor = byte4swap(0xffffff00);
		this->pinGroundColor         = byte4swap(0x515157ff);
		this->pinNotConnectedColor   = byte4swap(0x646464ff);
		this->pinTestPadColor        = byte4swap(0x615726ff);
		this->pinTestPadFillColor    = byte4swap(0x615726ff);
		this->pinA1PadColor          = byte4swap(0x8b2727ff);

		this->pinSelectedColor     = byte4swap(0x009200ff);
		this->pinSelectedFillColor = byte4swap(0x009200ff);
		this->pinSelectedTextColor = byte4swap(0xffffffff);

		this->pinSameNetColor     = byte4swap(0x49c024b5);
		this->pinSameNetFillColor = byte4swap(0x49c024b5);
		this->pinSameNetTextColor = byte4swap(0x111111ff);

		this->pinHaloColor     = byte4swap(0xffffff88);
		this->pinNetWebColor   = byte4swap(0xc53737b3);
		this->pinNetWebOSColor = byte4swap(0x8888ff2b);

		this->annotationPartAliasColor       = byte4swap(0xffff00ff);
		this->annotationBoxColor             = byte4swap(0x0000ff88);
		this->annotationStalkColor           = byte4swap(0xaaaaaaff);
		this->annotationPopupBackgroundColor = byte4swap(0x222222ff);
		this->annotationPopupTextColor       = byte4swap(0xe0e0e0ff);
	} else {
		this->backgroundColor          = byte4swap(0xffffffff);
		this->boardFillColor           = byte4swap(0xdbdbdbff);
		this->boardOutlineColor        = byte4swap(0x4444444e);
		this->viaHoleColor             = byte4swap(0x303030ff);
		this->traceSameNetHaloColor    = byte4swap(0x000000cc);
		this->silkscreenColor          = byte4swap(0x959595ff);
		this->partHullColor            = byte4swap(0x808080ff);
		this->partOutlineColor         = byte4swap(0x444444ff);
		this->partFillColor            = byte4swap(0xf6f6f6ff);
		this->partShadowColor          = byte4swap(0x00000080);
		this->partTextColor            = byte4swap(0xffffffff);
		this->partTextBackgroundColor  = byte4swap(0x00000032);
		this->partHighlightedColor     = byte4swap(0x05ff00ff);
		this->partHighlightedFillColor = byte4swap(0xffffffff);
		this->partHighlightedTextColor           = byte4swap(0x000000ff);
		this->partHighlightedTextBackgroundColor = byte4swap(0x4bc0a6ff);

		this->partResistorColor   = byte4swap(0x222222ff);
		this->partCapacitorColor  = byte4swap(0x9d8452ff);
		this->partInductorColor   = byte4swap(0xa6a6a6ff);
		this->partDiodeColor      = byte4swap(0x3a4398ff);
		this->partTransistorColor = byte4swap(0x333333ff);
		this->partICColor         = byte4swap(0x333333ff);
		this->partFuseColor       = byte4swap(0xffffffff);
		this->partConnectorColor  = byte4swap(0xe7e2d7ff);
		this->partCrystalColor    = byte4swap(0xbdbdb0ff);
		this->partJellyBeanColor  = byte4swap(0xdededeff);
		this->partNoStuffColor    = byte4swap(0xd2d2d236);

		this->pinDefaultColor        = byte4swap(0xa0a0d5ff);
		this->pinDefaultTextColor    = byte4swap(0x48485cff);
		this->pinTextBackgroundColor = byte4swap(0xffffff00);
		this->pinGroundColor         = byte4swap(0x959595ff);
		this->pinNotConnectedColor   = byte4swap(0x919191ff);
		this->pinTestPadColor        = byte4swap(0xbca749ff);
		this->pinTestPadFillColor    = byte4swap(0xbca749ff);
		this->pinA1PadColor          = byte4swap(0xff000094);

		this->pinSelectedColor     = byte4swap(0x5da156ff);
		this->pinSelectedFillColor = byte4swap(0x5da156ff);
		this->pinSelectedTextColor = byte4swap(0xffffffff);

		this->pinSameNetColor     = byte4swap(0x69c95fb4);
		this->pinSameNetFillColor = byte4swap(0x69c95fb4);
		this->pinSameNetTextColor = byte4swap(0x111111ff);

		this->pinHaloColor     = byte4swap(0x22ff2288);
		this->pinNetWebColor   = byte4swap(0x000000a5);
		this->pinNetWebOSColor = byte4swap(0x7b7b7b22);

		this->annotationPartAliasColor       = byte4swap(0xffff00ff);
		this->annotationBoxColor             = byte4swap(0x0000ff88);
		this->annotationStalkColor           = byte4swap(0x000000ff);
		this->annotationPopupBackgroundColor = byte4swap(0xeeeeeeff);
		this->annotationPopupTextColor       = byte4swap(0x000000ff);
	}

	SetCommonBoardColors(dark);

	this->partTypeColorsEnabled = true;
	this->pinDefaultFill        = true;
	this->pinGroundFill         = true;
	this->pinNotConnectedFill   = dark;
	this->pinTestPadFill        = true;
	this->pinA1PadFill          = true;
	this->pinSelectedFill       = true;
	this->pinSameNetFill        = true;
}

void ColorScheme::SetCommonBoardColors(bool dark) {
	this->gridMajorColor    = byte4swap(0x888888aa);
	this->gridMinorColor    = byte4swap(0x88888824);
	this->originMarkerColor = byte4swap(0x0039ff88);

	this->selectedMaskPins    = byte4swap(0xffffffff);
	this->selectedMaskParts   = byte4swap(0xffffffff);
	this->selectedMaskOutline = byte4swap(0xffffffff);

	this->orMaskPins    = byte4swap(0x0);
	this->orMaskParts   = byte4swap(0x0);
	this->orMaskOutline = byte4swap(0x0);

	(void)dark;
}

void ColorScheme::readFromConfig(Confparse &obvconfig) {
	const char *v = obvconfig.ParseStr("colorTheme", "light");
	ThemeSetStyle(v);

	for (const auto &group : Groups()) {
		for (const auto &item : group.items) {
			this->*item.color = byte4swap(obvconfig.ParseHex(item.key, byte4swap(this->*item.color)));
			if (item.fill) this->*item.fill = obvconfig.ParseBool(item.fillKey, this->*item.fill);
		}
	}

	for (size_t i = 0; i < traceColorCount; i++)
		traceColors[i] = byte4swap(obvconfig.ParseHex(TraceColorKey(i), byte4swap(traceColors[i])));

	partTypeColorsEnabled = obvconfig.ParseBool("partTypeColorsEnabled", partTypeColorsEnabled);
}

void ColorScheme::writeToConfig(Confparse &obvconfig) {
	obvconfig.WriteStr("colorTheme", baseStyle.c_str());

	for (const auto &group : Groups()) {
		for (const auto &item : group.items) {
			obvconfig.WriteHex(item.key, byte4swap(this->*item.color));
			if (item.fill) obvconfig.WriteBool(item.fillKey, this->*item.fill);
		}
	}

	for (size_t i = 0; i < traceColorCount; i++) obvconfig.WriteHex(TraceColorKey(i), byte4swap(traceColors[i]));

	obvconfig.WriteBool("partTypeColorsEnabled", partTypeColorsEnabled);
}

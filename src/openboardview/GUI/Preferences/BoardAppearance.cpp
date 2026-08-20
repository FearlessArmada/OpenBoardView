#include "BoardAppearance.h"

#include "imgui/imgui.h"

#include "GUI/DPI.h"
#include "GUI/widgets.h"

namespace Preferences {

BoardAppearance::BoardAppearance(Confparse &obvconfig, Config &config)
	: obvconfig(obvconfig), config(config) {
}

void BoardAppearance::render() {
	RightAlignedText("Annotation flag size", DPI(250));
	ImGui::SameLine();
	ImGui::InputInt("##annotationBoxSize", &config.annotationBoxSize);

	RightAlignedText("Annotation flag offset", DPI(250));
	ImGui::SameLine();
	ImGui::InputInt("##annotationBoxOffset", &config.annotationBoxOffset);

	RightAlignedText("Pin-1/A1 count threshold", DPI(250));
	ImGui::SameLine();
	if (ImGui::InputInt("##pinA1threshold", &config.pinA1threshold)) {
		if (config.pinA1threshold < 1) config.pinA1threshold = 1;
	}

	RightAlignedText("Pin select masks", DPI(250));
	ImGui::SameLine();
	ImGui::Checkbox("##pinSelectMasks", &config.pinSelectMasks);

	RightAlignedText("Pin halo", DPI(250));
	ImGui::SameLine();
	ImGui::Checkbox("##pinHalo", &config.pinHalo);

	RightAlignedText("Halo diameter", DPI(250));
	ImGui::SameLine();
	ImGui::InputFloat("##haloDiameter", &config.pinHaloDiameter);

	RightAlignedText("Halo thickness", DPI(250));
	ImGui::SameLine();
	ImGui::InputFloat("##haloThickness", &config.pinHaloThickness);

	RightAlignedText("Show net web", DPI(250));
	ImGui::SameLine();
	ImGui::Checkbox("##showNetWeb", &config.showNetWeb);

	RightAlignedText("Net web thickness", DPI(250));
	ImGui::SameLine();
	ImGui::InputInt("##netWebThickness", &config.netWebThickness);

	RightAlignedText("Fill parts", DPI(250));
	ImGui::SameLine();
	ImGui::Checkbox("##fillParts", &config.fillParts);

	ImGui::SameLine();
	RightAlignedText("Fill board", DPI(150));
	ImGui::SameLine();
	ImGui::Checkbox("##boardFill", &config.boardFill);

	RightAlignedText("Board fill spacing", DPI(250));
	ImGui::SameLine();
	ImGui::InputInt("##boardFillSpacing", &config.boardFillSpacing);

	RightAlignedText("Traces through parts", DPI(250));
	ImGui::SameLine();
	ImGui::SliderFloat("##traceShowThrough", &config.traceShowThrough, 0.0f, 1.0f, "%.2f");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("How strongly copper routed under a component still shows; 0 disables it");

	RightAlignedText("Show footprints", DPI(250));
	ImGui::SameLine();
	ImGui::Checkbox("##showFootprints", &config.showFootprints);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Draw the real footprint outline for formats that ship one");

	RightAlignedText("Part drop shadows", DPI(250));
	ImGui::SameLine();
	ImGui::Checkbox("##partShadows", &config.partShadows);

	RightAlignedText("Show parts name", DPI(250));
	ImGui::SameLine();
	ImGui::Checkbox("##showPartName", &config.showPartName);

	ImGui::SameLine();
	RightAlignedText("Show pins name", DPI(150));
	ImGui::SameLine();
	ImGui::Checkbox("##showPinName", &config.showPinName);

	ImGui::Dummy(ImVec2(1, DPI(6)));
	ImGui::Text("Grid");
	ImGui::Separator();

	RightAlignedText("Show grid", DPI(250));
	ImGui::SameLine();
	ImGui::Checkbox("##showGrid", &config.showGrid);

	ImGui::SameLine();
	RightAlignedText("Origin marker", DPI(150));
	ImGui::SameLine();
	ImGui::Checkbox("##showOriginMarker", &config.showOriginMarker);

	RightAlignedText("Grid spacing", DPI(250));
	ImGui::SameLine();
	if (ImGui::InputInt("##gridSpacing", &config.gridSpacing)) {
		if (config.gridSpacing < 1) config.gridSpacing = 1;
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Board units between minor grid lines");

	RightAlignedText("Major line every", DPI(250));
	ImGui::SameLine();
	if (ImGui::InputInt("##gridMajorEvery", &config.gridMajorEvery)) {
		if (config.gridMajorEvery < 1) config.gridMajorEvery = 1;
	}

	ImGui::Dummy(ImVec2(1, DPI(6)));
	ImGui::Text("Text size");
	ImGui::Separator();
	ImGui::TextDisabled("Board text is fitted to the part or pad it labels; these scale that fit.");

	RightAlignedText("Part names", DPI(250));
	ImGui::SameLine();
	ImGui::SliderFloat("##partNameFontScale", &config.partNameFontScale, 0.25f, 3.0f, "%.2fx");

	RightAlignedText("Pin names", DPI(250));
	ImGui::SameLine();
	ImGui::SliderFloat("##pinNameFontScale", &config.pinNameFontScale, 0.25f, 3.0f, "%.2fx");

	RightAlignedText("Silkscreen", DPI(250));
	ImGui::SameLine();
	ImGui::SliderFloat("##silkscreenFontScale", &config.silkscreenFontScale, 0.25f, 3.0f, "%.2fx");
}

} // namespace Preferences

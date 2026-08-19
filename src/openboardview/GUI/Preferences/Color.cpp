#include "Color.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "imgui/imgui.h"

#include "GUI/DPI.h"
#include "GUI/widgets.h"

namespace Preferences {

namespace {

void HexOf(uint32_t c, char *buf, size_t n) {
	snprintf(buf, n, "%08X", ColorScheme::byte4swap(c));
}

bool ContainsNoCase(const char *haystack, const char *needle) {
	if (!needle || !needle[0]) return true;
	if (!haystack) return false;

	for (const char *h = haystack; *h; h++) {
		const char *a = h;
		const char *b = needle;
		while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
			a++;
			b++;
		}
		if (!*b) return true;
	}
	return false;
}

} // namespace

Color::Color(KeyBindings &keyBindings, Confparse &obvconfig, ColorScheme &colorScheme, Theme &theme)
	: keyBindings(keyBindings), obvconfig(obvconfig), colorScheme(colorScheme), theme(theme) {
}

bool Color::matchesFilter(const char *label, const char *key) const {
	if (!filter[0]) return true;
	return ContainsNoCase(label, filter) || ContainsNoCase(key, filter);
}

bool Color::ColorPreferencesItem(const char *key, const char *label, uint32_t *c) {
	bool changed = false;

	ImGui::PushID(key);
	RightAlignedText(label, DPI(190));
	ImGui::SameLine();

	ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(*c);
	if (ImGui::ColorEdit4("##picker",
	                      &rgba.x,
	                      ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
	                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
		*c      = ImGui::ColorConvertFloat4ToU32(rgba);
		changed = true;
	}

	ImGui::SameLine();
	char buf[16];
	HexOf(*c, buf, sizeof(buf));
	ImGui::PushItemWidth(DPI(90));
	if (ImGui::InputText("##hex",
	                     buf,
	                     sizeof(buf),
	                     ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_CharsHexadecimal |
	                         ImGuiInputTextFlags_EnterReturnsTrue)) {
		*c      = ColorScheme::byte4swap(strtoul(buf, nullptr, 16));
		changed = true;
	}
	ImGui::PopItemWidth();

	if (changed) {
		char out[16];
		snprintf(out, sizeof(out), "0x%08x", ColorScheme::byte4swap(*c));
		obvconfig.WriteStr(key, out);
	}
	ImGui::PopID();

	return changed;
}

bool Color::ColorPreferencesItem(const ColorScheme::ColorItem &item) {
	bool changed = ColorPreferencesItem(item.key, item.label, &(colorScheme.*item.color));

	if (item.fill) {
		ImGui::SameLine();
		ImGui::PushID(item.fillKey);
		bool filled = colorScheme.*item.fill;
		if (ImGui::Checkbox("filled", &filled)) {
			colorScheme.*item.fill = filled;
			obvconfig.WriteBool(item.fillKey, filled);
			changed = true;
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Paint the pad solid as well as outlining it");
		ImGui::PopID();
	}

	return changed;
}

void Color::renderThemeBar() {
	const auto &themes = theme.available();
	std::string active = obvconfig.ParseStr("pcbTheme", "");

	RightAlignedText("Theme", DPI(190));
	ImGui::SameLine();
	ImGui::PushItemWidth(DPI(220));
	if (ImGui::BeginCombo("##themepicker", active.empty() ? "(unsaved)" : active.c_str())) {
		for (const auto &name : themes) {
			bool selected = (name == active);
			if (ImGui::Selectable(name.c_str(), selected)) {
				if (theme.apply(name, colorScheme, obvconfig))
					statusText = "Loaded theme \"" + name + "\"";
				else
					statusText = "Could not load theme \"" + name + "\"";
			}
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::PopItemWidth();

	ImGui::SameLine();
	if (ImGui::Button("Reload")) {
		theme.refresh();
		statusText = "Rescanned " + theme.directory().string();
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Re-read the themes folder after editing files by hand");

	RightAlignedText("Save as", DPI(190));
	ImGui::SameLine();
	ImGui::PushItemWidth(DPI(220));
	ImGui::InputTextWithHint("##saveas", "theme name", saveAsName, sizeof(saveAsName));
	ImGui::PopItemWidth();
	ImGui::SameLine();

	const bool nameOk = Theme::nameIsValid(saveAsName);
	ImGui::BeginDisabled(!nameOk);
	if (ImGui::Button("Save")) {
		if (theme.save(saveAsName, colorScheme)) {
			obvconfig.WriteStr("pcbTheme", saveAsName);
			statusText = std::string("Saved theme \"") + saveAsName + "\"";
			saveAsName[0] = '\0';
		} else {
			statusText = "Could not write theme file";
		}
	}
	ImGui::EndDisabled();
	if (!nameOk && saveAsName[0] && ImGui::IsItemHovered())
		ImGui::SetTooltip("Not a usable file name");

	if (!statusText.empty()) {
		ImGui::TextDisabled("%s", statusText.c_str());
	} else {
		ImGui::TextDisabled("Themes are stored in %s", theme.directory().string().c_str());
	}
}

void Color::menuItem() {
	if (ImGui::MenuItem("Colour Preferences")) {
		shown = true;
	}
}

bool Color::render() {
	bool m_needsRedraw = false;
	bool p_open        = true;

	ImGuiIO &io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), 0, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(DPI(660), DPI(620)), ImGuiCond_FirstUseEver);
	if (ImGui::BeginPopupModal("Colour Preferences", &p_open)) {
		shown = false;

		renderThemeBar();
		ImGui::Separator();

		RightAlignedText("Base style", DPI(190));
		ImGui::SameLine();
		{
			static const char *styles[]  = {"light", "dark", "classic-light", "classic-dark"};
			static const char *labels[]  = {"Light", "Dark", "Classic light", "Classic dark"};
			int current                  = 0;
			for (int i = 0; i < 4; i++)
				if (colorScheme.baseStyle == styles[i]) current = i;

			ImGui::PushItemWidth(DPI(220));
			if (ImGui::Combo("##basestyle", &current, labels, 4)) {
				colorScheme.ThemeSetStyle(styles[current]);
				colorScheme.writeToConfig(obvconfig);
				statusText    = std::string("Reset to base style \"") + labels[current] + "\"";
				m_needsRedraw = true;
			}
			ImGui::PopItemWidth();
		}
		ImGui::SameLine();
		if (ImGui::Checkbox("Colour parts by type", &colorScheme.partTypeColorsEnabled)) {
			obvconfig.WriteBool("partTypeColorsEnabled", colorScheme.partTypeColorsEnabled);
			m_needsRedraw = true;
		}

		ImGui::Separator();
		RightAlignedText("Filter", DPI(190));
		ImGui::SameLine();
		ImGui::PushItemWidth(DPI(220));
		ImGui::InputTextWithHint("##filter", "search colours", filter, sizeof(filter));
		ImGui::PopItemWidth();
		if (filter[0]) {
			ImGui::SameLine();
			if (ImGui::Button("Clear")) filter[0] = '\0';
		}

		ImGui::Separator();

		if (ImGui::BeginChild("##colorlist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.4f))) {
			if (ImGui::BeginTabBar("##colortabs")) {
				for (const auto &group : ColorScheme::Groups()) {
					size_t visible = 0;
					for (const auto &item : group.items)
						if (matchesFilter(item.label, item.key)) visible++;
					if (visible == 0) continue;

					if (ImGui::BeginTabItem(group.name)) {
						for (const auto &item : group.items) {
							if (!matchesFilter(item.label, item.key)) continue;
							if (ColorPreferencesItem(item)) m_needsRedraw = true;
						}
						ImGui::EndTabItem();
					}
				}

				size_t visibleTraces = 0;
				char label[32];
				for (size_t i = 0; i < ColorScheme::traceColorCount; i++) {
					snprintf(label, sizeof(label), "Layer %zu", i + 1);
					if (matchesFilter(label, ColorScheme::TraceColorKey(i))) visibleTraces++;
				}
				if (visibleTraces > 0 && ImGui::BeginTabItem("Layers")) {
					ImGui::TextDisabled("Copper layers cycle through this palette.");
					for (size_t i = 0; i < ColorScheme::traceColorCount; i++) {
						snprintf(label, sizeof(label), "Layer %zu", i + 1);
						if (!matchesFilter(label, ColorScheme::TraceColorKey(i))) continue;
						if (ColorPreferencesItem(ColorScheme::TraceColorKey(i), label, &colorScheme.traceColors[i]))
							m_needsRedraw = true;
					}
					ImGui::EndTabItem();
				}

				ImGui::EndTabBar();
			}
		}
		ImGui::EndChild();

		ImGui::Separator();
		if (ImGui::Button("Done") || keyBindings.isPressed("CloseDialog")) {
			statusText.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (shown) {
		theme.refresh();
		ImGui::OpenPopup("Colour Preferences");
	}

	return m_needsRedraw;
}

} // namespace Preferences

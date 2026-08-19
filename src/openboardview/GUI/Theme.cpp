#include "Theme.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

#include "ColorScheme.h"
#include "version.h"

namespace {

struct Builtin {
	const char *name;
	const char *style;
};

const Builtin builtins[] = {
    {"Light", "light"},
    {"Dark", "dark"},
    {"Classic Light", "classic-light"},
    {"Classic Dark", "classic-dark"},
};

} // namespace

bool Theme::nameIsValid(const std::string &name) {
	if (name.empty() || name.size() > 64) return false;
	if (name.front() == '.') return false;
	return name.find_first_of("/\\:*?\"<>|") == std::string::npos;
}

filesystem::path Theme::pathFor(const std::string &name) const {
	return dir / (name + extension);
}

void Theme::setDirectory(const filesystem::path &configDir) {
	std::error_code ec;

	dir = configDir / "themes";
	filesystem::create_directories(dir, ec);
	if (ec) {
		dir.clear();
		names.clear();
		return;
	}

	writeBuiltins();
	refresh();
}

void Theme::writeBuiltins() {
	for (const auto &builtin : builtins) {
		std::error_code ec;
		if (filesystem::exists(pathFor(builtin.name), ec)) continue;

		ColorScheme colors;
		colors.ThemeSetStyle(builtin.style);
		save(builtin.name, colors);
	}
}

void Theme::refresh() {
	names.clear();
	if (dir.empty()) return;

	std::error_code ec;
	for (const auto &entry : filesystem::directory_iterator(dir, ec)) {
		if (ec) break;
		if (!entry.is_regular_file(ec)) continue;
		if (entry.path().extension() != extension) continue;
		names.push_back(entry.path().stem().string());
	}
	std::sort(names.begin(), names.end());
}

bool Theme::save(const std::string &name, ColorScheme &colors) {
	if (dir.empty() || !nameIsValid(name)) return false;

	ofstream file;
	file.open(pathFor(name), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!file.is_open()) return false;

	char buf[256];
	auto writeLine = [&](const char *line) { file.write(line, strlen(line)); };
	auto writeHex  = [&](const char *key, uint32_t value) {
        snprintf(buf, sizeof(buf), "%s = 0x%08x\r\n", key, ColorScheme::byte4swap(value));
        writeLine(buf);
	};
	auto writeBool = [&](const char *key, bool value) {
		snprintf(buf, sizeof(buf), "%s = %s\r\n", key, value ? "true" : "false");
		writeLine(buf);
	};

	snprintf(buf, sizeof(buf), "#\r\n# %s\r\n#\r\n# " OBV_NAME " board theme. Colours are 0xRRGGBBAA.\r\n#\r\n", name.c_str());
	writeLine(buf);

	snprintf(buf, sizeof(buf), "colorTheme = %s\r\n", colors.baseStyle.c_str());
	writeLine(buf);
	writeBool("partTypeColorsEnabled", colors.partTypeColorsEnabled);

	for (const auto &group : ColorScheme::Groups()) {
		snprintf(buf, sizeof(buf), "\r\n# %s\r\n", group.name);
		writeLine(buf);
		for (const auto &item : group.items) {
			writeHex(item.key, colors.*item.color);
			if (item.fill) writeBool(item.fillKey, colors.*item.fill);
		}
	}

	writeLine("\r\n# Trace/layer palette\r\n");
	for (size_t i = 0; i < ColorScheme::traceColorCount; i++)
		writeHex(ColorScheme::TraceColorKey(i), colors.traceColors[i]);

	file.close();
	refresh();
	return true;
}

bool Theme::apply(const std::string &name, ColorScheme &colors, Confparse &obvconfig) {
	if (dir.empty()) return false;

	std::error_code ec;
	auto path = pathFor(name);
	if (!filesystem::exists(path, ec)) return false;

	Confparse themeconf{};
	if (themeconf.Load(path) != 0) return false;

	colors.readFromConfig(themeconf);

	colors.writeToConfig(obvconfig);
	obvconfig.WriteStr("pcbTheme", name.c_str());
	return true;
}

bool Theme::remove(const std::string &name) {
	if (dir.empty()) return false;

	std::error_code ec;
	bool removed = filesystem::remove(pathFor(name), ec);
	refresh();
	return removed && !ec;
}

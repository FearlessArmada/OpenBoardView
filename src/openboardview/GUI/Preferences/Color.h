#ifndef _PREFERENCES_COLOR_H_
#define _PREFERENCES_COLOR_H_

#include <cstdint>
#include <string>

#include "GUI/ColorScheme.h"
#include "GUI/Theme.h"
#include "UI/Keyboard/KeyBindings.h"
#include "confparse.h"

namespace Preferences {

class Color {
private:
	bool shown = false;
	KeyBindings &keyBindings;
	Confparse &obvconfig;
	ColorScheme &colorScheme;
	Theme &theme;

	char filter[64]         = "";
	char saveAsName[64]     = "";
	std::string statusText  = "";

	bool ColorPreferencesItem(const ColorScheme::ColorItem &item);

	bool ColorPreferencesItem(const char *key, const char *label, uint32_t *c);

	bool matchesFilter(const char *label, const char *key) const;

	void renderThemeBar();

public:
	Color(KeyBindings &keyBindings, Confparse &obvconfig, ColorScheme &colorScheme, Theme &theme);

	void menuItem();
	bool render();
};

} // namespace Preferences

#endif

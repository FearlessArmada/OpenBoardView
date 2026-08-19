#ifndef _THEME_H_
#define _THEME_H_

#include <string>
#include <vector>

#include "confparse.h"
#include "filesystem_impl.h"

class ColorScheme;

class Theme {
public:
	static constexpr const char *extension = ".obvtheme";

	void setDirectory(const filesystem::path &configDir);

	void refresh();

	const std::vector<std::string> &available() const {
		return names;
	}

	const filesystem::path &directory() const {
		return dir;
	}

	bool apply(const std::string &name, ColorScheme &colors, Confparse &obvconfig);

	bool save(const std::string &name, ColorScheme &colors);

	bool remove(const std::string &name);

	static bool nameIsValid(const std::string &name);

private:
	filesystem::path dir{};
	std::vector<std::string> names{};

	filesystem::path pathFor(const std::string &name) const;
	void writeBuiltins();
};

#endif//_THEME_H_

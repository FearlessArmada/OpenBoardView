#ifndef _COLORSCHEME_H_
#define _COLORSCHEME_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "confparse.h"

class ColorScheme {
public:
	static constexpr size_t traceColorCount = 16;

	std::string baseStyle = "light";

	uint32_t backgroundColor          = 0xffffffff;
	uint32_t boardFillColor           = 0xffdddddd;
	uint32_t boardOutlineColor        = 0xff00ffff;
	uint32_t gridMajorColor           = 0xaa888888;
	uint32_t gridMinorColor           = 0x24888888;
	uint32_t originMarkerColor        = 0x88ff3900;

	uint32_t partOutlineColor         = 0xff444444;
	uint32_t partHullColor            = 0x80808080;
	uint32_t partFillColor            = 0xffffffff;
	uint32_t partShadowColor          = 0x80000000;
	uint32_t partTextColor            = 0x80808080;
	uint32_t partTextBackgroundColor  = 0x00111111;
	uint32_t partHighlightedColor     = 0xff0000ee;
	uint32_t partHighlightedFillColor = 0xf4f0f0ff;
	uint32_t partHighlightedTextColor           = 0xff808000;
	uint32_t partHighlightedTextBackgroundColor = 0xff00eeee;

	uint32_t partResistorColor   = 0xff222222;
	uint32_t partCapacitorColor  = 0xff648090;
	uint32_t partInductorColor   = 0xff848484;
	uint32_t partDiodeColor      = 0xff2f2f72;
	uint32_t partTransistorColor = 0xff333333;
	uint32_t partICColor         = 0xff1e1e20;
	uint32_t partFuseColor       = 0xffcecece;
	uint32_t partConnectorColor  = 0xff737373;
	uint32_t partCrystalColor    = 0xff5a5a4a;
	uint32_t partJellyBeanColor  = 0xff5f5f5f;
	uint32_t partNoStuffColor    = 0x00000000;

	uint32_t traceColors[traceColorCount] = {
	    0xff4a90d9,
	    0xff3ac2c2,
	    0xff5fbf5f,
	    0xffc27ac2,
	    0xff4a4ad9,
	    0xffb8873a,
	    0xff7a7ad9,
	    0xff9f9f4a,
	    0xff3a6fb8,
	    0xff2f9c9c,
	    0xff4a9c4a,
	    0xff9c5f9c,
	    0xff6a3ab8,
	    0xff8f6a2f,
	    0xffb85f5f,
	    0xff6fb83a,
	};
	uint32_t viaHoleColor          = 0xff303030;
	uint32_t traceSameNetHaloColor = 0xccffffff;
	uint32_t silkscreenColor       = 0xffb0b0b0;

	uint32_t pinDefaultColor        = 0xff0000ff;
	uint32_t pinDefaultTextColor    = 0xffcc0000;
	uint32_t pinTextBackgroundColor = 0xffffff80;
	uint32_t pinGroundColor         = 0xff0000bb;
	uint32_t pinNotConnectedColor   = 0xffff0000;
	uint32_t pinTestPadColor        = 0xff888888;
	uint32_t pinTestPadFillColor    = 0xff8dc6d6;
	uint32_t pinA1PadColor          = 0xffdd0000;

	uint32_t pinSelectedColor     = 0x00000000;
	uint32_t pinSelectedFillColor = 0xffff8888;
	uint32_t pinSelectedTextColor = 0xffffffff;

	uint32_t pinSameNetColor     = 0xffaa4040;
	uint32_t pinSameNetFillColor = 0xffff9999;
	uint32_t pinSameNetTextColor = 0xff111111;

	uint32_t pinHaloColor     = 0x8822FF22;
	uint32_t pinNetWebColor   = 0xff0000ff;
	uint32_t pinNetWebOSColor = 0x0000ff22;

	uint32_t annotationPartAliasColor       = 0xcc00ffff;
	uint32_t annotationBoxColor             = 0xaa0000ff;
	uint32_t annotationStalkColor           = 0xff000000;
	uint32_t annotationPopupBackgroundColor = 0xffeeeeee;
	uint32_t annotationPopupTextColor       = 0xff000000;

	uint32_t selectedMaskPins    = 0xFFFFFFFF;
	uint32_t selectedMaskParts   = 0xFFFFFFFF;
	uint32_t selectedMaskOutline = 0xFFFFFFFF;

	uint32_t orMaskPins    = 0x00000000;
	uint32_t orMaskParts   = 0x00000000;
	uint32_t orMaskOutline = 0x00000000;

	bool partTypeColorsEnabled = false;
	bool pinDefaultFill        = false;
	bool pinGroundFill         = false;
	bool pinNotConnectedFill   = false;
	bool pinTestPadFill        = true;
	bool pinA1PadFill          = true;
	bool pinSelectedFill       = true;
	bool pinSameNetFill        = true;

	struct ColorItem {
		const char *key;
		const char *label;
		uint32_t ColorScheme::*color;
		const char *fillKey     = nullptr;
		bool ColorScheme::*fill = nullptr;
	};

	struct ColorGroup {
		const char *name;
		std::vector<ColorItem> items;
	};

	static const std::vector<ColorGroup> &Groups();

	static const char *TraceColorKey(size_t i);

	uint32_t PartTypeColor(int componentType) const;

	static uint32_t byte4swap(uint32_t x);

	void ThemeSetStyle(const char *name);
	void readFromConfig(Confparse &obvconfig);
	void writeToConfig(Confparse &obvconfig);

private:
	void SetClassicBoardColors(bool dark);
	void SetDefaultBoardColors(bool dark);
	void SetCommonBoardColors(bool dark);
};

#endif//_COLORSCHEME_H_

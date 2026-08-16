#pragma once

#include "FileFormats/BRDFile.h"

#include "imgui/imgui.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#define EMPTY_STRING ""
#define kBoardComponentPrefix "c_"
#define kBoardPinPrefix "p_"
#define kBoardNetPrefix "n_"
#define kBoardElementNameLength 127

struct Point;
struct BoardElement;
struct Net;
struct Pin;
struct Component;

typedef std::function<void(const char *)> TcharStringCallback;
typedef std::function<void(BoardElement *)> TboardElementCallback;

template <class T>
using SharedVector = std::vector<std::shared_ptr<T>>;

template <class T>
using SharedStringMap = std::map<std::string, std::shared_ptr<T>>;

enum EBoardSide {
	kBoardSideTop    = 0,
	kBoardSideBottom = 1,
	kBoardSideBoth   = 2,
};

// Checking whether str `prefix` is a prefix of str `base`.
inline static bool is_prefix(std::string prefix, std::string base) {
	if (prefix.size() > base.size()) return false;

	auto res = mismatch(prefix.begin(), prefix.end(), base.begin());
	if (res.first == prefix.end()) return true;

	return false;
}

template <class T>
inline static bool contains(const T &element, std::vector<T> &v) {
	return find(begin(v), end(v), element) != end(v);
}

template <class T>
inline static bool contains(T &element, std::vector<T> &v) {
	return find(begin(v), end(v), element) != end(v);
}

template <class T>
inline void remove(T &element, std::vector<T> &v) {

	auto it = std::find(v.begin(), v.end(), element);

	if (it != v.end()) {
		using std::swap;
		swap(*it, v.back());
		v.pop_back();
	}
}

// Any element being on the board.
struct BoardElement {
	// Side of the board the element is located. (top, bottom, both?)
	EBoardSide board_side = kBoardSideBoth;

	// String uniquely identifying this element on the board.
	virtual std::string UniqueId() const = 0;
};

// A point/position on the board relative to top left corner of the board.
// TODO: not sure how different formats will store this info.
struct Point {
	float x, y;

	Point()
	    : x(0.0f)
	    , y(0.0f){};
	Point(float _x, float _y)
	    : x(_x)
	    , y(_y){};
	Point(int _x, int _y)
	    : x(float(_x))
	    , y(float(_y)){};
};

// Shared potential between multiple Pins/Contacts.
struct Net : BoardElement {
	int number;
	std::string name;
	bool is_ground;

	SharedVector<Pin> pins;

	std::string UniqueId() const {
		return kBoardNetPrefix + name;
	}

	std::vector<const std::string *> searchableStringDetails() const;
};

// Any observeable contact (nails, component pins).
// Convieniently/Confusingly named Pin not Contact here.
struct Pin : BoardElement {
	enum EPinType {
		kPinTypeUnkown = 0,
		kPinTypeNotConnected,
		kPinTypeComponent,
		kPinTypeVia,
		kPinTypeTestPad,
	};

	// Type of Contact, e.g. pin, via, probe/test point.
	EPinType type;

	// Pin number / Nail count.
	std::string number;

	std::string name; // for BGA pads will be AZ82 etc

	// Position according to board file. (probably in inches)
	Point position;

	// Contact diameter, e.g. via or pin size. (probably in inches)
	float diameter;

	// Pad size, for the board files that record it.
	float width  = 0.0f;
	float height = 0.0f;

	std::vector<ImVec2> shape;

	BRDPadShape pad_shape = BRDPadShape::Unknown;

	bool hasPadSize() const {
		return width > 0.0f && height > 0.0f;
	}

	bool contains(float position_x, float position_y) const {
		const float dx = position_x - position.x;
		const float dy = position_y - position.y;

		if (shape.size() >= 3) {
			bool inside = false;

			for (size_t i = 0, j = shape.size() - 1; i < shape.size(); j = i++) {
				if (((shape[i].y > dy) != (shape[j].y > dy)) &&
				    (dx < (shape[j].x - shape[i].x) * (dy - shape[i].y) / (shape[j].y - shape[i].y) + shape[i].x))
					inside = !inside;
			}

			return inside;
		}

		if (hasPadSize()) {
			const float half_width  = width * 0.5f;
			const float half_height = height * 0.5f;

			if (pad_shape == BRDPadShape::Circle) {
				return dx * dx + dy * dy <= half_width * half_width;
			}

			if (pad_shape == BRDPadShape::Obround) {
				const float radius = std::min(half_width, half_height);
				const float reach_x = std::max(0.0f, half_width - radius);
				const float reach_y = std::max(0.0f, half_height - radius);
				const float ox = std::max(0.0f, std::fabs(dx) - reach_x);
				const float oy = std::max(0.0f, std::fabs(dy) - reach_y);
				return ox * ox + oy * oy <= radius * radius;
			}

			if (pad_shape == BRDPadShape::Rectangle) {
				return std::fabs(dx) <= half_width && std::fabs(dy) <= half_height;
			}

			if (width != height) {
				return std::fabs(dx) <= half_width && std::fabs(dy) <= half_height;
			}

			return dx * dx + dy * dy <= half_width * half_width;
		}

		return dx * dx + dy * dy < diameter * diameter;
	}

	// Net this contact is connected to, nulltpr if no info available.
	Net *net;

	// Contact belonging to this component (pin), nullptr if nail.
	std::shared_ptr<Component> component;

	std::string UniqueId() const {
		return kBoardPinPrefix + number;
	}
};

// A component on the board having multiple Pins.
struct Component : BoardElement {
	enum EMountType { kMountTypeUnknown = 0, kMountTypeSMD, kMountTypeDIP };

	enum EComponentType {
		kComponentTypeUnknown = 0,
		kComponentTypeDummy,
		kComponentTypeConnector,
		kComponentTypeIC,
		kComponentTypeResistor,
		kComponentTypeCapacitor,
		kComponentTypeDiode,
		kComponentTypeTransistor,
		kComponentTypeCrystal,
		kComponentTypeJellyBean,
		kComponentTypeMechanical
	};

	// How the part is attached to the board, either SMD, .., through-hole?
	EMountType mount_type = kMountTypeUnknown;

	// Type of component, eg. resistor, cap, etc.
	EComponentType component_type = kComponentTypeUnknown;

	// Part name as stored in board file.
	std::string name;

	// Part manufacturing code (aka. part number).
	std::string mfgcode;

	// Pins belonging to this component.
	SharedVector<Pin> pins;

	// Post calculated outlines
	//
	std::array<ImVec2, 4> outline;
	Point p1{0.0f, 0.0f}, p2{0.0f, 0.0f}; // for debugging

	bool outline_done = false;
	std::vector<ImVec2> hull;
	ImVec2 omin, omax;
	ImVec2 centerpoint;
	double expanse = 0.0f; // quick measure of distance between pins.

	// enum ComponentVisualModes { CVMNormal = 0, CVMSelected, CVMShowPins, CVMModeCount };
	enum ComponentVisualModes { CVMNormal = 0, CVMSelected, CVMModeCount };

	uint8_t visualmode = 0;

	// Mount type as readable string.
	std::string mount_type_str() {
		switch (mount_type) {
			case Component::kMountTypeSMD: return "SMD";
			case Component::kMountTypeDIP: return "DIP";
			default: return "UNKNOWN";
		}
	}

	// true if component is not representing a real/physical component.
	bool is_dummy() {
		return component_type == kComponentTypeDummy;
	}

	// true if this is a mechanical fixture rather than something electrical.
	bool is_mechanical() const {
		return component_type == kComponentTypeMechanical;
	}

	std::string UniqueId() const {
		return kBoardComponentPrefix + name;
	}

	std::vector<const std::string *> searchableStringDetails() const;
};

class Board {
  public:
	enum EBoardType { kBoardTypeUnknown = 0, kBoardTypeBRD = 0x01, kBoardTypeBDV = 0x02 };

	virtual ~Board() {}

	virtual SharedVector<Net> &Nets()                               = 0;
	virtual SharedVector<Component> &Components()                   = 0;
	virtual SharedVector<Pin> &Pins()                               = 0;
	virtual SharedVector<Point> &OutlinePoints()                    = 0;
	virtual std::vector<std::pair<Point, Point>> &OutlineSegments() = 0;

	EBoardType BoardType() {
		return kBoardTypeUnknown;
	}
};

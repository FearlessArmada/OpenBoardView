#pragma once

#include <cstdlib>
#include <string>
#include <vector>

#define READ_INT() strtol(p, &p, 10);
// Warning: read as int then cast to uint if positive
#define READ_UINT                                \
	[&]() {                                      \
		int value = strtol(p, &p, 10);           \
		ENSURE(value >= 0, error_msg);           \
		return static_cast<unsigned int>(value); \
	}
#define READ_DOUBLE() strtod(p, &p)
#define READ_STR                                     \
	[&]() {                                          \
		while ((*p) && (isspace((uint8_t)*p))) ++p;  \
		s = p;                                       \
		while ((*p) && (!isspace((uint8_t)*p))) ++p; \
		*p = 0;                                      \
		p++;                                         \
		return fix_to_utf8(s, &arena, arena_end);    \
	}

struct BRDPoint {
	// mil (thou) is used here
	int x = 0;
	int y = 0;

	// Declaring these constructors wouldn't be required in C++17
	BRDPoint() = default;
	BRDPoint(int x, int y) : x(x), y(y) {}

	BRDPoint &operator-=(const BRDPoint& point)& {
		x -= point.x;
		y -= point.y;
		return *this;
	}

	bool operator==(const BRDPoint& point) {
		return x == point.x && y == point.y;
	}

	bool operator!=(const BRDPoint& point) {
		return x != point.x || y != point.y;
	}
};

enum class BRDPartMountingSide { Both, Bottom, Top };
enum class BRDPartType { SMD, ThroughHole };

enum class BRDPartClass {
	IC = 0,
	Diode,
	Transistor,
	Resistor,
	ResistorNetwork,
	Capacitor,
	CapacitorNetwork,
	Zener,
	LED,
	Jumper,
	Battery,
	Mask,
	Relay,
	Fuse,
	Choke,
	Crystal,
	Switch,
	Connector,
	TestPoint,
	Transformer,
	Potentiometer,
	Mechanical,
	Strap,
	Fiducial,
	Unknown
};

struct BRDPart {
	const char *name = nullptr;
	std::string mfgcode;
	std::string value;
	std::string rating;
	std::string package;
	std::string misc;
	bool nostuff = false;
	int component_class = -1;
	BRDPartMountingSide mounting_side{};
	BRDPartType part_type{};
	unsigned int end_of_pins = 0;
	BRDPoint p1{0, 0};
	BRDPoint p2{0, 0};
	std::vector<std::pair<BRDPoint, BRDPoint>> outline_segments;
};

enum class BRDPinSide { Both, Bottom, Top };

enum class BRDPadShape {
	Unknown,
	Circle,
	Rectangle,
	Obround,
	Outline
};

// A vertex of a pad outline, in mil, relative to the pin it belongs to.
struct BRDPadPoint {
	float x = 0.0f;
	float y = 0.0f;

	BRDPadPoint() = default;
	BRDPadPoint(float x, float y) : x(x), y(y) {}
};

struct BRDPin {
	BRDPoint pos;
	int probe = 0;
	unsigned int part = 0;
	BRDPinSide side{};
	const char *net = "UNCONNECTED";
	double radius    = 0.5f;
	// Width and height for formats that support it, such as TVW.
	double width  = 0.0;
	double height = 0.0;
	std::vector<BRDPadPoint> shape;
	BRDPadShape pad_shape = BRDPadShape::Unknown;
	const char *snum = nullptr;
	const char *name = nullptr;

	bool operator<(const BRDPin &p) const // For sorting the vector
	{
		return part == p.part ? (std::string(snum) < std::string(p.snum)) : (part < p.part); // sort by part number then pin number
	}
};

struct BRDTrace {
	BRDPoint from;
	BRDPoint to;
	double width    = 0.0; // mil
	const char *net = nullptr;
};

struct BRDFillPoint {
	float x = 0.0f;
	float y = 0.0f;

	BRDFillPoint() = default;
	BRDFillPoint(float x, float y) : x(x), y(y) {}
};

struct BRDPour {
	std::vector<BRDPoint> outline;
	std::vector<std::vector<BRDPoint>> cutouts;
	const char *net = nullptr;

	std::vector<BRDFillPoint> fill;
	std::vector<unsigned int> fill_indices;

	BRDPoint bounds_min, bounds_max;

	void build_fill();
};

struct BRDVia {
	BRDPoint pos;
	double pad_radius  = 0.0; // mil
	double hole_radius = 0.0; // mil, 0 if the format doesn't say
	const char *net    = nullptr;
};

struct BRDTracePath {
	std::vector<BRDPoint> points;
	double width    = 0.0;
	const char *net = nullptr;
	BRDPoint bounds_min, bounds_max;
};

struct BRDText {
	std::string text;
	BRDPoint pos;
	double height = 0.0;
	bool mirrored = false;
};

struct BRDLayer {
	std::string name;
	std::vector<BRDTrace> traces;
	std::vector<BRDTracePath> paths;
	std::vector<BRDPour> pours;
	std::vector<BRDVia> vias;
	std::vector<BRDText> texts;

	void build_paths();
};

struct BRDNail {
	unsigned int probe = 0;
	BRDPoint pos;
	BRDPartMountingSide side{};
	const char *net = "UNCONNECTED";
};

class BRDFileBase {
  public:
	unsigned int num_format = 0;
	unsigned int num_parts  = 0;
	unsigned int num_pins   = 0;
	unsigned int num_nails  = 0;

	std::vector<BRDPoint> format;
	std::vector<std::pair<BRDPoint, BRDPoint>> outline_segments;
	std::vector<BRDPart> parts;
	std::vector<BRDPin> pins;
	std::vector<BRDNail> nails;
	// Empty for formats that keep no per-layer geometry.
	std::vector<BRDLayer> board_layers;

	bool valid = false;
	std::string error_msg = "";

	virtual ~BRDFileBase()
	{
		free(file_buf);
		file_buf = nullptr;
	}
  protected:
	void AddNailsAsPins();
	BRDFileBase() {}
	// file_buf is used by some implementations. But since the derived class constructurs
	// are already passed a memory buffer most usages are "historic unneeded extra copies".
	char *file_buf = nullptr;

	std::vector<std::pair<BRDPoint, BRDPoint>> arc_to_segments(double startAngle, double endAngle, double r, BRDPoint p1, BRDPoint p2, BRDPoint pc);

	static double arc_slice_angle_rad;

	double distance(const BRDPoint &p1, const BRDPoint &p2);
};

void stringfile(char *buffer, std::vector<char*> &lines);
char *fix_to_utf8(char *s, char **arena, char *arena_end);

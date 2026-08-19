#include "TVWFile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "utils.h"


static const char *TVW_MAGIC = "Tebo-ictview files.";

// Coordinates are stored in hundredths of a mil, BRDPoint holds mils.
static const int TVW_COORD_SCALE = 100;

// Aperture 0 through 9 are reserved.
static const uint32_t TVW_FIRST_DCODE = 10;

// Smallest a pin record can be: four words and an empty name.
static const size_t TVW_MIN_PIN_RECORD = 17;

static const uint32_t TVW_HANDLE_KIND_MASK  = 7;
static const uint32_t TVW_HANDLE_KIND_SHIFT = 3;
static const uint32_t TVW_HANDLE_KIND_PAD   = 0;
static const uint32_t TVW_HANDLE_NONE       = 0xffffffffu;

static const double TVW_ARC_DEGREES_PER_SEGMENT = 5.0;
static const int TVW_ARC_MIN_SEGMENTS           = 3;

static const uint32_t TVW_MAX_PART_PINS = 16384;

static const uint32_t TVW_MIN_PART_COUNT = 4;

// The header strings are obfuscated with a position dependent Caesar shift.
static char tvw_decode_char(unsigned char character, size_t index) {
	if (character >= '0' && character <= '9') // digits decode to 'a'-'j'
		return static_cast<char>('a' + ((character - '0') + 4 + index % 3) % 10);
	if (character >= 'a' && character <= 'j') // 'a'-'j' decode to digits
		return static_cast<char>('0' + (3 - (character - 'a') + static_cast<int>(index % 3) + 10) % 10);
	if (character >= 'k' && character <= 'z') // 'k'-'z' rotates within 'k'-'z'
		return static_cast<char>('k' + ((character - 'k') + 11 - static_cast<int>(index % 10) + 16) % 16);
	if (character >= 'A' && character <= 'Z') // 'A'-'Z' rotates within 'A'-'Z'
		return static_cast<char>('A' + ((character - 'A') + 5 + index % 10) % 26);
	return static_cast<char>(character);
}

static std::string tvw_decode_string(const std::string &encoded_string) {
	std::string decoded_string;
	decoded_string.reserve(encoded_string.size());

	for (size_t i = 0; i < encoded_string.size(); i++) {
		decoded_string.push_back(tvw_decode_char(static_cast<unsigned char>(encoded_string[i]), i));
	}

	return decoded_string;
}

static uint32_t twv_get_word(const uint8_t *brd_data, size_t position) {
	return
		   static_cast<uint32_t>(brd_data[position])
		| (static_cast<uint32_t>(brd_data[position + 1]) << 8)
		| (static_cast<uint32_t>(brd_data[position + 2]) << 16)
		| (static_cast<uint32_t>(brd_data[position + 3]) << 24);
}

bool TVWFile::verifyFormat(const std::vector<char> &brd_buffer) {
	TVWReader tvw_reader(brd_buffer);
	std::string magic = tvw_decode_string(tvw_reader.pstring());
	return tvw_reader.ok && magic == TVW_MAGIC;
}

static void tvw_read_pad_reference(TVWFile::TVWReader &tvw_reader, uint32_t *handle, uint32_t *group) {
	tvw_reader.version(0);
	uint32_t low  = tvw_reader.u32();
	uint32_t high = tvw_reader.u32();

	if (handle) {
		*handle = low;
	}

	if (group) {
		*group = high;
	}
}

static void tvw_read_pad(TVWFile::TVWReader &tvw_reader, uint32_t element_version, TVWFile::Pad &pad) {
	pad.net   = tvw_reader.u32();
	pad.dcode = tvw_reader.u32();
	pad.x     = tvw_reader.i32();
	pad.y     = tvw_reader.i32();
	tvw_reader.u8();

	if (tvw_reader.u8()) {
		if (tvw_reader.u8()) { // a test point
			tvw_reader.skip(8);
			tvw_reader.u32();
		}

		if (tvw_reader.u8()) { // the pad's extents, relative to its centre
			pad.has_extent = true;
			pad.x1         = tvw_reader.i32();
			pad.y1         = tvw_reader.i32();
			pad.x2         = tvw_reader.i32();
			pad.y2         = tvw_reader.i32();
		}

		if (tvw_reader.u8()) { // the drill: an offset from the centre, then a size
			pad.has_drill      = true;
			pad.drill_offset_x = tvw_reader.i32();
			pad.drill_offset_y = tvw_reader.i32();
			pad.drill_cx       = tvw_reader.i32();
			pad.drill_cy       = tvw_reader.i32();
		}
	}

	if (element_version > 1) {
		tvw_reader.u8(); // the low nibble of the flags word's top byte
	}
}

static void tvw_read_line(TVWFile::TVWReader &tvw_reader, uint32_t, TVWFile::Trace &trace) {
	trace.net   = tvw_reader.u32();
	trace.dcode = tvw_reader.u32();
	trace.x1    = tvw_reader.i32();
	trace.y1    = tvw_reader.i32();
	trace.x2    = tvw_reader.i32();
	trace.y2    = tvw_reader.i32();
}

static void tvw_read_arc(TVWFile::TVWReader &tvw_reader, uint32_t, TVWFile::Arc &arc) {
	arc.net         = tvw_reader.u32();
	arc.dcode       = tvw_reader.u32();
	arc.cx          = tvw_reader.i32();
	arc.cy          = tvw_reader.i32();
	arc.radius      = tvw_reader.i32();
	arc.start_angle = tvw_reader.f32();
	arc.sweep       = tvw_reader.f32();
}

static void tvw_read_surface(TVWFile::TVWReader &tvw_reader, uint32_t element_version, TVWFile::Surface &surface) {
	surface.net     = tvw_reader.u32();
	uint32_t points = tvw_reader.u32();

	if (points > tvw_reader.remaining(8)) {
		tvw_reader.ok = false;
		return;
	}

	surface.outline.reserve(points);
	for (uint32_t i = 0; i < points; i++) {
		int32_t x = tvw_reader.i32();
		surface.outline.emplace_back(x, tvw_reader.i32());
	}

	if (!element_version) {
		return;
	}

	tvw_reader.u32(); // the region's line width

	uint32_t cutouts = tvw_reader.u32();
	if (cutouts > tvw_reader.remaining(4)) {
		tvw_reader.ok = false;
		return;
	}

	if (!cutouts) {
		return;
	}

	const uint32_t cutout_version = tvw_reader.version(1);
	surface.cutouts.reserve(cutouts);

	for (uint32_t c = 0; c < cutouts && tvw_reader.ok; c++) {
		uint32_t cut_points = tvw_reader.u32();

		if (cut_points > tvw_reader.remaining(8)) {
			tvw_reader.ok = false;
			return;
		}

		std::vector<std::pair<int32_t, int32_t>> cutout;
		cutout.reserve(cut_points);
		for (uint32_t i = 0; i < cut_points; i++) {
			int32_t x = tvw_reader.i32();
			cutout.emplace_back(x, tvw_reader.i32());
		}

		if (cutout_version) {
			tvw_reader.u32();
		}

		surface.cutouts.push_back(std::move(cutout));
	}
}

static void tvw_read_text(TVWFile::TVWReader &tvw_reader, uint32_t element_version, TVWFile::Text *text) {
	std::string body = tvw_reader.pstring();
	int32_t x        = tvw_reader.i32();
	int32_t y        = tvw_reader.i32();
	tvw_reader.u32();
	int32_t height = tvw_reader.i32();
	int32_t width  = tvw_reader.i32();
	tvw_reader.u32();

	bool mirrored = false;

	if (!element_version) {
		tvw_reader.u8();
	} else if (element_version > 2) {
		mirrored = tvw_reader.u8() != 0;
	}

	if (element_version > 3) {
		tvw_reader.u8();
		tvw_reader.u8();
	}

	if (element_version > 4) {
		tvw_reader.u32();
	}

	if (element_version > 5) {
		tvw_reader.u32();
		tvw_reader.u32();
	}

	if (text) {
		text->text     = std::move(body);
		text->x        = x;
		text->y        = y;
		text->height   = height;
		text->width    = width;
		text->mirrored = mirrored;
	}
}


static void tvw_skip_probe(TVWFile::TVWReader &tvw_reader, uint32_t version) {
	if (tvw_reader.u8()) {
		tvw_read_pad_reference(tvw_reader, nullptr, nullptr);
	}

	tvw_reader.u32();
	tvw_reader.skip(8);
	tvw_reader.u32();

	if (version >= 1) {
		tvw_reader.u8();
	}

	if (version > 3) {
		tvw_reader.u8();
	}

	if (version > 1) {
		if (tvw_reader.u8()) {
			tvw_reader.pstring();
		}

		if (tvw_reader.u8()) {
			tvw_reader.skip(tvw_reader.u32());
		}

		if (version > 4 && tvw_reader.u8()) {
			tvw_reader.skip(tvw_reader.u32());
		}
	}

	if (version > 2) {
		tvw_reader.u32();
	}

	if (version > 6) {
		tvw_reader.u32();
	}
}

static void tvw_skip_map_point(TVWFile::TVWReader &tvw_reader, uint32_t version) {
	tvw_read_pad_reference(tvw_reader, nullptr, nullptr);
	tvw_reader.skip(8);
	tvw_reader.skip(16);
	tvw_reader.u8();

	if (version > 4) {
		tvw_reader.u8();
	}

	if (version > 6) {
		tvw_reader.u8();
	}

	tvw_reader.u32();

	if (version > 2) {
		uint32_t points = tvw_reader.u32();

		if (points > tvw_reader.remaining(8)) {
			tvw_reader.ok = false;
			return;
		}

		tvw_reader.skip(8 * points);
	}

	if (version == 1) {
		tvw_reader.u32();
	} else if (version && tvw_reader.u8()) {
		tvw_reader.u32();
		tvw_reader.u32();
		tvw_reader.u32();

		if (version > 8) {
			tvw_reader.u32();
			tvw_reader.u32();
		}
	}

	if (version > 3) {
		tvw_reader.pstring();
	}

	if (version > 5 && tvw_reader.u8() == 1) {
		tvw_reader.version(0);
		tvw_reader.u32();
		tvw_reader.u32();
	}

	if (version > 7) {
		tvw_reader.u32();
	}
}

static void tvw_skip_unit_reference(TVWFile::TVWReader &tvw_reader, uint32_t version) {
	tvw_reader.u32();
	tvw_reader.u32();

	if (version) {
		tvw_reader.u8();
	}
}

static void tvw_skip_colour_line(TVWFile::TVWReader &tvw_reader, uint32_t) {
	tvw_reader.skip(8);
	tvw_reader.skip(8);
	tvw_reader.u32();
	tvw_reader.u32();
}

static void tvw_skip_map_remark(TVWFile::TVWReader &tvw_reader, uint32_t version) {
	tvw_reader.u32();
	tvw_reader.u32();
	tvw_reader.skip(8);
	tvw_reader.u32();
	tvw_reader.u32();
	tvw_reader.u32();

	if (version > 1) {
		tvw_reader.u32();
	}
}

static void tvw_skip_list(TVWFile::TVWReader &tvw_reader, void (*record)(TVWFile::TVWReader &, uint32_t), size_t smallest) {
	uint32_t count = tvw_reader.u32();

	if (count > tvw_reader.remaining(smallest)) {
		tvw_reader.ok = false;
		return;
	}

	if (!count) {
		return;
	}

	uint32_t element_version = tvw_reader.u32();

	for (uint32_t i = 0; i < count && tvw_reader.ok; i++) {
		record(tvw_reader, element_version);
	}
}

bool TVWFile::read_layer_tail(TVWReader &tvw_reader) {
	tvw_reader.version(0);

	const uint32_t probe_version = tvw_reader.u32();

	for (int list = 0; list < 2; list++) {
		uint32_t count = tvw_reader.u32();

		if (count > tvw_reader.remaining(18)) {
			tvw_reader.ok = false;
			return false;
		}

		for (uint32_t i = 0; i < count && tvw_reader.ok; i++) {
			tvw_skip_probe(tvw_reader, probe_version);
		}
	}

	const uint32_t tail_version = tvw_reader.version(4);

	if (!tail_version) {
		tvw_reader.ok = false;
		return false;
	}

	tvw_skip_list(tvw_reader, tvw_skip_map_point, 40);
	tvw_skip_list(tvw_reader, tvw_skip_map_point, 40);
	tvw_skip_list(tvw_reader, tvw_skip_unit_reference, 8);
	tvw_skip_list(tvw_reader, [](TVWReader &reader, uint32_t version) { tvw_read_text(reader, version, nullptr); }, 17);

	if (tail_version > 1) {
		tvw_skip_list(tvw_reader, tvw_skip_colour_line, 24);
	}

	if (tail_version > 2) {
		tvw_skip_list(tvw_reader, tvw_skip_map_remark, 28);
	}

	return tvw_reader.ok;
}

bool TVWFile::read_layer_common(TVWReader &tvw_reader, Layer &layer) {
	const uint32_t version = tvw_reader.version(1);

	layer.name = tvw_reader.pstring();
	tvw_reader.pstring();

	if (version) {
		tvw_reader.pstring();
	}

	layer.kind        = tvw_reader.u32();
	layer.line_colour = tvw_reader.u32();
	layer.pad_colour  = tvw_reader.u32();
	return tvw_reader.ok;
}

bool TVWFile::read_apertures(TVWReader &tvw_reader, Layer &layer) {
	const uint32_t declared = tvw_reader.u32();

	if (!tvw_reader.ok || declared > tvw_reader.size) {
		tvw_reader.ok = false;
		return false;
	}

	if (declared <= TVW_FIRST_DCODE) {
		return true;
	}

	if (declared - TVW_FIRST_DCODE > tvw_reader.remaining(4)) {
		tvw_reader.ok = false;
		return false;
	}

	layer.apertures.assign(declared - TVW_FIRST_DCODE, Aperture());

	for (Aperture &aperture : layer.apertures) {
		const uint32_t present = tvw_reader.u32();

		if (!present) {
			continue;
		}

		if (present != 1) {
			tvw_reader.ok = false;
			return false;
		}

		aperture.w     = tvw_reader.i32();
		aperture.h     = tvw_reader.i32();
		aperture.kind  = tvw_reader.u32();
		aperture.angle = tvw_reader.f32();

		if (aperture.kind != APERTURE_CUSTOM) {
			aperture.extra = tvw_reader.i32();
			continue;
		}

		tvw_reader.pstring();
		tvw_reader.skip(16);

		uint32_t primitives = tvw_reader.u32();

		if (primitives > tvw_reader.remaining(16)) {
			tvw_reader.ok = false;
			return false;
		}

		for (uint32_t p = 0; p < primitives && tvw_reader.ok; p++) {
			const uint32_t type = tvw_reader.u32();
			tvw_reader.u32();
			tvw_reader.skip(8);

			if (type == 2) {
				uint32_t points = tvw_reader.u32();

				if (points > tvw_reader.remaining(8)) {
					tvw_reader.ok = false;
					return false;
				}

				if (aperture.outline.empty()) {
					aperture.outline.reserve(points);
					for (uint32_t i = 0; i < points; i++) {
						int32_t x = tvw_reader.i32();
						aperture.outline.emplace_back(x, tvw_reader.i32());
					}
				} else {
					tvw_reader.skip(8 * points);
				}

				continue;
			}

			static const size_t payload[] = {4, 8, 0, 28, 16, 20, 16};

			if (type >= sizeof(payload) / sizeof(payload[0])) {
				tvw_reader.ok = false;
				return false;
			}

			tvw_reader.skip(payload[type]);
		}

		if (aperture.outline.size() > 1 && aperture.outline.front() == aperture.outline.back()) {
			aperture.outline.pop_back();
		}
	}

	return tvw_reader.ok;
}

bool TVWFile::read_object_group(TVWReader &tvw_reader, Layer &layer) {
	tvw_reader.version(0);
	tvw_reader.u32();

	layer.group_pad_start.push_back(layer.pads.size());

	for (int kind = 0; kind < 5 && tvw_reader.ok; kind++) {
		static const size_t smallest[] = {19, 24, 28, 12, 29};

		uint32_t count = tvw_reader.u32();

		if (count > tvw_reader.remaining(smallest[kind])) {
			tvw_reader.ok = false;
			return false;
		}

		if (!count) {
			continue;
		}

		const uint32_t element_version = tvw_reader.u32();

		for (uint32_t i = 0; i < count && tvw_reader.ok; i++) {
			switch (kind) {
				case 0: {
					Pad pad;
					tvw_read_pad(tvw_reader, element_version, pad);
					layer.pads.push_back(pad);
					break;
				}
				case 1: {
					Trace trace;
					tvw_read_line(tvw_reader, element_version, trace);
					layer.traces.push_back(trace);
					break;
				}
				case 2: {
					Arc arc;
					tvw_read_arc(tvw_reader, element_version, arc);
					layer.arcs.push_back(arc);
					break;
				}
				case 3: {
					Surface surface;
					tvw_read_surface(tvw_reader, element_version, surface);
					layer.surfaces.push_back(std::move(surface));
					break;
				}
				default: {
					Text text;
					tvw_read_text(tvw_reader, element_version, &text);
					layer.texts.push_back(std::move(text));
					break;
				}
			}
		}
	}

	return tvw_reader.ok;
}

bool TVWFile::read_graphic_layer(TVWReader &tvw_reader, Layer &layer) {
	tvw_reader.version(3);
	tvw_reader.version(2);

	if (!read_layer_common(tvw_reader, layer) || !read_apertures(tvw_reader, layer)) {
		return false;
	}

	uint32_t groups = tvw_reader.u32();

	if (groups > tvw_reader.remaining(28)) {
		tvw_reader.ok = false;
		return false;
	}

	for (uint32_t g = 0; g < groups && tvw_reader.ok; g++) {
		read_object_group(tvw_reader, layer);
	}

	return tvw_reader.ok && read_layer_tail(tvw_reader);
}

bool TVWFile::read_drill_layer(TVWReader &tvw_reader, Layer &layer) {
	tvw_reader.version(2);
	const uint32_t base_version = tvw_reader.version(1);

	layer.name = tvw_reader.pstring();
	tvw_reader.pstring();

	if (base_version) {
		tvw_reader.pstring();
	}

	layer.kind        = tvw_reader.u32();
	layer.line_colour = tvw_reader.u32();
	layer.pad_colour  = tvw_reader.u32();
	tvw_reader.u32();
	tvw_reader.u32();

	uint32_t tools = tvw_reader.u32();

	if (!tvw_reader.ok || tools > tvw_reader.remaining(1)) {
		tvw_reader.ok = false;
		return false;
	}

	std::vector tool_size(tools, 0);

	for (uint32_t t = 0; t < tools && tvw_reader.ok; t++) {
		if (!tvw_reader.u8()) {
			continue;
		}

		tool_size[t] = tvw_reader.i32();
		for (int f = 0; f < 6; f++) tvw_reader.u32();
	}

	uint32_t holes = tvw_reader.u32();

	if (!tvw_reader.ok || holes > tvw_reader.remaining(17)) {
		tvw_reader.ok = false;
		return false;
	}

	if (holes) {
		if (tvw_reader.u32() != 4) {
			tvw_reader.ok = false;
			return false;
		}

		for (int c = 0; c < 4; c++) {
			tvw_reader.u32();
		}
	}

	layer.drills.reserve(holes);

	for (uint32_t h = 0; h < holes && tvw_reader.ok; h++) {
		const uint32_t kind = tvw_reader.u8();

		if (kind < 8 || kind > 11) {
			tvw_reader.ok = false;
			return false;
		}

		Drill drill;
		drill.tool = tvw_reader.u32();
		drill.net  = tvw_reader.u32();
		drill.x    = tvw_reader.i32();
		drill.y    = tvw_reader.i32();

		switch (kind) {
			case 9: tvw_reader.skip(8); break;
			case 10: tvw_reader.skip(12); break;
			case 11: tvw_reader.skip(12); break;
			default: break;
		}

		if (drill.tool < tool_size.size()) {
			drill.size = tool_size[drill.tool];
		}

		layer.drills.push_back(drill);
	}

	return tvw_reader.ok;
}

bool TVWFile::read_layer(TVWReader &tvw_reader, Layer &layer, uint32_t type) {
	layer.type = type;

	switch (type) {
		case 0: return read_graphic_layer(tvw_reader, layer);
		case 1: return read_drill_layer(tvw_reader, layer);
		default: tvw_reader.ok = false; return false;
	}
}

bool TVWFile::read_layers(TVWReader &tvw_reader) {
	tvw_reader.version(2);

	const uint32_t declared = tvw_reader.u32();

	if (!tvw_reader.ok || declared > tvw_reader.remaining(4)) {
		return false;
	}

	header.declared_layers = declared;
	layers.reserve(declared);

	for (uint32_t i = 0; i < declared; i++) {
		const uint32_t type = tvw_reader.u32();

		if (!tvw_reader.ok) {
			return false;
		}

		layers.emplace_back();

		if (!read_layer(tvw_reader, layers.back(), type)) {
			layers.pop_back();
			return false;
		}
	}

	return true;
}

bool TVWFile::find_nets(TVWReader &tvw_reader) {
	const size_t start = tvw_reader.pos;

	for (size_t position = start; position + 12 < tvw_reader.size; position++) {
		const uint32_t count = twv_get_word(tvw_reader.data, position);

		if (count < 2 || count > (tvw_reader.size - position) / 2 || twv_get_word(tvw_reader.data, position + 4) != count) {
			continue;
		}

		TVWReader probe(tvw_reader);
		probe.pos = position + 8;
		probe.ok  = true;

		std::vector<std::string> names;
		names.reserve(count);

		for (uint32_t i = 0; i < count && probe.ok; i++) {
			names.push_back(probe.pstring());
		}

		if (!probe.ok) {
			continue;
		}

		bool plausible = true;
		for (const std::string &name : names) {
			if (name.empty()) {
				plausible = false;
				break;
			}

			for (unsigned char c : name)
				if (c < 0x20 || c > 0x7e) {
					plausible = false;
					break;
				}

			if (!plausible) {
				break;
			}
		}

		if (!plausible) {
			continue;
		}

		net_names = std::move(names);
		tvw_reader.pos     = probe.pos;
		return true;
	}

	return false;
}

bool TVWFile::read_nets(TVWReader &tvw_reader) {
	uint32_t units = tvw_reader.u32();

	if (units) {
		tvw_reader.ok = false;
		return false;
	}

	tvw_reader.u32();

	tvw_reader.version(0);
	if (tvw_reader.u32()) {
		tvw_reader.ok = false;
		return false;
	}

	tvw_reader.u32();

	uint32_t names = tvw_reader.u32();

	if (!tvw_reader.ok || names > tvw_reader.remaining(1)) {
		tvw_reader.ok = false;
		return false;
	}

	net_names.reserve(names);
	for (uint32_t i = 0; i < names && tvw_reader.ok; i++) {
		net_names.push_back(tvw_reader.pstring());
	}

	return tvw_reader.ok;
}

bool TVWFile::read_header(TVWReader &tvw_reader) {
	ENSURE_OR_FAIL(tvw_decode_string(tvw_reader.pstring()) == TVW_MAGIC, error_msg, return false);

	header.version = tvw_reader.version(1);
	header.vendor  = tvw_decode_string(tvw_reader.pstring());
	header.model   = tvw_decode_string(tvw_reader.pstring());
	header.date    = tvw_decode_string(tvw_reader.pstring());

	for (int i = 0; i < 3; i++) {
		tvw_reader.pstring();
	}

	header.document_version = tvw_reader.u32();
	tvw_reader.version(2);

	ENSURE_OR_FAIL(tvw_reader.ok, error_msg, return false);
	return true;
}

bool TVWFile::Layer::is_silkscreen() const {
	return name.find("SILK") != std::string::npos;
}

const TVWFile::Pad *TVWFile::Layer::pad_for(uint32_t handle, uint32_t group) const {
	if (handle == TVW_HANDLE_NONE || (handle & TVW_HANDLE_KIND_MASK) != TVW_HANDLE_KIND_PAD) {
		return nullptr;
	}

	if (group >= group_pad_start.size()) {
		return nullptr;
	}

	const size_t index = group_pad_start[group] + (handle >> TVW_HANDLE_KIND_SHIFT);
	const size_t end   = group + 1 < group_pad_start.size() ? group_pad_start[group + 1] : pads.size();

	return index < end ? &pads[index] : nullptr;
}

const char *TVWFile::net_name(uint32_t index) {
	if (pooled_nets.empty() && !net_names.empty()) {
		pooled_nets.reserve(net_names.size());

		for (const std::string &name : net_names) {
			pooled_nets.push_back(pool(name));
		}
	}

	return index < pooled_nets.size() ? pooled_nets[index] : nullptr;
}

const char *TVWFile::pool(const std::string &string) {
	string_pool.push_back(string);
	return string_pool.back().c_str();
}

uint32_t TVWFile::walk_parts(const std::vector<char> &brd_buffer, size_t first_position, uint32_t count, std::vector<PartRecord> *output) {
	const uint8_t *brd_data = reinterpret_cast<const uint8_t *>(brd_buffer.data());
	const size_t size       = brd_buffer.size();

	uint32_t parsed        = 0;
	size_t position_offset = first_position;

	while (parsed < count) {
		TVWReader tvw_reader(brd_buffer);
		tvw_reader.pos = position_offset;

		PartRecord part_record;
		part_record.name = tvw_reader.pstring();

		if (part_record.name.empty() || !tvw_reader.ok) {
			break;
		}

		auto x1 = tvw_reader.i32();
		auto y1 = tvw_reader.i32();
		auto x2 = tvw_reader.i32();
		auto y2 = tvw_reader.i32();
		part_record.w  = std::abs(x2 - x1);
		part_record.h  = std::abs(y2 - y1);
		part_record.cx = tvw_reader.i32();
		part_record.cy = tvw_reader.i32();
		tvw_reader.u32();

		for (int f = 0; f < 4; f++) {
			tvw_reader.u32();
		}

		uint8_t flag = tvw_reader.u8();

		if (flag > 1) {
			break;
		}

		if (flag == 1) {
			part_record.value  = tvw_reader.pstring();
			part_record.rating = tvw_reader.pstring();
			tvw_reader.pstring();
			part_record.package = tvw_reader.pstring();
			part_record.mfgcode = tvw_reader.pstring();
		}

		tvw_reader.u32();

		uint32_t pin_count = tvw_reader.u32();
		uint32_t layer     = tvw_reader.u32();
		bool broken        = false;

		auto pin_count_fits = [&](uint32_t value) {
			return value <= TVW_MAX_PART_PINS && value <= (size - tvw_reader.pos) / TVW_MIN_PIN_RECORD;
		};

		while (true) {
			if (!tvw_reader.ok || !pin_count_fits(pin_count)) {
				broken = true;
				break;
			}

			for (uint32_t pin = 0; pin < pin_count; pin++) {
				PartRecord::Pin rp;
				tvw_read_pad_reference(tvw_reader, &rp.handle, &rp.group);
				rp.layer = layer;
				tvw_reader.u32(); // the pin number
				rp.name = tvw_reader.pstring();
				part_record.pins.push_back(std::move(rp));
			}

			if (!tvw_reader.ok) {
				broken = true;
				break;
			}

			if (size - tvw_reader.pos < 4) {
				break;
			}

			uint32_t next = twv_get_word(brd_data, tvw_reader.pos);
			if (next == 0) {
				tvw_reader.u32();
				break;
			}

			if (size - tvw_reader.pos >= 8 && pin_count_fits(next) && twv_get_word(brd_data, tvw_reader.pos + 4) < 64) {
				pin_count = tvw_reader.u32();
				layer     = tvw_reader.u32();
				continue;
			}

			break;
		}
		if (broken || !tvw_reader.ok) {
			break;
		}

		if (output) {
			output->push_back(std::move(part_record));
		}

		position_offset = tvw_reader.pos;
		parsed++;
	}

	return parsed;
}

void TVWFile::parse_parts(const std::vector<char> &brd_buffer, size_t search_from) {
	const uint8_t *brd_data = reinterpret_cast<const uint8_t *>(brd_buffer.data());
	const size_t size       = brd_buffer.size();

	size_t table      = 0;
	uint32_t declared = 0;

	for (size_t position = search_from; position + 16 < size; position++) {
		uint32_t count = twv_get_word(brd_data, position);
		if (count < TVW_MIN_PART_COUNT || count > 100000) {
			continue;
		}

		if (count <= declared) {
			continue;
		}

		size_t first = position + 8;
		uint8_t len  = brd_data[first];

		if (len < 1 || len > 16 || first + 1 + len >= size) {
			continue;
		}

		if (!isalpha(brd_data[first + 1])) {
			continue;
		}

		if (walk_parts(brd_buffer, first, count, nullptr) == count) {
			table    = position;
			declared = count;
		}
	}

	if (table == 0) {
		return;
	}

	std::vector<PartRecord> records;
	records.reserve(declared);
	walk_parts(brd_buffer, table + 8, declared, &records);

	uint32_t top_layer = UINT32_MAX, bottom_layer = 0;

	for (const PartRecord &record : records)
		for (const PartRecord::Pin &pin : record.pins) {
			if (pin.layer < top_layer) {
				top_layer = pin.layer;
			}

			if (pin.layer > bottom_layer) {
				bottom_layer = pin.layer;
			}
		}

	for (const PartRecord &record : records) {
		BRDPart part;
		part.name      = pool(record.name);
		part.mfgcode   = record.mfgcode;
		part.value     = record.value;
		part.rating    = record.rating;
		part.package   = record.package;
		part.part_type = BRDPartType::SMD;
		part.p1 = BRDPoint((record.cx - record.w / 2) / TVW_COORD_SCALE, (record.cy - record.h / 2) / TVW_COORD_SCALE);
		part.p2 = BRDPoint((record.cx + record.w / 2) / TVW_COORD_SCALE, (record.cy + record.h / 2) / TVW_COORD_SCALE);

		bool on_top = false,
			on_bottom = false;

		for (const PartRecord::Pin &record_pin : record.pins) {
			const bool bottom = record_pin.layer == bottom_layer && bottom_layer != top_layer;

			if (bottom) {
				on_bottom = true;
			} else {
				on_top = true;
			}

			if (record_pin.layer >= layers.size()) {
				continue;
			}

			const Layer &layer = layers[record_pin.layer];
			const Pad *found   = layer.pad_for(record_pin.handle, record_pin.group);

			if (!found) {
				continue;
			}

			const Pad &pad = *found;

			BRDPin pin;
			pin.part = parts.size() + 1;
			pin.pos  = BRDPoint(pad.x / TVW_COORD_SCALE, pad.y / TVW_COORD_SCALE);
			pin.side = bottom ? BRDPinSide::Bottom : BRDPinSide::Top;
			pin.snum = pool(record_pin.name);

			if (pad.net < net_names.size()) {
				if (const char *name = net_name(pad.net)) pin.net = name;
			}

			const Aperture *aperture = nullptr;
			if (pad.dcode >= TVW_FIRST_DCODE) {
				size_t aperture_index = pad.dcode - TVW_FIRST_DCODE;
				if (aperture_index < layer.apertures.size()) {
					aperture = &layer.apertures[aperture_index];
				}
			}

			int32_t width = 0, height = 0;
			if (pad.has_extent) {
				width  = std::abs(pad.x2 - pad.x1);
				height = std::abs(pad.y2 - pad.y1);
			} else if (aperture) {
				width  = aperture->w;
				height = aperture->h;
			}

			if (width > 0 && height > 0) {
				pin.width  = static_cast<double>(width) / TVW_COORD_SCALE;
				pin.height = static_cast<double>(height) / TVW_COORD_SCALE;
				pin.radius = std::min(width, height) / 2.0 / TVW_COORD_SCALE;

				bool plausible = aperture && aperture->w >= width && aperture->h >= height &&
				                 aperture->w <= 2 * width && aperture->h <= 2 * height;

				if (plausible && aperture->kind == APERTURE_CUSTOM && aperture->outline.size() >= 3) {
					pin.pad_shape = BRDPadShape::Outline;
					pin.shape.reserve(aperture->outline.size());

					for (const auto &vertex : aperture->outline) {
						pin.shape.emplace_back(static_cast<float>(vertex.first) / TVW_COORD_SCALE,
											   static_cast<float>(vertex.second) / TVW_COORD_SCALE);
					}
				} else if (plausible && aperture->angle != 0.0f &&
				           (aperture->kind == APERTURE_RECTANGLE || aperture->kind == APERTURE_ROUNDED)) {
					const double radians = aperture->angle * M_PI / 180.0;
					const double cosine = cos(radians), sine = sin(radians);
					const double half_w = width / 2.0 / TVW_COORD_SCALE;
					const double half_h = height / 2.0 / TVW_COORD_SCALE;
					const double corners[4][2] = {{-half_w, -half_h}, {half_w, -half_h}, {half_w, half_h}, {-half_w, half_h}};

					pin.pad_shape = BRDPadShape::Outline;
					pin.shape.reserve(4);

					for (const auto &corner : corners) {
						pin.shape.emplace_back(static_cast<float>(corner[0] * cosine - corner[1] * sine),
						                       static_cast<float>(corner[0] * sine + corner[1] * cosine));
					}
				} else if (plausible) {
					switch (aperture->kind) {
						case APERTURE_CIRCLE: pin.pad_shape = BRDPadShape::Circle; break;
						case APERTURE_RECTANGLE: pin.pad_shape = BRDPadShape::Rectangle; break;
						case APERTURE_ROUNDED: pin.pad_shape = aperture->is_obround() ? BRDPadShape::Obround : BRDPadShape::Rectangle; break;
						case APERTURE_DONUT: pin.pad_shape = BRDPadShape::Circle; break;
						default: break;
					}
				}
			}

			if (pad.has_drill) {
				part.part_type = BRDPartType::ThroughHole;
			}

			pins.push_back(pin);
		}

		part.mounting_side = (on_top && on_bottom)
									? BRDPartMountingSide::Both
									: on_bottom
											? BRDPartMountingSide::Bottom
											: BRDPartMountingSide::Top;

		part.end_of_pins = pins.size();
		parts.push_back(part);
	}
}

void TVWFile::build_layers() {
	std::unordered_set<uint64_t> pin_positions;
	pin_positions.reserve(pins.size() * 2);

	auto key = [](int32_t x, int32_t y) {
		return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(y);
	};

	for (const BRDPin &pin : pins) {
		pin_positions.insert(key(pin.pos.x, pin.pos.y));
	}

	std::unordered_map<uint64_t, int32_t> drill_of;

	for (const Layer &layer : layers) {
		for (const Drill &drill : layer.drills) {
			if (drill.size > 0) {
				drill_of[key(drill.x, drill.y)] = drill.size;
			}
		}

		for (const Pad &pad : layer.pads) {
			if (pad.has_drill) {
				drill_of.emplace(key(pad.x + pad.drill_offset_x, pad.y + pad.drill_offset_y),
				                 std::min(pad.drill_cx, pad.drill_cy));
			}
		}
	}

	for (size_t index = 0; index < layers.size(); index++) {
		const Layer &source = layers[index];

		if (source.traces.empty() && source.arcs.empty() && source.surfaces.empty() && source.texts.empty()) {
			continue;
		}

		BRDLayer layer;
		layer.name = source.name.empty() ? "Layer " + std::to_string(index) : source.name;

		auto trace_width = [&](uint32_t dcode) {
			if (dcode >= TVW_FIRST_DCODE) {
				const size_t aperture = dcode - TVW_FIRST_DCODE;
				if (aperture < source.apertures.size()) {
					return std::min(source.apertures[aperture].w, source.apertures[aperture].h) /
					       static_cast<double>(TVW_COORD_SCALE);
				}
			}

			return 0.0;
		};

		layer.traces.reserve(source.traces.size());
		for (const Trace &trace : source.traces) {
			BRDTrace out;
			out.from  = BRDPoint(trace.x1 / TVW_COORD_SCALE, trace.y1 / TVW_COORD_SCALE);
			out.to    = BRDPoint(trace.x2 / TVW_COORD_SCALE, trace.y2 / TVW_COORD_SCALE);
			out.width = trace_width(trace.dcode);
			out.net   = net_name(trace.net);
			layer.traces.push_back(out);
		}

		for (const Arc &arc : source.arcs) {
			double from = arc.start_angle;
			double span = arc.sweep;

			if (span < 0.0) {
				from += span;
				span = -span;
			}

			const int segments = std::max(TVW_ARC_MIN_SEGMENTS, static_cast<int>(std::ceil(span / TVW_ARC_DEGREES_PER_SEGMENT)));
			const double width = trace_width(arc.dcode);
			const char *net    = net_name(arc.net);

			auto point_at = [&](double degrees) {
				const double radians = degrees * M_PI / 180.0;
				return BRDPoint(static_cast<int>(arc.cx + arc.radius * cos(radians)) / TVW_COORD_SCALE,
				                static_cast<int>(arc.cy + arc.radius * sin(radians)) / TVW_COORD_SCALE);
			};

			BRDPoint previous = point_at(from);

			for (int i = 1; i <= segments; i++) {
				BRDTrace out;
				out.from  = previous;
				out.to    = point_at(from + span * i / segments);
				out.width = width;
				out.net   = net;

				previous = out.to;
				layer.traces.push_back(out);
			}
		}

		layer.pours.reserve(source.surfaces.size());
		for (const Surface &surface : source.surfaces) {
			BRDPour pour;
			pour.net = net_name(surface.net);

			pour.outline.reserve(surface.outline.size());
			for (const auto &vertex : surface.outline) {
				pour.outline.emplace_back(vertex.first / TVW_COORD_SCALE, vertex.second / TVW_COORD_SCALE);
			}

			pour.cutouts.reserve(surface.cutouts.size());
			for (const auto &cutout : surface.cutouts) {
				std::vector<BRDPoint> hole;
				hole.reserve(cutout.size());

				for (const auto &vertex : cutout) {
					hole.emplace_back(vertex.first / TVW_COORD_SCALE, vertex.second / TVW_COORD_SCALE);
				}

				pour.cutouts.push_back(std::move(hole));
			}

			pour.build_fill();
			layer.pours.push_back(std::move(pour));
		}

		layer.texts.reserve(source.texts.size());
		for (const Text &text : source.texts) {
			if (text.text.empty() || text.height <= 0) {
				continue;
			}

			BRDText out;
			out.text     = text.text;
			out.pos      = BRDPoint(text.x / TVW_COORD_SCALE, text.y / TVW_COORD_SCALE);
			out.height   = static_cast<double>(text.height) / TVW_COORD_SCALE;
			out.mirrored = text.mirrored;
			layer.texts.push_back(std::move(out));
		}

		for (const Pad &pad : source.pads) {
			if (pin_positions.count(key(pad.x / TVW_COORD_SCALE, pad.y / TVW_COORD_SCALE))) {
				continue;
			}

			const char *net = net_name(pad.net);

			if (net && !strncmp(net, "NC_", 3)) {
				continue;
			}

			int32_t width = 0, height = 0;

			if (pad.has_extent) {
				width  = std::abs(pad.x2 - pad.x1);
				height = std::abs(pad.y2 - pad.y1);
			} else if (pad.dcode >= TVW_FIRST_DCODE && pad.dcode - TVW_FIRST_DCODE < source.apertures.size()) {
				const Aperture &aperture = source.apertures[pad.dcode - TVW_FIRST_DCODE];
				width                    = aperture.w;
				height                   = aperture.h;
			}

			if (width <= 0 || height <= 0) {
				continue;
			}

			BRDVia via;
			via.pos        = BRDPoint(pad.x / TVW_COORD_SCALE, pad.y / TVW_COORD_SCALE);
			via.pad_radius = std::min(width, height) / 2.0 / TVW_COORD_SCALE;
			via.net        = net;

			auto drill = drill_of.find(key(pad.x, pad.y));
			if (drill != drill_of.end()) {
				const double radius = drill->second / 2.0 / TVW_COORD_SCALE;

				if (radius < via.pad_radius) {
					via.hole_radius = radius;
				}
			}

			layer.vias.push_back(via);
		}

		layer.build_paths();
		board_layers.push_back(std::move(layer));
	}
}

TVWFile::TVWFile(std::vector<char> &brd_buffer) {
	TVWReader r(brd_buffer);

	if (!read_header(r)) {
		return;
	}

	const bool layers_complete = read_layers(r);

	ENSURE_OR_FAIL(!layers.empty(), error_msg, return);

	if (layers_complete) {
		if (read_nets(r)) {
			parse_parts(brd_buffer, r.pos);
		}
	} else if (find_nets(r)) {
		parse_parts(brd_buffer, r.pos);
	}

	for (const Layer &layer : layers) {
		if (layer.name != "OUTLINE") {
			continue;
		}

		for (const Trace &trace : layer.traces) {
			outline_segments.emplace_back(BRDPoint(trace.x1 / TVW_COORD_SCALE, trace.y1 / TVW_COORD_SCALE),
			                              BRDPoint(trace.x2 / TVW_COORD_SCALE, trace.y2 / TVW_COORD_SCALE));
		}

		for (const Arc &arc : layer.arcs) {
			double from = arc.start_angle;
			double span = arc.sweep;

			if (span < 0.0) {
				from += span;
				span = -span;
			}

			const int segments = std::max(TVW_ARC_MIN_SEGMENTS, static_cast<int>(std::ceil(span / TVW_ARC_DEGREES_PER_SEGMENT)));

			auto point_at = [&](double degrees) {
				const double radians = degrees * M_PI / 180.0;
				return BRDPoint(static_cast<int>(arc.cx + arc.radius * cos(radians)) / TVW_COORD_SCALE,
				                static_cast<int>(arc.cy + arc.radius * sin(radians)) / TVW_COORD_SCALE);
			};

			BRDPoint previous = point_at(from);

			for (int i = 1; i <= segments; i++) {
				BRDPoint next = point_at(from + span * i / segments);
				outline_segments.emplace_back(previous, next);
				previous = next;
			}
		}

		break;
	}

	build_layers();

	if (parts.empty() || pins.empty()) {
		if (net_names.empty()) {
			error_msg = "TVW: this file is incomplete. It carries " + std::to_string(layers.size()) + " of the " +
			            std::to_string(header.declared_layers) +
			            " layers it declares, and ends before its netlist and parts table.";
		} else {
			error_msg = "TVW: this file uses a layout that is not supported yet (" +
			            (header.vendor.empty() ? std::string("unknown vendor") : header.vendor) + ", " +
			            std::to_string(layers.size()) + " layers, " + std::to_string(parts.size()) + " parts, " +
			            std::to_string(pins.size()) + " pins).";
		}

		return;
	}

	valid      = true;
	num_parts  = parts.size();
	num_pins   = pins.size();
	num_format = format.size();
	num_nails  = nails.size();
}

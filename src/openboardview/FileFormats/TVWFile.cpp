#include "TVWFile.h"

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "utils.h"

static const char *TVW_MAGIC = "Tebo-ictview files.";

// Coordinates are stored in hundredths of a mil, BRDPoint holds mils.
static const int TVW_COORD_SCALE = 100;

static const uint32_t TVW_FIRST_DCODE = 10;

// Smallest a pin record can be: four words and an empty name.
static const size_t TVW_MIN_PIN_RECORD = 17;

// Smallest a surface cutout can be: a point count, two points and a trailing word.
static const size_t TVW_MIN_CUTOUT_RECORD = 24;

// Net, aperture, centre, radius, start angle, sweep.
static const size_t TVW_ARC_RECORD = 28;

static const double TVW_ARC_DEGREES_PER_SEGMENT = 5.0;
static const int TVW_ARC_MIN_SEGMENTS           = 3;

static const uint32_t TVW_MAX_PART_PINS = 16384;

static const int32_t TVW_PIN_BBOX_SLACK     = 500;
static const double TVW_MIN_PAD_LAYER_SCORE = 0.9;

static const uint32_t TVW_MIN_PART_COUNT = 4;

static const uint32_t TVW_MAX_LAYER = 256;

static const uint8_t TVW_LAYER_ANCHOR[] = {0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
static const uint32_t TVW_PRIM_MARKER = 0xffffffffu;

//The header strings are obfuscated with a position dependent Caesar shift.
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

static BRDPoint tvw_get_point_at(const uint8_t *brd_data, size_t position) {
	int32_t x = static_cast<int32_t>(twv_get_word(brd_data, position)) / TVW_COORD_SCALE;
	int32_t y = static_cast<int32_t>(twv_get_word(brd_data, position + 4)) / TVW_COORD_SCALE;
	return {x, y};
}

static size_t tvw_aligned_prim_size(uint32_t type) {
	switch (type) {
		case 10:
		case 11:
		case 12:
		case 13: return 24;
		case 5: return 56;
		case 25: return 216;
		default: return 0;
	}
}

static size_t tvw_walk_aligned(const uint8_t *brd_data, size_t position, size_t end_position,
                               std::vector<std::pair<BRDPoint, BRDPoint>> *output) {
	while (position + 8 <= end_position && twv_get_word(brd_data, position) == TVW_PRIM_MARKER) {
		size_t encoding_size = tvw_aligned_prim_size(twv_get_word(brd_data, position + 4));

		if (encoding_size == 0 || encoding_size > end_position - position) {
			break;
		}

		if (output && encoding_size == 24) {
			output->emplace_back(tvw_get_point_at(brd_data, position + 8), tvw_get_point_at(brd_data, position + 16));
		}

		position += encoding_size;
	}

	return position;
}

struct TVWArc {
	BRDPoint centre;
	int radius        = 0;
	float start_angle = 0.0f;
	float end_angle   = 0.0f;
};

static float tvw_f32_at(const uint8_t *brd_data, size_t position) {
	uint32_t word = twv_get_word(brd_data, position);
	float value;
	memcpy(&value, &word, sizeof(value));
	return value;
}

static size_t tvw_walk_packed(const uint8_t *brd_data, size_t position, size_t end_position,
                              std::vector<std::pair<BRDPoint, BRDPoint>> *output, std::vector<TVWArc> *arcs) {
	if (position == 0) {
		return position;
	}

	uint8_t hint = brd_data[position - 1];

	while (position + 8 <= end_position && twv_get_word(brd_data, position) == TVW_PRIM_MARKER) {
		size_t body = position + 8;
		if (hint == 0x0a) {
			if (end_position - body < 21) {
				break;
			}

			if (output) {
				output->emplace_back(tvw_get_point_at(brd_data, body), tvw_get_point_at(brd_data, body + 8));
			}

			body += 20;
		} else if (hint == 0x08) {
			if (end_position - body < 9) {
				break;
			}

			body += 8;
		} else if (hint == 0x0b) {
			if (end_position - body < 25) {
				break;
			}

			if (arcs) {
				TVWArc arc;
				arc.centre      = tvw_get_point_at(brd_data, body);
				arc.radius      = static_cast<int32_t>(twv_get_word(brd_data, body + 8)) / TVW_COORD_SCALE;
				arc.start_angle = tvw_f32_at(brd_data, body + 16);
				arc.end_angle   = tvw_f32_at(brd_data, body + 20);
				arcs->push_back(arc);
			}

			body += 24;
		} else {
			break;
		}

		hint = brd_data[body];
		position = body + 1;
	}

	return position;
}

bool TVWFile::verifyFormat(const std::vector<char> &brd_buffer) {
	TVWReader tvw_reader(brd_buffer);
	std::string magic = tvw_decode_string(tvw_reader.pstring());
	return tvw_reader.ok && magic == TVW_MAGIC;
}

bool TVWFile::parse_header(TVWReader &tvw_reader) {
	ENSURE_OR_FAIL(tvw_decode_string(tvw_reader.pstring()) == TVW_MAGIC, error_msg, return false);

	header.version = tvw_reader.u32();
	header.vendor  = tvw_decode_string(tvw_reader.pstring());
	header.model   = tvw_decode_string(tvw_reader.pstring());
	header.date    = tvw_decode_string(tvw_reader.pstring());

	for (int i = 0; i < 3; i++) {
		tvw_reader.pstring();
	}

	for (uint32_t &field : header.fields) {
		field = tvw_reader.u32();
	}

	ENSURE_OR_FAIL(tvw_reader.ok, error_msg, return false);
	return true;
}

void TVWFile::parse_layers(const std::vector<char> &brd_buffer) {
	const uint8_t *brd_data = reinterpret_cast<const uint8_t *>(brd_buffer.data());
	const size_t brd_size = brd_buffer.size();
	const size_t anchor_length = sizeof(TVW_LAYER_ANCHOR);

	struct Candidate {
		size_t body;
		size_t anchor;
		std::string name;
	};

	std::vector<Candidate> found;

	for (size_t position = 0; position + anchor_length < brd_size; position++) {
		if (memcmp(brd_data + position, TVW_LAYER_ANCHOR, anchor_length) != 0) {
			continue;
		}

		size_t position_anchor_offset = position + anchor_length;
		uint8_t length = brd_data[position_anchor_offset];
		if (length == 0 || length > 40 || position_anchor_offset + 1 + length >= brd_size) {
			continue;
		}

		const uint8_t *name = brd_data + position_anchor_offset + 1;
		size_t repeat = position_anchor_offset + 1 + length;

		if (repeat + 1 + length >= brd_size) {
			continue;
		}

		if (brd_data[repeat] != length || memcmp(brd_data + repeat + 1, name, length) != 0) {
			continue;
		}

		size_t path = repeat + 1 + length;

		if (path >= brd_size || brd_data[path] < 20) {
			continue;
		}

		size_t body = path + 1 + brd_data[path];
		if (body >= brd_size) {
			continue;
		}

		found.push_back({body, position, std::string(reinterpret_cast<const char *>(name), length)});
	}

	for (size_t i = 0; i < found.size(); i++) {
		const size_t end = (i + 1 < found.size())
								? found[i + 1].anchor
								: brd_size;

		Layer layer;
		layer.name  = found[i].name;
		layer.begin = found[i].anchor;
		layer.body  = found[i].body;
		layer.end   = end;

		size_t body = found[i].body;

		if (body + 12 <= end) {
			layer.colour = twv_get_word(brd_data, body + 4);
		}

		size_t start_position = body;

		while (start_position + 4 <= end && twv_get_word(brd_data, start_position) != TVW_PRIM_MARKER) {
			start_position++;
		}

		if (start_position + 8 <= end) {
			std::vector<std::pair<BRDPoint, BRDPoint>> aligned, packed;
			std::vector<TVWArc> arcs;
			size_t aligned_end_position = tvw_walk_aligned(brd_data, start_position, end, &aligned);
			size_t packed_end_position  = tvw_walk_packed(brd_data, start_position, end, &packed, &arcs);

			if (packed_end_position > aligned_end_position) {
				layer.segments = std::move(packed);

				for (const TVWArc &arc : arcs) {
					double start_rad = arc.start_angle * M_PI / 180.0;
					double end_rad   = arc.end_angle * M_PI / 180.0;

					if (end_rad < start_rad) {
						std::swap(start_rad, end_rad);
					}

					BRDPoint p1(arc.centre.x + static_cast<int>(arc.radius * cos(start_rad)),
					            arc.centre.y + static_cast<int>(arc.radius * sin(start_rad)));
					BRDPoint p2(arc.centre.x + static_cast<int>(arc.radius * cos(end_rad)),
					            arc.centre.y + static_cast<int>(arc.radius * sin(end_rad)));

					for (const auto &segment : arc_to_segments(start_rad, end_rad, arc.radius, p1, p2, arc.centre)) {
						layer.segments.push_back(segment);
					}
				}
			} else {
				layer.segments = std::move(aligned);
			}
		}

		layers.push_back(std::move(layer));
	}
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

bool TVWFile::walk_pads(const uint8_t *brd_data, size_t brd_size, size_t position, uint32_t count, std::vector<Pad> *output,
                        size_t *end_position) {
	for (uint32_t i = 0; i < count; i++) {
		if (position >= brd_size || brd_size - position < 19) {
			return false;
		}

		Pad pad;
		pad.net   = twv_get_word(brd_data, position);
		pad.dcode = twv_get_word(brd_data, position + 4);
		pad.x     = static_cast<int32_t>(twv_get_word(brd_data, position + 8));
		pad.y     = static_cast<int32_t>(twv_get_word(brd_data, position + 12));

		size_t position_offset = position + 19;
		if (brd_data[position + 17] != 0) {
			if (brd_data[position + 18] & 1) {
				position_offset += 12;
			}

			for (int block = 0; block < 2; block++) {
				if (position_offset >= brd_size) {
					return false;
				}

				uint8_t present = brd_data[position_offset++];

				if (present == 0) {
					continue;
				}

				if (present != 1) {
					return false;
				}

				if (brd_size - position_offset < 16) {
					return false;
				}

				if (block == 0) {
					pad.has_extent = true;
					pad.x1         = static_cast<int32_t>(twv_get_word(brd_data, position_offset));
					pad.y1         = static_cast<int32_t>(twv_get_word(brd_data, position_offset + 4));
					pad.x2         = static_cast<int32_t>(twv_get_word(brd_data, position_offset + 8));
					pad.y2         = static_cast<int32_t>(twv_get_word(brd_data, position_offset + 12));
				} else {
					pad.has_drill      = true;
					pad.drill_offset_x = static_cast<int32_t>(twv_get_word(brd_data, position_offset));
					pad.drill_offset_y = static_cast<int32_t>(twv_get_word(brd_data, position_offset + 4));
					pad.drill_cx       = static_cast<int32_t>(twv_get_word(brd_data, position_offset + 8));
					pad.drill_cy       = static_cast<int32_t>(twv_get_word(brd_data, position_offset + 12));
				}
				position_offset += 16;
			}
			position_offset++;
		}
		if (position_offset > brd_size) {
			return false;
		}

		if (output) {
			output->push_back(pad);
		}

		position = position_offset;
	}

	if (end_position) {
		*end_position = position;
	}

	return true;
}

bool TVWFile::walk_apertures(const uint8_t *brd_data, size_t position, size_t end_position, uint32_t declared,
                             std::vector<Aperture> *output) {
	if (declared < TVW_FIRST_DCODE || declared - TVW_FIRST_DCODE > (end_position - position) / 4) {
		return false;
	}

	std::vector<Aperture> apertures(declared - TVW_FIRST_DCODE);

	for (uint32_t slot = 0; slot < declared - TVW_FIRST_DCODE; slot++) {
		if (position + 4 > end_position) {
			return false;
		}

		uint32_t present = twv_get_word(brd_data, position);
		position += 4;

		if (present == 0) {
			continue;
		}

		if (present != 1 || position + 16 > end_position) {
			return false;
		}

		Aperture &aperture = apertures[slot];
		aperture.w         = static_cast<int32_t>(twv_get_word(brd_data, position));
		aperture.h         = static_cast<int32_t>(twv_get_word(brd_data, position + 4));
		aperture.kind      = twv_get_word(brd_data, position + 8);
		aperture.angle     = tvw_f32_at(brd_data, position + 12);
		position += 16;

		if (aperture.kind != APERTURE_CUSTOM) {
			if (position + 4 > end_position) {
				return false;
			}

			aperture.extra = static_cast<int32_t>(twv_get_word(brd_data, position));
			position += 4;
			continue;
		}

		size_t at = position;

		if (at >= end_position) {
			return false;
		}

		uint8_t name_length = brd_data[at];
		at += 1 + name_length;
		at += 16;

		if (at + 4 > end_position) {
			return false;
		}

		uint32_t sub_shapes = twv_get_word(brd_data, at);
		at += 4;

		if (sub_shapes > 64) {
			return false;
		}

		for (uint32_t sub = 0; sub < sub_shapes; sub++) {
			if (at + 4 > end_position) {
				return false;
			}

			uint32_t type = twv_get_word(brd_data, at);
			at += 4;

			if (type == 2) {
				if (at + 16 > end_position) {
					return false;
				}

				uint32_t points = twv_get_word(brd_data, at + 12);
				at += 16;

				if (points > 20000 || at + points * 8 > end_position) {
					return false;
				}

				if (aperture.outline.empty()) {
					aperture.outline.reserve(points);
					for (uint32_t point = 0; point < points; point++)
						aperture.outline.emplace_back(static_cast<int32_t>(twv_get_word(brd_data, at + point * 8)),
						                              static_cast<int32_t>(twv_get_word(brd_data, at + point * 8 + 4)));
				}

				at += points * 8;
			} else {
				static const size_t payload[] = {4, 8, 0, 28, 16, 20, 16};

				if (type >= sizeof(payload) / sizeof(payload[0])) {
					return false;
				}

				at += 12 + payload[type];

				if (at > end_position) {
					return false;
				}
			}
		}

		if (aperture.outline.size() > 1 && aperture.outline.front() == aperture.outline.back())
			aperture.outline.pop_back();

		position = at;
	}

	if (position != end_position) {
		return false;
	}

	if (output) {
		*output = std::move(apertures);
	}

	return true;
}

bool TVWFile::walk_traces(const uint8_t *brd_data, size_t brd_size, size_t position, std::vector<Trace> *output,
                          size_t *end_position) {
	if (position + 8 > brd_size) {
		return false;
	}

	uint32_t count = twv_get_word(brd_data, position);
	if (twv_get_word(brd_data, position + 4) != 0) {
		return false;
	}

	position += 8;
	if (count > (brd_size - position) / 24) {
		return false;
	}

	if (output) {
		output->reserve(count);
	}

	if (end_position) {
		*end_position = position + count * 24;
	}

	for (uint32_t i = 0; i < count; i++, position += 24) {
		Trace trace;
		trace.net   = twv_get_word(brd_data, position);
		trace.dcode = twv_get_word(brd_data, position + 4);
		trace.x1    = static_cast<int32_t>(twv_get_word(brd_data, position + 8));
		trace.y1    = static_cast<int32_t>(twv_get_word(brd_data, position + 12));
		trace.x2    = static_cast<int32_t>(twv_get_word(brd_data, position + 16));
		trace.y2    = static_cast<int32_t>(twv_get_word(brd_data, position + 20));

		if (output) {
			output->push_back(trace);
		}
	}

	return true;
}

bool TVWFile::walk_surfaces(const uint8_t *brd_data, size_t position, size_t end_position, std::vector<Arc> *arcs_output,
                            std::vector<Surface> *output) {
	if (position + 4 > end_position) {
		return false;
	}

	uint32_t arcs = twv_get_word(brd_data, position);
	position += 4;
	if (arcs > 0) {
		position += 4;

		if (arcs > (end_position - position) / TVW_ARC_RECORD) {
			return false;
		}

		if (arcs_output) {
			arcs_output->reserve(arcs);
		}

		for (uint32_t i = 0; i < arcs; i++, position += TVW_ARC_RECORD) {
			if (!arcs_output) {
				continue;
			}

			Arc arc;
			arc.net         = twv_get_word(brd_data, position);
			arc.dcode       = twv_get_word(brd_data, position + 4);
			arc.cx          = static_cast<int32_t>(twv_get_word(brd_data, position + 8));
			arc.cy          = static_cast<int32_t>(twv_get_word(brd_data, position + 12));
			arc.radius      = static_cast<int32_t>(twv_get_word(brd_data, position + 16));
			arc.start_angle = tvw_f32_at(brd_data, position + 20);
			arc.sweep       = tvw_f32_at(brd_data, position + 24);
			arcs_output->push_back(arc);
		}
	}

	if (position + 8 > end_position) {
		return false;
	}

	uint32_t count = twv_get_word(brd_data, position);
	position += 8;

	if (count < 1 || count > (end_position - position) / 8) {
		return false;
	}

	std::vector<Surface> surfaces;
	surfaces.reserve(count);

	for (uint32_t i = 0; i < count; i++) {
		if (position + 8 > end_position) {
			return false;
		}

		Surface surface;
		surface.net    = twv_get_word(brd_data, position);
		uint32_t points = twv_get_word(brd_data, position + 4);
		position += 8;

		if (points < 2 || points > (end_position - position) / 8) {
			return false;
		}

		surface.outline.reserve(points);
		for (uint32_t p = 0; p < points; p++, position += 8) {
			surface.outline.emplace_back(static_cast<int32_t>(twv_get_word(brd_data, position)),
										 static_cast<int32_t>(twv_get_word(brd_data, position + 4)));
		}

		if (position + 8 > end_position) {
			return false;
		}

		position += 4;
		uint32_t cutouts = twv_get_word(brd_data, position);
		position += 4;

		if (cutouts > (end_position - position) / TVW_MIN_CUTOUT_RECORD) {
			return false;
		}

		if (cutouts > 0) {
			position += 4;
			for (uint32_t c = 0; c < cutouts; c++) {
				if (position + 4 > end_position) {
					return false;
				}

				uint32_t cut_points = twv_get_word(brd_data, position);
				position += 4;

				if (cut_points < 2 || cut_points > (end_position - position) / 8) {
					return false;
				}

				std::vector<std::pair<int32_t, int32_t>> cutout;
				cutout.reserve(cut_points);
				for (uint32_t p = 0; p < cut_points; p++, position += 8)
					cutout.emplace_back(static_cast<int32_t>(twv_get_word(brd_data, position)),
					                    static_cast<int32_t>(twv_get_word(brd_data, position + 4)));

				if (position + 4 > end_position) {
					return false;
				}

				position += 4;
				surface.cutouts.push_back(std::move(cutout));
			}
		}

		surfaces.push_back(std::move(surface));
	}

	if (output) {
		*output = std::move(surfaces);
	}

	return true;
}

void TVWFile::parse_pad_tables(const std::vector<char> &brd_buffer) {
	const uint8_t *brd_data  = reinterpret_cast<const uint8_t *>(brd_buffer.data());
	const size_t size = brd_buffer.size();

	if (size < 24) {
		return;
	}

	for (size_t position = 0; position + 24 < size; position++) {
		if (brd_data[position] != 1) {
			continue;
		}

		if (twv_get_word(brd_data, position) != 1 || twv_get_word(brd_data, position + 4) != 0 || twv_get_word(brd_data, position + 8) != 1) {
			continue;
		}

		if (twv_get_word(brd_data, position + 16) != 2) {
			continue;
		}

		uint32_t count = twv_get_word(brd_data, position + 12);

		if (count < 2 || count > size) {
			continue;
		}

		PadTable table;
		size_t pads_end = 0;
		if (!walk_pads(brd_data, size, position + 20, count, &table.pads, &pads_end)) {
			continue;
		}

		size_t traces_end = 0;
		if (walk_traces(brd_data, size, pads_end, &table.traces, &traces_end)) {
			size_t layer_end = size;
			for (const Layer &layer : layers)
				if (layer.begin > position && layer.begin < layer_end) {
					layer_end = layer.begin;
				}

			walk_surfaces(brd_data, traces_end, layer_end, &table.arcs, &table.surfaces);
		}

		table.layer = layers.size();
		for (size_t i = 0; i < layers.size(); i++)
			if (position >= layers[i].begin && position < layers[i].end) {
				table.layer = i;
			}

		if (table.layer < layers.size()) {
			const size_t declared_at   = layers[table.layer].body + 12;
			const size_t apertures_at  = declared_at + 4;
			if (apertures_at < position) {
				walk_apertures(brd_data, apertures_at, position, twv_get_word(brd_data, declared_at), &table.apertures);
			}
		}

		pad_tables.push_back(std::move(table));
	}
}

void TVWFile::parse_nets(const std::vector<char> &brd_buffer) {
	const uint8_t *brd_data  = reinterpret_cast<const uint8_t *>(brd_buffer.data());
	const size_t size = brd_buffer.size();

	for (size_t position = 0; position + 12 < size; position++) {
		if (twv_get_word(brd_data, position) != 0) {
			continue;
		}

		uint32_t count = twv_get_word(brd_data, position + 4);

		if (count < 8 || count > (size - position - 12) / 2) {
			continue;
		}

		if (twv_get_word(brd_data, position + 8) != count) {
			continue;
		}

		std::vector<std::string> names;
		size_t position_offset = position + 12;

		for (uint32_t i = 0; i < count; i++) {
			if (position_offset >= size) {
				break;
			}

			uint8_t length = brd_data[position_offset];

			if (length == 0 || position_offset + 1 + length > size) {
				break;
			}

			bool printable = true;

			for (uint8_t c = 0; c < length; c++) {
				if (brd_data[position_offset + 1 + c] < 0x20 || brd_data[position_offset + 1 + c] > 0x7e) {
					printable = false;
				}
			}

			if (!printable) {
				break;
			}

			names.emplace_back(reinterpret_cast<const char *>(brd_data + position_offset + 1), length);
			position_offset += 1 + length;
		}
		if (names.size() == count) {
			net_names = std::move(names);
			return;
		}
	}
}

uint32_t TVWFile::walk_parts(const std::vector<char> &brd_buffer, size_t first_position, uint32_t count, std::vector<PartRecord> *output) {
	const uint8_t *brd_data  = reinterpret_cast<const uint8_t *>(brd_buffer.data());
	const size_t size = brd_buffer.size();

	uint32_t parsed = 0;
	size_t position_offset = first_position;

	while (parsed < count) {
		TVWReader tvw_reader(brd_buffer);
		tvw_reader.pos = position_offset;

		PartRecord part_record;
		part_record.name = tvw_reader.pstring();

		if (part_record.name.empty() || !tvw_reader.ok) {
			break;
		}

		auto x1 = static_cast<int32_t>(tvw_reader.u32());
		auto y1 = static_cast<int32_t>(tvw_reader.u32());
		auto x2 = static_cast<int32_t>(tvw_reader.u32());
		auto y2 = static_cast<int32_t>(tvw_reader.u32());
		part_record.w      = std::abs(x2 - x1);
		part_record.h      = std::abs(y2 - y1);
		part_record.cx     = static_cast<int32_t>(tvw_reader.u32());
		part_record.cy     = static_cast<int32_t>(tvw_reader.u32());
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
				tvw_reader.u32();
				PartRecord::Pin rp;
				rp.handle = tvw_reader.u32();
				rp.layer  = layer;
				tvw_reader.u32();
				tvw_reader.u32();
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

bool TVWFile::choose_pad_layers(const std::vector<PartRecord> &records) {
	if (pad_tables.empty() || records.empty()) {
		return false;
	}

	std::unordered_map<uint32_t, std::vector<uint32_t>> votes;
	size_t total = 0;

	for (const PartRecord &record : records) {
		const int32_t low_x  = record.cx - record.w / 2 - TVW_PIN_BBOX_SLACK;
		const int32_t high_x = record.cx + record.w / 2 + TVW_PIN_BBOX_SLACK;
		const int32_t low_y  = record.cy - record.h / 2 - TVW_PIN_BBOX_SLACK;
		const int32_t high_y = record.cy + record.h / 2 + TVW_PIN_BBOX_SLACK;

		for (const PartRecord::Pin &record_pin : record.pins) {
			total++;

			if (record_pin.layer > TVW_MAX_LAYER) {
				continue;
			}

			std::vector<uint32_t> &vote = votes[record_pin.layer];

			if (vote.empty()) {
				vote.assign(pad_tables.size(), 0);
			}

			const size_t index = record_pin.handle / 8;

			for (size_t t = 0; t < pad_tables.size(); t++) {
				if (index >= pad_tables[t].pads.size()) {
					continue;
				}

				const Pad &pad = pad_tables[t].pads[index];

				if (pad.x >= low_x && pad.x <= high_x && pad.y >= low_y && pad.y <= high_y) {
					vote[t]++;
				}
			}
		}
	}

	uint32_t highest = 0;

	for (const auto &entry : votes) {
		highest = std::max(highest, entry.first);
	}

	tables_by_layer.assign(highest + 1, nullptr);

	size_t matched = 0;

	for (const auto &entry : votes) {
		const std::vector<uint32_t> &vote = entry.second;
		const size_t best = std::max_element(vote.begin(), vote.end()) - vote.begin();

		tables_by_layer[entry.first] = &pad_tables[best];
		matched += vote[best];
	}

	return total && static_cast<double>(matched) / total >= TVW_MIN_PAD_LAYER_SCORE;
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

	for (const PadTable &table : pad_tables) {
		for (const Pad &pad : table.pads) {
			if (pad.has_drill) {
				drill_of[key(pad.x, pad.y)] = std::min(pad.drill_cx, pad.drill_cy);
			}
		}
	}

	for (const PadTable &table : pad_tables) {
		if (table.traces.empty() && table.arcs.empty() && table.surfaces.empty()) {
			continue;
		}

		BRDLayer layer;
		const size_t layer_index = table.layer;
		layer.name = layer_index < layers.size() && !layers[layer_index].name.empty() ? layers[layer_index].name
		                                                                              : "Layer " + std::to_string(layer_index);

		auto trace_width = [&](uint32_t dcode) {
			if (dcode >= TVW_FIRST_DCODE) {
				const size_t aperture = dcode - TVW_FIRST_DCODE;
				if (aperture < table.apertures.size()) {
					return std::min(table.apertures[aperture].w, table.apertures[aperture].h) /
					       static_cast<double>(TVW_COORD_SCALE);
				}
			}

			return 0.0;
		};

		layer.traces.reserve(table.traces.size());
		for (const Trace &trace : table.traces) {
			BRDTrace out;
			out.from  = BRDPoint(trace.x1 / TVW_COORD_SCALE, trace.y1 / TVW_COORD_SCALE);
			out.to    = BRDPoint(trace.x2 / TVW_COORD_SCALE, trace.y2 / TVW_COORD_SCALE);
			out.width = trace_width(trace.dcode);

			if (trace.net < net_names.size()) {
				out.net = net_name(trace.net);
			}

			layer.traces.push_back(out);
		}

		for (const Arc &arc : table.arcs) {
			double from = arc.start_angle;
			double span = arc.sweep;

			if (span < 0.0) {
				from += span;
				span = -span;
			}

			const int segments = std::max(TVW_ARC_MIN_SEGMENTS, static_cast<int>(std::ceil(span / TVW_ARC_DEGREES_PER_SEGMENT)));
			const double width = trace_width(arc.dcode);
			const char *net    = arc.net < net_names.size() ? net_name(arc.net) : nullptr;

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

		layer.pours.reserve(table.surfaces.size());
		for (const Surface &surface : table.surfaces) {
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

		for (const Pad &pad : table.pads) {
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
			} else if (pad.dcode >= TVW_FIRST_DCODE && pad.dcode - TVW_FIRST_DCODE < table.apertures.size()) {
				const Aperture &aperture = table.apertures[pad.dcode - TVW_FIRST_DCODE];
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

void TVWFile::parse_parts(const std::vector<char> &brd_buffer) {
	const uint8_t *brd_data  = reinterpret_cast<const uint8_t *>(brd_buffer.data());
	const size_t size = brd_buffer.size();

	size_t table      = 0;
	uint32_t declared = 0;

	for (size_t position = 0; position + 16 < size; position++) {
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

	if (!choose_pad_layers(records)) {
		return;
	}

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
			}else {
				on_top = true;
			}

			if (record_pin.layer >= tables_by_layer.size() || !tables_by_layer[record_pin.layer]) {
				continue;
			}

			const PadTable &table = *tables_by_layer[record_pin.layer];
			size_t index          = record_pin.handle / 8;

			if (index >= table.pads.size()) {
				continue;
			}

			const Pad &pad = table.pads[index];

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
				if (aperture_index < table.apertures.size()) {
					aperture = &table.apertures[aperture_index];
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

	build_layers();
}

TVWFile::TVWFile(std::vector<char> &brd_buffer) {
	TVWReader tvw_reader(brd_buffer);

	if (!parse_header(tvw_reader)) {
		return;
	}

	parse_layers(brd_buffer);
	ENSURE_OR_FAIL(!layers.empty(), error_msg, return);

	parse_nets(brd_buffer);
	parse_pad_tables(brd_buffer);
	parse_parts(brd_buffer);

	for (const Layer &layer : layers) {
		if (layer.name == "OUTLINE") {
			outline_segments = layer.segments;
			break;
		}
	}

	if (parts.empty() || pins.empty()) {
		if (net_names.empty()) {
			error_msg = "TVW: this file is incomplete. It carries " + std::to_string(layers.size()) + " of the " +
			            std::to_string(header.layer_count()) +
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

#include "TVWFile.h"

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "utils.h"

static const char *TVW_MAGIC = "Tebo-ictview files.";

// Coordinates are stored in hundredths of a mil, BRDPoint holds mils.
static const int TVW_COORD_SCALE = 100;

static const uint32_t TVW_FIRST_DCODE = 10;

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

/*
 * Record size for the word aligned primitive encoding used by the copper,
 * silkscreen and mask layers. Types 10 to 13 carry a line segment as four
 * coordinates; the payload of types 5 and 25 is not understood yet, so they
 * are only skipped over.
 */
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

	// Skip padding
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

const char *TVWFile::pool(const std::string &string) {
	string_pool.push_back(string);
	return string_pool.back().c_str();
}

bool TVWFile::walk_pads(const uint8_t *brd_data, size_t brd_size, size_t position, uint32_t count, std::vector<Pad> *output) {
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
					pad.has_drill = true;
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

	return true;
}

bool TVWFile::walk_apertures(const uint8_t *brd_data, size_t position, size_t end_position, std::vector<Aperture> *output) {
	std::vector<Aperture> apertures;

	while (position + 20 <= end_position) {
		Aperture aperture;
		aperture.w    = static_cast<int32_t>(twv_get_word(brd_data, position + 4));
		aperture.h    = static_cast<int32_t>(twv_get_word(brd_data, position + 8));
		aperture.kind = twv_get_word(brd_data, position + 12);

		if (aperture.kind != APERTURE_CUSTOM) {
			apertures.push_back(std::move(aperture));
			position += 24;
			continue;
		}

		size_t at = position + 20;

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
			} else if (type == 5) {
				at += 32;
			} else {
				return false;
			}
		}

		if (aperture.outline.size() > 1 && aperture.outline.front() == aperture.outline.back())
			aperture.outline.pop_back();

		apertures.push_back(std::move(aperture));
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
		if (!walk_pads(brd_data, size, position + 20, count, &table.pads)) {
			continue;
		}

		table.layer = layers.size();
		for (size_t i = 0; i < layers.size(); i++)
			if (position >= layers[i].begin && position < layers[i].end) {
				table.layer = i;
			}

		if (table.layer < layers.size()) {
			const size_t apertures_at = layers[table.layer].body + 16;
			if (apertures_at < position) {
				walk_apertures(brd_data, apertures_at, position, &table.apertures);
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
			for (int s = 0; s < 3; s++) tvw_reader.pstring();
			tvw_reader.pstring();
			part_record.mfgcode = tvw_reader.pstring();
		}

		tvw_reader.u32();

		uint32_t pin_count = tvw_reader.u32();
		uint32_t layer     = tvw_reader.u32();
		bool broken        = false;

		while (true) {
			if (!tvw_reader.ok || pin_count > 4096) {
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

			if (size - tvw_reader.pos >= 8 && next <= 4096 && twv_get_word(brd_data, tvw_reader.pos + 4) < 64) {
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

double TVWFile::score_pad_layers(const std::vector<PartRecord> &records, const std::vector<size_t> &layer_of_table) const {
	size_t inside = 0,
			total = 0;

	for (const PartRecord &rec : records) {
		int32_t low_x = rec.cx - rec.w / 2,
				high_x = rec.cx + rec.w / 2;

		int32_t low_y = rec.cy - rec.h / 2,
				high_y = rec.cy + rec.h / 2;

		for (const PartRecord::Pin &pin : rec.pins) {
			total++;

			for (size_t t = 0; t < pad_tables.size(); t++) {
				if (layer_of_table[t] != pin.layer) {
					continue;
				}

				size_t index = pin.handle / 8;

				if (index >= pad_tables[t].pads.size()) {
					break;
				}

				const Pad &pad = pad_tables[t].pads[index];
				if (pad.x >= low_x - 500 && pad.x <= high_x + 500 && pad.y >= low_y - 500 && pad.y <= high_y + 500) {
					inside++;
				}

				break;
			}
		}
	}

	return total ? static_cast<double>(inside) / total : 0.0;
}

bool TVWFile::choose_pad_layers(const std::vector<PartRecord> &records) {
	if (pad_tables.empty() || records.empty()) {
		return false;
	}

	std::vector<size_t> candidate(pad_tables.size());
	double best_score = 0.0;

	for (size_t t = 0; t < pad_tables.size(); t++) {
		candidate[t] = pad_tables[t].layer;
	}

	std::vector<size_t> best_candidate = candidate;
	best_score = score_pad_layers(records, candidate);

	for (size_t base = 0; base <= 16 && best_score < 0.999; base++) {
		for (size_t t = 0; t < pad_tables.size(); t++) {
			candidate[t] = base + t;
		}

		double score = score_pad_layers(records, candidate);

		if (score > best_score) {
			best_score = score;
			best_candidate = candidate;
		}
	}

	if (best_score < 0.9) {
		return false;
	}

	size_t highest = 0;

	for (size_t layer : best_candidate) {
		highest = std::max(highest, layer);
	}

	tables_by_layer.assign(highest + 1, nullptr);

	for (size_t t = 0; t < pad_tables.size(); t++) {
		if (!tables_by_layer[best_candidate[t]]) {
			tables_by_layer[best_candidate[t]] = &pad_tables[t];
		}
	}

	return true;
}

void TVWFile::parse_parts(const std::vector<char> &brd_buffer) {
	const uint8_t *brd_data  = reinterpret_cast<const uint8_t *>(brd_buffer.data());
	const size_t size = brd_buffer.size();

	size_t table      = 0;
	uint32_t declared = 0;

	for (size_t position = 0; position + 16 < size; position++) {
		uint32_t count = twv_get_word(brd_data, position);
		if (count < 64 || count > 100000) {
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
				pin.net = pool(net_names[pad.net]);
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

				bool plausible = aperture && std::abs(aperture->w - width) * 4 < width &&
				                 std::abs(aperture->h - height) * 4 < height;

				if (plausible && aperture->kind == APERTURE_CUSTOM && aperture->outline.size() >= 3) {
					pin.pad_shape = BRDPadShape::Outline;
					pin.shape.reserve(aperture->outline.size());

					for (const auto &vertex : aperture->outline) {
						pin.shape.emplace_back(static_cast<float>(vertex.first) / TVW_COORD_SCALE,
											   static_cast<float>(vertex.second) / TVW_COORD_SCALE);
					}
				} else if (plausible) {
					switch (aperture->kind) {
						case APERTURE_CIRCLE: pin.pad_shape = BRDPadShape::Circle; break;
						case APERTURE_RECTANGLE: pin.pad_shape = BRDPadShape::Rectangle; break;
						case APERTURE_OBROUND: pin.pad_shape = BRDPadShape::Obround; break;
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
		error_msg = "TVW: this file uses a layout that is not supported yet (" +
		            (header.vendor.empty() ? std::string("unknown vendor") : header.vendor) + ", " +
		            std::to_string(layers.size()) + " layers, " + std::to_string(parts.size()) + " parts, " +
		            std::to_string(pins.size()) + " pins).";
		return;
	}

	valid      = true;
	num_parts  = parts.size();
	num_pins   = pins.size();
	num_format = format.size();
	num_nails  = nails.size();
}

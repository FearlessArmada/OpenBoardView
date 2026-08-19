#pragma once

#include "BRDFileBase.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

struct TVWReader;

struct TVWFile : public BRDFileBase {
  public:
	TVWFile(std::vector<char> &brd_buffer);

	static bool verifyFormat(const std::vector<char> &brd_buffer);

	struct FileHeader {
		uint32_t version = 0;
		std::string vendor;
		std::string model;
		std::string date;
		uint32_t document_version = 0;
		uint32_t declared_layers  = 0;
	};

	struct TVWReader {
		const uint8_t *data = nullptr;
		size_t size         = 0;
		size_t pos          = 0;
		bool ok             = true;

		TVWReader(const std::vector<char> &brd_buffer)
			: data(reinterpret_cast<const uint8_t *>(brd_buffer.data())), size(brd_buffer.size()) {}

		bool require(size_t n) {
			if (!ok) {
				return false;
			}

			if (n > size - pos) {
				ok = false;
			}

			return ok;
		}

		uint8_t u8() {
			if (!require(1)) {
				return 0;
			}

			return data[pos++];
		}

		uint32_t u32() {
			if (!require(4)) {
				return 0;
			}

			uint32_t word = static_cast<uint32_t>(data[pos]) | (static_cast<uint32_t>(data[pos + 1]) << 8) |
						 (static_cast<uint32_t>(data[pos + 2]) << 16) | (static_cast<uint32_t>(data[pos + 3]) << 24);
			pos += 4;
			return word;
		}

		int32_t i32() {
			return static_cast<int32_t>(u32());
		}

		float f32() {
			uint32_t word = u32();
			float value;
			memcpy(&value, &word, sizeof(value));
			return value;
		}

		void skip(size_t n) {
			if (require(n)) {
				pos += n;
			}
		}

		uint32_t version(uint32_t highest) {
			uint32_t written = u32();

			if (written > highest) {
				ok = false;
				return 0;
			}

			return written;
		}

		std::string pstring() {
			uint32_t length = u8();

			if (length == 0xff) {
				length = u8() | (static_cast<uint32_t>(u8()) << 8);

				if (length == 0xffff) {
					length = u32();
				}
			}

			if (!require(length)) {
				return {};
			}

			std::string s(reinterpret_cast<const char *>(data + pos), length);
			pos += length;
			return s;
		}

		size_t remaining(size_t record_size) const {
			return (size - pos) / record_size;
		}
	};

	struct Text {
		std::string text;
		int32_t x = 0, y = 0;
		int32_t height = 0;
		int32_t width  = 0;
		bool mirrored  = false;
	};

	struct Drill {
		uint32_t tool = 0;
		uint32_t net  = 0;
		int32_t x = 0, y = 0;
		int32_t size = 0;
	};

	struct Aperture {
		int32_t w = 0,
				h = 0;
		uint32_t kind = 0;
		int32_t extra = 0;
		float angle   = 0.0f;
		std::vector<std::pair<int32_t, int32_t>> outline;

		bool is_obround() const {
			return kind == APERTURE_ROUNDED && extra == std::min(w, h);
		}
	};

	static const uint32_t APERTURE_CIRCLE    = 0;
	static const uint32_t APERTURE_RECTANGLE = 1;
	static const uint32_t APERTURE_ROUNDED = 3;
	static const uint32_t APERTURE_DONUT   = 4;
	static const uint32_t APERTURE_CUSTOM  = 5;

	struct Pad {
		uint32_t net   = 0;
		uint32_t dcode = 0;
		int32_t x = 0, y = 0;
		bool has_extent = false;
		int32_t x1 = 0,
				y1 = 0,
				x2 = 0,
				y2 = 0;
		bool has_drill = false;
		int32_t drill_offset_x = 0,
				drill_offset_y = 0,
				drill_cx       = 0,
				drill_cy       = 0;
	};

	struct PartRecord {
		std::string name;
		std::string mfgcode;
		std::string value;
		std::string rating;
		std::string package;
		int32_t w = 0,
				h = 0;
		int32_t cx = 0,
				cy = 0;
		int32_t rotation      = 0;
		uint32_t package_index = 0;
		int32_t part_class    = -1;

		struct Pin {
			uint32_t handle = 0;
			uint32_t group  = 0;
			uint32_t layer  = 0;
			std::string name;
		};
		std::vector<Pin> pins;
	};

	struct Trace {
		uint32_t net   = 0;
		uint32_t dcode = 0;
		int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	};

	struct Arc {
		uint32_t net   = 0;
		uint32_t dcode = 0;
		int32_t cx = 0, cy = 0;
		int32_t radius    = 0;
		float start_angle = 0.0f;
		float sweep       = 0.0f;
	};

	struct Surface {
		uint32_t net = 0;
		std::vector<std::pair<int32_t, int32_t>> outline;
		std::vector<std::vector<std::pair<int32_t, int32_t>>> cutouts;
	};

	struct Layer {
		std::string name;
		uint32_t type        = 0;
		uint32_t kind        = 0;
		uint32_t line_colour = 0;
		uint32_t pad_colour  = 0;

		std::vector<Aperture> apertures;
		std::vector<Pad> pads;
		std::vector<Trace> traces;
		std::vector<Arc> arcs;
		std::vector<Surface> surfaces;
		std::vector<Text> texts;
		std::vector<Drill> drills;

		std::vector<size_t> group_pad_start;

		bool is_silkscreen() const;
		const Pad *pad_for(uint32_t handle, uint32_t group) const;
	};

  private:
	FileHeader header;
	std::vector<std::string> net_names;
	std::vector<Layer> layers;

	std::deque<std::string> string_pool;

	const char *pool(const std::string &string);

	std::vector<const char *> pooled_nets;

	const char *net_name(uint32_t index);

	bool read_header(TVWReader &tvw_reader);
	bool read_layers(TVWReader &tvw_reader);
	bool read_layer(TVWReader &tvw_reader, Layer &layer, uint32_t type);
	bool read_graphic_layer(TVWReader &tvw_reader, Layer &layer);
	bool read_drill_layer(TVWReader &tvw_reader, Layer &layer);
	bool read_layer_common(TVWReader &tvw_reader, Layer &layer);
	bool read_apertures(TVWReader &tvw_reader, Layer &layer);
	bool read_object_group(TVWReader &tvw_reader, Layer &layer);
	bool read_layer_tail(TVWReader &tvw_reader);
	bool read_nets(TVWReader &tvw_reader);
	bool find_nets(TVWReader &tvw_reader);

	static uint32_t walk_parts(const std::vector<char> &brd_buffer, size_t first_position, uint32_t count, std::vector<PartRecord> *output);

	void build_layers();
	void parse_parts(const std::vector<char> &brd_buffer, size_t search_from);
};

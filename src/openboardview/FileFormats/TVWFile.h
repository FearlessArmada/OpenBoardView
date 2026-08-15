#pragma once

#include "BRDFileBase.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct TVWReader;

struct TVWFile : public BRDFileBase {
  public:
	TVWFile(std::vector<char> &brd_buffer);

	static bool verifyFormat(const std::vector<char> &brd_buffer);

  private:
	struct FileHeader {
		uint32_t version = 0;
		std::string vendor;
		std::string model;
		std::string date;
		uint32_t fields[5] = {};

		uint32_t layer_count() const {
			return fields[3];
		}
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

		std::string pstring() {
			uint8_t length = u8();

			if (!require(length)) {
				return {};
			}

			std::string s(reinterpret_cast<const char *>(data + pos), length);
			pos += length;
			return s;
		}
	};

	struct Layer {
		std::string name;
		uint32_t colour = 0;
		size_t begin = 0;
		size_t end   = 0;
		std::vector<std::pair<BRDPoint, BRDPoint>> segments;
	};

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
	};

	struct PartRecord {
		std::string name;
		std::string mfgcode;
		int32_t w = 0,
				h = 0;
		int32_t cx = 0,
				cy = 0;
		struct Pin {
			uint32_t handle = 0;
			uint32_t layer  = 0;
			std::string name;
		};
		std::vector<Pin> pins;
	};

	FileHeader header;
	std::vector<Layer> layers;
	std::vector<std::string> net_names;

	struct PadTable {
		size_t layer = 0;
		std::vector<Pad> pads;
	};
	std::vector<PadTable> pad_tables;

	std::vector<const std::vector<Pad> *> pads_by_layer;

	std::deque<std::string> string_pool;

	const char *pool(const std::string &string);

	static bool walk_pads(const uint8_t *brd_data, size_t brd_size, size_t position, uint32_t count, std::vector<Pad> *output);

	static uint32_t walk_parts(const std::vector<char> &brd_buffer, size_t first_position, uint32_t count, std::vector<PartRecord> *output);

	double score_pad_layers(const std::vector<PartRecord> &records, const std::vector<size_t> &layer_of_table) const;
	bool choose_pad_layers(const std::vector<PartRecord> &records);

	bool parse_header(TVWReader &tvw_reader);
	void parse_layers(const std::vector<char> &brd_buffer);
	void parse_nets(const std::vector<char> &brd_buffer);
	void parse_pad_tables(const std::vector<char> &brd_buffer);
	void parse_parts(const std::vector<char> &brd_buffer);
};

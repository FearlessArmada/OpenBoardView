#pragma once

#include "BRDFileBase.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

struct XZZPCBFile : public BRDFileBase {
  public:
	XZZPCBFile(std::vector<char> &buf, uint64_t key);

	static bool verifyFormat(const std::vector<char> &buf);

  private:
	uint64_t key = 0ul;
	static const int XZZ_GLOBAL_SCALE = 10000;
	static const uint32_t XZZ_FIRST_COPPER_LAYER = 1;
	static const uint32_t XZZ_LAST_COPPER_LAYER  = 16;
	static const uint32_t XZZ_OUTLINE_LAYER      = 28;
	std::unordered_map<uint32_t, std::string> net_dict;
	std::unordered_map<std::string, std::unordered_map<std::string, std::string>> diode_dict; // <Net Name, <Pin Name, Reading>>
	std::unordered_map<uint32_t, const char *> net_pool;
	std::map<uint32_t, std::vector<BRDTrace>> copper_traces;

	struct XZZVia {
		BRDPoint pos;
		double pad_radius  = 0.0;
		double hole_radius = 0.0;
		uint32_t layer_a   = 0;
		uint32_t layer_b   = 0;
		uint32_t net_index = 0;
	};
	std::vector<XZZVia> copper_vias;

	struct Butterfly {
		bool found      = false;
		bool vertical   = true;
		int fold_at     = 0;
		int mirror_span = 0;
	};

	const std::array<uint8_t, 8> getKeyParity() const;
	bool checkKey(uint64_t key) const;
	std::string keyToString(uint64_t key) const;

	// DES
	std::vector<char> des_decrypt(const std::vector<char> &buf);

	std::vector<std::pair<BRDPoint, BRDPoint>> xzz_arc_to_segments(int startAngle, int endAngle, int r, BRDPoint pc);
	void parse_arc_block(const std::vector<char> &buf);
	void parse_via_block(const std::vector<char> &buf);
	void parse_line_segment_block(const std::vector<char> &buf);
	BRDPin parse_pin_block(const std::vector<char> &buf, uint32_t &current_pointer);
	void parse_part_block(std::vector<char> &encrypted_buf);
	void parse_test_pad_block(const std::vector<char> &buf);
	void parse_net_block(const std::vector<char> &buf);
	void process_block(std::vector<char> &block_buf, uint8_t block_type);
	void process_blocks(const std::vector<char> &buf, uint32_t main_data_start, uint32_t main_data_blocks_size);

	static int unscale(uint32_t raw);
	const char *net_name(uint32_t net_index);
	void add_copper_segment(uint32_t layer, const BRDPoint &from, const BRDPoint &to, uint32_t width, uint32_t net_index);
	void build_layers();

	Butterfly detect_butterfly() const;
	void fold_butterfly(const Butterfly &butterfly);
	void weld_outline(int tolerance);

	BRDPoint find_xy_translation() const;
	void translate_segments(const BRDPoint &xy_translation);
	void translate_pins(const BRDPoint &xy_translation);
	void translate_copper(const BRDPoint &xy_translation);
};

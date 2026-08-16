#include "BRDFileBase.h"

#include "utf8/utf8.h"
#include <algorithm>
#include <cstdint>
#include <cmath>

double BRDFileBase::arc_slice_angle_rad = 0.1;

// from stb.h
void stringfile(char *buffer, std::vector<char*> &lines) {
	char *s;
	size_t count, i;

	// two passes through: first time count lines, second time set them
	for (i = 0; i < 2; ++i) {
		s                   = buffer;
		if (i == 1) lines[0] = s;
		count               = 1; // was '1',  but C arrays are 0-indexed
		while (*s) {
			if (*s == '\n' || *s == '\r') {

				// If this is the 2nd pass, then terminate the line at the first line break char
				if (i == 1) *s = 0;

				s++; // next char

				// if the termination is a CRLF combo, then jump to the next char
				if ((*s == '\r') || (*s == '\n')) s++;

				// if the char is valid (first after line break), set up the next item in the line array
				if (*s) { // it's not over yet
					if (i == 1) {
						lines[count] = s;
						//					  list[count+1] = NULL;
						// fprintf(stdout,"%s\n",list[count]);
					}
					++count;
				}
			}
			s++;
		} // while s

		// Generate the required array to hold all the line starting points
		if (i == 0) {
			lines.resize(count);
		}
	}
}

char *fix_to_utf8(char *s, char **arena, char *arena_end) {
	if (!utf8valid(s)) {
		return s;
	}
	char *p     = *arena;
	char *begin = p;
	while (*s) {
		uint32_t c = (uint8_t)*s;
		if (c < 0x80) {
			if (p + 1 >= arena_end) goto done;
			*p++ = c;
		} else {
			if (p + 2 >= arena_end) goto done;
			*p++ = 0xc0 | (c >> 6);
			*p++ = 0x80 | (c & 0x3f);
		}
		++s;
	}
	if (p + 1 >= arena_end) goto done;
	*p++ = 0;
done:
	*arena = p;
	return begin;
}

void BRDFileBase::AddNailsAsPins() {
	for (auto &nail : nails) {
		BRDPin pin;
		pin.pos = nail.pos;
		if (nail.side == BRDPartMountingSide::Both) {
			pin.part = parts.size();
			pin.side = BRDPinSide::Both;
		} else if (nail.side == BRDPartMountingSide::Top) {
			pin.part = parts.size();
			pin.side = BRDPinSide::Top;
		} else {
			pin.part  = parts.size() - 1;
			pin.side = BRDPinSide::Bottom;
		}
		pin.probe = nail.probe;
		pin.net   = nail.net;
		pins.push_back(pin);
	}
}

std::vector<std::pair<BRDPoint, BRDPoint> > BRDFileBase::arc_to_segments(
	double startAngle, double endAngle, double r, BRDPoint p1, BRDPoint p2, BRDPoint pc)
{
	std::vector<std::pair<BRDPoint, BRDPoint>> arc_segments{};

	BRDPoint p = p1;
	BRDPoint pold = p1;
	for (double i = startAngle + arc_slice_angle_rad; i < endAngle; i += arc_slice_angle_rad) {
		p.x = pc.x + r * cos(i);
		p.y = pc.y + r * sin(i);
		arc_segments.push_back({pold, p});
		pold = p;
	}
	arc_segments.push_back({p, p2});

	return arc_segments;
}


double BRDFileBase::distance(const BRDPoint &p1, const BRDPoint &p2) {
	return sqrt(pow(p1.x - p2.x, 2) + pow(p1.y - p2.y, 2));
}

// Bounding box of a point list, for dropping what is off screen.
static void bounds_of(const std::vector<BRDPoint> &points, BRDPoint &lo, BRDPoint &hi) {
	if (points.empty()) {
		lo = hi = BRDPoint(0, 0);
		return;
	}

	lo = hi = points.front();
	for (const BRDPoint &p : points) {
		lo.x = std::min(lo.x, p.x);
		lo.y = std::min(lo.y, p.y);
		hi.x = std::max(hi.x, p.x);
		hi.y = std::max(hi.y, p.y);
	}
}

namespace {

struct SweepEdge {
	double x_top    = 0.0;
	double y_top    = 0.0;
	double y_bottom = 0.0;
	double x_per_y  = 0.0;

	double x_at(double y) const {
		return x_top + (y - y_top) * x_per_y;
	}
};

struct OpenSpan {
	size_t left_edge  = 0;
	size_t right_edge = 0;
	double y_start    = 0.0;
	double x_left     = 0.0;
	double x_right    = 0.0;
};

void collect_edges(const std::vector<BRDPoint> &ring, std::vector<SweepEdge> &edges, std::vector<double> &scanlines) {
	if (ring.size() < 3) {
		return;
	}

	for (size_t i = 0, count = ring.size(); i < count; i++) {
		const BRDPoint &from = ring[i];
		const BRDPoint &to   = ring[(i + 1) % count];

		scanlines.push_back(from.y);

		if (from.y == to.y) {
			continue;
		}

		SweepEdge edge;
		edge.y_top    = std::min(from.y, to.y);
		edge.y_bottom = std::max(from.y, to.y);
		edge.x_top    = (from.y < to.y) ? from.x : to.x;
		edge.x_per_y  = static_cast<double>(to.x - from.x) / (to.y - from.y);
		edges.push_back(edge);
	}
}

}

void BRDPour::build_fill() {
	bounds_of(outline, bounds_min, bounds_max);
	fill.clear();
	fill_indices.clear();

	std::vector<SweepEdge> edges;
	std::vector<double> scanlines;
	collect_edges(outline, edges, scanlines);

	for (const auto &cutout : cutouts) {
		collect_edges(cutout, edges, scanlines);
	}

	if (edges.size() < 2) {
		return;
	}

	std::sort(scanlines.begin(), scanlines.end());
	scanlines.erase(std::unique(scanlines.begin(), scanlines.end()), scanlines.end());

	if (scanlines.size() < 2) {
		return;
	}

	std::vector<size_t> edges_by_top(edges.size());

	for (size_t i = 0; i < edges_by_top.size(); i++) {
		edges_by_top[i] = i;
	}

	std::sort(edges_by_top.begin(), edges_by_top.end(), [&](size_t a, size_t b) {
		return edges[a].y_top < edges[b].y_top;
	});

	auto close_span = [&](const OpenSpan &span, double y_end) {
		const double x_left  = edges[span.left_edge].x_at(y_end);
		const double x_right = edges[span.right_edge].x_at(y_end);

		if (x_right - x_left < 1e-6 && span.x_right - span.x_left < 1e-6) {
			return;
		}

		const unsigned int base = static_cast<unsigned int>(fill.size());
		fill.push_back({static_cast<float>(span.x_left), static_cast<float>(span.y_start)});
		fill.push_back({static_cast<float>(span.x_right), static_cast<float>(span.y_start)});
		fill.push_back({static_cast<float>(x_right), static_cast<float>(y_end)});
		fill.push_back({static_cast<float>(x_left), static_cast<float>(y_end)});

		for (unsigned int corner : {0u, 1u, 2u, 0u, 2u, 3u}) {
			fill_indices.push_back(base + corner);
		}
	};

	std::vector<size_t> active_edges;
	std::vector<OpenSpan> open_spans, next_spans;
	size_t next_edge = 0;

	for (size_t i = 0; i + 1 < scanlines.size(); i++) {
		const double y_top    = scanlines[i];
		const double y_bottom = scanlines[i + 1];

		while (next_edge < edges_by_top.size() && edges[edges_by_top[next_edge]].y_top <= y_top)
			active_edges.push_back(edges_by_top[next_edge++]);
		active_edges.erase(
		    std::remove_if(active_edges.begin(), active_edges.end(), [&](size_t edge) {
			    return edges[edge].y_bottom <= y_top;
		    }),
		    active_edges.end());

		const double y_middle = (y_top + y_bottom) / 2.0;
		std::sort(active_edges.begin(), active_edges.end(), [&](size_t a, size_t b) {
			return edges[a].x_at(y_middle) < edges[b].x_at(y_middle);
		});

		next_spans.clear();
		for (size_t crossing = 0; crossing + 1 < active_edges.size(); crossing += 2) {
			const size_t left_edge = active_edges[crossing], right_edge = active_edges[crossing + 1];

			OpenSpan span;
			bool already_open = false;
			for (const OpenSpan &open : open_spans) {
				if (open.left_edge != left_edge || open.right_edge != right_edge) {
					continue;
				}

				span         = open;
				already_open = true;
				break;
			}

			if (!already_open) {
				span.left_edge  = left_edge;
				span.right_edge = right_edge;
				span.y_start    = y_top;
				span.x_left     = edges[left_edge].x_at(y_top);
				span.x_right    = edges[right_edge].x_at(y_top);
			}

			next_spans.push_back(span);
		}

		for (const OpenSpan &open : open_spans) {
			bool kept = false;

			for (const OpenSpan &next : next_spans) {
				if (next.left_edge != open.left_edge || next.right_edge != open.right_edge) {
					continue;
				}

				kept = true;
				break;
			}

			if (!kept) {
				close_span(open, y_top);
			}
		}

		open_spans.swap(next_spans);
	}

	for (const OpenSpan &open : open_spans) {
		close_span(open, scanlines.back());
	}
}

void BRDLayer::build_paths() {
	paths.clear();

	if (traces.empty()) {
		return;
	}

	for (const BRDTrace &trace : traces) {
		bool continues = !paths.empty() && paths.back().width == trace.width && paths.back().net == trace.net &&
		                 paths.back().points.back().x == trace.from.x && paths.back().points.back().y == trace.from.y;

		if (!continues) {
			paths.emplace_back();
			paths.back().width = trace.width;
			paths.back().net   = trace.net;
			paths.back().points.push_back(trace.from);
		}

		paths.back().points.push_back(trace.to);
	}

	for (BRDTracePath &path : paths) {
		bounds_of(path.points, path.bounds_min, path.bounds_max);
	}
}

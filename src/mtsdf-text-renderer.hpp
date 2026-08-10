// SPDX-FileCopyrightText: 2026 Mugen Art Lab
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct MtsdfPulseRange {
	uint32_t start = 0;
	uint32_t length = 0;
};

struct MtsdfTextStyle {
	std::string font_face = "Arial";
	int font_size = 72;
	int64_t font_flags = 0;

	uint32_t text_color = 0xFFFFFF;
	int text_opacity = 100;

	bool outline_enabled = true;
	int outline_size = 2;
	uint32_t outline_color = 0x000000;
	int outline_opacity = 100;

	bool shadow_enabled = true;
	int shadow_x = 4;
	int shadow_y = 4;
	uint32_t shadow_color = 0x000000;
	int shadow_opacity = 50;
};

class MtsdfTextRenderer {
public:
	MtsdfTextRenderer();
	~MtsdfTextRenderer();

	MtsdfTextRenderer(const MtsdfTextRenderer &) = delete;
	MtsdfTextRenderer &operator=(const MtsdfTextRenderer &) = delete;

	bool update(const std::string &text, const MtsdfTextStyle &style);
	bool update(const std::string &text, const MtsdfTextStyle &style,
		    const std::vector<MtsdfPulseRange> &pulse_ranges);

	bool render();
	bool render(float pulse_scale);

	bool ready() const;
	uint32_t width() const;
	uint32_t height() const;

	/* Exact visible glyph bounds inside the renderer output surface.
	   These exclude MTSDF distance-field padding and shadow expansion. */
	float visual_left() const;
	float visual_top() const;
	float visual_width() const;
	float visual_height() const;

	/* Kept for compatibility/debugging; skin layout should use the exact
	   visual bounds above rather than assuming symmetric padding. */
	float visual_padding() const;
	const std::string &error() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
// SPDX-FileCopyrightText: 2026 Mugen Art Lab
// SPDX-License-Identifier: GPL-2.0-or-later

#include "heartbeat-skin-engine.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <graphics/graphics.h>
#include <graphics/vec4.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

/* windows.h can expose min/max macros. They break std::min/std::max and can
   produce misleading parser errors later in calls such as gs_draw_sprite. */
#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

enum class SkinElementType {
	Rect,
	Text,
	Ecg,
};

enum class SkinTextAlign {
	Left,
	Center,
	Right,
};

enum class SkinTextVAlign {
	Top,
	Center,
	Bottom,
};

struct SkinZone {
	int max_bpm = 240;
	uint32_t color = 0x0066CC66;
	std::string status = "STABLE";
};

struct SkinElement {
	SkinElementType type = SkinElementType::Rect;

	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;

	std::string color_token = "#ffffff";
	int opacity = 100;
	bool pulse = false;

	std::string value;
	MtsdfTextStyle text_style;
	std::unique_ptr<MtsdfTextRenderer> text_renderer;
	SkinTextAlign text_align = SkinTextAlign::Left;
	SkinTextVAlign text_valign = SkinTextVAlign::Top;

	float ecg_line_thickness = 2.0f;
	int ecg_history_ms = 3200;
	float ecg_amplitude = 0.9f;
	int ecg_baseline_opacity = 28;
};

struct LoadedSkin {
	HeartbeatSkinInfo info;
	uint32_t width = 640;
	uint32_t height = 260;
	uint32_t background = 0xFF171411;

	std::vector<SkinZone> zones;
	uint32_t offline_color = 0x00808080;
	std::string offline_status = "NO SIGNAL";

	std::vector<SkinElement> elements;
};

struct SkinDescriptor {
	HeartbeatSkinInfo info;
	std::string json_path;
	std::string root_dir;
};

static int clamp_int(int value, int min_value, int max_value)
{
	return std::max(min_value, std::min(max_value, value));
}

static double clamp_double(double value, double min_value, double max_value)
{
	return std::max(min_value, std::min(max_value, value));
}

static std::string path_join(const std::string &a, const std::string &b)
{
	if (a.empty())
		return b;
	if (b.empty())
		return a;

	const char last = a.back();
	if (last == '/' || last == '\\')
		return a + b;

	return a + "/" + b;
}

static bool starts_with(const std::string &value, const std::string &prefix)
{
	return value.size() >= prefix.size() &&
	       std::equal(prefix.begin(), prefix.end(), value.begin());
}

static uint32_t parse_hex_color(const std::string &value,
				uint32_t fallback)
{
	if (value.size() != 7 && value.size() != 9)
		return fallback;

	if (value[0] != '#')
		return fallback;

	char *end = nullptr;
	const unsigned long parsed =
		std::strtoul(value.c_str() + 1, &end, 16);

	if (!end || *end != '\0')
		return fallback;

	const uint8_t red =
		static_cast<uint8_t>((parsed >> (value.size() == 9 ? 24 : 16)) & 0xFF);
	const uint8_t green =
		static_cast<uint8_t>((parsed >> (value.size() == 9 ? 16 : 8)) & 0xFF);
	const uint8_t blue =
		static_cast<uint8_t>((parsed >> (value.size() == 9 ? 8 : 0)) & 0xFF);

	const uint8_t alpha =
		value.size() == 9
			? static_cast<uint8_t>(parsed & 0xFF)
			: 0xFF;

	return static_cast<uint32_t>(red) |
	       (static_cast<uint32_t>(green) << 8) |
	       (static_cast<uint32_t>(blue) << 16) |
	       (static_cast<uint32_t>(alpha) << 24);
}

static uint32_t color_with_opacity(uint32_t color, int opacity)
{
	const uint32_t source_alpha =
		(color >> 24) & 0xFFu;

	const uint32_t alpha =
		static_cast<uint32_t>(
			std::lround(
				static_cast<double>(source_alpha) *
				static_cast<double>(
					clamp_int(opacity, 0, 100)) /
				100.0));

	return (color & 0x00FFFFFFu) |
	       ((alpha & 0xFFu) << 24);
}

static uint32_t color_with_absolute_opacity(uint32_t color, int opacity)
{
	const uint32_t alpha =
		static_cast<uint32_t>(
			std::lround(
				255.0 *
				static_cast<double>(
					clamp_int(opacity, 0, 100)) /
				100.0));

	return (color & 0x00FFFFFFu) |
	       ((alpha & 0xFFu) << 24);
}

static bool json_has(obs_data_t *data, const char *key)
{
	return data && obs_data_has_user_value(data, key);
}

static int json_int(obs_data_t *data, const char *key, int fallback)
{
	return json_has(data, key)
		       ? static_cast<int>(obs_data_get_int(data, key))
		       : fallback;
}

static double json_double(obs_data_t *data, const char *key, double fallback)
{
	return json_has(data, key)
		       ? obs_data_get_double(data, key)
		       : fallback;
}

static bool json_bool(obs_data_t *data, const char *key, bool fallback)
{
	return json_has(data, key)
		       ? obs_data_get_bool(data, key)
		       : fallback;
}

static std::string json_string(obs_data_t *data,
			       const char *key,
			       const std::string &fallback = {})
{
	if (!data || !json_has(data, key))
		return fallback;

	const char *value = obs_data_get_string(data, key);
	return value ? std::string(value) : fallback;
}

static std::string normalize_locale_key(std::string value)
{
	std::transform(
		value.begin(),
		value.end(),
		value.begin(),
		[](unsigned char c) {
			if (c == '_')
				return '-';

			return static_cast<char>(
				std::tolower(c));
		});

	return value;
}

static std::string json_localized_string(
	obs_data_t *data,
	const char *key,
	const std::string &fallback = {})
{
	if (!data || !key || !json_has(data, key))
		return fallback;

	obs_data_item_t *item =
		obs_data_item_byname(data, key);

	if (!item)
		return fallback;

	const enum obs_data_type type =
		obs_data_item_gettype(item);

	if (type == OBS_DATA_STRING) {
		const char *value =
			obs_data_item_get_string(item);

		const std::string result =
			value ? std::string(value) : fallback;

		obs_data_item_release(&item);

		return result.empty()
			       ? fallback
			       : result;
	}

	if (type != OBS_DATA_OBJECT) {
		obs_data_item_release(&item);
		return fallback;
	}

	obs_data_t *localized =
		obs_data_item_get_obj(item);

	obs_data_item_release(&item);

	if (!localized)
		return fallback;

	std::map<std::string, std::string> values;
	std::string first_value;

	for (obs_data_item_t *entry =
		     obs_data_first(localized);
	     entry;
	     obs_data_item_next(&entry)) {
		if (obs_data_item_gettype(entry) !=
		    OBS_DATA_STRING)
			continue;

		const char *entry_name =
			obs_data_item_get_name(entry);

		const char *entry_value =
			obs_data_item_get_string(entry);

		if (!entry_name ||
		    !entry_value ||
		    !*entry_value)
			continue;

		const std::string normalized =
			normalize_locale_key(
				entry_name);

		values[normalized] =
			entry_value;

		if (first_value.empty())
			first_value = entry_value;
	}

	obs_data_release(localized);

	auto find_value =
		[&](const std::string &locale)
			-> std::string {
			if (locale.empty())
				return {};

			const auto found =
				values.find(
					normalize_locale_key(
						locale));

			return found != values.end()
				       ? found->second
				       : std::string();
		};

	const char *obs_locale =
		obs_get_locale();

	const std::string current_locale =
		normalize_locale_key(
			obs_locale
				? std::string(obs_locale)
				: std::string());

	std::string selected =
		find_value(current_locale);

	if (selected.empty()) {
		const size_t separator =
			current_locale.find('-');

		if (separator !=
		    std::string::npos) {
			selected =
				find_value(
					current_locale.substr(
						0,
						separator));
		}
	}

	if (selected.empty())
		selected = find_value("en-us");

	if (selected.empty())
		selected = find_value("en");

	if (selected.empty())
		selected = first_value;

	return selected.empty()
		       ? fallback
		       : selected;
}

static std::string utf8_lower(std::string value)
{
	std::transform(
		value.begin(), value.end(), value.begin(),
		[](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
	return value;
}

static bool info_less(const HeartbeatSkinInfo &a,
		      const HeartbeatSkinInfo &b)
{
	return utf8_lower(a.name) < utf8_lower(b.name);
}

static bool read_skin_metadata(const std::string &json_path,
			       bool built_in,
			       SkinDescriptor &out)
{
	obs_data_t *root =
		obs_data_create_from_json_file(json_path.c_str());

	if (!root)
		return false;

	const int schema = json_int(root, "schema", 1);
	const std::string id = json_string(root, "id");
	const std::string name = json_localized_string(root, "name");

	if (schema != 1 || id.empty() || name.empty()) {
		obs_data_release(root);
		return false;
	}

	out.info.id = id;
	out.info.name = name;
	out.info.author = json_string(root, "author", "Unknown");
	out.info.version = json_string(root, "version", "1.0");
	out.info.description = json_localized_string(root, "description");
	out.info.built_in = built_in;

	obs_data_t *customization =
		obs_data_get_obj(root, "customization");

	if (customization) {
		out.info.background_opacity_customizable =
			json_bool(
				customization,
				"background_opacity",
				false);

		out.info.background_opacity_default =
			clamp_int(
				json_int(
					customization,
					"background_opacity_default",
					100),
				0,
				100);

		obs_data_release(customization);
	}

	out.json_path = json_path;

	obs_data_release(root);
	return true;
}

static void scan_skin_root(const std::string &root,
			   bool built_in,
			   std::map<std::string, SkinDescriptor> &by_id)
{
	if (root.empty())
		return;

	os_dir_t *dir = os_opendir(root.c_str());
	if (!dir)
		return;

	for (;;) {
		struct os_dirent *entry = os_readdir(dir);
		if (!entry)
			break;

		if (!entry->directory)
			continue;

		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;

		const std::string skin_dir =
			path_join(root, entry->d_name);

		const std::string json_path =
			path_join(skin_dir, "skin.json");

		if (!os_file_exists(json_path.c_str()))
			continue;

		SkinDescriptor descriptor;
		descriptor.root_dir = skin_dir;

		if (!read_skin_metadata(
			    json_path, built_in, descriptor)) {
			blog(
				LOG_WARNING,
				"[Mugen Heartbeat] skin ignored: invalid metadata in '%s'.",
				json_path.c_str());
			continue;
		}

		/* User skins are scanned second and intentionally override a built-in
		   skin with the same id. This makes local skin development painless. */
		by_id[descriptor.info.id] =
			std::move(descriptor);
	}

	os_closedir(dir);
}

static std::string replace_all(std::string text,
			       const std::string &needle,
			       const std::string &replacement)
{
	if (needle.empty())
		return text;

	size_t pos = 0;
	while ((pos = text.find(needle, pos)) != std::string::npos) {
		text.replace(pos, needle.size(), replacement);
		pos += replacement.size();
	}

	return text;
}

static std::wstring utf8_to_wide(const std::string &text)
{
#ifdef _WIN32
	if (text.empty())
		return {};

	const int needed =
		MultiByteToWideChar(
			CP_UTF8, 0,
			text.c_str(),
			static_cast<int>(text.size()),
			nullptr, 0);

	if (needed <= 0)
		return {};

	std::wstring result(
		static_cast<size_t>(needed), L'\0');

	MultiByteToWideChar(
		CP_UTF8, 0,
		text.c_str(),
		static_cast<int>(text.size()),
		result.data(), needed);

	return result;
#else
	(void)text;
	return {};
#endif
}

static bool shell_open(const std::string &target)
{
#ifdef _WIN32
	const std::wstring wide =
		utf8_to_wide(target);

	if (wide.empty())
		return false;

	const HINSTANCE result =
		ShellExecuteW(
			nullptr,
			L"open",
			wide.c_str(),
			nullptr,
			nullptr,
			SW_SHOWNORMAL);

	return reinterpret_cast<INT_PTR>(result) > 32;
#else
	(void)target;
	return false;
#endif
}

static void draw_solid_rect(float width,
			    float height,
			    uint32_t rgba)
{
	if (width <= 0.0f || height <= 0.0f)
		return;

	gs_effect_t *solid =
		obs_get_base_effect(OBS_EFFECT_SOLID);

	if (!solid)
		return;

	gs_eparam_t *color_param =
		gs_effect_get_param_by_name(solid, "color");

	gs_technique_t *technique =
		gs_effect_get_technique(solid, "Solid");

	if (!color_param || !technique)
		return;

	vec4 color;
	vec4_from_rgba_srgb(&color, rgba);

	const bool previous_srgb =
		gs_framebuffer_srgb_enabled();

	gs_enable_framebuffer_srgb(true);

	gs_effect_set_vec4(
		color_param, &color);

	gs_technique_begin(technique);
	gs_technique_begin_pass(technique, 0);

	/* Keep geometry in floating-point space. gs_draw_sprite() accepts
	   integer dimensions, which used to truncate short/diagonal ECG
	   segments and could leave sub-pixel seams between neighbours. */
	gs_draw_quadf(
		nullptr,
		0,
		std::max(1.0f, width),
		std::max(1.0f, height));

	gs_technique_end_pass(technique);
	gs_technique_end(technique);

	gs_enable_framebuffer_srgb(
		previous_srgb);
}

static void draw_solid_rect_at(float x,
			       float y,
			       float width,
			       float height,
			       uint32_t rgba)
{
	if (width <= 0.0f || height <= 0.0f)
		return;

	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	draw_solid_rect(width, height, rgba);
	gs_matrix_pop();
}

static void draw_solid_line_raw(float x0,
				float y0,
				float x1,
				float y1,
				float thickness,
				uint32_t rgba)
{
	const float dx = x1 - x0;
	const float dy = y1 - y0;
	const float length = std::sqrt(dx * dx + dy * dy);

	if (!std::isfinite(length) || length <= 0.001f)
		return;

	const float angle = std::atan2(dy, dx);
	const float half_thickness =
		std::max(0.5f, thickness * 0.5f);

	gs_matrix_push();
	gs_matrix_translate3f(x0, y0, 0.0f);
	gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, angle);

	/* Square caps overlap adjacent segments by half the line thickness.
	   Together with float-sized quads this removes the tiny cracks that
	   became visible when the source was scaled or filtered by OBS. */
	gs_matrix_translate3f(
		-half_thickness,
		-half_thickness,
		0.0f);

	draw_solid_rect(
		length + half_thickness * 2.0f,
		thickness,
		rgba);

	gs_matrix_pop();
}

static void draw_solid_line(float x0,
			    float y0,
			    float x1,
			    float y1,
			    float thickness,
			    uint32_t rgba)
{
	/* Keep the v0.1.32 float geometry + overlapping caps, but do not
	   stack wider translucent copies underneath the ECG. At sharp QRS
	   joints those passes accumulated alpha and produced a thick outlined
	   "glow" instead of real antialiasing. HiDPI Lite already gives us a
	   cleaner result without changing the intended stroke weight. */
	draw_solid_line_raw(
		x0, y0, x1, y1,
		thickness,
		rgba);
}

static MtsdfTextStyle parse_text_style(obs_data_t *data)
{
	MtsdfTextStyle style;

	style.font_face =
		json_string(data, "font", "Arial");

	style.font_size =
		clamp_int(
			json_int(data, "size", 48),
			8, 500);

	style.font_flags = 0;

	if (json_bool(data, "bold", false))
		style.font_flags |= OBS_FONT_BOLD;

	if (json_bool(data, "italic", false))
		style.font_flags |= OBS_FONT_ITALIC;

	style.text_color = 0x00FFFFFF;
	style.text_opacity =
		clamp_int(
			json_int(data, "opacity", 100),
			0, 100);

	style.outline_enabled =
		json_bool(data, "outline", false);

	style.outline_size =
		clamp_int(
			json_int(data, "outline_size", 2),
			1, 20);

	style.outline_color =
		parse_hex_color(
			json_string(
				data,
				"outline_color",
				"#000000"),
			0xFF000000) &
		0x00FFFFFFu;

	style.outline_opacity =
		clamp_int(
			json_int(
				data,
				"outline_opacity",
				100),
			0, 100);

	style.shadow_enabled =
		json_bool(data, "shadow", false);

	style.shadow_x =
		clamp_int(
			json_int(data, "shadow_x", 3),
			0, 30);

	style.shadow_y =
		clamp_int(
			json_int(data, "shadow_y", 3),
			0, 30);

	style.shadow_color =
		parse_hex_color(
			json_string(
				data,
				"shadow_color",
				"#000000"),
			0xFF000000) &
		0x00FFFFFFu;

	style.shadow_opacity =
		clamp_int(
			json_int(
				data,
				"shadow_opacity",
				50),
			0, 100);

	return style;
}

static bool parse_loaded_skin(const SkinDescriptor &descriptor,
			      LoadedSkin &skin,
			      std::string &error)
{
	obs_data_t *root =
		obs_data_create_from_json_file(
			descriptor.json_path.c_str());

	if (!root) {
		error = "Could not parse skin.json";
		return false;
	}

	const int schema = json_int(root, "schema", 1);
	if (schema != 1) {
		error = "Unsupported skin schema";
		obs_data_release(root);
		return false;
	}

	skin = LoadedSkin{};
	skin.info = descriptor.info;

	obs_data_t *canvas =
		obs_data_get_obj(root, "canvas");

	if (canvas) {
		skin.width =
			static_cast<uint32_t>(
				clamp_int(
					json_int(
						canvas,
						"width",
						640),
					64, 4096));

		skin.height =
			static_cast<uint32_t>(
				clamp_int(
					json_int(
						canvas,
						"height",
						260),
					64, 4096));

		skin.background =
			parse_hex_color(
				json_string(
					canvas,
					"background",
					"#111417"),
				0xFF171411);

		obs_data_release(canvas);
	}

	obs_data_t *offline =
		obs_data_get_obj(root, "offline");

	if (offline) {
		skin.offline_color =
			parse_hex_color(
				json_string(
					offline,
					"color",
					"#7c8589"),
				0xFF89857C);

		skin.offline_status =
			json_string(
				offline,
				"status",
				"NO SIGNAL");

		obs_data_release(offline);
	}

	obs_data_array_t *zones =
		obs_data_get_array(root, "zones");

	if (zones) {
		const size_t count =
			obs_data_array_count(zones);

		for (size_t i = 0; i < count; ++i) {
			obs_data_t *item =
				obs_data_array_item(zones, i);

			if (!item)
				continue;

			SkinZone zone;

			zone.max_bpm =
				clamp_int(
					json_int(
						item,
						"max_bpm",
						240),
					30, 300);

			zone.color =
				parse_hex_color(
					json_string(
						item,
						"color",
						"#66cc66"),
					0xFF66CC66);

			zone.status =
				json_string(
					item,
					"status",
					"STABLE");

			skin.zones.push_back(
				std::move(zone));

			obs_data_release(item);
		}

		obs_data_array_release(zones);
	}

	if (skin.zones.empty()) {
		skin.zones.push_back(
			{89, 0xFF66CC66, "STABLE"});
		skin.zones.push_back(
			{119, 0xFF4CB5E0, "CAUTION"});
		skin.zones.push_back(
			{240, 0xFF5757E8, "DANGER"});
	}

	std::sort(
		skin.zones.begin(),
		skin.zones.end(),
		[](const SkinZone &a,
		   const SkinZone &b) {
			return a.max_bpm < b.max_bpm;
		});

	obs_data_array_t *elements =
		obs_data_get_array(root, "elements");

	if (elements) {
		const size_t count =
			obs_data_array_count(elements);

		for (size_t i = 0; i < count; ++i) {
			obs_data_t *item =
				obs_data_array_item(elements, i);

			if (!item)
				continue;

			const std::string type =
				json_string(
					item,
					"type");

			SkinElement element;
			element.x =
				static_cast<float>(
					json_double(
						item, "x", 0.0));
			element.y =
				static_cast<float>(
					json_double(
						item, "y", 0.0));
			element.pulse =
				json_bool(
					item, "pulse", false);

			if (type == "rect") {
				element.type =
					SkinElementType::Rect;

				element.width =
					static_cast<float>(
						std::max(
							0.0,
							json_double(
								item,
								"width",
								0.0)));

				element.height =
					static_cast<float>(
						std::max(
							0.0,
							json_double(
								item,
								"height",
								0.0)));

				element.color_token =
					json_string(
						item,
						"color",
						"#ffffff");

				element.opacity =
					clamp_int(
						json_int(
							item,
							"opacity",
							100),
						0, 100);

				skin.elements.push_back(
					std::move(element));
			} else if (type == "text") {
				element.type =
					SkinElementType::Text;

				element.value =
					json_string(
						item,
						"value");

				element.color_token =
					json_string(
						item,
						"color",
						"#ffffff");

				element.text_style =
					parse_text_style(item);

				element.width =
					static_cast<float>(
						(std::max)(
							0.0,
							json_double(
								item,
								"width",
								0.0)));

				element.height =
					static_cast<float>(
						(std::max)(
							0.0,
							json_double(
								item,
								"height",
								0.0)));

				const std::string align =
					utf8_lower(
						json_string(
							item,
							"align",
							"left"));

				if (align == "center")
					element.text_align =
						SkinTextAlign::Center;
				else if (align == "right")
					element.text_align =
						SkinTextAlign::Right;

				const std::string valign =
					utf8_lower(
						json_string(
							item,
							"valign",
							"top"));

				if (valign == "center" ||
				    valign == "middle") {
					element.text_valign =
						SkinTextVAlign::Center;
				} else if (valign == "bottom") {
					element.text_valign =
						SkinTextVAlign::Bottom;
				}

				element.text_renderer =
					std::make_unique<
						MtsdfTextRenderer>();

				skin.elements.push_back(
					std::move(element));
			} else if (type == "ecg") {
				element.type =
					SkinElementType::Ecg;

				element.width =
					static_cast<float>(
						(std::max)(
							0.0,
							json_double(
								item,
								"width",
								0.0)));

				element.height =
					static_cast<float>(
						(std::max)(
							0.0,
							json_double(
								item,
								"height",
								0.0)));

				element.color_token =
					json_string(
						item,
						"color",
						"zone");

				element.opacity =
					clamp_int(
						json_int(
							item,
							"opacity",
							100),
						0, 100);

				element.ecg_line_thickness =
					static_cast<float>(
						clamp_double(
							json_double(
								item,
								"line_thickness",
								2.0),
							1.0, 8.0));

				element.ecg_history_ms =
					clamp_int(
						json_int(
							item,
							"history_ms",
							3200),
						1000, 8000);

				element.ecg_amplitude =
					static_cast<float>(
						clamp_double(
							json_double(
								item,
								"amplitude",
								0.9),
							0.1, 1.0));

				element.ecg_baseline_opacity =
					clamp_int(
						json_int(
							item,
							"baseline_opacity",
							28),
						0, 100);

				skin.elements.push_back(
					std::move(element));
			}

			obs_data_release(item);
		}

		obs_data_array_release(elements);
	}

	obs_data_release(root);

	if (skin.elements.empty()) {
		error = "Skin has no supported elements";
		return false;
	}

	return true;
}

} // namespace

struct HeartbeatSkinRenderer::Impl {
	mutable std::mutex mutex;
	std::vector<HeartbeatSkinInfo> catalog;
	std::map<std::string, SkinDescriptor> descriptors;

	std::unique_ptr<LoadedSkin> skin;
	std::string selected_id;
	std::string error;
	bool force_reload = false;

	int last_bpm = -999;
	bool last_live = false;
	uint32_t zone_color = 0xFFFFFFFF;
	std::string zone_status = "NO SIGNAL";

	std::deque<int64_t> ecg_beats_ms;
	int64_t last_ecg_beat_ms = 0;
	int64_t ecg_sweep_start_ms = 0;
	float last_pulse_scale = 1.0f;

	void resolve_zone(int bpm, bool live)
	{
		if (!skin) {
			zone_color = 0xFFFFFFFF;
			zone_status = "NO SIGNAL";
			return;
		}

		if (!live) {
			zone_color =
				skin->offline_color;
			zone_status =
				skin->offline_status;
			return;
		}

		const SkinZone *selected =
			&skin->zones.back();

		for (const auto &zone :
		     skin->zones) {
			if (bpm <= zone.max_bpm) {
				selected = &zone;
				break;
			}
		}

		zone_color = selected->color;
		zone_status = selected->status;
	}

	uint32_t resolve_color(
		const std::string &token,
		uint32_t fallback) const
	{
		if (token == "zone")
			return zone_color;

		return parse_hex_color(
			token, fallback);
	}

	void refresh_text_elements(
		int bpm,
		bool live)
	{
		if (!skin)
			return;

		resolve_zone(bpm, live);

		for (auto &element :
		     skin->elements) {
			if (element.type !=
			    SkinElementType::Text ||
			    !element.text_renderer)
				continue;

			std::string text =
				element.value;

			text = replace_all(
				text,
				"{bpm}",
				live
					? std::to_string(bpm)
					: "--");

			text = replace_all(
				text,
				"{status}",
				zone_status);

			text = replace_all(
				text,
				"{heart}",
				"\xE2\x99\xA5");

			MtsdfTextStyle style =
				element.text_style;

			style.text_color =
				resolve_color(
					element.color_token,
					0xFFFFFFFF) &
				0x00FFFFFFu;

			element.text_renderer->update(
				text, style);
		}
	}

	void update_ecg_clock(float pulse_scale, bool animate)
	{
		const int64_t now_ms =
			static_cast<int64_t>(
				os_gettime_ns() / 1000000ULL);

		const bool pulse_active =
			animate &&
			std::isfinite(pulse_scale) &&
			pulse_scale > 1.005f;

		const bool was_active =
			last_pulse_scale > 1.005f;

		if (pulse_active && !was_active &&
		    (last_ecg_beat_ms <= 0 ||
		     now_ms - last_ecg_beat_ms >= 180)) {
			ecg_beats_ms.push_back(now_ms);
			last_ecg_beat_ms = now_ms;
		}

		last_pulse_scale =
			animate ? pulse_scale : 1.0f;

		while (!ecg_beats_ms.empty() &&
		       now_ms - ecg_beats_ms.front() > 9000) {
			ecg_beats_ms.pop_front();
		}
	}

	void render_ecg_element(
		const SkinElement &element,
		bool animate)
	{
		if (element.width <= 0.0f ||
		    element.height <= 0.0f)
			return;

		const uint32_t raw_color =
			resolve_color(
				element.color_token,
				0xFFFFFFFF);

		const uint32_t baseline_color =
			color_with_opacity(
				raw_color,
				(element.opacity *
				 element.ecg_baseline_opacity) /
					100);

		const uint32_t trace_color =
			color_with_opacity(
				raw_color,
				element.opacity);

		const float thickness =
			(std::max)(
				1.0f,
				element.ecg_line_thickness);

		const float baseline_y =
			element.y +
			element.height * 0.56f;

		/* The quiet baseline spans the monitor, while the bright trace is
		   written from left to right just like a classic bedside display. */
		draw_solid_rect_at(
			element.x,
			baseline_y - thickness * 0.5f,
			element.width,
			thickness,
			baseline_color);

		if (!animate)
			return;

		const int64_t now_ms =
			static_cast<int64_t>(
				os_gettime_ns() / 1000000ULL);

		const int64_t sweep_ms =
			(std::max)(
				1000,
				element.ecg_history_ms);

		if (ecg_sweep_start_ms <= 0)
			ecg_sweep_start_ms = now_ms;

		while (now_ms - ecg_sweep_start_ms >=
		       sweep_ms) {
			ecg_sweep_start_ms += sweep_ms;
		}

		while (!ecg_beats_ms.empty() &&
		       ecg_beats_ms.front() <
			       ecg_sweep_start_ms) {
			ecg_beats_ms.pop_front();
		}

		const float sweep01 =
			static_cast<float>(
				now_ms - ecg_sweep_start_ms) /
			static_cast<float>(sweep_ms);

		const float cursor_x =
			element.x +
			element.width *
				(std::min)(
					1.0f,
					(std::max)(
						0.0f,
						sweep01));

		const float amp =
			element.height *
			0.44f *
			element.ecg_amplitude;

		const float wave_width =
			(std::min)(
				element.width * 0.16f,
				(std::max)(
					42.0f,
					element.height * 1.30f));

		struct EcgPoint {
			float x;
			float y;
		};

		std::vector<EcgPoint> points;
		points.reserve(128);
		points.push_back(
			{element.x, baseline_y});

		auto append_point =
			[&](float x, float y) {
				if (x < element.x)
					x = element.x;

				if (x > cursor_x)
					x = cursor_x;

				if (!points.empty() &&
				    std::fabs(
					    points.back().x - x) <
					    0.01f &&
				    std::fabs(
					    points.back().y - y) <
					    0.01f)
					return;

				points.push_back({x, y});
			};

		auto append_partial_segment =
			[&](float x0,
			    float y0,
			    float x1,
			    float y1) -> bool {
				if (cursor_x >= x1) {
					append_point(x1, y1);
					return true;
				}

				if (cursor_x <= x0)
					return false;

				const float span =
					(std::max)(
						0.001f,
						x1 - x0);

				const float t =
					(cursor_x - x0) /
					span;

				append_point(
					cursor_x,
					y0 + (y1 - y0) * t);

				return false;
			};

		for (const int64_t beat_ms :
		     ecg_beats_ms) {
			if (beat_ms <
			    ecg_sweep_start_ms ||
			    beat_ms > now_ms)
				continue;

			const float beat01 =
				static_cast<float>(
					beat_ms -
					ecg_sweep_start_ms) /
				static_cast<float>(
					sweep_ms);

			const float beat_x =
				element.x +
				element.width * beat01;

			if (beat_x > cursor_x)
				break;

			if (points.back().x < beat_x) {
				append_point(
					beat_x,
					baseline_y);
			}

			const float xs[] = {
				0.00f, 0.08f, 0.13f, 0.18f,
				0.26f, 0.31f, 0.36f, 0.42f,
				0.54f, 0.68f, 0.78f, 0.88f,
				1.00f};

			const float ys[] = {
				0.00f, 0.00f, -0.10f, 0.00f,
				0.00f, 0.20f, -1.00f, 0.34f,
				0.00f, 0.00f, -0.22f, 0.00f,
				0.00f};

			float previous_x =
				beat_x +
				xs[0] * wave_width;

			float previous_y =
				baseline_y +
				ys[0] * amp;

			append_point(
				previous_x,
				previous_y);

			bool complete = true;

			for (size_t i = 1;
			     i < sizeof(xs) / sizeof(xs[0]);
			     ++i) {
				const float next_x =
					beat_x +
					xs[i] *
						wave_width;

				const float next_y =
					baseline_y +
					ys[i] * amp;

				if (!append_partial_segment(
					    previous_x,
					    previous_y,
					    next_x,
					    next_y)) {
					complete = false;
					break;
				}

				previous_x = next_x;
				previous_y = next_y;
			}

			if (!complete)
				break;
		}

		if (points.back().x < cursor_x) {
			append_point(
				cursor_x,
				baseline_y);
		}

		for (size_t i = 1;
		     i < points.size();
		     ++i) {
			draw_solid_line(
				points[i - 1].x,
				points[i - 1].y,
				points[i].x,
				points[i].y,
				thickness,
				trace_color);
		}

		/* Tiny bright writing head: the direction is visually obvious even
		   when the current section of the trace is flat. */
		draw_solid_rect_at(
			cursor_x - thickness,
			baseline_y - thickness,
			thickness * 2.0f,
			thickness * 2.0f,
			trace_color);
	}

	bool render_element(
		SkinElement &element,
		float pulse_scale,
		bool animate)
	{
		const float scale =
			animate && element.pulse
				? pulse_scale
				: 1.0f;

		if (element.type ==
		    SkinElementType::Rect) {
			if (element.width <= 0.0f ||
			    element.height <= 0.0f)
				return true;

			const uint32_t color =
				color_with_opacity(
					resolve_color(
						element.color_token,
						0xFFFFFFFF),
					element.opacity);

			gs_matrix_push();

			if (std::isfinite(scale) &&
			    std::fabs(scale - 1.0f) >
				    0.0001f) {
				gs_matrix_translate3f(
					element.x +
						element.width * 0.5f,
					element.y +
						element.height * 0.5f,
					0.0f);

				gs_matrix_scale3f(
					scale, scale, 1.0f);

				gs_matrix_translate3f(
					-element.width * 0.5f,
					-element.height * 0.5f,
					0.0f);
			} else {
				gs_matrix_translate3f(
					element.x,
					element.y,
					0.0f);
			}

			draw_solid_rect(
				element.width,
				element.height,
				color);

			gs_matrix_pop();
			return true;
		}

		if (element.type == SkinElementType::Ecg) {
			render_ecg_element(
				element,
				animate && element.pulse);
			return true;
		}

		if (!element.text_renderer ||
		    !element.text_renderer->ready())
			return false;

		const float width =
			static_cast<float>(
				element.text_renderer->width());

		const float height =
			static_cast<float>(
				element.text_renderer->height());

		/* Use exact unpadded glyph bounds from the authoritative MTSDF
		   layout. Skin x/y can therefore define a real layout box instead
		   of guessing around the distance-field margin. */
		const float visual_left =
			element.text_renderer->visual_left();

		const float visual_top =
			element.text_renderer->visual_top();

		const float visual_width =
			(std::max)(
				1.0f,
				element.text_renderer->visual_width());

		const float visual_height =
			(std::max)(
				1.0f,
				element.text_renderer->visual_height());

		float visible_x = element.x;
		float visible_y = element.y;

		if (element.width > 0.0f) {
			if (element.text_align ==
			    SkinTextAlign::Center) {
				visible_x +=
					(element.width - visual_width) *
					0.5f;
			} else if (element.text_align ==
				   SkinTextAlign::Right) {
				visible_x +=
					element.width - visual_width;
			}
		}

		if (element.height > 0.0f) {
			if (element.text_valign ==
			    SkinTextVAlign::Center) {
				visible_y +=
					(element.height - visual_height) *
					0.5f;
			} else if (element.text_valign ==
				   SkinTextVAlign::Bottom) {
				visible_y +=
					element.height - visual_height;
			}
		}

		const float draw_x =
			visible_x - visual_left;

		const float draw_y =
			visible_y - visual_top;

		gs_matrix_push();

		if (std::isfinite(scale) &&
		    std::fabs(scale - 1.0f) >
			    0.0001f) {
			/* Pulse around the visible glyph center, not the padded
			   renderer surface center. */
			gs_matrix_translate3f(
				visible_x + visual_width * 0.5f,
				visible_y + visual_height * 0.5f,
				0.0f);

			gs_matrix_scale3f(
				scale, scale, 1.0f);

			gs_matrix_translate3f(
				-(visual_left +
				  visual_width * 0.5f),
				-(visual_top +
				  visual_height * 0.5f),
				0.0f);
		} else {
			gs_matrix_translate3f(
				draw_x,
				draw_y,
				0.0f);
		}

		const bool rendered =
			element.text_renderer->render();

		gs_matrix_pop();
		return rendered;
	}
};

HeartbeatSkinRenderer::HeartbeatSkinRenderer()
	: impl_(std::make_unique<Impl>())
{
	reload_catalog();
}

HeartbeatSkinRenderer::~HeartbeatSkinRenderer() = default;

std::string HeartbeatSkinRenderer::user_skin_directory()
{
	char *path =
		obs_module_config_path("skins");

	if (!path)
		return {};

	std::string result = path;
	bfree(path);
	return result;
}

static bool copy_skin_doc_file(
	const std::string &source_path,
	const std::string &destination_path)
{
	std::ifstream input(
		source_path,
		std::ios::binary);

	if (!input.is_open())
		return false;

	std::ofstream output(
		destination_path,
		std::ios::binary |
			std::ios::trunc);

	if (!output.is_open())
		return false;

	output << input.rdbuf();
	return output.good();
}

static void sync_user_skin_docs(
	const std::string &user_path)
{
	static const char *files[] = {
		"README.md",
		"README.ru.md",
		"SKIN-FORMAT.md",
		"SKIN-FORMAT.ru.md",
	};

	for (const char *file_name : files) {
		const std::string module_relative =
			path_join("skins", file_name);

		char *source_path =
			obs_module_file(
				module_relative.c_str());

		if (!source_path)
			continue;

		const std::string destination_path =
			path_join(
				user_path,
				file_name);

		if (!copy_skin_doc_file(
			    source_path,
			    destination_path)) {
			blog(
				LOG_WARNING,
				"[Mugen Heartbeat] failed to sync user-skin doc: %s",
				file_name);
		}

		bfree(source_path);
	}
}

bool HeartbeatSkinRenderer::ensure_user_skin_directory()
{
	const std::string path =
		user_skin_directory();

	if (path.empty())
		return false;

	const int result =
		os_mkdirs(path.c_str());

	const bool ready =
		result == MKDIR_SUCCESS ||
		result == MKDIR_EXISTS;

	if (ready)
		sync_user_skin_docs(path);

	return ready;
}

bool HeartbeatSkinRenderer::open_user_skin_directory()
{
	if (!ensure_user_skin_directory())
		return false;

	return shell_open(
		user_skin_directory());
}

bool HeartbeatSkinRenderer::open_skin_gallery()
{
	return shell_open(
		"https://github.com/Mugen-Art-Lab/Mugen-Heartbeat");
}

void HeartbeatSkinRenderer::reload_catalog()
{
	if (!impl_)
		return;

	std::map<std::string, SkinDescriptor>
		by_id;

	char *built_in_path =
		obs_module_file("skins");

	if (built_in_path) {
		scan_skin_root(
			built_in_path,
			true,
			by_id);

		bfree(built_in_path);
	}

	ensure_user_skin_directory();

	const std::string user_path =
		user_skin_directory();

	scan_skin_root(
		user_path,
		false,
		by_id);

	std::vector<HeartbeatSkinInfo>
		catalog;

	for (const auto &[id, descriptor] :
	     by_id) {
		(void)id;
		catalog.push_back(
			descriptor.info);
	}

	std::sort(
		catalog.begin(),
		catalog.end(),
		info_less);

	{
		std::scoped_lock lock(
			impl_->mutex);

		impl_->descriptors =
			std::move(by_id);

		impl_->catalog =
			std::move(catalog);

		if (!impl_->selected_id.empty())
			impl_->force_reload = true;

		blog(
			LOG_INFO,
			"[Mugen Heartbeat] skin catalog: %zu skin(s) available.",
			impl_->catalog.size());
	}
}

std::vector<HeartbeatSkinInfo>
HeartbeatSkinRenderer::catalog() const
{
	if (!impl_)
		return {};

	std::scoped_lock lock(
		impl_->mutex);

	return impl_->catalog;
}

bool HeartbeatSkinRenderer::info_for(
	const std::string &id,
	HeartbeatSkinInfo &out) const
{
	if (!impl_)
		return false;

	std::scoped_lock lock(
		impl_->mutex);

	for (const auto &info :
	     impl_->catalog) {
		if (info.id == id) {
			out = info;
			return true;
		}
	}

	return false;
}

bool HeartbeatSkinRenderer::ensure_selected(
	const std::string &id)
{
	if (!impl_ || id.empty())
		return false;

	std::scoped_lock lock(
		impl_->mutex);

	if (impl_->skin &&
	    impl_->selected_id == id &&
	    !impl_->force_reload) {
		return true;
	}

	auto it =
		impl_->descriptors.find(id);

	if (it == impl_->descriptors.end()) {
		impl_->error =
			"Skin not found: " + id;
		return false;
	}

	auto loaded =
		std::make_unique<LoadedSkin>();

	std::string error;

	if (!parse_loaded_skin(
		    it->second,
		    *loaded,
		    error)) {
		impl_->error =
			error.empty()
				? "Could not load skin"
				: error;

		blog(
			LOG_WARNING,
			"[Mugen Heartbeat] skin '%s' failed to load: %s.",
			id.c_str(),
			impl_->error.c_str());

		return false;
	}

	impl_->skin =
		std::move(loaded);

	impl_->selected_id = id;
	impl_->error.clear();
	impl_->force_reload = false;

	impl_->last_bpm = -999;
	impl_->last_live = false;
	impl_->ecg_beats_ms.clear();
	impl_->last_ecg_beat_ms = 0;
	impl_->last_pulse_scale = 1.0f;

	impl_->refresh_text_elements(
		0, false);

	blog(
		LOG_INFO,
		"[Mugen Heartbeat] skin loaded: '%s' by %s (%ux%u).",
		impl_->skin->info.name.c_str(),
		impl_->skin->info.author.c_str(),
		impl_->skin->width,
		impl_->skin->height);

	return true;
}

std::string
HeartbeatSkinRenderer::selected_id() const
{
	if (!impl_)
		return {};

	std::scoped_lock lock(
		impl_->mutex);

	return impl_->selected_id;
}

void HeartbeatSkinRenderer::update(
	int bpm,
	bool live)
{
	if (!impl_)
		return;

	std::scoped_lock lock(
		impl_->mutex);

	if (!impl_->skin)
		return;

	if (impl_->last_bpm == bpm &&
	    impl_->last_live == live)
		return;

	impl_->last_bpm = bpm;
	impl_->last_live = live;

	if (!live) {
		impl_->ecg_beats_ms.clear();
		impl_->last_ecg_beat_ms = 0;
		impl_->last_pulse_scale = 1.0f;
	}

	impl_->refresh_text_elements(
		bpm, live);
}

bool HeartbeatSkinRenderer::render(
	float pulse_scale,
	bool animate,
	int background_opacity)
{
	if (!impl_)
		return false;

	std::scoped_lock lock(
		impl_->mutex);

	if (!impl_->skin)
		return false;

	impl_->update_ecg_clock(
		pulse_scale, animate);

	for (const auto &element :
	     impl_->skin->elements) {
		if (element.type ==
			    SkinElementType::Text &&
		    (!element.text_renderer ||
		     !element.text_renderer->ready())) {
			return false;
		}
	}

	const bool custom_background_opacity =
		impl_->skin->info.background_opacity_customizable;

	const int effective_background_opacity =
		custom_background_opacity
			? clamp_int(
				background_opacity,
				0,
				100)
			: 100;

	/* For a native skin Background opacity control the percentage is
	   absolute: 100% means fully opaque and 0% fully transparent.
	   ECG/text/status keep their own independent opacity. */
	const uint32_t background =
		custom_background_opacity
			? color_with_absolute_opacity(
				impl_->skin->background,
				effective_background_opacity)
			: impl_->skin->background;

	if (((background >> 24) & 0xFFu) > 0) {
		draw_solid_rect(
			static_cast<float>(
				impl_->skin->width),
			static_cast<float>(
				impl_->skin->height),
			background);
	}

	for (auto &element :
	     impl_->skin->elements) {
		if (!impl_->render_element(
			    element,
			    pulse_scale,
			    animate))
			return false;
	}

	return true;
}

bool HeartbeatSkinRenderer::ready() const
{
	if (!impl_)
		return false;

	std::scoped_lock lock(
		impl_->mutex);

	return impl_->skin != nullptr;
}

uint32_t HeartbeatSkinRenderer::width() const
{
	if (!impl_)
		return 0;

	std::scoped_lock lock(
		impl_->mutex);

	return impl_->skin
		       ? impl_->skin->width
		       : 0;
}

uint32_t HeartbeatSkinRenderer::height() const
{
	if (!impl_)
		return 0;

	std::scoped_lock lock(
		impl_->mutex);

	return impl_->skin
		       ? impl_->skin->height
		       : 0;
}

std::string HeartbeatSkinRenderer::error() const
{
	if (!impl_)
		return {};

	std::scoped_lock lock(
		impl_->mutex);

	return impl_->error;
}

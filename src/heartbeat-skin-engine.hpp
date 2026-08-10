// SPDX-FileCopyrightText: 2026 Mugen Art Lab
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "mtsdf-text-renderer.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct HeartbeatSkinInfo {
	std::string id;
	std::string name;
	std::string author;
	std::string version;
	std::string description;
	bool built_in = false;

	/* Optional skin-declared customization exposed by the OBS Properties UI. */
	bool background_opacity_customizable = false;
	int background_opacity_default = 100;
};

class HeartbeatSkinRenderer {
public:
	HeartbeatSkinRenderer();
	~HeartbeatSkinRenderer();

	HeartbeatSkinRenderer(const HeartbeatSkinRenderer &) = delete;
	HeartbeatSkinRenderer &operator=(const HeartbeatSkinRenderer &) = delete;

	void reload_catalog();
	std::vector<HeartbeatSkinInfo> catalog() const;
	bool info_for(const std::string &id, HeartbeatSkinInfo &out) const;

	/* Safe to call from video_tick. Loads/reloads the selected skin only
	   when the id changed or Refresh requested a reload. */
	bool ensure_selected(const std::string &id);
	std::string selected_id() const;

	void update(int bpm, bool live);
	bool render(float pulse_scale,
		    bool animate,
		    int background_opacity = 100);

	bool ready() const;
	uint32_t width() const;
	uint32_t height() const;
	std::string error() const;

	static std::string user_skin_directory();
	static bool ensure_user_skin_directory();
	static bool open_user_skin_directory();
	static bool open_skin_gallery();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

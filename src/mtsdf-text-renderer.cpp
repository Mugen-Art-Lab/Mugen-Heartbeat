// SPDX-FileCopyrightText: 2026 Mugen Art Lab
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "mtsdf-text-renderer.hpp"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <msdfgen.h>

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr double FIELD_SCALE = 64.0;
constexpr double FIELD_PX_RANGE = 64.0;
constexpr double EDGE_COLORING_ANGLE = 3.0;

constexpr uint32_t ATLAS_WIDTH = 1024;
constexpr uint32_t ATLAS_HEIGHT = 1024;
constexpr uint32_t ATLAS_GUTTER = 2;

constexpr const char *PREWARM_TEXT =
	"\xE2\x99\xA5"
	"0123456789BPM-";

struct GlyphKey {
	uint64_t face_hash = 0;
	uint16_t glyph_index = 0;

	bool operator<(const GlyphKey &other) const
	{
		if (face_hash != other.face_hash)
			return face_hash < other.face_hash;
		return glyph_index < other.glyph_index;
	}
};

struct GlyphEntry {
	GlyphKey key;
	bool drawable = false;

	uint32_t atlas_x = 0;
	uint32_t atlas_y = 0;
	uint32_t atlas_width = 0;
	uint32_t atlas_height = 0;

	float u0 = 0.0f;
	float v0 = 0.0f;
	float u1 = 0.0f;
	float v1 = 0.0f;

	float plane_left = 0.0f;
	float plane_top = 0.0f;
	float plane_right = 0.0f;
	float plane_bottom = 0.0f;

	/* Unpadded shape bounds in em-space. */
	float visible_plane_left = 0.0f;
	float visible_plane_top = 0.0f;
	float visible_plane_right = 0.0f;
	float visible_plane_bottom = 0.0f;
};

struct CapturedGlyph {
	ComPtr<IDWriteFontFace> face;
	uint16_t glyph_index = 0;
	float em_size = 1.0f;
	float baseline_x = 0.0f;
	float baseline_y = 0.0f;
	float pen_x = 0.0f;
	DWRITE_GLYPH_OFFSET offset{};
	uint32_t text_start = 0;
	uint32_t text_length = 0;
};

struct RenderVertex {
	float x = 0.0f;
	float y = 0.0f;
	float u = 0.0f;
	float v = 0.0f;
	float pulse = 0.0f;
	float pulse_center_x = 0.0f;
	float pulse_center_y = 0.0f;
};

static bool same_point(const msdfgen::Point2 &a, const msdfgen::Point2 &b)
{
	const double dx = a.x - b.x;
	const double dy = a.y - b.y;
	return dx * dx + dy * dy < 1e-12;
}

static std::wstring utf8_to_wide(const std::string &text)
{
	if (text.empty())
		return {};

	const int count = MultiByteToWideChar(
		CP_UTF8, MB_ERR_INVALID_CHARS,
		text.data(), static_cast<int>(text.size()),
		nullptr, 0);

	if (count <= 0)
		return {};

	std::wstring result(static_cast<size_t>(count), L'\0');

	if (MultiByteToWideChar(
		    CP_UTF8, MB_ERR_INVALID_CHARS,
		    text.data(), static_cast<int>(text.size()),
		    result.data(), count) != count)
		return {};

	return result;
}

static uint8_t to_u8(float value)
{
	const float clamped = (std::max)(0.0f, (std::min)(1.0f, value));
	return static_cast<uint8_t>(std::lround(clamped * 255.0f));
}

static uint32_t color_with_opacity(uint32_t color, int opacity)
{
	const int clamped = (std::max)(0, (std::min)(100, opacity));
	const uint32_t alpha = static_cast<uint32_t>(clamped * 255 / 100);
	return (color & 0x00FFFFFFu) | (alpha << 24);
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
	const auto *bytes = static_cast<const uint8_t *>(data);
	for (size_t i = 0; i < size; ++i) {
		hash ^= static_cast<uint64_t>(bytes[i]);
		hash *= 1099511628211ULL;
	}
	return hash;
}

static uint64_t font_face_hash(IDWriteFontFace *face)
{
	if (!face)
		return 0;

	uint64_t hash = 1469598103934665603ULL;
	UINT32 file_count = 0;

	if (SUCCEEDED(face->GetFiles(&file_count, nullptr)) && file_count > 0) {
		std::vector<IDWriteFontFile *> files(file_count, nullptr);

		if (SUCCEEDED(face->GetFiles(&file_count, files.data()))) {
			for (UINT32 i = 0; i < file_count; ++i) {
				IDWriteFontFile *file = files[i];
				if (!file)
					continue;

				const void *reference_key = nullptr;
				UINT32 reference_key_size = 0;

				if (SUCCEEDED(file->GetReferenceKey(
					    &reference_key, &reference_key_size)) &&
				    reference_key && reference_key_size > 0) {
					hash = hash_bytes(hash, reference_key, reference_key_size);
				}

				file->Release();
			}

			const UINT32 face_index = face->GetIndex();
			const DWRITE_FONT_SIMULATIONS simulations = face->GetSimulations();
			const DWRITE_FONT_FACE_TYPE type = face->GetType();

			hash = hash_bytes(hash, &face_index, sizeof(face_index));
			hash = hash_bytes(hash, &simulations, sizeof(simulations));
			hash = hash_bytes(hash, &type, sizeof(type));
			return hash;
		}

		for (auto *file : files) {
			if (file)
				file->Release();
		}
	}

	const uintptr_t identity = reinterpret_cast<uintptr_t>(face);
	return hash_bytes(hash, &identity, sizeof(identity));
}

class ShapeGeometrySink final : public ID2D1SimplifiedGeometrySink {
public:
	explicit ShapeGeometrySink(msdfgen::Shape &shape) : shape_(shape) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override
	{
		if (!object)
			return E_POINTER;
		if (iid == __uuidof(IUnknown) ||
		    iid == __uuidof(ID2D1SimplifiedGeometrySink)) {
			*object = static_cast<ID2D1SimplifiedGeometrySink *>(this);
			AddRef();
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }

	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG value = --refs_;
		if (value == 0)
			delete this;
		return value;
	}

	void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE) override {}
	void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) override {}

	void STDMETHODCALLTYPE BeginFigure(
		D2D1_POINT_2F start, D2D1_FIGURE_BEGIN) override
	{
		contour_ = &shape_.addContour();
		start_ = convert(start);
		current_ = start_;
		figure_open_ = true;
	}

	void STDMETHODCALLTYPE AddLines(
		const D2D1_POINT_2F *points, UINT32 count) override
	{
		if (!figure_open_ || !contour_ || !points)
			return;

		for (UINT32 i = 0; i < count; ++i) {
			const msdfgen::Point2 next = convert(points[i]);
			if (!same_point(current_, next))
				contour_->addEdge(msdfgen::EdgeHolder(current_, next));
			current_ = next;
		}
	}

	void STDMETHODCALLTYPE AddBeziers(
		const D2D1_BEZIER_SEGMENT *beziers, UINT32 count) override
	{
		if (!figure_open_ || !contour_ || !beziers)
			return;

		for (UINT32 i = 0; i < count; ++i) {
			const msdfgen::Point2 c1 = convert(beziers[i].point1);
			const msdfgen::Point2 c2 = convert(beziers[i].point2);
			const msdfgen::Point2 end = convert(beziers[i].point3);
			contour_->addEdge(msdfgen::EdgeHolder(current_, c1, c2, end));
			current_ = end;
		}
	}

	void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END end) override
	{
		if (figure_open_ && contour_ &&
		    end == D2D1_FIGURE_END_CLOSED &&
		    !same_point(current_, start_)) {
			contour_->addEdge(msdfgen::EdgeHolder(current_, start_));
		}
		contour_ = nullptr;
		figure_open_ = false;
	}

	HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }

private:
	static msdfgen::Point2 convert(const D2D1_POINT_2F &p)
	{
		return msdfgen::Point2(
			static_cast<double>(p.x),
			-static_cast<double>(p.y));
	}

	std::atomic<ULONG> refs_{1};
	msdfgen::Shape &shape_;
	msdfgen::Contour *contour_ = nullptr;
	msdfgen::Point2 start_;
	msdfgen::Point2 current_;
	bool figure_open_ = false;
};

class CaptureTextRenderer final : public IDWriteTextRenderer {
public:
	explicit CaptureTextRenderer(std::vector<CapturedGlyph> &glyphs)
		: glyphs_(glyphs) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override
	{
		if (!object)
			return E_POINTER;

		if (iid == __uuidof(IUnknown) ||
		    iid == __uuidof(IDWritePixelSnapping) ||
		    iid == __uuidof(IDWriteTextRenderer)) {
			*object = static_cast<IDWriteTextRenderer *>(this);
			AddRef();
			return S_OK;
		}

		*object = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }

	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG value = --refs_;
		if (value == 0)
			delete this;
		return value;
	}

	HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void *, BOOL *disabled) override
	{
		if (!disabled)
			return E_POINTER;
		*disabled = TRUE;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetCurrentTransform(void *, DWRITE_MATRIX *matrix) override
	{
		if (!matrix)
			return E_POINTER;
		matrix->m11 = 1.0f;
		matrix->m12 = 0.0f;
		matrix->m21 = 0.0f;
		matrix->m22 = 1.0f;
		matrix->dx = 0.0f;
		matrix->dy = 0.0f;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void *, FLOAT *pixels_per_dip) override
	{
		if (!pixels_per_dip)
			return E_POINTER;
		*pixels_per_dip = 1.0f;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DrawGlyphRun(
		void *, FLOAT baseline_origin_x, FLOAT baseline_origin_y,
		DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN *glyph_run,
		const DWRITE_GLYPH_RUN_DESCRIPTION *description, IUnknown *) override
	{
		if (!glyph_run || !glyph_run->fontFace || glyph_run->glyphCount == 0)
			return S_OK;

		float pen_x = 0.0f;
		DWRITE_FONT_METRICS font_metrics{};
		glyph_run->fontFace->GetMetrics(&font_metrics);

		for (UINT32 i = 0; i < glyph_run->glyphCount; ++i) {
			CapturedGlyph captured;
			captured.face = glyph_run->fontFace;
			captured.glyph_index = glyph_run->glyphIndices[i];
			captured.em_size = glyph_run->fontEmSize;
			captured.baseline_x = baseline_origin_x;
			captured.baseline_y = baseline_origin_y;
			captured.pen_x = pen_x;

			if (glyph_run->glyphOffsets)
				captured.offset = glyph_run->glyphOffsets[i];

			if (description &&
			    description->clusterMap &&
			    description->stringLength > 0) {
				UINT32 cluster_text_start = 0;
				UINT16 cluster_glyph_start = 0;
				bool cluster_found = false;

				for (UINT32 j = 0;
				     j < description->stringLength;
				     ++j) {
					const UINT16 cluster =
						description->clusterMap[j];

					if (cluster <= i &&
					    (!cluster_found ||
					     cluster >= cluster_glyph_start)) {
						cluster_text_start = j;
						cluster_glyph_start = cluster;
						cluster_found = true;
					}
				}

				if (cluster_found) {
					UINT32 cluster_text_end =
						description->stringLength;

					for (UINT32 j =
						     cluster_text_start + 1;
					     j < description->stringLength;
					     ++j) {
						if (description->clusterMap[j] !=
						    cluster_glyph_start) {
							cluster_text_end = j;
							break;
						}
					}

					captured.text_start =
						description->textPosition +
						cluster_text_start;

					captured.text_length =
						cluster_text_end -
						cluster_text_start;
				}
			}

			glyphs_.push_back(std::move(captured));

			float advance = 0.0f;

			if (glyph_run->glyphAdvances) {
				advance = glyph_run->glyphAdvances[i];
			} else if (font_metrics.designUnitsPerEm > 0) {
				DWRITE_GLYPH_METRICS metrics{};
				const UINT16 index = glyph_run->glyphIndices[i];

				if (SUCCEEDED(glyph_run->fontFace->GetDesignGlyphMetrics(
					    &index, 1, &metrics, FALSE))) {
					advance =
						static_cast<float>(metrics.advanceWidth) *
						glyph_run->fontEmSize /
						static_cast<float>(font_metrics.designUnitsPerEm);
				}
			}

			pen_x += advance;
		}

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DrawUnderline(
		void *, FLOAT, FLOAT, const DWRITE_UNDERLINE *, IUnknown *) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DrawStrikethrough(
		void *, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH *, IUnknown *) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DrawInlineObject(
		void *, FLOAT, FLOAT, IDWriteInlineObject *,
		BOOL, BOOL, IUnknown *) override
	{
		return S_OK;
	}

private:
	std::atomic<ULONG> refs_{1};
	std::vector<CapturedGlyph> &glyphs_;
};

class GlyphAtlas {
public:
	GlyphAtlas()
	{
		pixels_.assign(
			static_cast<size_t>(ATLAS_WIDTH) *
				static_cast<size_t>(ATLAS_HEIGHT) * 4,
			0);
	}

	void reset()
	{
		entries_.clear();
		std::fill(pixels_.begin(), pixels_.end(), static_cast<uint8_t>(0));
		cursor_x_ = ATLAS_GUTTER;
		cursor_y_ = ATLAS_GUTTER;
		row_height_ = 0;
		dirty_ = true;
		++atlas_rebuild_count_;
	}

	const GlyphEntry *ensure(
		IDWriteFontFace *face, uint16_t glyph_index, std::string &error)
	{
		if (!face) {
			error = "MTSDF atlas: null font face.";
			return nullptr;
		}

		const GlyphKey key{font_face_hash(face), glyph_index};
		auto existing = entries_.find(key);

		if (existing != entries_.end()) {
			++glyph_cache_hit_count_;
			return &existing->second;
		}

		GlyphEntry entry;
		entry.key = key;

		msdfgen::Shape shape;
		auto *sink = new ShapeGeometrySink(shape);
		const UINT16 index = glyph_index;

		const HRESULT hr = face->GetGlyphRunOutline(
			1.0f, &index, nullptr, nullptr, 1, FALSE, FALSE, sink);

		sink->Release();

		if (FAILED(hr)) {
			error = "MTSDF atlas: DirectWrite glyph outline extraction failed.";
			return nullptr;
		}

		if (shape.edgeCount() == 0) {
			entry.drawable = false;
			auto inserted = entries_.emplace(key, entry);
			++glyph_generate_count_;
			return &inserted.first->second;
		}

		shape.normalize();

		if (!shape.validate()) {
			error = "MTSDF atlas: invalid glyph vector shape.";
			return nullptr;
		}

		msdfgen::edgeColoringSimple(shape, EDGE_COLORING_ANGLE);
		const auto bounds = shape.getBounds();

		if (!(bounds.r > bounds.l) || !(bounds.t > bounds.b)) {
			error = "MTSDF atlas: invalid glyph bounds.";
			return nullptr;
		}

		const double pad_px = std::ceil(FIELD_PX_RANGE * 0.5) + 3.0;
		const double pad_em = pad_px / FIELD_SCALE;

		const int bitmap_width = (std::max)(
			8,
			static_cast<int>(std::ceil(
				(bounds.r - bounds.l) * FIELD_SCALE + 2.0 * pad_px)));

		const int bitmap_height = (std::max)(
			8,
			static_cast<int>(std::ceil(
				(bounds.t - bounds.b) * FIELD_SCALE + 2.0 * pad_px)));

		uint32_t packed_x = 0;
		uint32_t packed_y = 0;

		if (!allocate_region(
			    static_cast<uint32_t>(bitmap_width),
			    static_cast<uint32_t>(bitmap_height),
			    packed_x, packed_y)) {
			error = "MTSDF atlas: 1024x1024 atlas is full; using GDI+ fallback.";
			return nullptr;
		}

		msdfgen::Bitmap<float, 4> bitmap(bitmap_width, bitmap_height);

		const msdfgen::Projection projection(
			msdfgen::Vector2(FIELD_SCALE),
			msdfgen::Vector2(-bounds.l + pad_em, -bounds.b + pad_em));

		const msdfgen::SDFTransformation transformation(
			projection,
			msdfgen::Range(FIELD_PX_RANGE / FIELD_SCALE));

		msdfgen::generateMTSDF(bitmap, shape, transformation);

		for (int y = 0; y < bitmap_height; ++y) {
			const int source_y = bitmap_height - 1 - y;

			for (int x = 0; x < bitmap_width; ++x) {
				const float *source = bitmap(x, source_y);

				const uint32_t atlas_x =
					packed_x + static_cast<uint32_t>(x);
				const uint32_t atlas_y =
					packed_y + static_cast<uint32_t>(y);

				const size_t destination =
					(static_cast<size_t>(atlas_y) *
						 static_cast<size_t>(ATLAS_WIDTH) +
					 static_cast<size_t>(atlas_x)) *
					4;

				pixels_[destination + 0] = to_u8(source[0]);
				pixels_[destination + 1] = to_u8(source[1]);
				pixels_[destination + 2] = to_u8(source[2]);
				pixels_[destination + 3] = to_u8(source[3]);
			}
		}

		entry.drawable = true;
		entry.atlas_x = packed_x;
		entry.atlas_y = packed_y;
		entry.atlas_width = static_cast<uint32_t>(bitmap_width);
		entry.atlas_height = static_cast<uint32_t>(bitmap_height);

		entry.u0 = static_cast<float>(packed_x) / static_cast<float>(ATLAS_WIDTH);
		entry.v0 = static_cast<float>(packed_y) / static_cast<float>(ATLAS_HEIGHT);
		entry.u1 =
			static_cast<float>(packed_x + static_cast<uint32_t>(bitmap_width)) /
			static_cast<float>(ATLAS_WIDTH);
		entry.v1 =
			static_cast<float>(packed_y + static_cast<uint32_t>(bitmap_height)) /
			static_cast<float>(ATLAS_HEIGHT);

		entry.plane_left = static_cast<float>(bounds.l - pad_em);
		entry.plane_right = static_cast<float>(bounds.r + pad_em);
		entry.plane_top = static_cast<float>(-(bounds.t + pad_em));
		entry.plane_bottom = static_cast<float>(-(bounds.b - pad_em));

		entry.visible_plane_left = static_cast<float>(bounds.l);
		entry.visible_plane_right = static_cast<float>(bounds.r);
		entry.visible_plane_top = static_cast<float>(-bounds.t);
		entry.visible_plane_bottom = static_cast<float>(-bounds.b);

		auto inserted = entries_.emplace(key, entry);
		dirty_ = true;
		++glyph_generate_count_;
		return &inserted.first->second;
	}

	const std::vector<uint8_t> &pixels() const { return pixels_; }
	bool dirty() const { return dirty_; }
	void clear_dirty() { dirty_ = false; }

	uint64_t atlas_rebuild_count() const { return atlas_rebuild_count_; }
	uint64_t glyph_generate_count() const { return glyph_generate_count_; }
	uint64_t glyph_cache_hit_count() const { return glyph_cache_hit_count_; }

private:
	bool allocate_region(
		uint32_t width, uint32_t height, uint32_t &x, uint32_t &y)
	{
		if (width + 2 * ATLAS_GUTTER > ATLAS_WIDTH ||
		    height + 2 * ATLAS_GUTTER > ATLAS_HEIGHT)
			return false;

		if (cursor_x_ + width + ATLAS_GUTTER > ATLAS_WIDTH) {
			cursor_x_ = ATLAS_GUTTER;
			cursor_y_ += row_height_ + ATLAS_GUTTER;
			row_height_ = 0;
		}

		if (cursor_y_ + height + ATLAS_GUTTER > ATLAS_HEIGHT)
			return false;

		x = cursor_x_;
		y = cursor_y_;
		cursor_x_ += width + ATLAS_GUTTER;
		row_height_ = (std::max)(row_height_, height);
		return true;
	}

	std::map<GlyphKey, GlyphEntry> entries_;
	std::vector<uint8_t> pixels_;

	uint32_t cursor_x_ = ATLAS_GUTTER;
	uint32_t cursor_y_ = ATLAS_GUTTER;
	uint32_t row_height_ = 0;
	bool dirty_ = true;

	uint64_t atlas_rebuild_count_ = 0;
	uint64_t glyph_generate_count_ = 0;
	uint64_t glyph_cache_hit_count_ = 0;
};

static bool capture_layout(
	IDWriteFactory *factory,
	const std::string &text,
	const MtsdfTextStyle &style,
	std::vector<CapturedGlyph> &glyphs,
	std::string &error)
{
	if (!factory) {
		error = "MTSDF atlas: DirectWrite factory is unavailable.";
		return false;
	}

	const std::wstring wide_text = utf8_to_wide(text);
	const std::wstring wide_face = utf8_to_wide(
		style.font_face.empty() ? std::string("Arial") : style.font_face);

	if (wide_text.empty()) {
		error = "MTSDF atlas: UTF-8 text conversion failed.";
		return false;
	}

	if (wide_face.empty()) {
		error = "MTSDF atlas: font family conversion failed.";
		return false;
	}

	const DWRITE_FONT_WEIGHT weight =
		(style.font_flags & OBS_FONT_BOLD)
			? DWRITE_FONT_WEIGHT_BOLD
			: DWRITE_FONT_WEIGHT_NORMAL;

	const DWRITE_FONT_STYLE font_style =
		(style.font_flags & OBS_FONT_ITALIC)
			? DWRITE_FONT_STYLE_ITALIC
			: DWRITE_FONT_STYLE_NORMAL;

	ComPtr<IDWriteTextFormat> format;

	HRESULT hr = factory->CreateTextFormat(
		wide_face.c_str(), nullptr, weight, font_style,
		DWRITE_FONT_STRETCH_NORMAL,
		static_cast<FLOAT>((std::max)(style.font_size, 1)),
		L"en-US", &format);

	if (FAILED(hr)) {
		error = "MTSDF atlas: DirectWrite could not create the selected font.";
		return false;
	}

	format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

	ComPtr<IDWriteTextLayout> layout;

	hr = factory->CreateTextLayout(
		wide_text.c_str(), static_cast<UINT32>(wide_text.size()),
		format.Get(), 16384.0f, 4096.0f, &layout);

	if (FAILED(hr)) {
		error = "MTSDF atlas: DirectWrite text layout creation failed.";
		return false;
	}

	layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
	glyphs.clear();

	auto *renderer = new CaptureTextRenderer(glyphs);
	hr = layout->Draw(nullptr, renderer, 0.0f, 0.0f);
	renderer->Release();

	if (FAILED(hr)) {
		error = "MTSDF atlas: DirectWrite glyph-run capture failed.";
		return false;
	}

	return true;
}

static bool same_geometry_style(
	const MtsdfTextStyle &a, const MtsdfTextStyle &b)
{
	const int64_t geometry_flags = OBS_FONT_BOLD | OBS_FONT_ITALIC;

	return a.font_face == b.font_face &&
	       (a.font_flags & geometry_flags) == (b.font_flags & geometry_flags);
}

} // namespace

struct MtsdfTextRenderer::Impl {
	MtsdfTextStyle style;
	std::string text;
	std::vector<MtsdfPulseRange> pulse_ranges;
	bool has_style = false;
	bool cpu_ready = false;

	std::string error_text;
	std::string last_logged_error;

	ComPtr<IDWriteFactory> dwrite_factory;
	GlyphAtlas atlas;
	std::vector<RenderVertex> vertices;

	uint32_t content_width = 0;
	uint32_t content_height = 0;
	uint32_t output_width = 0;
	uint32_t output_height = 0;

	float visual_bounds_left = 0.0f;
	float visual_bounds_top = 0.0f;
	float visual_bounds_right = 0.0f;
	float visual_bounds_bottom = 0.0f;

	bool geometry_dirty = true;

	gs_texture_t *atlas_texture = nullptr;
	gs_vertbuffer_t *vertex_buffer = nullptr;
	uint32_t vertex_count = 0;

	gs_effect_t *effect = nullptr;
	bool effect_failed = false;

	gs_eparam_t *image_param = nullptr;
	gs_eparam_t *fill_color_param = nullptr;
	gs_eparam_t *outline_color_param = nullptr;
	gs_eparam_t *outline_size_param = nullptr;
	gs_eparam_t *px_range_param = nullptr;
	gs_eparam_t *texture_size_param = nullptr;
	gs_eparam_t *pulse_scale_param = nullptr;
	gs_technique_t *draw_technique = nullptr;

	uint64_t layout_rebuild_count = 0;
	uint64_t vertex_update_count = 0;

	Impl()
	{
		const HRESULT hr = DWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown **>(dwrite_factory.GetAddressOf()));

		if (FAILED(hr))
			set_error("MTSDF atlas: DWriteCreateFactory failed.");
	}

	void set_error(const std::string &message)
	{
		error_text = message;
		cpu_ready = false;

		if (!message.empty() && message != last_logged_error) {
			blog(LOG_WARNING, "[Mugen Heartbeat] %s", message.c_str());
			last_logged_error = message;
		}
	}

	void clear_error() { error_text.clear(); }

	bool prewarm_common_glyphs()
	{
		std::vector<CapturedGlyph> glyphs;
		std::string error;

		MtsdfTextStyle prewarm_style = style;
		prewarm_style.font_size = 72;

		if (!capture_layout(
			    dwrite_factory.Get(), PREWARM_TEXT,
			    prewarm_style, glyphs, error)) {
			set_error(error);
			return false;
		}

		for (const auto &captured : glyphs) {
			if (!atlas.ensure(
				    captured.face.Get(), captured.glyph_index, error)) {
				set_error(error);
				return false;
			}
		}

		blog(
			LOG_INFO,
			"[Mugen Heartbeat] MTSDF glyph atlas warm: "
			"rebuild=%llu generated=%llu cacheHits=%llu",
			static_cast<unsigned long long>(atlas.atlas_rebuild_count()),
			static_cast<unsigned long long>(atlas.glyph_generate_count()),
			static_cast<unsigned long long>(atlas.glyph_cache_hit_count()));

		return true;
	}

	void update_output_dimensions()
	{
		output_width = content_width;
		output_height = content_height;

		if (style.shadow_enabled && style.shadow_opacity > 0) {
			output_width += static_cast<uint32_t>((std::max)(style.shadow_x, 0));
			output_height += static_cast<uint32_t>((std::max)(style.shadow_y, 0));
		}
	}

	void append_quad(
		float x0, float y0, float x1, float y1,
		const GlyphEntry &glyph, int pulse_group,
		float pulse_center_x, float pulse_center_y)
	{
		const float pulse = pulse_group >= 0 ? 1.0f : 0.0f;

		const RenderVertex tl{
			x0, y0, glyph.u0, glyph.v0,
			pulse, pulse_center_x, pulse_center_y};

		const RenderVertex tr{
			x1, y0, glyph.u1, glyph.v0,
			pulse, pulse_center_x, pulse_center_y};

		const RenderVertex bl{
			x0, y1, glyph.u0, glyph.v1,
			pulse, pulse_center_x, pulse_center_y};

		const RenderVertex br{
			x1, y1, glyph.u1, glyph.v1,
			pulse, pulse_center_x, pulse_center_y};

		vertices.push_back(tl);
		vertices.push_back(tr);
		vertices.push_back(bl);
		vertices.push_back(bl);
		vertices.push_back(tr);
		vertices.push_back(br);
	}

	int pulse_group_for_glyph(
		const CapturedGlyph &glyph) const
	{
		if (glyph.text_length == 0 ||
		    pulse_ranges.empty())
			return -1;

		const uint64_t glyph_start =
			glyph.text_start;

		const uint64_t glyph_end =
			glyph_start + glyph.text_length;

		for (size_t i = 0;
		     i < pulse_ranges.size();
		     ++i) {
			const auto &range = pulse_ranges[i];

			if (range.length == 0)
				continue;

			const uint64_t range_start =
				range.start;

			const uint64_t range_end =
				range_start + range.length;

			if (glyph_start < range_end &&
			    glyph_end > range_start) {
				return static_cast<int>(i);
			}
		}

		return -1;
	}

	bool rebuild_layout()
	{
		std::vector<CapturedGlyph> captured;
		std::string error;

		if (!capture_layout(
			    dwrite_factory.Get(), text, style, captured, error)) {
			set_error(error);
			return false;
		}

		struct PendingQuad {
			const GlyphEntry *glyph = nullptr;
			float x0 = 0.0f;
			float y0 = 0.0f;
			float x1 = 0.0f;
			float y1 = 0.0f;
			int pulse_group = -1;
		};

		std::vector<PendingQuad> pending;
		pending.reserve(captured.size());

		float min_x = std::numeric_limits<float>::max();
		float min_y = std::numeric_limits<float>::max();
		float max_x = -std::numeric_limits<float>::max();
		float max_y = -std::numeric_limits<float>::max();

		float visible_min_x = std::numeric_limits<float>::max();
		float visible_min_y = std::numeric_limits<float>::max();
		float visible_max_x = -std::numeric_limits<float>::max();
		float visible_max_y = -std::numeric_limits<float>::max();

		for (const auto &placement : captured) {
			const GlyphEntry *glyph = atlas.ensure(
				placement.face.Get(), placement.glyph_index, error);

			if (!glyph) {
				set_error(error);
				return false;
			}

			if (!glyph->drawable)
				continue;

			const float baseline_x =
				placement.baseline_x + placement.pen_x +
				placement.offset.advanceOffset;

			const float baseline_y =
				placement.baseline_y - placement.offset.ascenderOffset;

			const float x0 =
				baseline_x + glyph->plane_left * placement.em_size;
			const float y0 =
				baseline_y + glyph->plane_top * placement.em_size;
			const float x1 =
				baseline_x + glyph->plane_right * placement.em_size;
			const float y1 =
				baseline_y + glyph->plane_bottom * placement.em_size;

			const float visible_x0 =
				baseline_x +
				glyph->visible_plane_left * placement.em_size;
			const float visible_y0 =
				baseline_y +
				glyph->visible_plane_top * placement.em_size;
			const float visible_x1 =
				baseline_x +
				glyph->visible_plane_right * placement.em_size;
			const float visible_y1 =
				baseline_y +
				glyph->visible_plane_bottom * placement.em_size;

			pending.push_back(PendingQuad{
				glyph, x0, y0, x1, y1,
				pulse_group_for_glyph(placement)});

			min_x = (std::min)(min_x, x0);
			min_y = (std::min)(min_y, y0);
			max_x = (std::max)(max_x, x1);
			max_y = (std::max)(max_y, y1);

			visible_min_x =
				(std::min)(visible_min_x, visible_x0);
			visible_min_y =
				(std::min)(visible_min_y, visible_y0);
			visible_max_x =
				(std::max)(visible_max_x, visible_x1);
			visible_max_y =
				(std::max)(visible_max_y, visible_y1);
		}

		if (pending.empty()) {
			set_error("MTSDF atlas: current text has no drawable glyphs.");
			return false;
		}

		const float shift_x = -min_x;
		const float shift_y = -min_y;

		visual_bounds_left = visible_min_x + shift_x;
		visual_bounds_top = visible_min_y + shift_y;
		visual_bounds_right = visible_max_x + shift_x;
		visual_bounds_bottom = visible_max_y + shift_y;

		struct PulseBounds {
			bool valid = false;
			float min_x = 0.0f;
			float min_y = 0.0f;
			float max_x = 0.0f;
			float max_y = 0.0f;
		};

		std::vector<PulseBounds> pulse_bounds(
			pulse_ranges.size());

		for (const auto &quad : pending) {
			if (quad.pulse_group < 0)
				continue;

			auto &bounds =
				pulse_bounds[
					static_cast<size_t>(
						quad.pulse_group)];

			const float x0 = quad.x0 + shift_x;
			const float y0 = quad.y0 + shift_y;
			const float x1 = quad.x1 + shift_x;
			const float y1 = quad.y1 + shift_y;

			if (!bounds.valid) {
				bounds.valid = true;
				bounds.min_x = x0;
				bounds.min_y = y0;
				bounds.max_x = x1;
				bounds.max_y = y1;
			} else {
				bounds.min_x =
					(std::min)(bounds.min_x, x0);
				bounds.min_y =
					(std::min)(bounds.min_y, y0);
				bounds.max_x =
					(std::max)(bounds.max_x, x1);
				bounds.max_y =
					(std::max)(bounds.max_y, y1);
			}
		}

		vertices.clear();
		vertices.reserve(pending.size() * 6);

		for (const auto &quad : pending) {
			float center_x = 0.0f;
			float center_y = 0.0f;

			if (quad.pulse_group >= 0) {
				const auto &bounds =
					pulse_bounds[
						static_cast<size_t>(
							quad.pulse_group)];

				if (bounds.valid) {
					center_x =
						(bounds.min_x +
						 bounds.max_x) *
						0.5f;

					center_y =
						(bounds.min_y +
						 bounds.max_y) *
						0.5f;
				}
			}

			append_quad(
				quad.x0 + shift_x,
				quad.y0 + shift_y,
				quad.x1 + shift_x,
				quad.y1 + shift_y,
				*quad.glyph,
				quad.pulse_group,
				center_x,
				center_y);
		}

		content_width = static_cast<uint32_t>(
			std::ceil((std::max)(max_x - min_x, 1.0f)));

		content_height = static_cast<uint32_t>(
			std::ceil((std::max)(max_y - min_y, 1.0f)));

		update_output_dimensions();
		geometry_dirty = true;
		cpu_ready = true;
		clear_error();
		++layout_rebuild_count;
		return true;
	}

	bool update_cpu(
		const std::string &new_text,
		const MtsdfTextStyle &new_style,
		const std::vector<MtsdfPulseRange> &new_pulse_ranges)
	{
		if (!dwrite_factory) {
			set_error("MTSDF atlas: DirectWrite is unavailable.");
			return false;
		}

		const bool geometry_style_changed =
			!has_style || !same_geometry_style(style, new_style);

		const bool text_changed = text != new_text;

		const bool pulse_ranges_changed =
			pulse_ranges.size() !=
				new_pulse_ranges.size() ||
			!std::equal(
				pulse_ranges.begin(),
				pulse_ranges.end(),
				new_pulse_ranges.begin(),
				[](const MtsdfPulseRange &a,
				   const MtsdfPulseRange &b) {
					return a.start == b.start &&
					       a.length == b.length;
				});

		const bool font_size_changed =
			!has_style || style.font_size != new_style.font_size;

		const bool shadow_dimensions_changed =
			!has_style ||
			style.shadow_enabled != new_style.shadow_enabled ||
			style.shadow_x != new_style.shadow_x ||
			style.shadow_y != new_style.shadow_y ||
			style.shadow_opacity != new_style.shadow_opacity;

		style = new_style;
		text = new_text;
		pulse_ranges = new_pulse_ranges;
		has_style = true;

		if (geometry_style_changed) {
			atlas.reset();
			if (!prewarm_common_glyphs())
				return false;
		}

		if (geometry_style_changed ||
		    text_changed ||
		    pulse_ranges_changed ||
		    font_size_changed ||
		    !cpu_ready) {
			if (!rebuild_layout())
				return false;
		} else if (shadow_dimensions_changed) {
			update_output_dimensions();
		}

		return true;
	}

	bool ensure_effect()
	{
		if (effect)
			return true;
		if (effect_failed)
			return false;

		char *effect_path = obs_module_file("mugen-heartbeat-mtsdf.effect");

		if (!effect_path) {
			effect_failed = true;
			set_error("MTSDF atlas: effect file was not found; using GDI+ fallback.");
			return false;
		}

		char *errors = nullptr;
		effect = gs_effect_create_from_file(effect_path, &errors);
		bfree(effect_path);

		if (!effect) {
			std::string message =
				"MTSDF atlas: shader compilation failed; using GDI+ fallback.";

			if (errors && *errors)
				message += std::string(" ") + errors;

			if (errors)
				bfree(errors);

			effect_failed = true;
			set_error(message);
			return false;
		}

		if (errors)
			bfree(errors);

		image_param = gs_effect_get_param_by_name(effect, "image");
		fill_color_param = gs_effect_get_param_by_name(effect, "fillColor");
		outline_color_param = gs_effect_get_param_by_name(effect, "outlineColor");
		outline_size_param = gs_effect_get_param_by_name(effect, "outlineSize");
		px_range_param = gs_effect_get_param_by_name(effect, "pxRange");
		texture_size_param = gs_effect_get_param_by_name(effect, "textureSize");
		pulse_scale_param = gs_effect_get_param_by_name(effect, "pulseScale");
		draw_technique = gs_effect_get_technique(effect, "Draw");

		if (!image_param || !fill_color_param ||
		    !outline_color_param || !outline_size_param ||
		    !px_range_param || !texture_size_param ||
		    !pulse_scale_param || !draw_technique) {
			effect_failed = true;
			set_error(
				"MTSDF atlas: required shader parameter is missing; using GDI+ fallback.");
			return false;
		}

		return true;
	}

	bool ensure_atlas_texture()
	{
		if (!cpu_ready)
			return false;

		const auto &pixels = atlas.pixels();
		if (pixels.empty())
			return false;

		if (!atlas_texture) {
			const uint8_t *data = pixels.data();

			atlas_texture = gs_texture_create(
				ATLAS_WIDTH, ATLAS_HEIGHT, GS_RGBA,
				1, &data, GS_DYNAMIC);

			if (!atlas_texture) {
				set_error(
					"MTSDF atlas: GPU atlas texture creation failed; using GDI+ fallback.");
				return false;
			}

			atlas.clear_dirty();
			return true;
		}

		if (atlas.dirty()) {
			gs_texture_set_image(
				atlas_texture, pixels.data(),
				ATLAS_WIDTH * 4, false);

			atlas.clear_dirty();
		}

		return true;
	}

	bool ensure_vertex_buffer()
	{
		if (!cpu_ready || vertices.empty())
			return false;

		if (!geometry_dirty && vertex_buffer)
			return true;

		if (vertex_buffer) {
			gs_vertexbuffer_destroy(vertex_buffer);
			vertex_buffer = nullptr;
		}

		gs_render_start(true);

		for (const auto &vertex : vertices) {
			gs_texcoord(vertex.u, vertex.v, 0);
			gs_texcoord(vertex.x, vertex.y, 1);
			gs_texcoord(vertex.pulse, 0.0f, 2);
			gs_texcoord(
				vertex.pulse_center_x,
				vertex.pulse_center_y,
				3);
			gs_vertex2f(vertex.x, vertex.y);
		}

		vertex_buffer = gs_render_save();

		if (!vertex_buffer) {
			set_error(
				"MTSDF atlas: vertex buffer creation failed; using GDI+ fallback.");
			return false;
		}

		vertex_count = static_cast<uint32_t>(vertices.size());
		geometry_dirty = false;
		++vertex_update_count;
		return true;
	}

	bool draw_pass(
		uint32_t color, int opacity,
		uint32_t outline_color, int outline_opacity,
		float outline_size, float pulse_scale)
	{
		if (!vertex_buffer || vertex_count == 0)
			return false;

		struct vec4 fill;
		struct vec4 outline;

		vec4_from_rgba_srgb(
			&fill, color_with_opacity(color, opacity));

		vec4_from_rgba_srgb(
			&outline,
			color_with_opacity(outline_color, outline_opacity));

		struct vec2 texture_size;
		vec2_set(
			&texture_size,
			static_cast<float>(ATLAS_WIDTH),
			static_cast<float>(ATLAS_HEIGHT));

		gs_effect_set_texture(image_param, atlas_texture);
		gs_effect_set_vec4(fill_color_param, &fill);
		gs_effect_set_vec4(outline_color_param, &outline);
		gs_effect_set_float(outline_size_param, outline_size);
		gs_effect_set_float(
			px_range_param, static_cast<float>(FIELD_PX_RANGE));
		gs_effect_set_vec2(texture_size_param, &texture_size);

		if (pulse_scale_param) {
			gs_effect_set_float(
				pulse_scale_param,
				(std::max)(pulse_scale, 0.01f));
		}

		gs_load_vertexbuffer(vertex_buffer);
		gs_load_indexbuffer(nullptr);

		const size_t passes = gs_technique_begin(draw_technique);

		for (size_t pass = 0; pass < passes; ++pass) {
			gs_technique_begin_pass(draw_technique, pass);
			gs_draw(GS_TRIS, 0, vertex_count);
			gs_technique_end_pass(draw_technique);
		}

		gs_technique_end(draw_technique);
		return passes > 0;
	}

	bool render_internal(float pulse_scale)
	{
		if (!cpu_ready)
			return false;

		if (!ensure_effect() ||
		    !ensure_atlas_texture() ||
		    !ensure_vertex_buffer())
			return false;

		const bool previous_srgb = gs_framebuffer_srgb_enabled();
		gs_enable_framebuffer_srgb(true);

		gs_blend_state_push();
		gs_reset_blend_state();

		if (style.shadow_enabled && style.shadow_opacity > 0) {
			gs_matrix_push();

			gs_matrix_translate3f(
				static_cast<float>(style.shadow_x),
				static_cast<float>(style.shadow_y), 0.0f);

			if (!draw_pass(
				    style.shadow_color, style.shadow_opacity,
				    style.shadow_color, 0, 0.0f,
				    pulse_scale)) {
				gs_matrix_pop();
				gs_blend_state_pop();
				gs_enable_framebuffer_srgb(previous_srgb);
				return false;
			}

			gs_matrix_pop();
		}

		const int requested_outline =
			style.outline_enabled
				? (std::max)(style.outline_size, 0)
				: 0;

		const float max_outline =
			static_cast<float>((std::max)(style.font_size, 1)) *
			static_cast<float>(
				(FIELD_PX_RANGE * 0.5 - 2.0) / FIELD_SCALE);

		const float outline_size = (std::min)(
			static_cast<float>(requested_outline), max_outline);

		const bool ok = draw_pass(
			style.text_color, style.text_opacity,
			style.outline_color,
			style.outline_enabled ? style.outline_opacity : 0,
			outline_size, pulse_scale);

		gs_load_vertexbuffer(nullptr);
		gs_blend_state_pop();
		gs_enable_framebuffer_srgb(previous_srgb);
		return ok;
	}

	void destroy_graphics()
	{
		if (!atlas_texture && !vertex_buffer && !effect)
			return;

		obs_enter_graphics();

		if (vertex_buffer) {
			gs_vertexbuffer_destroy(vertex_buffer);
			vertex_buffer = nullptr;
		}

		if (atlas_texture) {
			gs_texture_destroy(atlas_texture);
			atlas_texture = nullptr;
		}

		if (effect) {
			gs_effect_destroy(effect);
			effect = nullptr;
		}

		obs_leave_graphics();
	}

	void log_final_stats()
	{
		blog(
			LOG_INFO,
			"[Mugen Heartbeat] MTSDF atlas stats: "
			"atlasRebuilds=%llu glyphGenerated=%llu "
			"glyphCacheHits=%llu layoutRebuilds=%llu "
			"vertexUpdates=%llu",
			static_cast<unsigned long long>(atlas.atlas_rebuild_count()),
			static_cast<unsigned long long>(atlas.glyph_generate_count()),
			static_cast<unsigned long long>(atlas.glyph_cache_hit_count()),
			static_cast<unsigned long long>(layout_rebuild_count),
			static_cast<unsigned long long>(vertex_update_count));
	}
};

MtsdfTextRenderer::MtsdfTextRenderer()
	: impl_(std::make_unique<Impl>())
{
}

MtsdfTextRenderer::~MtsdfTextRenderer()
{
	if (impl_) {
		impl_->log_final_stats();
		impl_->destroy_graphics();
	}
}

bool MtsdfTextRenderer::update(
	const std::string &text, const MtsdfTextStyle &style)
{
	static const std::vector<MtsdfPulseRange> none;

	return update(text, style, none);
}

bool MtsdfTextRenderer::update(
	const std::string &text,
	const MtsdfTextStyle &style,
	const std::vector<MtsdfPulseRange> &pulse_ranges)
{
	return impl_ &&
	       impl_->update_cpu(
		       text, style, pulse_ranges);
}

bool MtsdfTextRenderer::render()
{
	return render(1.0f);
}

bool MtsdfTextRenderer::render(float pulse_scale)
{
	return impl_ &&
	       impl_->render_internal(pulse_scale);
}

bool MtsdfTextRenderer::ready() const
{
	return impl_ && impl_->cpu_ready;
}

uint32_t MtsdfTextRenderer::width() const
{
	return impl_ ? impl_->output_width : 0;
}

uint32_t MtsdfTextRenderer::height() const
{
	return impl_ ? impl_->output_height : 0;
}

float MtsdfTextRenderer::visual_left() const
{
	return impl_ ? impl_->visual_bounds_left : 0.0f;
}

float MtsdfTextRenderer::visual_top() const
{
	return impl_ ? impl_->visual_bounds_top : 0.0f;
}

float MtsdfTextRenderer::visual_width() const
{
	if (!impl_)
		return 0.0f;

	return (std::max)(
		0.0f,
		impl_->visual_bounds_right -
			impl_->visual_bounds_left);
}

float MtsdfTextRenderer::visual_height() const
{
	if (!impl_)
		return 0.0f;

	return (std::max)(
		0.0f,
		impl_->visual_bounds_bottom -
			impl_->visual_bounds_top);
}

float MtsdfTextRenderer::visual_padding() const
{
	if (!impl_ || !impl_->has_style)
		return 0.0f;

	const double pad_px =
		std::ceil(FIELD_PX_RANGE * 0.5) + 3.0;

	const double pad_em =
		pad_px / FIELD_SCALE;

	return static_cast<float>(
		pad_em *
		static_cast<double>(
			(std::max)(impl_->style.font_size, 1)));
}

const std::string &MtsdfTextRenderer::error() const
{
	static const std::string empty;
	return impl_ ? impl_->error_text : empty;
}
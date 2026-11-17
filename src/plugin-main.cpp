// SPDX-FileCopyrightText: 2026 Mugen Art Lab
// SPDX-License-Identifier: GPL-2.0-or-later

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>



#include "mtsdf-text-renderer.hpp"
#include "heartbeat-skin-engine.hpp"
#include <graphics/matrix4.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <map>

#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;

#define SETTING_TEST_MODE "test_mode"
#define SETTING_TEST_BPM "test_bpm"
#define SETTING_DEVICE_ADDRESS "device_address"
#define SETTING_DEVICE_NAME "device_name"
#define SETTING_AUTO_CONNECT "auto_connect"
#define SETTING_DISPLAY_FORMAT "display_format"
#define SETTING_CUSTOM_FORMAT "custom_format"
#define SETTING_FONT "display_font"
#define SETTING_TEXT_COLOR "text_color"
#define SETTING_TEXT_OPACITY "text_opacity"
#define SETTING_OUTLINE_ENABLED "outline_enabled"
#define SETTING_OUTLINE_SIZE "outline_size"
#define SETTING_OUTLINE_COLOR "outline_color"
#define SETTING_OUTLINE_OPACITY "outline_opacity"
#define SETTING_SHADOW_ENABLED "shadow_enabled"
#define SETTING_SHADOW_X "shadow_x"
#define SETTING_SHADOW_Y "shadow_y"
#define SETTING_SHADOW_COLOR "shadow_color"
#define SETTING_SHADOW_OPACITY "shadow_opacity"
#define SETTING_BEAT_ANIMATION "beat_animation"
#define SETTING_BEAT_STRENGTH "beat_strength"
#define SETTING_VISUAL_MODE "visual_mode"
#define SETTING_SKIN_ID "skin_id"
#define SETTING_SKIN_BACKGROUND_OPACITY "skin_background_opacity"

static constexpr size_t HEARTBEAT_REACTION_COUNT = 3;

static constexpr const char *REACTION_ENABLED_KEYS[HEARTBEAT_REACTION_COUNT] = {
	"reaction_1_enabled",
	"reaction_2_enabled",
	"reaction_3_enabled",
};

static constexpr const char *REACTION_UI_TOGGLE_KEYS[HEARTBEAT_REACTION_COUNT] = {
	"reaction_1_ui_toggle",
	"reaction_2_ui_toggle",
	"reaction_3_ui_toggle",
};

static constexpr const char *REACTION_ACTION_KEYS[HEARTBEAT_REACTION_COUNT] = {
	"reaction_1_action",
	"reaction_2_action",
	"reaction_3_action",
};

static constexpr const char *REACTION_SCENE_KEYS[HEARTBEAT_REACTION_COUNT] = {
	"reaction_1_scene",
	"reaction_2_scene",
	"reaction_3_scene",
};

static constexpr const char *REACTION_SOURCE_KEYS[HEARTBEAT_REACTION_COUNT] = {
	"reaction_1_source",
	"reaction_2_source",
	"reaction_3_source",
};

static constexpr const char *REACTION_FILTER_KEYS[HEARTBEAT_REACTION_COUNT] = {
	"reaction_1_filter",
	"reaction_2_filter",
	"reaction_3_filter",
};

static constexpr const char *REACTION_ON_BPM_KEYS[HEARTBEAT_REACTION_COUNT] = {
	"reaction_1_on_bpm",
	"reaction_2_on_bpm",
	"reaction_3_on_bpm",
};

static constexpr const char *REACTION_OFF_BPM_KEYS[HEARTBEAT_REACTION_COUNT] = {
	"reaction_1_off_bpm",
	"reaction_2_off_bpm",
	"reaction_3_off_bpm",
};

static constexpr const char *REACTION_ON_DELAY_KEYS[HEARTBEAT_REACTION_COUNT] = {
	"reaction_1_on_delay",
	"reaction_2_on_delay",
	"reaction_3_on_delay",
};

static constexpr const char *REACTION_OFF_DELAY_KEYS[HEARTBEAT_REACTION_COUNT] = {
	"reaction_1_off_delay",
	"reaction_2_off_delay",
	"reaction_3_off_delay",
};

static constexpr const char *REACTION_GROUP_LOCALE_KEYS[HEARTBEAT_REACTION_COUNT] = {
	"MugenHeartbeat.Reaction1",
	"MugenHeartbeat.Reaction2",
	"MugenHeartbeat.Reaction3",
};

#define CHILD_TEXT_SOURCE_ID "text_gdiplus_v3"

static constexpr size_t RENDER_LEVEL_COUNT = 9;
static constexpr float RENDER_SCALES[RENDER_LEVEL_COUNT] = {
	0.25f,
	0.353553f,
	0.5f,
	0.707107f,
	1.0f,
	1.414214f,
	2.0f,
	2.828427f,
	4.0f
};
static constexpr size_t RENDER_LEVEL_NATIVE = 4; /* 1.0x */

static const guid HEART_RATE_SERVICE_UUID{
	0x0000180D, 0x0000, 0x1000, {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};
static const guid HEART_RATE_MEASUREMENT_UUID{
	0x00002A37, 0x0000, 0x1000, {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

static std::string sanitize_ble_name(std::string name)
{
	const std::string replacement = "\xEF\xBF\xBD";
	size_t pos = 0;
	while ((pos = name.find(replacement, pos)) != std::string::npos)
		name.erase(pos, replacement.size());

	while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
		name.pop_back();

	size_t first = name.find_first_not_of(" \t");
	if (first != std::string::npos && first > 0)
		name.erase(0, first);

	return name.empty() ? std::string("(unnamed)") : name;
}

static int ble_name_quality(const std::string &name)
{
	if (name.empty() || name == "(unnamed)")
		return 0;

	if (name.rfind("Bluetooth ", 0) == 0)
		return 1;

	/* Windows/Xiaomi sometimes expose only the manufacturer after a radio
	   reset. It is still better than a MAC fallback, but do not let it replace
	   an already saved full product name. */
	if (name == "Xiaomi")
		return 2;

	return 3;
}

static bool ble_name_is_better(const std::string &candidate,
			       const std::string &current)
{
	const int candidate_quality = ble_name_quality(candidate);
	const int current_quality = ble_name_quality(current);

	if (candidate_quality != current_quality)
		return candidate_quality > current_quality;

	return candidate.size() > current.size();
}

static std::string preferred_ble_name(const std::string &a,
				      const std::string &b)
{
	return ble_name_is_better(b, a) ? b : a;
}

static std::string bluetooth_address_suffix(uint64_t address)
{
	std::ostringstream ss;
	ss << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
	   << (address & 0xFFFF);
	return ss.str();
}

static std::string status_device_name(std::string name, uint64_t)
{
	name = sanitize_ble_name(std::move(name));

	if (ble_name_quality(name) <= 1)
		return {};

	/* Never manufacture a product-looking suffix from the BLE address.
	   Xiaomi's visible product suffix is not guaranteed to be derived from
	   the MAC (e.g. one tested Band is "...364F" at MAC ...26:14). */
	return name;
}

static std::string display_ble_name(std::string name, uint64_t address)
{
	name = sanitize_ble_name(std::move(name));

	if (ble_name_quality(name) <= 1)
		return "Device " + bluetooth_address_suffix(address);

	return name;
}
struct HeartRateDeviceInfo {
	uint64_t address = 0;
	std::string name;
	int rssi = 0;
	int64_t last_seen_ms = 0;
	BluetoothAddressType address_type = BluetoothAddressType::Unspecified;
	bool connectable = false;
};

enum class HeartbeatTimingMode {
	SyntheticBpm,
	RrIntervals,
};

struct HeartbeatIntervalSample {
	uint64_t sequence = 0;
	double interval_ms = 0.0;
	int64_t received_ms = 0;
};
enum class BleState {
	Idle,
	Scanning,
	Connecting,
	Connected,
	Error,
};

static bool ensure_mugen_heartbeat_config_dir()
{
	char *config_dir = obs_module_config_path(nullptr);
	if (!config_dir)
		return false;

	const int result = os_mkdirs(config_dir);
	bfree(config_dir);

	if (result == MKDIR_ERROR) {
		blog(LOG_WARNING,
		     "[Mugen Heartbeat] failed to create plugin config directory.");
		return false;
	}

	return true;
}

class BleHeartRateClient {
public:
	~BleHeartRateClient()
	{
		shutdown();
	}

	void start_scan(uint64_t preferred_address = 0)
	{
		stop_scan();

		{
			std::scoped_lock lock(devices_mutex_);
			devices_.clear();
		}

		/* A saved Band is allowed to qualify by exact address even when the
		   particular advertisement packet Windows gives us omits 0x180D.
		   New/unknown devices still need to advertise the Heart Rate service. */
		scan_preferred_address_.store(preferred_address);

		scan_stop_requested_.store(false);
		set_state(BleState::Scanning, {});
		scan_thread_ = std::thread([this]() { scan_worker(); });
	}

	void stop_scan()
	{
		scan_stop_requested_.store(true);
		if (scan_thread_.joinable() && scan_thread_.get_id() != std::this_thread::get_id())
			scan_thread_.join();

		if (state_.load() == BleState::Scanning)
			set_state(BleState::Idle, {});
	}

	void connect(uint64_t address, bool maintain_connection)
	{
		constexpr int64_t FRESH_ADVERTISEMENT_MS = 7000;

		const BleState current_state = state_.load();
		const uint64_t current_target = target_address_.load();

		if (current_target == address &&
		    (maintain_session_.load() ||
		     connected_.load() ||
		     current_state == BleState::Connecting)) {
			blog(LOG_INFO,
			     "[Mugen Heartbeat] BLE connect request ignored: "
			     "the same Band already owns the active session.");
			return;
		}

		if (!seen_recently(address, FRESH_ADVERTISEMENT_MS)) {
			blog(LOG_INFO,
			     "[Mugen Heartbeat] BLE connect request deferred: "
			     "selected Band has no fresh connectable advertisement.");
			set_state(
				BleState::Error,
				"Selected Band is not freshly visible; scan before connecting.");
			return;
		}

		const BluetoothAddressType address_type = address_type_for(address);

		stop_scan();

		/* Cancel any previous maintained connection worker before replacing it. */
		connect_cancel_requested_.store(true);

		if (connection_thread_.joinable() &&
		    connection_thread_.get_id() != std::this_thread::get_id())
			connection_thread_.join();

		connect_cancel_requested_.store(false);

		connection_thread_ =
			std::thread([this, address, address_type, maintain_connection]() {
				try {
					winrt::init_apartment(
						winrt::apartment_type::multi_threaded);

					/* Release the previous session without a synchronous
					   CCCD write. A new session is about to own the link. */
					disconnect_internal(false);

					target_address_.store(address);
					first_measurement_logged_.store(false);
					service_discovery_failures_.store(0);
					service_discovery_started_ms_.store(0);

					set_state(BleState::Connecting, {});

					auto device =
						address_type != BluetoothAddressType::Unspecified
							? BluetoothLEDevice::FromBluetoothAddressAsync(
								  address, address_type)
								  .get()
							: BluetoothLEDevice::FromBluetoothAddressAsync(
								  address)
								  .get();

					if (!device)
						throw std::runtime_error(
							"Windows could not open the BLE heart-rate device.");

					blog(LOG_INFO,
					     "[Mugen Heartbeat] BLE device opened: "
					     "addressType=%d connectionStatus=%d name=%s",
					     static_cast<int>(address_type),
					     static_cast<int>(device.ConnectionStatus()),
					     sanitize_ble_name(to_string(device.Name())).c_str());

					auto session =
						GattSession::FromDeviceIdAsync(
							device.BluetoothDeviceId())
							.get();

					if (!session)
						throw std::runtime_error(
							"Windows could not create a GATT session.");

					blog(LOG_INFO,
					     "[Mugen Heartbeat] GATT session created: "
					     "canMaintain=%s status=%d",
					     session.CanMaintainConnection() ? "yes" : "no",
					     static_cast<int>(session.SessionStatus()));

					if (maintain_connection && !session.CanMaintainConnection())
						throw std::runtime_error(
							"Windows reports that this GATT session cannot maintain a connection.");

					session_status_token_ =
						session.SessionStatusChanged(
							[this](
								GattSession const &sender,
								GattSessionStatusChangedEventArgs const &args) {
								blog(
									LOG_INFO,
									"[Mugen Heartbeat] GATT session status: "
									"status=%d error=%d maintain=%s",
									static_cast<int>(args.Status()),
									static_cast<int>(args.Error()),
									sender.MaintainConnection()
										? "yes"
										: "no");

								revision_.fetch_add(1);
							});

					has_session_status_token_ = true;

					device_status_token_ =
						device.ConnectionStatusChanged(
							[this](
								BluetoothLEDevice const &sender,
								winrt::Windows::Foundation::IInspectable const &) {
								const auto status =
									sender.ConnectionStatus();

								blog(
									LOG_INFO,
									"[Mugen Heartbeat] BLE physical link: %s",
									status ==
											BluetoothConnectionStatus::Connected
										? "Connected"
										: "Disconnected");

								if (status ==
								    BluetoothConnectionStatus::Disconnected) {
									connected_.store(false);
									bpm_.store(0);
									last_bpm_ms_.store(0);

									if (maintain_session_.load() &&
									    !connect_cancel_requested_.load() &&
									    !shutdown_.load())
										set_state(
											BleState::Connecting, {});
									else
										set_state(BleState::Idle, {});
								} else {
									bool gatt_ready = false;

									{
										std::scoped_lock lock(
											connection_mutex_);

										gatt_ready =
											static_cast<bool>(
												measurement_);
									}

									/* If the GATT objects survived the range
									   loss, Windows can resume the existing
									   subscription on the maintained session. */
									if (gatt_ready &&
									    maintain_session_.load()) {
										connected_.store(true);
										set_state(
											BleState::Connected, {});
									}
								}
							});

					has_device_status_token_ = true;

					/* Keep strong references BEFORE asking Windows to maintain
					   the connection. Dropping all BluetoothLEDevice references
					   would otherwise allow Windows to disconnect it. */
					{
						std::scoped_lock lock(connection_mutex_);

						device_ = device;
						gatt_session_ = session;
						connected_name_ =
							sanitize_ble_name(
								to_string(device.Name()));
						connected_address_ = address;
					}

					remember_name(
						address,
						sanitize_ble_name(to_string(device.Name())));

					/* An explicit Connect still initiates uncached GATT
					   discovery, but must not arm endless Windows reconnect. */
					session.MaintainConnection(maintain_connection);
					maintain_session_.store(maintain_connection);

					blog(
						LOG_INFO,
						"[Mugen Heartbeat] GATT MaintainConnection %s; "
						"starting uncached Heart Rate discovery immediately.",
						maintain_connection ? "enabled" : "disabled (manual)");

					/* Do not wait for BluetoothLEDevice::ConnectionStatus to
					   remain Connected before issuing GATT traffic. Windows
					   documents uncached service discovery itself as a
					   connection trigger. This also avoids missing very short
					   Connected windows where the peripheral/stack drops the
					   idle link before our polling loop wakes up. */
					auto wait_for_heart_rate_service = [&]() -> GattDeviceService {
						unsigned attempt = 0;
						const int64_t discovery_start_ms = steady_ms();
						constexpr int64_t MANUAL_DISCOVERY_LIMIT_MS = 60000;

						while (!connect_cancel_requested_.load() &&
						       !shutdown_.load()) {
							/* Manual click is not permission to wait forever. */
							if (!maintain_session_.load() &&
							    steady_ms() - discovery_start_ms >=
								    MANUAL_DISCOVERY_LIMIT_MS)
								throw std::runtime_error(
									"Manual Heart Rate service connection timed out; "
									"press Connect to try again.");
							++attempt;

							if (attempt == 1)
								service_discovery_started_ms_.store(steady_ms());

							blog(
								LOG_INFO,
								"[Mugen Heartbeat] Heart Rate service discovery "
								"attempt %u: connectionStatus=%d sessionStatus=%d.",
								attempt,
								static_cast<int>(
									device.ConnectionStatus()),
								static_cast<int>(
									session.SessionStatus()));

							auto services =
								device.GetGattServicesForUuidAsync(
									HEART_RATE_SERVICE_UUID,
									BluetoothCacheMode::Uncached)
									.get();

							if (services.Status() ==
								    GattCommunicationStatus::Success &&
							    services.Services().Size() > 0) {
								service_discovery_failures_.store(0);
								service_discovery_started_ms_.store(0);
								revision_.fetch_add(1);

								blog(
									LOG_INFO,
									"[Mugen Heartbeat] Heart Rate service discovery "
									"succeeded on attempt %u: connectionStatus=%d.",
									attempt,
									static_cast<int>(
										device.ConnectionStatus()));

								return services.Services().GetAt(0);
							}

							service_discovery_failures_.store(attempt);

							/* Refresh Properties when the simple-user warning
							   first becomes useful, not on every retry. */
							if (attempt == 3)
								revision_.fetch_add(1);

							blog(
								LOG_WARNING,
								"[Mugen Heartbeat] Heart Rate service discovery "
								"retry %u: GattStatus=%d serviceCount=%u "
								"connectionStatus=%d sessionStatus=%d; "
								"keeping maintained session.",
								attempt,
								static_cast<int>(services.Status()),
								static_cast<unsigned>(
									services.Services().Size()),
								static_cast<int>(
									device.ConnectionStatus()),
								static_cast<int>(
									session.SessionStatus()));

							/* Fast retries are useful during normal startup.
							   Once the Band is clearly in the known "visible but
							   Share HR/GATT unavailable" state, stop hammering it.
							   The same maintained session can recover immediately
							   after the user restarts Share HR. */
							const int backoff_ms = attempt < 3 ? 500 : 2000;
							const int slices = backoff_ms / 100;

							for (int i = 0;
							     i < slices &&
							     !connect_cancel_requested_.load() &&
							     !shutdown_.load();
							     ++i) {
								std::this_thread::sleep_for(
									std::chrono::milliseconds(100));
							}
						}

						throw std::runtime_error(
							"BLE connection cancelled while waiting for Heart Rate service.");
					};

					auto service = wait_for_heart_rate_service();

					auto characteristics =
						service.GetCharacteristicsForUuidAsync(
							HEART_RATE_MEASUREMENT_UUID,
							BluetoothCacheMode::Uncached)
							.get();

					if (characteristics.Status() !=
						    GattCommunicationStatus::Success ||
					    characteristics.Characteristics().Size() ==
						    0)
						throw std::runtime_error(
							"Heart Rate Measurement 0x2A37 was not available.");

					auto measurement =
						characteristics.Characteristics().GetAt(0);

					auto properties =
						measurement.CharacteristicProperties();

					GattClientCharacteristicConfigurationDescriptorValue
						cccd =
							GattClientCharacteristicConfigurationDescriptorValue::
								None;

					if ((properties &
					     GattCharacteristicProperties::Notify) !=
					    GattCharacteristicProperties::None) {
						cccd =
							GattClientCharacteristicConfigurationDescriptorValue::
								Notify;
					} else if (
						(properties &
						 GattCharacteristicProperties::Indicate) !=
						GattCharacteristicProperties::None) {
						cccd =
							GattClientCharacteristicConfigurationDescriptorValue::
								Indicate;
					}

					if (cccd ==
					    GattClientCharacteristicConfigurationDescriptorValue::
						    None)
						throw std::runtime_error(
							"Heart Rate Measurement has no Notify/Indicate support.");

					measurement_token_ =
						measurement.ValueChanged(
							[this](
								GattCharacteristic const &,
								GattValueChangedEventArgs const &args) {
								on_measurement(args);
							});

					has_measurement_token_ = true;

					auto cccd_status =
						measurement
							.WriteClientCharacteristicConfigurationDescriptorAsync(
								cccd)
							.get();

					if (cccd_status !=
					    GattCommunicationStatus::Success)
						throw std::runtime_error(
							"Could not subscribe to Heart Rate Measurement.");

					{
						std::scoped_lock lock(connection_mutex_);

						service_ = service;
						measurement_ = measurement;
					}

					connected_.store(true);
					bpm_.store(0);
					last_bpm_ms_.store(0);
					service_discovery_failures_.store(0);
					service_discovery_started_ms_.store(0);

					set_state(BleState::Connected, {});

					blog(
						LOG_INFO,
						"[Mugen Heartbeat] Heart Rate GATT subscription ready; "
						"maintained session remains active.");
				} catch (hresult_error const &e) {
					if (!connect_cancel_requested_.load() &&
					    !shutdown_.load()) {
						disconnect_internal(false);
						set_state(
							BleState::Error,
							to_string(e.message()));
					}
				} catch (std::exception const &e) {
					if (!connect_cancel_requested_.load() &&
					    !shutdown_.load()) {
						disconnect_internal(false);
						set_state(BleState::Error, e.what());
					}
				} catch (...) {
					if (!connect_cancel_requested_.load() &&
					    !shutdown_.load()) {
						disconnect_internal(false);
						set_state(
							BleState::Error,
							"Unknown BLE error");
					}
				}
			});
	}

	/* The checkbox may change while a manual connection is live.
	   Update Windows auto-maintenance without disconnecting an active Band. */
	void set_connection_maintenance(uint64_t address, bool enabled)
	{
		if (!address || target_address_.load() != address)
			return;

		const BleState state = state_.load();
		if (state != BleState::Connecting &&
		    state != BleState::Connected)
			return;

		if (maintain_session_.load() == enabled)
			return;

		GattSession session = nullptr;
		{
			std::scoped_lock lock(connection_mutex_);
			session = gatt_session_;
		}
		if (!session)
			return;

		try {
			session.MaintainConnection(enabled);
			maintain_session_.store(enabled);
			blog(LOG_INFO,
			     "[Mugen Heartbeat] Windows GATT auto-maintenance %s "
			     "(controlled by source auto-connect settings).",
			     enabled ? "enabled" : "disabled");
		} catch (hresult_error const &e) {
			blog(LOG_WARNING,
			     "[Mugen Heartbeat] Could not update GATT auto-maintenance: %s",
			     to_string(e.message()).c_str());
		}
	}

	void disconnect()
	{
		connect_cancel_requested_.store(true);

		stop_scan();

		if (connection_thread_.joinable() &&
		    connection_thread_.get_id() != std::this_thread::get_id())
			connection_thread_.join();

		connection_thread_ = std::thread([this]() {
			try {
				winrt::init_apartment(winrt::apartment_type::multi_threaded);
			} catch (...) {
			}

			disconnect_internal(true);
			set_state(BleState::Idle, {});
		});
	}

	void disconnect_fast()
	{
		connect_cancel_requested_.store(true);

		stop_scan();

		/* Finish an already-completed/active connection worker before touching
		   its WinRT objects. In the normal connected state this join returns
		   immediately because the connect worker has already finished. */
		if (connection_thread_.joinable() &&
		    connection_thread_.get_id() != std::this_thread::get_id())
			connection_thread_.join();

		/* Do the actual release on an MTA thread, but do not perform the
		   potentially-blocking CCCD/GATT unsubscribe. Join this short worker
		   so a newly-created Heartbeat Source can scan immediately afterward. */
		std::thread release_thread([this]() {
			try {
				winrt::init_apartment(winrt::apartment_type::multi_threaded);
			} catch (...) {
			}

			disconnect_internal(false);
			set_state(BleState::Idle, {});
		});

		release_thread.join();
	}

	void shutdown()
	{
				connect_cancel_requested_.store(true);
if (shutdown_.exchange(true))
			return;

		stop_scan();

		if (connection_thread_.joinable() && connection_thread_.get_id() != std::this_thread::get_id())
			connection_thread_.join();

		try {
			winrt::init_apartment(winrt::apartment_type::multi_threaded);
		} catch (...) {
		}
		disconnect_internal(false);
	}

	bool seen_recently(uint64_t address, int64_t max_age_ms) const
	{
		const int64_t now = steady_ms();

		std::scoped_lock lock(devices_mutex_);
		auto it = devices_.find(address);

		if (it == devices_.end() ||
		    it->second.last_seen_ms <= 0 ||
		    !it->second.connectable)
			return false;

		const int64_t age = now - it->second.last_seen_ms;
		return age >= 0 && age <= max_age_ms;
	}

	BluetoothAddressType address_type_for(uint64_t address) const
	{
		std::scoped_lock lock(devices_mutex_);
		auto it = devices_.find(address);

		return it != devices_.end()
			       ? it->second.address_type
			       : BluetoothAddressType::Unspecified;
	}
	std::vector<HeartRateDeviceInfo> devices() const
	{
		std::vector<HeartRateDeviceInfo> result;
		std::scoped_lock lock(devices_mutex_);
		result.reserve(devices_.size());
		for (auto const &[address, item] : devices_)
			result.push_back(item);
		return result;
	}

	BleState state() const
	{
		return state_.load();
	}

	bool connected() const
	{
		return connected_.load();
	}

	int bpm() const
	{
		return bpm_.load();
	}

	bool bpm_fresh() const
	{
		const auto last = last_bpm_ms_.load();
		if (last <= 0)
			return false;
		const auto now = steady_ms();
		return now >= last && (now - last) <= 3000;
	}

	bool rr_intervals_seen() const
	{
		return rr_seen_.load();
	}

	bool rr_timing_fresh() const
	{
		const int64_t last = last_rr_ms_.load();
		if (last <= 0)
			return false;

		const int64_t now = steady_ms();
		return now >= last && (now - last) <= 3000;
	}

	HeartbeatTimingMode heartbeat_timing_mode() const
	{
		return rr_timing_fresh()
			       ? HeartbeatTimingMode::RrIntervals
			       : HeartbeatTimingMode::SyntheticBpm;
	}

	double synthetic_beat_interval_ms() const
	{
		const int value = bpm_.load();
		return value > 0
			       ? 60000.0 / static_cast<double>(value)
			       : 0.0;
	}

	std::vector<HeartbeatIntervalSample> rr_interval_history() const
	{
		std::scoped_lock lock(beat_mutex_);
		return rr_history_;
	}
	std::string connected_name() const
	{
		std::scoped_lock lock(connection_mutex_);
		return connected_name_;
	}

	uint64_t connected_address() const
	{
		std::scoped_lock lock(connection_mutex_);
		return connected_address_;
	}

	uint64_t active_address() const
	{
		return target_address_.load();
	}

	void load_known_names_from_disk()
	{
		std::scoped_lock io_lock(identity_io_mutex_);

		if (!ensure_mugen_heartbeat_config_dir())
			return;

		char *path = obs_module_config_path("device-names.json");
		if (!path)
			return;

		obs_data_t *root =
			obs_data_create_from_json_file_safe(path, "bak");

		bfree(path);

		if (!root)
			return;

		obs_data_array_t *devices =
			obs_data_get_array(root, "devices");

		size_t loaded = 0;

		if (devices) {
			const size_t count = obs_data_array_count(devices);

			std::scoped_lock lock(devices_mutex_);

			for (size_t i = 0; i < count; ++i) {
				obs_data_t *item =
					obs_data_array_item(devices, i);

				if (!item)
					continue;

				const uint64_t address =
					static_cast<uint64_t>(
						obs_data_get_int(
							item, "address"));

				std::string name =
					sanitize_ble_name(
						obs_data_get_string(
							item, "name"));

				obs_data_release(item);

				/* Do not persist/revive Windows' generic Bluetooth+MAC
				   fallback. Manufacturer or full product names only. */
				if (address == 0 ||
				    ble_name_quality(name) < 2)
					continue;

				auto &known =
					known_device_names_[address];

				if (ble_name_is_better(name, known))
					known = name;

				++loaded;
			}

			obs_data_array_release(devices);
		}

		obs_data_release(root);

		if (loaded > 0) {
			blog(LOG_INFO,
			     "[Mugen Heartbeat] loaded %zu cached BLE device name(s).",
			     loaded);
		}
	}

	void save_known_names_to_disk() const
	{
		std::scoped_lock io_lock(identity_io_mutex_);

		if (!ensure_mugen_heartbeat_config_dir())
			return;

		std::vector<std::pair<uint64_t, std::string>> snapshot;

		{
			std::scoped_lock lock(devices_mutex_);

			snapshot.reserve(known_device_names_.size());

			for (auto const &[address, name] :
			     known_device_names_) {
				if (address != 0 &&
				    ble_name_quality(name) >= 2) {
					snapshot.emplace_back(
						address, name);
				}
			}
		}

		char *path = obs_module_config_path("device-names.json");
		if (!path)
			return;

		obs_data_t *root = obs_data_create();
		obs_data_array_t *devices = obs_data_array_create();

		for (auto const &[address, name] : snapshot) {
			obs_data_t *item = obs_data_create();

			obs_data_set_int(
				item,
				"address",
				static_cast<long long>(address));

			obs_data_set_string(
				item,
				"name",
				name.c_str());

			obs_data_array_push_back(devices, item);
			obs_data_release(item);
		}

		obs_data_set_array(root, "devices", devices);

		const bool saved =
			obs_data_save_json_safe(
				root, path, "tmp", "bak");

		if (!saved) {
			blog(LOG_WARNING,
			     "[Mugen Heartbeat] failed to save BLE device-name cache.");
		}

		obs_data_array_release(devices);
		obs_data_release(root);
		bfree(path);
	}

	void remember_name(uint64_t address, std::string name)
	{
		if (address == 0)
			return;

		name = sanitize_ble_name(std::move(name));

		/* "Bluetooth aa:bb..." is a Windows fallback, not identity. */
		if (ble_name_quality(name) < 2)
			return;

		bool changed = false;

		{
			std::scoped_lock lock(devices_mutex_);

			auto &known = known_device_names_[address];
			if (ble_name_is_better(name, known)) {
				known = name;
				changed = true;
			}

			auto it = devices_.find(address);
			if (it != devices_.end() &&
			    ble_name_is_better(known, it->second.name)) {
				it->second.name = known;
				changed = true;
			}
		}

		if (changed) {
			revision_.fetch_add(1);
			save_known_names_to_disk();
		}
	}

	std::string known_name_for(uint64_t address) const
	{
		if (address == 0)
			return {};

		std::scoped_lock lock(devices_mutex_);
		auto it = known_device_names_.find(address);
		return it != known_device_names_.end() ? it->second : std::string{};
	}

	unsigned service_discovery_failures() const
	{
		return service_discovery_failures_.load();
	}

	int64_t service_discovery_started_ms() const
	{
		return service_discovery_started_ms_.load();
	}

	std::string error_text() const
	{
		std::scoped_lock lock(state_mutex_);
		return error_text_;
	}

	uint64_t revision() const
	{
		return revision_.load();
	}

private:
	static int64_t steady_ms()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			       std::chrono::steady_clock::now().time_since_epoch())
			.count();
	}

	static bool advertisement_has_hr_service(BluetoothLEAdvertisement const &advertisement)
	{
		/* WinRT normally exposes 16-bit service UUID AD structures through
		   ServiceUuids(). Keep that fast path first. */
		auto uuids = advertisement.ServiceUuids();
		for (uint32_t i = 0; i < uuids.Size(); ++i) {
			if (uuids.GetAt(i) == HEART_RATE_SERVICE_UUID)
				return true;
		}

		/* Some peripherals/stack states expose the UUID only in raw AD
		   sections (notably Service Data, AD type 0x16). Accept the standard
		   little-endian 0x180D encoding there as well. */
		auto sections = advertisement.DataSections();
		for (uint32_t i = 0; i < sections.Size(); ++i) {
			auto section = sections.GetAt(i);
			const uint8_t type = section.DataType();

			if (type != 0x02 && type != 0x03 && type != 0x16)
				continue;

			DataReader reader = DataReader::FromBuffer(section.Data());
			const uint32_t length = reader.UnconsumedBufferLength();
			if (length < 2)
				continue;

			std::vector<uint8_t> bytes(length);
			reader.ReadBytes(bytes);

			if (type == 0x16) {
				if (bytes[0] == 0x0D && bytes[1] == 0x18)
					return true;
				continue;
			}

			for (size_t offset = 0; offset + 1 < bytes.size(); offset += 2) {
				if (bytes[offset] == 0x0D && bytes[offset + 1] == 0x18)
					return true;
			}
		}

		return false;
	}

	static std::string format_address(uint64_t address)
	{
		std::ostringstream ss;
		ss << std::uppercase << std::hex << std::setfill('0');
		for (int shift = 40; shift >= 0; shift -= 8) {
			if (shift != 40)
				ss << ':';
			ss << std::setw(2) << ((address >> shift) & 0xFF);
		}
		return ss.str();
	}

	void scan_worker()
	{
		try {
			winrt::init_apartment(winrt::apartment_type::multi_threaded);

			const uint64_t preferred_address =
				scan_preferred_address_.load();

			std::atomic<uint64_t> packet_count{0};
			std::atomic<uint64_t> connectable_count{0};
			std::atomic<uint64_t> hr_qualified_count{0};
			std::atomic<uint64_t> preferred_count{0};
			std::atomic<uint64_t> accepted_count{0};
			std::atomic<bool> preferred_connectable{false};
			std::atomic<int> preferred_rssi{0};
			std::atomic<int> preferred_address_type{
				static_cast<int>(BluetoothAddressType::Unspecified)};

			BluetoothLEAdvertisementWatcher watcher;
			watcher.ScanningMode(BluetoothLEScanningMode::Active);

			auto received_token = watcher.Received(
				[this,
				 preferred_address,
				 &packet_count,
				 &connectable_count,
				 &hr_qualified_count,
				 &preferred_count,
				 &accepted_count,
				 &preferred_connectable,
				 &preferred_rssi,
				 &preferred_address_type](
					BluetoothLEAdvertisementWatcher const &,
					BluetoothLEAdvertisementReceivedEventArgs const &args) {
					try {
						packet_count.fetch_add(1);

						const uint64_t address =
							args.BluetoothAddress();
						const bool connectable =
							args.IsConnectable();

						if (connectable)
							connectable_count.fetch_add(1);

						auto advertisement =
							args.Advertisement();

						const bool has_hr_service =
							advertisement_has_hr_service(
								advertisement);
						const bool is_preferred =
							preferred_address != 0 &&
							address == preferred_address;

						if (has_hr_service)
							hr_qualified_count.fetch_add(1);

						if (is_preferred) {
							preferred_count.fetch_add(1);
							preferred_rssi.store(
								args.RawSignalStrengthInDBm());
							preferred_address_type.store(
								static_cast<int>(
									args.BluetoothAddressType()));

							if (connectable)
								preferred_connectable.store(true);
						}

						/* Unknown devices must prove they advertise the
						   standard Heart Rate service. The already-selected
						   Band may also qualify by its exact fresh address:
						   Windows can deliver primary ADV and scan response
						   data as separate packets. */
						if (!has_hr_service && !is_preferred)
							return;

						accepted_count.fetch_add(1);

						std::string name =
							sanitize_ble_name(
								to_string(
									advertisement.LocalName()));

						bool added = false;
						bool identity_changed = false;
						{
							std::scoped_lock lock(
								devices_mutex_);

							auto &known = known_device_names_[address];

							if (ble_name_quality(name) >= 2 &&
							    ble_name_is_better(name, known)) {
								known = name;
								identity_changed = true;
							}

							name = preferred_ble_name(name, known);

							auto it = devices_.find(address);

							if (it == devices_.end()) {
								devices_.emplace(
									address,
									HeartRateDeviceInfo{
										address,
										name,
										args.RawSignalStrengthInDBm(),
										steady_ms(),
										args.BluetoothAddressType(),
										connectable});
								added = true;
							} else {
								const std::string better =
									preferred_ble_name(
										it->second.name,
										name);

								if (better != it->second.name) {
									it->second.name = better;
									identity_changed = true;
								}

								it->second.rssi =
									args.RawSignalStrengthInDBm();
								it->second.last_seen_ms =
									steady_ms();
								it->second.address_type =
									args.BluetoothAddressType();
								it->second.connectable =
									it->second.connectable ||
									connectable;
							}
						}

						if (identity_changed) {
							revision_.fetch_add(1);
							save_known_names_to_disk();
						}

						if (added)
							revision_.fetch_add(1);
					} catch (hresult_error const &e) {
						blog(
							LOG_WARNING,
							"[Mugen Heartbeat] BLE advertisement callback error: %s",
							to_string(e.message()).c_str());
					} catch (...) {
						blog(
							LOG_WARNING,
							"[Mugen Heartbeat] BLE advertisement callback error: unknown");
					}
				});

			watcher.Start();

			const auto deadline =
				std::chrono::steady_clock::now() +
				std::chrono::seconds(5);

			while (!scan_stop_requested_.load() &&
			       !shutdown_.load() &&
			       std::chrono::steady_clock::now() < deadline) {
				std::this_thread::sleep_for(
					std::chrono::milliseconds(100));
			}

			watcher.Stop();
			watcher.Received(received_token);

			blog(
				LOG_INFO,
				"[Mugen Heartbeat] BLE scan summary: "
				"packets=%llu connectable=%llu hrQualified=%llu "
				"preferred=%llu accepted=%llu "
				"preferredConnectable=%s preferredRssi=%d "
				"preferredAddressType=%d",
				static_cast<unsigned long long>(
					packet_count.load()),
				static_cast<unsigned long long>(
					connectable_count.load()),
				static_cast<unsigned long long>(
					hr_qualified_count.load()),
				static_cast<unsigned long long>(
					preferred_count.load()),
				static_cast<unsigned long long>(
					accepted_count.load()),
				preferred_connectable.load() ? "yes" : "no",
				preferred_rssi.load(),
				preferred_address_type.load());

			if (packet_count.load() > 0 &&
			    hr_qualified_count.load() == 0 &&
			    preferred_count.load() == 0) {
				blog(
					LOG_INFO,
					"[Mugen Heartbeat] BLE scan note: watcher receives BLE traffic, "
					"but neither 0x180D nor the selected Band address was observed.");
			}

			if (!shutdown_.load() &&
			    state_.load() == BleState::Scanning) {
				set_state(BleState::Idle, {});
			}
		} catch (hresult_error const &e) {
			set_state(
				BleState::Error,
				to_string(e.message()));
		} catch (std::exception const &e) {
			set_state(BleState::Error, e.what());
		} catch (...) {
			set_state(
				BleState::Error,
				"Unknown BLE scan error");
		}
	}

	void on_measurement(GattValueChangedEventArgs const &args)
	{
		try {
			DataReader reader = DataReader::FromBuffer(args.CharacteristicValue());
			const uint32_t length = reader.UnconsumedBufferLength();
			if (length < 2)
				return;

			std::vector<uint8_t> bytes(length);
			reader.ReadBytes(bytes);

			const uint8_t flags = bytes[0];
			const bool is_uint16 = (flags & 0x01) != 0;

			int value = 0;
			if (is_uint16) {
				if (bytes.size() < 3)
					return;
				value = bytes[1] | (bytes[2] << 8);
			} else {
				value = bytes[1];
			}

			if (value <= 0 || value > 255)
				return;

			bpm_.store(value);
			last_bpm_ms_.store(steady_ms());
			/* Standard Heart Rate Measurement can optionally carry one or
			   more RR-Intervals. Keep them even though Smart Band 10 does
			   not currently send them; other standard HRS devices may. */
			const bool has_energy_expended = (flags & 0x08) != 0;
			const bool has_rr = (flags & 0x10) != 0;
			size_t rr_offset = is_uint16 ? 3u : 2u;

			if (has_energy_expended) {
				if (rr_offset + 1 < bytes.size())
					rr_offset += 2;
				else
					rr_offset = bytes.size();
			}

			if (has_rr) {
				std::vector<double> parsed_rr_ms;

				while (rr_offset + 1 < bytes.size()) {
					const uint16_t rr_1024 =
						static_cast<uint16_t>(
							bytes[rr_offset] |
							(bytes[rr_offset + 1] << 8));
					rr_offset += 2;

					if (rr_1024 == 0)
						continue;

					parsed_rr_ms.push_back(
						static_cast<double>(rr_1024) *
						1000.0 / 1024.0);
				}

				if (!parsed_rr_ms.empty()) {
					const int64_t received_ms = steady_ms();

					{
						std::scoped_lock lock(beat_mutex_);

						for (const double interval_ms :
						     parsed_rr_ms) {
							rr_history_.push_back(
								HeartbeatIntervalSample{
									next_rr_sequence_++,
									interval_ms,
									received_ms});
						}

						constexpr size_t RR_HISTORY_LIMIT = 64;
						if (rr_history_.size() >
						    RR_HISTORY_LIMIT) {
							rr_history_.erase(
								rr_history_.begin(),
								rr_history_.begin() +
									static_cast<
										std::ptrdiff_t>(
										rr_history_.size() -
										RR_HISTORY_LIMIT));
						}
					}

					rr_seen_.store(true);
					last_rr_ms_.store(received_ms);
				}
			}
if (!first_measurement_logged_.exchange(true)) {
				blog(LOG_INFO,
				     "[Mugen Heartbeat] first Heart Rate measurement received: %d BPM",
				     value);
			}
		} catch (...) {
			/* Ignore malformed notification packets. */
		}
	}

	void disconnect_internal(bool unsubscribe)
	{
		GattCharacteristic measurement = nullptr;
		BluetoothLEDevice device = nullptr;
		GattSession session = nullptr;

		{
			std::scoped_lock lock(connection_mutex_);

			measurement = measurement_;
			device = device_;
			session = gatt_session_;
		}

		maintain_session_.store(false);

		/* Cancelling MaintainConnection is local and must happen before
		   releasing the device/session references. */
		if (session) {
			try {
				session.MaintainConnection(false);
			} catch (...) {
			}

			try {
				if (has_session_status_token_) {
					session.SessionStatusChanged(
						session_status_token_);
					has_session_status_token_ = false;
				}
			} catch (...) {
			}
		}

		if (measurement) {
			try {
				if (has_measurement_token_) {
					measurement.ValueChanged(
						measurement_token_);
					has_measurement_token_ = false;
				}
			} catch (...) {
			}

			if (unsubscribe) {
				try {
					measurement
						.WriteClientCharacteristicConfigurationDescriptorAsync(
							GattClientCharacteristicConfigurationDescriptorValue::
								None)
						.get();
				} catch (...) {
				}
			}
		}

		if (device) {
			try {
				if (has_device_status_token_) {
					device.ConnectionStatusChanged(
						device_status_token_);
					has_device_status_token_ = false;
				}
			} catch (...) {
			}
		}

		if (session) {
			try {
				session.Close();
			} catch (...) {
			}
		}

		{
			std::scoped_lock lock(connection_mutex_);

			measurement_ = nullptr;
			service_ = nullptr;
			gatt_session_ = nullptr;
			device_ = nullptr;

			connected_name_.clear();
			connected_address_ = 0;
		}

		connected_.store(false);
		bpm_.store(0);
		last_bpm_ms_.store(0);
		{
			std::scoped_lock lock(beat_mutex_);
			rr_history_.clear();
			next_rr_sequence_ = 1;
		}
		rr_seen_.store(false);
		last_rr_ms_.store(0);
		target_address_.store(0);
		first_measurement_logged_.store(false);
		service_discovery_failures_.store(0);
		service_discovery_started_ms_.store(0);
	}

	void set_state(BleState state, std::string error)
	{
		const char *state_name = "Unknown";

		switch (state) {
		case BleState::Idle:
			state_name = "Idle";
			break;
		case BleState::Scanning:
			state_name = "Scanning";
			break;
		case BleState::Connecting:
			state_name = "Connecting";
			break;
		case BleState::Connected:
			state_name = "Connected";
			break;
		case BleState::Error:
			state_name = "Error";
			break;
		}

		if (error.empty()) {
			blog(LOG_INFO,
			     "[Mugen Heartbeat] BLE state: %s",
			     state_name);
		} else {
			blog(LOG_WARNING,
			     "[Mugen Heartbeat] BLE state: %s -- %s",
			     state_name,
			     error.c_str());
		}

		state_.store(state);
		{
			std::scoped_lock lock(state_mutex_);
			error_text_ = std::move(error);
		}
		revision_.fetch_add(1);
	}

	mutable std::mutex devices_mutex_;
	mutable std::mutex identity_io_mutex_;
	std::map<uint64_t, HeartRateDeviceInfo> devices_;
	std::map<uint64_t, std::string> known_device_names_;

	mutable std::mutex connection_mutex_;
	BluetoothLEDevice device_ = nullptr;
	GattSession gatt_session_ = nullptr;
	GattDeviceService service_ = nullptr;
	GattCharacteristic measurement_ = nullptr;
	event_token measurement_token_{};
	event_token device_status_token_{};
	event_token session_status_token_{};
	bool has_measurement_token_ = false;
	bool has_device_status_token_ = false;
	bool has_session_status_token_ = false;
	std::string connected_name_;
	uint64_t connected_address_ = 0;

	mutable std::mutex state_mutex_;
	std::string error_text_;

	std::thread scan_thread_;
	std::thread connection_thread_;
	std::atomic<bool> scan_stop_requested_{false};
	std::atomic<uint64_t> scan_preferred_address_{0};
	std::atomic<bool> shutdown_{false};
	std::atomic<bool> connect_cancel_requested_{false};
	std::atomic<bool> maintain_session_{false};
	std::atomic<uint64_t> target_address_{0};
	std::atomic<bool> first_measurement_logged_{false};
	mutable std::mutex beat_mutex_;
	std::vector<HeartbeatIntervalSample> rr_history_;
	uint64_t next_rr_sequence_ = 1;
	std::atomic<bool> rr_seen_{false};
	std::atomic<int64_t> last_rr_ms_{0};
	std::atomic<unsigned> service_discovery_failures_{0};
	std::atomic<int64_t> service_discovery_started_ms_{0};
	std::atomic<bool> connected_{false};
	std::atomic<int> bpm_{0};
	std::atomic<int64_t> last_bpm_ms_{0};
	std::atomic<BleState> state_{BleState::Idle};
	std::atomic<uint64_t> revision_{1};
};

static BleHeartRateClient *g_ble = nullptr;

struct mugen_heartbeat_properties_task {
	obs_weak_source_t *weak_source = nullptr;
	uint64_t connected_address = 0;
	std::string connected_name;
};

static void mugen_heartbeat_refresh_properties_ui(void *param)
{
	auto *task = static_cast<mugen_heartbeat_properties_task *>(param);
	if (!task)
		return;

	obs_source_t *source =
		task->weak_source ? obs_weak_source_get_source(task->weak_source) : nullptr;

	if (source) {
		if (task->connected_address != 0 && !task->connected_name.empty()) {
			obs_data_t *settings = obs_source_get_settings(source);

			uint64_t current_address = 0;
			const char *address_text =
				obs_data_get_string(settings, SETTING_DEVICE_ADDRESS);

			if (address_text && *address_text) {
				try {
					current_address = std::stoull(address_text, nullptr, 16);
				} catch (...) {
					current_address = 0;
				}
			}

			if (current_address == task->connected_address) {
				const char *saved_name =
					obs_data_get_string(settings, SETTING_DEVICE_NAME);

				const std::string current_name =
					saved_name ? saved_name : "";

				if (ble_name_is_better(
					    task->connected_name,
					    current_name)) {
					obs_data_set_string(
						settings,
						SETTING_DEVICE_NAME,
						task->connected_name.c_str());

					obs_source_update(source, settings);
				}
			}

			obs_data_release(settings);
		}

		obs_source_update_properties(source);
		obs_source_release(source);
	}

	if (task->weak_source)
		obs_weak_source_release(task->weak_source);

	delete task;
}

static void mugen_heartbeat_queue_properties_refresh(obs_source_t *source,
						      uint64_t selected_address)
{
	if (!source)
		return;

	obs_weak_source_t *weak = obs_source_get_weak_source(source);
	if (!weak)
		return;

	auto *task = new mugen_heartbeat_properties_task();
	task->weak_source = weak;

	if (g_ble && g_ble->connected() &&
	    selected_address != 0 &&
	    g_ble->connected_address() == selected_address) {
		task->connected_address = selected_address;
		task->connected_name = g_ble->connected_name();
	}

	obs_queue_task(OBS_TASK_UI, mugen_heartbeat_refresh_properties_ui, task, false);
}

static int heartbeat_clamp_int(int value, int low, int high)
{
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

static double heartbeat_clamp_double(double value, double low, double high)
{
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

struct HeartbeatReaction {
	bool enabled = false;
	std::string action = "show_source";
	std::string target_scene;
	std::string target_source;
	std::string target_filter;
	int activate_bpm = 120;
	int reset_bpm = 110;
	int activate_delay_ms = 1000;
	int reset_delay_ms = 2000;

	/* Runtime state. */
	bool runtime_applied = false;
	bool runtime_active = false;
	bool candidate_high = false;
	int64_t candidate_since_ms = 0;
};

static bool heartbeat_reaction_is_filter(const std::string &action)
{
	return action == "enable_filter" || action == "disable_filter";
}

static bool heartbeat_reaction_is_source(const std::string &action)
{
	return action == "show_source" || action == "hide_source";
}

static bool heartbeat_reaction_target_ready(const HeartbeatReaction &reaction)
{
	if (reaction.target_scene.empty() || reaction.target_source.empty())
		return false;

	if (heartbeat_reaction_is_filter(reaction.action) &&
	    reaction.target_filter.empty())
		return false;

	return heartbeat_reaction_is_filter(reaction.action) ||
	       heartbeat_reaction_is_source(reaction.action);
}

struct HeartbeatReactionActionTask {
	std::string action;
	std::string target_scene;
	std::string target_source;
	std::string target_filter;
	bool high = false;
};

static bool heartbeat_reaction_set_scene_item_visible(
	const std::string &scene_name,
	const std::string &source_name,
	bool visible)
{
	if (scene_name.empty() || source_name.empty())
		return false;

	obs_source_t *scene_source =
		obs_get_source_by_name(scene_name.c_str());

	if (!scene_source)
		return false;

	obs_scene_t *scene = obs_scene_from_source(scene_source);
	obs_sceneitem_t *item =
		scene ? obs_scene_find_source_recursive(
				scene, source_name.c_str())
		      : nullptr;

	const bool found = item != nullptr;

	if (item)
		obs_sceneitem_set_visible(item, visible);

	obs_source_release(scene_source);
	return found;
}

static void heartbeat_reaction_apply_ui(void *param)
{
	std::unique_ptr<HeartbeatReactionActionTask> task(
		static_cast<HeartbeatReactionActionTask *>(param));

	if (!task || task->target_source.empty())
		return;

	if (heartbeat_reaction_is_source(task->action)) {
		const bool visible =
			task->action == "show_source"
				? task->high
				: !task->high;

		const bool found =
			heartbeat_reaction_set_scene_item_visible(
				task->target_scene,
				task->target_source,
				visible);

		if (!found) {
			blog(LOG_WARNING,
			     "[Mugen Heartbeat] BPM reaction scene item not found: "
			     "scene='%s' source='%s'.",
			     task->target_scene.c_str(),
			     task->target_source.c_str());
			return;
		}

		blog(LOG_INFO,
		     "[Mugen Heartbeat] BPM reaction: scene '%s' item '%s' -> %s.",
		     task->target_scene.c_str(),
		     task->target_source.c_str(),
		     visible ? "visible" : "hidden");

		return;
	}

	if (heartbeat_reaction_is_filter(task->action)) {
		obs_source_t *source =
			obs_get_source_by_name(task->target_source.c_str());

		if (!source) {
			blog(LOG_WARNING,
			     "[Mugen Heartbeat] BPM reaction target source not found: '%s'.",
			     task->target_source.c_str());
			return;
		}

		obs_source_t *filter =
			obs_source_get_filter_by_name(
				source,
				task->target_filter.c_str());

		if (!filter) {
			blog(LOG_WARNING,
			     "[Mugen Heartbeat] BPM reaction filter not found: '%s' on '%s'.",
			     task->target_filter.c_str(),
			     task->target_source.c_str());
			obs_source_release(source);
			return;
		}

		const bool enabled =
			task->action == "enable_filter"
				? task->high
				: !task->high;

		obs_source_set_enabled(filter, enabled);

		blog(LOG_INFO,
		     "[Mugen Heartbeat] BPM reaction: filter '%s' on '%s' -> %s.",
		     task->target_filter.c_str(),
		     task->target_source.c_str(),
		     enabled ? "enabled" : "disabled");

		obs_source_release(filter);
		obs_source_release(source);
	}
}

static void heartbeat_reaction_queue_action(const HeartbeatReaction &reaction,
					     bool high)
{
	if (!heartbeat_reaction_target_ready(reaction))
		return;

	auto *task = new HeartbeatReactionActionTask();
	task->action = reaction.action;
	task->target_scene = reaction.target_scene;
	task->target_source = reaction.target_source;
	task->target_filter = reaction.target_filter;
	task->high = high;

	obs_queue_task(
		OBS_TASK_UI,
		heartbeat_reaction_apply_ui,
		task,
		false);
}

static void heartbeat_reaction_reset_runtime(HeartbeatReaction &reaction)
{
	reaction.runtime_applied = false;
	reaction.runtime_active = false;
	reaction.candidate_high = false;
	reaction.candidate_since_ms = 0;
}

static bool heartbeat_reaction_same_target(const HeartbeatReaction &a,
					   const HeartbeatReaction &b)
{
	return a.action == b.action &&
	       a.target_scene == b.target_scene &&
	       a.target_source == b.target_source &&
	       a.target_filter == b.target_filter;
}

struct HeartbeatPulseMarkupPiece {
	std::string text;
	bool pulse = false;
	bool drawable = false;
	uint32_t utf16_start = 0;
	uint32_t utf16_length = 0;
};
struct mugen_heartbeat_source {
	obs_source_t *source = nullptr;
	std::unique_ptr<MtsdfTextRenderer> mtsdf_renderer;
	std::unique_ptr<HeartbeatSkinRenderer> skin_renderer;
	std::string visual_mode = "text";
	std::string skin_id = "bio-monitor";
	int skin_background_opacity = 100;
	obs_source_t *text_sources[RENDER_LEVEL_COUNT] = {};
	obs_source_t *shadow_sources[RENDER_LEVEL_COUNT] = {};
	int test_bpm = 87;
	bool test_mode = false;
	bool auto_connect = false;
	bool auto_connect_paused = false;
	bool manual_connect_pending = false;
	int64_t manual_connect_deadline_ms = 0;
	uint64_t selected_address = 0;
	std::string selected_device_name;
	int64_t next_auto_connect_ms = 0;
	int64_t ble_watchdog_stale_since_ms = 0;
	int64_t ble_watchdog_cooldown_until_ms = 0;

	HeartbeatReaction reactions[HEARTBEAT_REACTION_COUNT];
	/* UI-only state: showing the editor never toggles the reaction itself. */
	bool reaction_ui_expanded[HEARTBEAT_REACTION_COUNT] = {};

	std::string display_format = "heart_bpm";
	std::string custom_format = "{heart} {bpm} BPM";
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

	bool beat_animation_enabled = false;
	int beat_strength = 8;
	double beat_interval_ms = 0.0;
	int64_t beat_last_trigger_ms = 0;
	int64_t beat_next_trigger_ms = 0;
	uint64_t beat_last_rr_sequence = 0;

	bool pulse_markup_active = false;
	std::vector<HeartbeatPulseMarkupPiece> pulse_markup_pieces;
	std::vector<MtsdfPulseRange> pulse_markup_ranges;

	bool appearance_dirty = true;
	int last_rendered_bpm = -999;
	bool last_rendered_live = false;
	uint64_t last_ble_revision = 0;
};
static std::mutex g_source_registry_mutex;
static std::vector<mugen_heartbeat_source *> g_source_registry;

static void mugen_heartbeat_register_source(mugen_heartbeat_source *ctx)
{
	if (!ctx)
		return;

	std::scoped_lock lock(g_source_registry_mutex);
	g_source_registry.push_back(ctx);
}

static size_t mugen_heartbeat_unregister_source(mugen_heartbeat_source *ctx)
{
	std::scoped_lock lock(g_source_registry_mutex);

	for (auto it = g_source_registry.begin();
	     it != g_source_registry.end(); ++it) {
		if (*it == ctx) {
			g_source_registry.erase(it);
			break;
		}
	}

	return g_source_registry.size();
}

static bool mugen_heartbeat_address_has_user(uint64_t address)
{
	if (address == 0)
		return false;

	std::scoped_lock lock(g_source_registry_mutex);

	for (auto *source : g_source_registry) {
		if (source && source->selected_address == address)
			return true;
	}

	return false;
}

/* A global BLE session can serve several Heartbeat sources. Only an enabled
   auto-connect setting for this exact Band authorizes Windows maintenance. */
static bool mugen_heartbeat_auto_maintenance_wanted(uint64_t address)
{
	if (!address)
		return false;

	std::scoped_lock lock(g_source_registry_mutex);
	for (auto *source : g_source_registry) {
		if (source && source->selected_address == address &&
		    source->auto_connect && !source->auto_connect_paused &&
		    !source->test_mode)
			return true;
	}

	return false;
}


/* Several source instances can share one global BLE client. Show which
   *other* source has opted into reconnecting to this same device. */
static std::string mugen_heartbeat_other_auto_connect_source_name(
	const mugen_heartbeat_source *ctx)
{
	if (!ctx || !ctx->selected_address)
		return {};

	std::scoped_lock lock(g_source_registry_mutex);
	for (auto *other : g_source_registry) {
		if (!other || other == ctx || !other->source ||
		    other->selected_address != ctx->selected_address ||
		    !other->auto_connect || other->auto_connect_paused ||
		    other->test_mode)
			continue;

		const char *name = obs_source_get_name(other->source);
		if (name && *name)
			return name;
	}

	return {};
}

static const char *mugen_heartbeat_get_name(void *)
{
	return obs_module_text("MugenHeartbeat.SourceName");
}

static int64_t mugen_heartbeat_now_ms()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		       std::chrono::steady_clock::now().time_since_epoch())
		.count();
}
static std::string bluetooth_address_value(uint64_t address)
{
	std::ostringstream ss;
	ss << std::uppercase << std::hex << std::setw(12) << std::setfill('0') << address;
	return ss.str();
}

static uint64_t parse_bluetooth_address(const char *text)
{
	if (!text || !*text)
		return 0;

	try {
		return std::stoull(text, nullptr, 16);
	} catch (...) {
		return 0;
	}
}

static std::string display_device(const HeartRateDeviceInfo &item)
{
	std::ostringstream address;
	address << std::uppercase << std::hex << std::setfill('0');

	for (int shift = 40; shift >= 0; shift -= 8) {
		if (shift != 40)
			address << ':';
		address << std::setw(2) << ((item.address >> shift) & 0xFF);
	}

	std::string shown_name = display_ble_name(item.name, item.address);

	std::ostringstream ss;
	ss << shown_name << "  \xC2\xB7  " << address.str();

	if (item.rssi != 0)
		ss << "  \xC2\xB7  " << std::dec << item.rssi << " dBm";

	return ss.str();
}

static std::string localized_ble_status(const mugen_heartbeat_source *ctx)
{
	if (!g_ble)
		return obs_module_text("MugenHeartbeat.StatusIdle");

	switch (g_ble->state()) {
	case BleState::Scanning:
		return obs_module_text("MugenHeartbeat.StatusScanning");

	case BleState::Connecting:
		if (!ctx || ctx->selected_address == 0)
			return obs_module_text("MugenHeartbeat.StatusIdle");

		/* After a few real GATT failures the device is present, but the
		   Heart Rate service is unavailable. This is the state our thermos
		   tests reproduced and Share HR restart reliably recovered. */
		if (g_ble->service_discovery_failures() >= 3)
			return obs_module_text(
				"MugenHeartbeat.StatusPulseUnavailable");

		return obs_module_text("MugenHeartbeat.StatusConnecting");

	case BleState::Connected: {
		if (!ctx || ctx->selected_address == 0 ||
		    g_ble->connected_address() != ctx->selected_address)
			return obs_module_text("MugenHeartbeat.StatusIdle");

		std::string result =
			obs_module_text("MugenHeartbeat.StatusConnected");

		std::string name = ctx->selected_device_name;
		name = preferred_ble_name(
			name,
			g_ble->known_name_for(ctx->selected_address));
		name = preferred_ble_name(name, g_ble->connected_name());
		name = status_device_name(name, ctx->selected_address);

		if (!name.empty())
			result += ": " + name;

		return result;
	}

	case BleState::Error:
		if (!ctx || ctx->selected_address == 0)
			return obs_module_text("MugenHeartbeat.StatusIdle");

		return obs_module_text("MugenHeartbeat.StatusError");

	case BleState::Idle:
	default:
		return obs_module_text("MugenHeartbeat.StatusIdle");
	}
}

static void mugen_heartbeat_replace_all(std::string &text,
					 const std::string &token,
					 const std::string &value)
{
	if (token.empty())
		return;

	size_t position = 0;
	while ((position = text.find(token, position)) != std::string::npos) {
		text.replace(position, token.size(), value);
		position += value.size();
	}
}

static std::string mugen_heartbeat_make_display_text(const mugen_heartbeat_source *ctx,
						      int bpm, bool live_value)
{
	const std::string value = live_value ? std::to_string(bpm) : std::string("--");
	static const std::string heart = "\xE2\x99\xA5";

	if (!ctx || ctx->display_format == "heart_bpm")
		return heart + " " + value + " BPM";

	if (ctx->display_format == "heart")
		return heart + " " + value;

	if (ctx->display_format == "bpm")
		return value + " BPM";

	if (ctx->display_format == "custom") {
		std::string text = ctx->custom_format;
		mugen_heartbeat_replace_all(text, "{heart}", heart);
		mugen_heartbeat_replace_all(text, "{bpm}", value);
		return text;
	}

	return value;
}

static void mugen_heartbeat_apply_text_style(obs_source_t *source,
					      const mugen_heartbeat_source *ctx,
					      const std::string &text,
					      bool shadow,
					      float render_scale)
{
	if (!source || !ctx)
		return;

	const uint32_t color = shadow ? ctx->shadow_color : ctx->text_color;
	const int opacity = shadow ? ctx->shadow_opacity : ctx->text_opacity;
	const bool outline = shadow ? false : ctx->outline_enabled;
	const uint32_t outline_color = shadow ? 0x000000 : ctx->outline_color;
	const int outline_opacity = shadow ? 0 : ctx->outline_opacity;

	int physical_font_size =
		static_cast<int>(std::lround(static_cast<double>(ctx->font_size) *
					     render_scale));
	if (physical_font_size < 4)
		physical_font_size = 4;

	int physical_outline_size =
		static_cast<int>(std::lround(static_cast<double>(
			shadow ? 1 : ctx->outline_size) * render_scale));
	if (physical_outline_size < 1)
		physical_outline_size = 1;

	obs_data_t *settings = obs_source_get_settings(source);
	if (!settings)
		return;

	obs_data_set_string(settings, "text", text.c_str());
	obs_data_set_bool(settings, "read_from_file", false);
	obs_data_set_string(settings, "file", "");

	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", ctx->font_face.c_str());
	obs_data_set_int(font, "size", physical_font_size);
	obs_data_set_int(font, "flags", ctx->font_flags);
	obs_data_set_obj(settings, "font", font);

	obs_data_set_int(settings, "color", color);
	obs_data_set_int(settings, "opacity", opacity);

	obs_data_set_bool(settings, "gradient", false);
	obs_data_set_int(settings, "gradient_color", color);
	obs_data_set_int(settings, "gradient_opacity", opacity);
	obs_data_set_double(settings, "gradient_dir", 90.0);

	obs_data_set_int(settings, "bk_color", 0x000000);
	obs_data_set_int(settings, "bk_opacity", 0);

	obs_data_set_string(settings, "align", "left");
	obs_data_set_string(settings, "valign", "top");
	obs_data_set_bool(settings, "vertical", false);
	obs_data_set_int(settings, "transform", 0);
	obs_data_set_bool(settings, "antialiasing", true);

	obs_data_set_bool(settings, "outline", outline);
	obs_data_set_int(settings, "outline_size", physical_outline_size);
	obs_data_set_int(settings, "outline_color", outline_color);
	obs_data_set_int(settings, "outline_opacity", outline_opacity);

	obs_data_set_bool(settings, "chatlog", false);
	obs_data_set_int(settings, "chatlog_lines", 6);
	obs_data_set_bool(settings, "extents", false);
	obs_data_set_bool(settings, "extents_wrap", true);

	obs_source_update(source, settings);

	obs_data_release(font);
	obs_data_release(settings);
}


static bool mugen_heartbeat_current_bpm(const mugen_heartbeat_source *ctx,
					int &bpm)
{
	bpm = 0;

	if (!ctx)
		return false;

	if (ctx->test_mode) {
		bpm = ctx->test_bpm;
		return true;
	}

	if (ctx->selected_address != 0 &&
	    g_ble &&
	    g_ble->connected() &&
	    g_ble->connected_address() == ctx->selected_address &&
	    g_ble->bpm_fresh()) {
		bpm = g_ble->bpm();
		return true;
	}

	return false;
}

static double mugen_heartbeat_clamp_beat_interval(double interval_ms)
{
	if (!std::isfinite(interval_ms) || interval_ms <= 0.0)
		return 0.0;

	if (interval_ms < 250.0)
		return 250.0;

	if (interval_ms > 2000.0)
		return 2000.0;

	return interval_ms;
}

static void mugen_heartbeat_trigger_beat(mugen_heartbeat_source *ctx,
					 int64_t now,
					 double interval_ms)
{
	if (!ctx)
		return;

	interval_ms =
		mugen_heartbeat_clamp_beat_interval(interval_ms);

	if (interval_ms <= 0.0)
		return;

	ctx->beat_interval_ms = interval_ms;
	ctx->beat_last_trigger_ms = now;
	ctx->beat_next_trigger_ms =
		now + static_cast<int64_t>(std::lround(interval_ms));
}

static void mugen_heartbeat_reset_beat_animation(
	mugen_heartbeat_source *ctx)
{
	if (!ctx)
		return;

	ctx->beat_interval_ms = 0.0;
	ctx->beat_last_trigger_ms = 0;
	ctx->beat_next_trigger_ms = 0;
	ctx->beat_last_rr_sequence = 0;
}

static void mugen_heartbeat_update_beat_animation(
	mugen_heartbeat_source *ctx)
{
	if (!ctx)
		return;

	int bpm = 0;
	const bool bpm_valid =
		mugen_heartbeat_current_bpm(ctx, bpm);

	if (!ctx->beat_animation_enabled || !bpm_valid) {
		mugen_heartbeat_reset_beat_animation(ctx);
		return;
	}

	const int64_t now = mugen_heartbeat_now_ms();

	/* Manual/test BPM intentionally always uses synthetic timing so the
	   animation can be tested without wearing or stressing a sensor. */
	const bool rr_allowed =
		!ctx->test_mode &&
		ctx->selected_address != 0 &&
		g_ble &&
		g_ble->connected() &&
		g_ble->connected_address() == ctx->selected_address &&
		g_ble->heartbeat_timing_mode() ==
			HeartbeatTimingMode::RrIntervals;

	if (rr_allowed) {
		const auto history = g_ble->rr_interval_history();

		if (!history.empty()) {
			const HeartbeatIntervalSample *latest = nullptr;

			for (const auto &sample : history) {
				if (sample.sequence >
				    ctx->beat_last_rr_sequence)
					latest = &sample;
			}

			if (latest) {
				ctx->beat_last_rr_sequence =
					latest->sequence;

				/* BLE notifications can be delayed or batched. Use the
				   newest RR value as the cadence and pulse on receipt,
				   then predict the following beat from that interval. */
				mugen_heartbeat_trigger_beat(
					ctx, now, latest->interval_ms);
				return;
			}
		}
	}

	const double interval_ms =
		mugen_heartbeat_clamp_beat_interval(
			60000.0 / static_cast<double>(bpm));

	if (interval_ms <= 0.0)
		return;

	if (ctx->beat_last_trigger_ms == 0 ||
	    ctx->beat_next_trigger_ms == 0) {
		mugen_heartbeat_trigger_beat(
			ctx, now, interval_ms);
		return;
	}

	/* Follow BPM changes without abruptly restarting the current pulse. */
	ctx->beat_interval_ms = interval_ms;
	ctx->beat_next_trigger_ms =
		ctx->beat_last_trigger_ms +
		static_cast<int64_t>(std::lround(interval_ms));

	if (now >= ctx->beat_next_trigger_ms) {
		mugen_heartbeat_trigger_beat(
			ctx, now, interval_ms);
	}
}

static double mugen_heartbeat_smoothstep(double x)
{
	if (x <= 0.0)
		return 0.0;
	if (x >= 1.0)
		return 1.0;

	return x * x * (3.0 - 2.0 * x);
}

static float mugen_heartbeat_current_beat_scale(
	const mugen_heartbeat_source *ctx)
{
	if (!ctx || !ctx->beat_animation_enabled ||
	    ctx->beat_last_trigger_ms <= 0)
		return 1.0f;

	const int64_t elapsed =
		mugen_heartbeat_now_ms() -
		ctx->beat_last_trigger_ms;

	if (elapsed < 0 || elapsed >= 250)
		return 1.0f;

	double envelope = 0.0;

	/* A compact "lub-dub" scale envelope:
	   strong first pulse, smaller second pulse, then full rest. */
	if (elapsed < 55) {
		envelope =
			mugen_heartbeat_smoothstep(
				static_cast<double>(elapsed) / 55.0);
	} else if (elapsed < 130) {
		envelope =
			1.0 -
			mugen_heartbeat_smoothstep(
				static_cast<double>(elapsed - 55) /
				75.0);
	} else if (elapsed < 175) {
		envelope =
			0.42 *
			mugen_heartbeat_smoothstep(
				static_cast<double>(elapsed - 130) /
				45.0);
	} else {
		envelope =
			0.42 *
			(1.0 -
			 mugen_heartbeat_smoothstep(
				 static_cast<double>(elapsed - 175) /
				 75.0));
	}

	const double strength =
		static_cast<double>(ctx->beat_strength) / 100.0;

	return static_cast<float>(
		1.0 + envelope * strength);
}
static void mugen_heartbeat_apply_reaction_state(HeartbeatReaction &reaction,
						 bool high)
{
	if (reaction.runtime_applied &&
	    reaction.runtime_active == high) {
		reaction.candidate_since_ms = 0;
		return;
	}

	heartbeat_reaction_queue_action(reaction, high);

	reaction.runtime_applied = true;
	reaction.runtime_active = high;
	reaction.candidate_high = high;
	reaction.candidate_since_ms = 0;
}

static void mugen_heartbeat_update_reactions(mugen_heartbeat_source *ctx)
{
	if (!ctx)
		return;

	int bpm = 0;
	const bool bpm_valid =
		mugen_heartbeat_current_bpm(ctx, bpm);
	const int64_t now = mugen_heartbeat_now_ms();

	for (size_t i = 0; i < HEARTBEAT_REACTION_COUNT; ++i) {
		auto &reaction = ctx->reactions[i];

		if (!reaction.enabled ||
		    !heartbeat_reaction_target_ready(reaction)) {
			reaction.candidate_since_ms = 0;
			continue;
		}

		/* First valid sample establishes the normal (inactive) state.
		   If it is already above the activation threshold, respect the
		   configured activation delay before firing. */
		if (!reaction.runtime_applied) {
			if (!bpm_valid)
				continue;

			if (bpm < reaction.activate_bpm) {
				mugen_heartbeat_apply_reaction_state(
					reaction, false);
				continue;
			}
		}

		const bool wants_high =
			bpm_valid && bpm >= reaction.activate_bpm;

		const bool wants_low =
			!bpm_valid ||
			(reaction.runtime_active &&
			 bpm <= reaction.reset_bpm);

		if (!reaction.runtime_active) {
			if (!wants_high) {
				reaction.candidate_since_ms = 0;
				continue;
			}

			if (reaction.candidate_since_ms == 0 ||
			    !reaction.candidate_high) {
				reaction.candidate_high = true;
				reaction.candidate_since_ms = now;
			}

			if (now - reaction.candidate_since_ms >=
			    reaction.activate_delay_ms) {
				blog(LOG_INFO,
				     "[Mugen Heartbeat] BPM reaction %zu activated at %d BPM.",
				     i + 1, bpm);

				mugen_heartbeat_apply_reaction_state(
					reaction, true);
			}

			continue;
		}

		if (!wants_low) {
			reaction.candidate_since_ms = 0;
			continue;
		}

		if (reaction.candidate_since_ms == 0 ||
		    reaction.candidate_high) {
			reaction.candidate_high = false;
			reaction.candidate_since_ms = now;
		}

		if (now - reaction.candidate_since_ms >=
		    reaction.reset_delay_ms) {
			blog(LOG_INFO,
			     "[Mugen Heartbeat] BPM reaction %zu reset%s.",
			     i + 1,
			     bpm_valid ? "" : " after BPM signal loss");

			mugen_heartbeat_apply_reaction_state(
				reaction, false);
		}
	}
}

static bool mugen_heartbeat_markup_piece_drawable(
	const std::string &text)
{
	return text.find_first_not_of(" \t\r\n") !=
	       std::string::npos;
}

static void mugen_heartbeat_append_markup_piece(
	std::vector<HeartbeatPulseMarkupPiece> &pieces,
	std::string &plain_text,
	const std::string &text,
	bool pulse)
{
	if (text.empty())
		return;

	plain_text += text;

	if (!pieces.empty() &&
	    pieces.back().pulse == pulse) {
		pieces.back().text += text;
		pieces.back().drawable =
			mugen_heartbeat_markup_piece_drawable(
				pieces.back().text);
		return;
	}

	HeartbeatPulseMarkupPiece piece;
	piece.text = text;
	piece.pulse = pulse;
	piece.drawable =
		mugen_heartbeat_markup_piece_drawable(text);
	pieces.push_back(std::move(piece));
}

static bool mugen_heartbeat_parse_pulse_markup(
	const std::string &input,
	std::string &plain_text,
	std::vector<HeartbeatPulseMarkupPiece> &pieces)
{
	static const std::string open_tag = "[pulse]";
	static const std::string close_tag = "[/pulse]";

	plain_text.clear();
	pieces.clear();

	size_t pos = 0;
	bool inside_pulse = false;
	bool saw_complete_pair = false;

	while (pos < input.size()) {
		const size_t open = input.find(open_tag, pos);
		const size_t close = input.find(close_tag, pos);

		if (open == std::string::npos &&
		    close == std::string::npos) {
			mugen_heartbeat_append_markup_piece(
				pieces,
				plain_text,
				input.substr(pos),
				inside_pulse);
			pos = input.size();
			break;
		}

		const bool next_is_open =
			open != std::string::npos &&
			(close == std::string::npos ||
			 open < close);

		const size_t next =
			next_is_open ? open : close;

		mugen_heartbeat_append_markup_piece(
			pieces,
			plain_text,
			input.substr(pos, next - pos),
			inside_pulse);

		if (next_is_open) {
			if (inside_pulse) {
				plain_text = input;
				pieces.clear();
				return false;
			}

			inside_pulse = true;
			pos = next + open_tag.size();
		} else {
			if (!inside_pulse) {
				plain_text = input;
				pieces.clear();
				return false;
			}

			inside_pulse = false;
			saw_complete_pair = true;
			pos = next + close_tag.size();
		}
	}

	if (inside_pulse) {
		plain_text = input;
		pieces.clear();
		return false;
	}

	if (!saw_complete_pair) {
		plain_text = input;
		pieces.clear();
		return false;
	}

	return true;
}

static void mugen_heartbeat_update_text(mugen_heartbeat_source *ctx)
{
	if (!ctx || !ctx->text_sources[RENDER_LEVEL_NATIVE])
		return;

	int bpm = 0;
	const bool live_value =
		mugen_heartbeat_current_bpm(ctx, bpm);

	const bool appearance_dirty = ctx->appearance_dirty;

	if (ctx->visual_mode == "skin" &&
	    ctx->skin_renderer &&
	    ctx->skin_renderer->ensure_selected(
		    ctx->skin_id)) {
		if (ctx->last_rendered_bpm == bpm &&
		    ctx->last_rendered_live == live_value &&
		    !appearance_dirty)
			return;

		ctx->last_rendered_bpm = bpm;
		ctx->last_rendered_live = live_value;

		ctx->skin_renderer->update(
			bpm, live_value);

		ctx->appearance_dirty = false;
		return;
	}

	if (ctx->last_rendered_bpm == bpm &&
	    ctx->last_rendered_live == live_value &&
	    !appearance_dirty)
		return;

	ctx->last_rendered_bpm = bpm;
	ctx->last_rendered_live = live_value;

	std::string text =
		mugen_heartbeat_make_display_text(
			ctx, bpm, live_value);

	std::string pulse_plain_text;
	std::vector<HeartbeatPulseMarkupPiece>
		parsed_pulse_pieces;

	const bool pulse_markup_valid =
		ctx->display_format == "custom" &&
		mugen_heartbeat_parse_pulse_markup(
			text,
			pulse_plain_text,
			parsed_pulse_pieces);

	ctx->pulse_markup_active = false;
	ctx->pulse_markup_pieces.clear();
	ctx->pulse_markup_ranges.clear();

	if (pulse_markup_valid) {
		text = pulse_plain_text;
		ctx->pulse_markup_pieces =
			parsed_pulse_pieces;

		uint32_t utf16_cursor = 0;

		for (auto &piece :
		     ctx->pulse_markup_pieces) {
			const winrt::hstring wide_piece =
				winrt::to_hstring(piece.text);

			piece.utf16_start =
				utf16_cursor;

			piece.utf16_length =
				static_cast<uint32_t>(
					wide_piece.size());

			if (piece.pulse &&
			    piece.drawable &&
			    piece.utf16_length > 0) {
				ctx->pulse_markup_active = true;

				MtsdfPulseRange range;
				range.start =
					piece.utf16_start;
				range.length =
					piece.utf16_length;

				ctx->pulse_markup_ranges.push_back(
					range);
			}

			utf16_cursor +=
				piece.utf16_length;
		}
	}
	MtsdfTextStyle mtsdf_style;
	mtsdf_style.font_face = ctx->font_face;
	mtsdf_style.font_size = ctx->font_size;
	mtsdf_style.font_flags = ctx->font_flags;

	mtsdf_style.text_color = ctx->text_color;
	mtsdf_style.text_opacity = ctx->text_opacity;

	mtsdf_style.outline_enabled = ctx->outline_enabled;
	mtsdf_style.outline_size = ctx->outline_size;
	mtsdf_style.outline_color = ctx->outline_color;
	mtsdf_style.outline_opacity = ctx->outline_opacity;

	mtsdf_style.shadow_enabled = ctx->shadow_enabled;
	mtsdf_style.shadow_x = ctx->shadow_x;
	mtsdf_style.shadow_y = ctx->shadow_y;
	mtsdf_style.shadow_color = ctx->shadow_color;
	mtsdf_style.shadow_opacity = ctx->shadow_opacity;

	if (ctx->mtsdf_renderer) {
		ctx->mtsdf_renderer->update(
			text,
			mtsdf_style,
			ctx->pulse_markup_ranges);
	}

	for (size_t i = 0; i < RENDER_LEVEL_COUNT; ++i) {
		mugen_heartbeat_apply_text_style(
			ctx->text_sources[i], ctx, text, false, RENDER_SCALES[i]);

		mugen_heartbeat_apply_text_style(
			ctx->shadow_sources[i], ctx, text, true, RENDER_SCALES[i]);
	}

	if (appearance_dirty) {
		blog(LOG_DEBUG,
		     "[Mugen Heartbeat] appearance updated: color=#%06X opacity=%d "
		     "outline=%s shadow=%s shadowOpacity=%d renderer=MTSDF",
		     ctx->text_color & 0xFFFFFF, ctx->text_opacity,
		     ctx->outline_enabled ? "on" : "off",
		     ctx->shadow_enabled ? "on" : "off",
		     ctx->shadow_opacity);
	}

	ctx->appearance_dirty = false;
}

static std::string mugen_heartbeat_skin_opacity_setting_key(
	const std::string &skin_id)
{
	std::string key = "skin_background_opacity__";

	for (const unsigned char ch : skin_id) {
		key.push_back(
			std::isalnum(ch)
				? static_cast<char>(ch)
				: '_');
	}

	return key;
}

static int mugen_heartbeat_skin_default_background_opacity(
	mugen_heartbeat_source *ctx,
	const std::string &skin_id)
{
	if (!ctx || !ctx->skin_renderer)
		return 100;

	HeartbeatSkinInfo info;
	if (!ctx->skin_renderer->info_for(
		    skin_id,
		    info))
		return 100;

	return heartbeat_clamp_int(
		info.background_opacity_default,
		0,
		100);
}

static void mugen_heartbeat_update(void *data, obs_data_t *settings)
{
	auto *ctx = static_cast<mugen_heartbeat_source *>(data);
	if (!ctx)
		return;

	const bool previous_auto_connect = ctx->auto_connect;
	const uint64_t previous_address = ctx->selected_address;
	const std::string previous_visual_mode = ctx->visual_mode;
	const std::string previous_skin_id = ctx->skin_id;

	const std::string previous_display_format = ctx->display_format;
	const std::string previous_custom_format = ctx->custom_format;
	const std::string previous_font_face = ctx->font_face;
	const int previous_font_size = ctx->font_size;
	const int64_t previous_font_flags = ctx->font_flags;
	const uint32_t previous_text_color = ctx->text_color;
	const int previous_text_opacity = ctx->text_opacity;
	const bool previous_outline_enabled = ctx->outline_enabled;
	const int previous_outline_size = ctx->outline_size;
	const uint32_t previous_outline_color = ctx->outline_color;
	const int previous_outline_opacity = ctx->outline_opacity;
	const bool previous_shadow_enabled = ctx->shadow_enabled;
	const int previous_shadow_x = ctx->shadow_x;
	const int previous_shadow_y = ctx->shadow_y;
	const uint32_t previous_shadow_color = ctx->shadow_color;
	const int previous_shadow_opacity = ctx->shadow_opacity;

	ctx->test_mode = obs_data_get_bool(settings, SETTING_TEST_MODE);
	ctx->test_bpm = static_cast<int>(obs_data_get_int(settings, SETTING_TEST_BPM));
	ctx->auto_connect = obs_data_get_bool(settings, SETTING_AUTO_CONNECT);

	const char *visual_mode =
		obs_data_get_string(
			settings, SETTING_VISUAL_MODE);

	ctx->visual_mode =
		(visual_mode &&
		 std::string(visual_mode) == "skin")
			? "skin"
			: "text";

	const char *skin_id =
		obs_data_get_string(
			settings, SETTING_SKIN_ID);

	ctx->skin_id =
		(skin_id && *skin_id)
			? skin_id
			: "bio-monitor";

	const std::string skin_opacity_key =
		mugen_heartbeat_skin_opacity_setting_key(
			ctx->skin_id);

	int skin_background_opacity = 100;

	if (obs_data_has_user_value(
		    settings,
		    skin_opacity_key.c_str())) {
		skin_background_opacity =
			static_cast<int>(
				obs_data_get_int(
					settings,
					skin_opacity_key.c_str()));
	} else if (obs_data_has_user_value(
			   settings,
			   SETTING_SKIN_BACKGROUND_OPACITY)) {
		/* Migrate a v0.1.31/v0.1.32 shared value without losing
		   an opacity the user already changed manually. */
		skin_background_opacity =
			static_cast<int>(
				obs_data_get_int(
					settings,
					SETTING_SKIN_BACKGROUND_OPACITY));

		obs_data_set_int(
			settings,
			skin_opacity_key.c_str(),
			skin_background_opacity);
	} else {
		skin_background_opacity =
			mugen_heartbeat_skin_default_background_opacity(
				ctx,
				ctx->skin_id);

		obs_data_set_int(
			settings,
			skin_opacity_key.c_str(),
			skin_background_opacity);

		obs_data_set_int(
			settings,
			SETTING_SKIN_BACKGROUND_OPACITY,
			skin_background_opacity);
	}

	ctx->skin_background_opacity =
		heartbeat_clamp_int(
			skin_background_opacity,
			0,
			100);

	for (size_t i = 0; i < HEARTBEAT_REACTION_COUNT; ++i) {
		const HeartbeatReaction old = ctx->reactions[i];
		HeartbeatReaction next = old;

		next.enabled =
			obs_data_get_bool(settings, REACTION_ENABLED_KEYS[i]);

		const char *action =
			obs_data_get_string(settings, REACTION_ACTION_KEYS[i]);
		next.action =
			(action && *action) ? action : "show_source";

		const char *target_scene =
			obs_data_get_string(settings, REACTION_SCENE_KEYS[i]);
		next.target_scene =
			(target_scene && *target_scene)
				? target_scene
				: "";

		const char *target_source =
			obs_data_get_string(settings, REACTION_SOURCE_KEYS[i]);
		next.target_source =
			(target_source && *target_source)
				? target_source
				: "";

		const char *target_filter =
			obs_data_get_string(settings, REACTION_FILTER_KEYS[i]);
		next.target_filter =
			(target_filter && *target_filter)
				? target_filter
				: "";

		next.activate_bpm =
			static_cast<int>(
				obs_data_get_int(
					settings,
					REACTION_ON_BPM_KEYS[i]));

		next.reset_bpm =
			static_cast<int>(
				obs_data_get_int(
					settings,
					REACTION_OFF_BPM_KEYS[i]));

		const double activate_delay =
			obs_data_get_double(
				settings,
				REACTION_ON_DELAY_KEYS[i]);

		const double reset_delay =
			obs_data_get_double(
				settings,
				REACTION_OFF_DELAY_KEYS[i]);

		next.activate_bpm =
			heartbeat_clamp_int(next.activate_bpm, 30, 240);

		next.reset_bpm =
			heartbeat_clamp_int(next.reset_bpm, 30, 240);

		if (next.reset_bpm >= next.activate_bpm) {
			next.reset_bpm =
				heartbeat_clamp_int(
					next.activate_bpm - 5,
					30, 240);

			obs_data_set_int(
				settings,
				REACTION_OFF_BPM_KEYS[i],
				next.reset_bpm);
		}

		next.activate_delay_ms =
			static_cast<int>(
				std::lround(
					heartbeat_clamp_double(
						activate_delay, 0.0, 10.0) *
					1000.0));

		next.reset_delay_ms =
			static_cast<int>(
				std::lround(
					heartbeat_clamp_double(
						reset_delay, 0.0, 10.0) *
					1000.0));

		const bool target_changed =
			!heartbeat_reaction_same_target(old, next);

		const bool enabled_changed =
			old.enabled != next.enabled;

		if ((target_changed || enabled_changed) &&
		    old.enabled &&
		    old.runtime_applied &&
		    heartbeat_reaction_target_ready(old)) {
			/* Restore the old target to its normal/inactive state
			   before giving control to a different target. */
			heartbeat_reaction_queue_action(old, false);
		}

		if (target_changed || enabled_changed) {
			heartbeat_reaction_reset_runtime(next);
		} else if (old.activate_bpm != next.activate_bpm ||
			   old.reset_bpm != next.reset_bpm ||
			   old.activate_delay_ms != next.activate_delay_ms ||
			   old.reset_delay_ms != next.reset_delay_ms) {
			next.candidate_since_ms = 0;
		}

		ctx->reactions[i] = std::move(next);
	}

	ctx->selected_address =
		parse_bluetooth_address(obs_data_get_string(settings, SETTING_DEVICE_ADDRESS));

	const char *saved_device_name =
		obs_data_get_string(settings, SETTING_DEVICE_NAME);

	if (ctx->selected_address == 0) {
		ctx->selected_device_name.clear();
		obs_data_set_string(settings, SETTING_DEVICE_NAME, "");
	} else if (previous_address != 0 &&
		   ctx->selected_address != previous_address) {
		ctx->selected_device_name =
			g_ble ? g_ble->known_name_for(ctx->selected_address)
			      : std::string{};

		obs_data_set_string(
			settings,
			SETTING_DEVICE_NAME,
			ctx->selected_device_name.c_str());
	} else {
		ctx->selected_device_name =
			(saved_device_name && *saved_device_name)
				? saved_device_name
				: "";

		if (g_ble) {
			ctx->selected_device_name = preferred_ble_name(
				ctx->selected_device_name,
				g_ble->known_name_for(ctx->selected_address));
		}
	}

	if (g_ble && ctx->selected_address != 0 &&
	    !ctx->selected_device_name.empty()) {
		g_ble->remember_name(
			ctx->selected_address,
			ctx->selected_device_name);
	}

	const char *display_format = obs_data_get_string(settings, SETTING_DISPLAY_FORMAT);
	ctx->display_format =
		(display_format && *display_format) ? display_format : "heart_bpm";

	const char *custom_format =
		obs_data_get_string(settings, SETTING_CUSTOM_FORMAT);
	ctx->custom_format =
		(custom_format && *custom_format)
			? custom_format
			: "{heart} {bpm} BPM";

	obs_data_t *display_font = obs_data_get_obj(settings, SETTING_FONT);
	if (display_font) {
		const char *face = obs_data_get_string(display_font, "face");
		if (face && *face)
			ctx->font_face = face;

		ctx->font_size = static_cast<int>(obs_data_get_int(display_font, "size"));
		if (ctx->font_size < 8)
			ctx->font_size = 8;
		if (ctx->font_size > 500)
			ctx->font_size = 500;

		ctx->font_flags = obs_data_get_int(display_font, "flags");
		obs_data_release(display_font);
	}

	ctx->text_color = static_cast<uint32_t>(obs_data_get_int(settings, SETTING_TEXT_COLOR));
	ctx->text_opacity = static_cast<int>(obs_data_get_int(settings, SETTING_TEXT_OPACITY));

	ctx->outline_enabled = obs_data_get_bool(settings, SETTING_OUTLINE_ENABLED);
	ctx->outline_size = static_cast<int>(obs_data_get_int(settings, SETTING_OUTLINE_SIZE));
	ctx->outline_color = static_cast<uint32_t>(obs_data_get_int(settings, SETTING_OUTLINE_COLOR));
	ctx->outline_opacity = static_cast<int>(obs_data_get_int(settings, SETTING_OUTLINE_OPACITY));

	ctx->shadow_enabled = obs_data_get_bool(settings, SETTING_SHADOW_ENABLED);
	ctx->shadow_x = static_cast<int>(obs_data_get_int(settings, SETTING_SHADOW_X));
	ctx->shadow_y = static_cast<int>(obs_data_get_int(settings, SETTING_SHADOW_Y));
	ctx->shadow_color = static_cast<uint32_t>(obs_data_get_int(settings, SETTING_SHADOW_COLOR));
	ctx->shadow_opacity = static_cast<int>(obs_data_get_int(settings, SETTING_SHADOW_OPACITY));

	const bool previous_beat_animation_enabled =
		ctx->beat_animation_enabled;

	ctx->beat_animation_enabled =
		obs_data_get_bool(settings, SETTING_BEAT_ANIMATION);

	ctx->beat_strength =
		static_cast<int>(
			obs_data_get_int(
				settings, SETTING_BEAT_STRENGTH));

	if (ctx->beat_strength < 1)
		ctx->beat_strength = 1;
	if (ctx->beat_strength > 25)
		ctx->beat_strength = 25;

	if (previous_beat_animation_enabled !=
	    ctx->beat_animation_enabled) {
		mugen_heartbeat_reset_beat_animation(ctx);
	}

	if (ctx->text_opacity < 0)
		ctx->text_opacity = 0;
	if (ctx->text_opacity > 100)
		ctx->text_opacity = 100;

	if (ctx->outline_size < 1)
		ctx->outline_size = 1;
	if (ctx->outline_size > 20)
		ctx->outline_size = 20;

	if (ctx->outline_opacity < 0)
		ctx->outline_opacity = 0;
	if (ctx->outline_opacity > 100)
		ctx->outline_opacity = 100;

	if (ctx->shadow_x < 0)
		ctx->shadow_x = 0;
	if (ctx->shadow_x > 30)
		ctx->shadow_x = 30;
	if (ctx->shadow_y < 0)
		ctx->shadow_y = 0;
	if (ctx->shadow_y > 30)
		ctx->shadow_y = 30;

	if (ctx->shadow_opacity < 0)
		ctx->shadow_opacity = 0;
	if (ctx->shadow_opacity > 100)
		ctx->shadow_opacity = 100;

	if (ctx->test_bpm < 30)
		ctx->test_bpm = 30;
	if (ctx->test_bpm > 240)
		ctx->test_bpm = 240;

	/* Selecting another device or explicitly enabling auto-connect is an
	   intentional action, so clear a manual-disconnect pause. */
	if (ctx->selected_address != previous_address ||
	    (ctx->auto_connect && !previous_auto_connect)) {
		ctx->auto_connect_paused = false;
		ctx->next_auto_connect_ms = 0;
	}

	/* A previously clicked Connect must never silently target another Band. */
	if (ctx->selected_address != previous_address) {
		ctx->manual_connect_pending = false;
		ctx->manual_connect_deadline_ms = 0;
	}

	if (!ctx->auto_connect)
		ctx->auto_connect_paused = false;

	/* Switching off auto-connect must immediately revoke Windows' waiting
	   policy, even if the source is hidden and not receiving video ticks. */
	if (g_ble &&
	    (previous_auto_connect != ctx->auto_connect ||
	     previous_address != ctx->selected_address)) {
		const uint64_t active = g_ble->active_address();
		if (active)
			g_ble->set_connection_maintenance(
				active, mugen_heartbeat_auto_maintenance_wanted(active));
	}

	const bool appearance_changed =
		previous_visual_mode != ctx->visual_mode ||
		previous_skin_id != ctx->skin_id ||
		previous_display_format != ctx->display_format ||
		previous_custom_format != ctx->custom_format ||
		previous_font_face != ctx->font_face ||
		previous_font_size != ctx->font_size ||
		previous_font_flags != ctx->font_flags ||
		previous_text_color != ctx->text_color ||
		previous_text_opacity != ctx->text_opacity ||
		previous_outline_enabled != ctx->outline_enabled ||
		previous_outline_size != ctx->outline_size ||
		previous_outline_color != ctx->outline_color ||
		previous_outline_opacity != ctx->outline_opacity ||
		previous_shadow_enabled != ctx->shadow_enabled ||
		previous_shadow_x != ctx->shadow_x ||
		previous_shadow_y != ctx->shadow_y ||
		previous_shadow_color != ctx->shadow_color ||
		previous_shadow_opacity != ctx->shadow_opacity;

	if (appearance_changed) {
		ctx->appearance_dirty = true;
		ctx->last_rendered_bpm = -999;
	}
}

static void *mugen_heartbeat_create(obs_data_t *settings, obs_source_t *source)
{
	auto *ctx = new mugen_heartbeat_source();
	ctx->source = source;
	ctx->mtsdf_renderer = std::make_unique<MtsdfTextRenderer>();
	ctx->skin_renderer = std::make_unique<HeartbeatSkinRenderer>();

	bool children_ok = true;

	for (size_t i = 0; i < RENDER_LEVEL_COUNT; ++i) {
		ctx->text_sources[i] =
			obs_source_create_private(CHILD_TEXT_SOURCE_ID, nullptr, nullptr);
		ctx->shadow_sources[i] =
			obs_source_create_private(CHILD_TEXT_SOURCE_ID, nullptr, nullptr);

		if (!ctx->text_sources[i] || !ctx->shadow_sources[i]) {
			children_ok = false;
			break;
		}
	}

	if (!children_ok) {
		blog(LOG_ERROR,
		     "[Mugen Heartbeat] Could not create adaptive private '%s' text sources.",
		     CHILD_TEXT_SOURCE_ID);

		for (size_t i = 0; i < RENDER_LEVEL_COUNT; ++i) {
			if (ctx->shadow_sources[i])
				obs_source_release(ctx->shadow_sources[i]);
			if (ctx->text_sources[i])
				obs_source_release(ctx->text_sources[i]);
		}

		delete ctx;
		return nullptr;
	}

	mugen_heartbeat_update(ctx, settings);
	mugen_heartbeat_register_source(ctx);

	blog(LOG_INFO,
	     "[Mugen Heartbeat] source created (autoConnect=%s selectedDevice=%s; "
	     "MTSDF glyph-atlas renderer; skin engine ready; GDI+ fallback ready)",
	     ctx->auto_connect ? "on" : "off",
	     ctx->selected_address ? "yes" : "no");

	return ctx;
}

static void mugen_heartbeat_destroy(void *data)
{
	auto *ctx = static_cast<mugen_heartbeat_source *>(data);
	if (!ctx)
		return;

	ctx->skin_renderer.reset();
	ctx->mtsdf_renderer.reset();

	for (size_t i = 0; i < HEARTBEAT_REACTION_COUNT; ++i) {
		auto &reaction = ctx->reactions[i];

		if (reaction.enabled &&
		    reaction.runtime_applied &&
		    heartbeat_reaction_target_ready(reaction)) {
			heartbeat_reaction_queue_action(
				reaction, false);
		}
	}

	const size_t remaining_sources =
		mugen_heartbeat_unregister_source(ctx);

	if (g_ble) {
		const uint64_t connected_address =
			g_ble->connected_address();

		const bool connected_band_still_has_user =
			mugen_heartbeat_address_has_user(connected_address);

		/* g_ble is currently global. Never leave its BLE session orphaned
		   after the Source that owned it has been destroyed.

		   Important: use the fast path here. Waiting synchronously for a
		   GATT unsubscribe during source destruction can make OBS shutdown
		   wait on a Band that is no longer responding. */
		if (remaining_sources == 0 ||
		    (connected_address != 0 &&
		     !connected_band_still_has_user)) {
			g_ble->disconnect_fast();
		}
	}

	for (size_t i = 0; i < RENDER_LEVEL_COUNT; ++i) {
		if (ctx->shadow_sources[i])
			obs_source_release(ctx->shadow_sources[i]);
		if (ctx->text_sources[i])
			obs_source_release(ctx->text_sources[i]);
	}

	delete ctx;
}

static void mugen_heartbeat_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, SETTING_TEST_MODE, false);
	obs_data_set_default_int(settings, SETTING_TEST_BPM, 87);
	obs_data_set_default_string(settings, SETTING_DEVICE_ADDRESS, "");
	obs_data_set_default_string(settings, SETTING_DEVICE_NAME, "");
	obs_data_set_default_bool(settings, SETTING_AUTO_CONNECT, false);
	obs_data_set_default_string(settings, SETTING_VISUAL_MODE, "text");
	obs_data_set_default_string(settings, SETTING_SKIN_ID, "bio-monitor");
	obs_data_set_default_int(
		settings,
		SETTING_SKIN_BACKGROUND_OPACITY,
		100);

	static constexpr int DEFAULT_ON_BPM[HEARTBEAT_REACTION_COUNT] = {
		120, 140, 160
	};

	static constexpr int DEFAULT_OFF_BPM[HEARTBEAT_REACTION_COUNT] = {
		110, 130, 150
	};

	for (size_t i = 0; i < HEARTBEAT_REACTION_COUNT; ++i) {
		obs_data_set_default_bool(
			settings,
			REACTION_ENABLED_KEYS[i],
			false);

		obs_data_set_default_string(
			settings,
			REACTION_ACTION_KEYS[i],
			"show_source");

		obs_data_set_default_string(
			settings,
			REACTION_SCENE_KEYS[i],
			"");

		obs_data_set_default_string(
			settings,
			REACTION_SOURCE_KEYS[i],
			"");

		obs_data_set_default_string(
			settings,
			REACTION_FILTER_KEYS[i],
			"");

		obs_data_set_default_int(
			settings,
			REACTION_ON_BPM_KEYS[i],
			DEFAULT_ON_BPM[i]);

		obs_data_set_default_int(
			settings,
			REACTION_OFF_BPM_KEYS[i],
			DEFAULT_OFF_BPM[i]);

		obs_data_set_default_double(
			settings,
			REACTION_ON_DELAY_KEYS[i],
			1.0);

		obs_data_set_default_double(
			settings,
			REACTION_OFF_DELAY_KEYS[i],
			2.0);
	}

	obs_data_set_default_string(settings, SETTING_DISPLAY_FORMAT, "heart_bpm");
	obs_data_set_default_string(settings, SETTING_CUSTOM_FORMAT, "{heart} {bpm} BPM");

	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", "Arial");
	obs_data_set_int(font, "size", 72);
	obs_data_set_int(font, "flags", 0);
	obs_data_set_default_obj(settings, SETTING_FONT, font);
	obs_data_release(font);

	obs_data_set_default_int(settings, SETTING_TEXT_COLOR, 0xFFFFFF);
	obs_data_set_default_int(settings, SETTING_TEXT_OPACITY, 100);

	obs_data_set_default_bool(settings, SETTING_OUTLINE_ENABLED, true);
	obs_data_set_default_int(settings, SETTING_OUTLINE_SIZE, 2);
	obs_data_set_default_int(settings, SETTING_OUTLINE_COLOR, 0x000000);
	obs_data_set_default_int(settings, SETTING_OUTLINE_OPACITY, 100);

	obs_data_set_default_bool(settings, SETTING_SHADOW_ENABLED, true);
	obs_data_set_default_int(settings, SETTING_SHADOW_X, 4);
	obs_data_set_default_int(settings, SETTING_SHADOW_Y, 4);
	obs_data_set_default_int(settings, SETTING_SHADOW_COLOR, 0x000000);
	obs_data_set_default_int(settings, SETTING_SHADOW_OPACITY, 50);

	obs_data_set_default_bool(
		settings, SETTING_BEAT_ANIMATION, false);
	obs_data_set_default_int(
		settings, SETTING_BEAT_STRENGTH, 8);
}

static mugen_heartbeat_source *source_from_props(obs_properties_t *props)
{
	return static_cast<mugen_heartbeat_source *>(obs_properties_get_param(props));
}

static bool mugen_heartbeat_visual_mode_modified(
	obs_properties_t *props,
	obs_property_t *,
	obs_data_t *settings)
{
	if (!props || !settings)
		return false;

	const char *mode =
		obs_data_get_string(
			settings, SETTING_VISUAL_MODE);

	const bool skin_mode =
		mode && std::string(mode) == "skin";

	obs_property_t *skin_group =
		obs_properties_get(
			props, "skin_group");

	obs_property_t *appearance_group =
		obs_properties_get(
			props, "appearance_group");

	if (skin_group)
		obs_property_set_visible(
			skin_group, skin_mode);

	if (appearance_group)
		obs_property_set_visible(
			appearance_group, !skin_mode);

	return true;
}

static std::string mugen_heartbeat_skin_info_text(
	const HeartbeatSkinInfo *info);

static void mugen_heartbeat_update_skin_properties_ui(
	obs_properties_t *props,
	mugen_heartbeat_source *ctx)
{
	if (!props || !ctx || !ctx->skin_renderer)
		return;

	HeartbeatSkinInfo info;
	const bool have_info =
		ctx->skin_renderer->info_for(
			ctx->skin_id,
			info);

	obs_property_t *skin_group =
		obs_properties_get(
			props,
			"skin_group");

	obs_properties_t *skin_group_props =
		skin_group
			? obs_property_group_content(skin_group)
			: nullptr;

	if (!skin_group_props)
		return;

	obs_property_t *info_prop =
		obs_properties_get(
			skin_group_props,
			"skin_info");

	if (info_prop) {
		const std::string info_text =
			mugen_heartbeat_skin_info_text(
				have_info
					? &info
					: nullptr);

		obs_property_set_description(
			info_prop,
			info_text.c_str());
	}

	obs_property_t *opacity_prop =
		obs_properties_get(
			skin_group_props,
			SETTING_SKIN_BACKGROUND_OPACITY);

	if (opacity_prop) {
		obs_property_set_visible(
			opacity_prop,
			have_info &&
				info.background_opacity_customizable);
	}
}

static bool mugen_heartbeat_skin_background_opacity_modified(
	obs_properties_t *props,
	obs_property_t *,
	obs_data_t *settings)
{
	auto *ctx = source_from_props(props);
	if (!ctx || !settings)
		return false;

	const char *skin_id =
		obs_data_get_string(
			settings,
			SETTING_SKIN_ID);

	if (!skin_id || !*skin_id)
		return false;

	const int opacity =
		heartbeat_clamp_int(
			static_cast<int>(
				obs_data_get_int(
					settings,
					SETTING_SKIN_BACKGROUND_OPACITY)),
			0,
			100);

	const std::string key =
		mugen_heartbeat_skin_opacity_setting_key(
			skin_id);

	obs_data_set_int(
		settings,
		key.c_str(),
		opacity);

	ctx->skin_background_opacity = opacity;
	ctx->appearance_dirty = true;
	ctx->last_rendered_bpm = -999;

	return false;
}

static bool mugen_heartbeat_skin_modified(
	obs_properties_t *props,
	obs_property_t *,
	obs_data_t *settings)
{
	auto *ctx = source_from_props(props);
	if (!ctx || !ctx->skin_renderer || !settings)
		return false;

	const char *skin_id =
		obs_data_get_string(
			settings, SETTING_SKIN_ID);

	if (!skin_id || !*skin_id)
		return false;

	const std::string previous_skin_id =
		ctx->skin_id;

	if (!previous_skin_id.empty()) {
		const std::string previous_key =
			mugen_heartbeat_skin_opacity_setting_key(
				previous_skin_id);

		obs_data_set_int(
			settings,
			previous_key.c_str(),
			ctx->skin_background_opacity);
	}

	ctx->skin_id = skin_id;

	const std::string next_key =
		mugen_heartbeat_skin_opacity_setting_key(
			ctx->skin_id);

	int next_opacity =
		mugen_heartbeat_skin_default_background_opacity(
			ctx,
			ctx->skin_id);

	if (obs_data_has_user_value(
		    settings,
		    next_key.c_str())) {
		next_opacity =
			static_cast<int>(
				obs_data_get_int(
					settings,
					next_key.c_str()));
	} else {
		obs_data_set_int(
			settings,
			next_key.c_str(),
			next_opacity);
	}

	next_opacity =
		heartbeat_clamp_int(
			next_opacity,
			0,
			100);

	obs_data_set_int(
		settings,
		SETTING_SKIN_BACKGROUND_OPACITY,
		next_opacity);

	ctx->skin_background_opacity =
		next_opacity;

	mugen_heartbeat_update_skin_properties_ui(
		props,
		ctx);

	ctx->appearance_dirty = true;
	ctx->last_rendered_bpm = -999;

	return true;
}
static bool mugen_heartbeat_skin_open_folder_clicked(
	obs_properties_t *,
	obs_property_t *,
	void *)
{
	HeartbeatSkinRenderer::open_user_skin_directory();
	return false;
}

static bool mugen_heartbeat_skin_refresh_clicked(
	obs_properties_t *props,
	obs_property_t *,
	void *)
{
	auto *ctx = source_from_props(props);

	if (!ctx || !ctx->skin_renderer)
		return false;

	/*
	 * Explicit Refresh is allowed to rescan the catalog and reload the
	 * selected skin. Unlike BLE status updates, this is a user-requested
	 * operation.
	 */
	ctx->skin_renderer->reload_catalog();

	/*
	 * OBS does not necessarily recreate the already-open combo box just
	 * because the button callback returns true. Refresh its items in-place
	 * as well, so a newly added skin appears immediately without closing and
	 * reopening Properties.
	 */
	obs_property_t *skin_prop =
		obs_properties_get(props, SETTING_SKIN_ID);

	if (!skin_prop) {
		obs_property_t *skin_group =
			obs_properties_get(props, "skin_group");

		if (skin_group) {
			obs_properties_t *skin_group_props =
				obs_property_group_content(skin_group);

			if (skin_group_props) {
				skin_prop = obs_properties_get(
					skin_group_props,
					SETTING_SKIN_ID);
			}
		}
	}

	if (skin_prop) {
		obs_property_list_clear(skin_prop);

		bool current_skin_listed = false;

		for (const auto &info :
		     ctx->skin_renderer->catalog()) {
			std::string label = info.name;

			if (!info.author.empty()) {
				label += " - ";
				label += info.author;
			}

			obs_property_list_add_string(
				skin_prop,
				label.c_str(),
				info.id.c_str());

			if (info.id == ctx->skin_id)
				current_skin_listed = true;
		}

		if (!ctx->skin_id.empty() &&
		    !current_skin_listed) {
			obs_property_list_add_string(
				skin_prop,
				ctx->skin_id.c_str(),
				ctx->skin_id.c_str());
		}
	}

	/* Refresh metadata/custom controls too. This matters when a user edits
	   the selected community skin while the Properties window is open. */
	mugen_heartbeat_update_skin_properties_ui(
		props,
		ctx);

	ctx->appearance_dirty = true;
	ctx->last_rendered_bpm = -999;

	return true;
}
static bool mugen_heartbeat_open_external_url(const wchar_t *url, const char *name)
{
	const HINSTANCE result = ShellExecuteW(
		nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) <= 32)
		blog(LOG_WARNING, "[Mugen Heartbeat] Could not open %s", name);
	return false;
}

static bool mugen_heartbeat_about_project_clicked(
	obs_properties_t *, obs_property_t *, void *)
{
	return mugen_heartbeat_open_external_url(
		L"https://github.com/Mugen-Art-Lab/Mugen-Heartbeat", "project page");
}

static bool mugen_heartbeat_about_website_clicked(
	obs_properties_t *, obs_property_t *, void *)
{
	return mugen_heartbeat_open_external_url(
		L"https://mugenartlab.com/", "Mugen Art Lab website");
}

static bool mugen_heartbeat_skin_gallery_clicked(
	obs_properties_t *,
	obs_property_t *,
	void *)
{
	HeartbeatSkinRenderer::open_skin_gallery();
	return false;
}

static std::string mugen_heartbeat_skin_info_text(
	const HeartbeatSkinInfo *info)
{
	if (!info)
		return obs_module_text(
			"MugenHeartbeat.SkinUnavailable");

	std::ostringstream ss;

	ss << info->name;

	if (!info->author.empty())
		ss << " - " << info->author;

	if (!info->version.empty())
		ss << " | v" << info->version;

	if (!info->description.empty())
		ss << "\n" << info->description;

	return ss.str();
}

static bool scan_clicked(obs_properties_t *props, obs_property_t *, void *)
{
	auto *ctx = source_from_props(props);

	if (g_ble)
		g_ble->start_scan(ctx ? ctx->selected_address : 0);

	/* Returning true asks OBS to rebuild the properties UI safely
	   after this button callback has finished. */
	return true;
}



static bool connect_clicked(obs_properties_t *props, obs_property_t *, void *)
{
	auto *ctx = source_from_props(props);
	if (!ctx || !ctx->source || !g_ble)
		return false;

	obs_data_t *settings = obs_source_get_settings(ctx->source);
	const uint64_t address =
		parse_bluetooth_address(
			obs_data_get_string(settings, SETTING_DEVICE_ADDRESS));
	obs_data_release(settings);

	if (address == 0)
		return true;

	ctx->selected_address = address;
	ctx->auto_connect_paused = false;

	constexpr int64_t FRESH_ADVERTISEMENT_MS = 7000;
	constexpr int64_t MANUAL_REQUEST_LIMIT_MS = 60000;
	const int64_t now = mugen_heartbeat_now_ms();
	ctx->manual_connect_deadline_ms = now + MANUAL_REQUEST_LIMIT_MS;

	if (g_ble->seen_recently(address, FRESH_ADVERTISEMENT_MS)) {
		ctx->manual_connect_pending = false;
		ctx->next_auto_connect_ms = now + 5000;

		blog(LOG_INFO,
		     "[Mugen Heartbeat] manual Connect: fresh selected Band; connecting.");

		g_ble->connect(
			address, mugen_heartbeat_auto_maintenance_wanted(address));
	} else {
		ctx->manual_connect_pending = true;
		ctx->next_auto_connect_ms = now + 6000;

		blog(LOG_INFO,
		     "[Mugen Heartbeat] manual Connect: selected Band is stale/not visible; "
		     "scanning before GATT.");

		g_ble->start_scan(address);
	}

	return true;
}

static bool disconnect_clicked(obs_properties_t *props, obs_property_t *, void *)
{
	auto *ctx = source_from_props(props);
	if (!ctx)
		return false;

	/* A manual Connect can still be searching for its first advertisement.
	   Cancel that pending search as well as established GATT connections. */
	const bool cancel_manual_scan = ctx->manual_connect_pending && g_ble &&
		g_ble->state() == BleState::Scanning;
	ctx->auto_connect_paused = true;
	ctx->manual_connect_pending = false;
	ctx->manual_connect_deadline_ms = 0;
	ctx->next_auto_connect_ms = 0;

	if (g_ble && cancel_manual_scan)
		g_ble->stop_scan();

	if (g_ble &&
	    ctx->selected_address != 0 &&
	    g_ble->active_address() == ctx->selected_address &&
	    (g_ble->state() == BleState::Connected ||
	     g_ble->state() == BleState::Connecting)) {
		blog(LOG_INFO,
		     "[Mugen Heartbeat] manual Disconnect: cancelling active BLE session.");
		g_ble->disconnect();
	}

	return true;
}


/* The two pairing hints are context-sensitive:
   - no selected device: explain the first device search;
   - selected but disconnected: explain the Connect action;
   - connecting: status/Cancel already explain the pending operation;
   - connected: neither first-use instruction is needed.
   OBS recreates properties on BLE revisions; the device combo and action
   callbacks also update the currently open properties dialog. */
static void mugen_heartbeat_sync_ble_pairing_help(
	obs_properties_t *props, const mugen_heartbeat_source *ctx,
	uint64_t selected_address)
{
	if (!props)
		return;

	const bool selected_connected =
		g_ble && selected_address != 0 && g_ble->connected() &&
		g_ble->connected_address() == selected_address;
	const bool selected_connecting =
		selected_address != 0 &&
		((ctx && ctx->selected_address == selected_address &&
		  ctx->manual_connect_pending) ||
		 (g_ble && g_ble->active_address() == selected_address &&
		  g_ble->state() == BleState::Connecting));

	if (auto *help = obs_properties_get(props, "ble_help")) {
		obs_property_set_description(
			help,
			obs_module_text(selected_address
						? "MugenHeartbeat.BleHelpSelected"
						: "MugenHeartbeat.BleHelp"));
		obs_property_set_visible(
			help, !selected_connected && !selected_connecting);
	}

	if (auto *xiaomi = obs_properties_get(props, "ble_xiaomi_help"))
		obs_property_set_visible(xiaomi, !selected_connected);
}

static bool mugen_heartbeat_device_ui_modified(
	obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	if (!props || !settings)
		return false;

	const uint64_t selected =
		parse_bluetooth_address(
			obs_data_get_string(settings, SETTING_DEVICE_ADDRESS));
	mugen_heartbeat_sync_ble_pairing_help(
		props, source_from_props(props), selected);
	return true;
}

static const char *mugen_heartbeat_connection_button_locale_key(
	const mugen_heartbeat_source *ctx)
{
	if (!ctx || !g_ble || !ctx->selected_address)
		return "MugenHeartbeat.Connect";

	if (g_ble->connected() &&
	    g_ble->connected_address() == ctx->selected_address)
		return "MugenHeartbeat.Disconnect";

	if (ctx->manual_connect_pending ||
	    (g_ble->active_address() == ctx->selected_address &&
	     g_ble->state() == BleState::Connecting))
		return "MugenHeartbeat.CancelConnection";

	return "MugenHeartbeat.Connect";
}

static bool mugen_heartbeat_connection_button_clicked(
	obs_properties_t *props, obs_property_t *property, void *priv)
{
	auto *ctx = source_from_props(props);
	if (!ctx || !property || !g_ble)
		return false;

	const bool was_connected =
		ctx->selected_address != 0 &&
		g_ble->connected() &&
		g_ble->connected_address() == ctx->selected_address;
	const bool was_connecting =
		ctx->selected_address != 0 &&
		g_ble->active_address() == ctx->selected_address &&
		g_ble->state() == BleState::Connecting;
	const bool stop = was_connected || was_connecting ||
		ctx->manual_connect_pending;

	const bool handled = stop
		? disconnect_clicked(props, property, priv)
		: connect_clicked(props, property, priv);
	if (!handled)
		return false;

	/* Button clicks refresh the CURRENT properties object in OBS.
	   Label the pending scan as cancellable, and immediately return to
	   Connect on manual disconnect without waiting for async GATT cleanup. */
	const char *locale_key = stop
		? "MugenHeartbeat.Connect"
		: mugen_heartbeat_connection_button_locale_key(ctx);
	obs_property_set_description(property, obs_module_text(locale_key));

	/* Refresh open-dialog guidance immediately when connecting or cancelling;
	   later BLE state revisions will refresh it again on connection changes. */
	mugen_heartbeat_sync_ble_pairing_help(
		props, ctx, ctx->selected_address);
	return true;
}

static bool mugen_heartbeat_auto_connect_ui_modified(
	obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	if (!props || !settings)
		return false;

	auto *notice = obs_properties_get(props, "ble_shared_auto_notice");
	if (!notice)
		return false;

	const bool own_auto_connect =
		obs_data_get_bool(settings, SETTING_AUTO_CONNECT);
	const std::string other =
		mugen_heartbeat_other_auto_connect_source_name(source_from_props(props));

	if (!other.empty()) {
		std::string text =
			obs_module_text("MugenHeartbeat.OtherSourceAutoConnect");
		text += ": ";
		text += other;
		obs_property_set_description(notice, text.c_str());
	}

	const bool show = !own_auto_connect && !other.empty();
	obs_property_set_visible(notice, show);
	return true;
}

struct HeartbeatSceneListEntry {
	std::string label;
	std::string value;
};

static bool heartbeat_case_insensitive_less(const std::string &a,
					    const std::string &b)
{
	return std::lexicographical_compare(
		a.begin(), a.end(),
		b.begin(), b.end(),
		[](unsigned char left, unsigned char right) {
			return std::tolower(left) < std::tolower(right);
		});
}

struct HeartbeatSceneListContext {
	std::vector<HeartbeatSceneListEntry> entries;
};

static bool heartbeat_reaction_collect_scene(void *data,
					     obs_source_t *source)
{
	auto *ctx = static_cast<HeartbeatSceneListContext *>(data);

	if (!ctx || !source)
		return true;

	const char *name = obs_source_get_name(source);
	if (!name || !*name)
		return true;

	ctx->entries.push_back({name, name});
	return true;
}

static void heartbeat_reaction_fill_scene_list(obs_property_t *property,
					       const std::string &current)
{
	if (!property)
		return;

	obs_property_list_clear(property);
	obs_property_list_add_string(
		property,
		obs_module_text("MugenHeartbeat.ReactionChooseScene"),
		"");

	HeartbeatSceneListContext ctx;
	obs_enum_scenes(heartbeat_reaction_collect_scene, &ctx);

	std::sort(
		ctx.entries.begin(),
		ctx.entries.end(),
		[](const HeartbeatSceneListEntry &a,
		   const HeartbeatSceneListEntry &b) {
			return heartbeat_case_insensitive_less(
				a.label, b.label);
		});

	bool current_found = false;

	for (auto const &entry : ctx.entries) {
		obs_property_list_add_string(
			property,
			entry.label.c_str(),
			entry.value.c_str());

		if (entry.value == current)
			current_found = true;
	}

	if (!current.empty() && !current_found) {
		obs_property_list_add_string(
			property,
			current.c_str(),
			current.c_str());
	}
}

struct HeartbeatSceneItemListEntry {
	std::string label;
	std::string source_name;
};

struct HeartbeatSceneItemCollectContext {
	std::string prefix;
	std::vector<HeartbeatSceneItemListEntry> *entries = nullptr;
};

static bool heartbeat_reaction_collect_scene_item(
	obs_scene_t *,
	obs_sceneitem_t *item,
	void *data)
{
	auto *ctx =
		static_cast<HeartbeatSceneItemCollectContext *>(data);

	if (!ctx || !ctx->entries || !item)
		return true;

	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source)
		return true;

	const char *name = obs_source_get_name(source);
	if (!name || !*name)
		return true;

	const char *id = obs_source_get_id(source);
	const bool is_heartbeat =
		id && std::string(id) == "mugen_heartbeat_source";

	const std::string label =
		ctx->prefix.empty()
			? std::string(name)
			: ctx->prefix + " \xE2\x80\xBA " + name;

	if (!is_heartbeat)
		ctx->entries->push_back({label, name});

	if (obs_sceneitem_is_group(item)) {
		HeartbeatSceneItemCollectContext child_ctx;
		child_ctx.prefix = label;
		child_ctx.entries = ctx->entries;

		obs_sceneitem_group_enum_items(
			item,
			heartbeat_reaction_collect_scene_item,
			&child_ctx);
	}

	return true;
}

static void heartbeat_reaction_fill_source_list(obs_property_t *property,
						const std::string &scene_name,
						const std::string &current)
{
	if (!property)
		return;

	obs_property_list_clear(property);
	obs_property_list_add_string(
		property,
		obs_module_text("MugenHeartbeat.ReactionChooseSource"),
		"");

	std::vector<HeartbeatSceneItemListEntry> entries;

	if (!scene_name.empty()) {
		obs_source_t *scene_source =
			obs_get_source_by_name(scene_name.c_str());

		if (scene_source) {
			obs_scene_t *scene =
				obs_scene_from_source(scene_source);

			if (scene) {
				HeartbeatSceneItemCollectContext ctx;
				ctx.entries = &entries;

				obs_scene_enum_items(
					scene,
					heartbeat_reaction_collect_scene_item,
					&ctx);
			}

			obs_source_release(scene_source);
		}
	}

	std::sort(
		entries.begin(),
		entries.end(),
		[](const HeartbeatSceneItemListEntry &a,
		   const HeartbeatSceneItemListEntry &b) {
			return heartbeat_case_insensitive_less(
				a.label, b.label);
		});

	bool current_found = false;

	for (auto const &entry : entries) {
		obs_property_list_add_string(
			property,
			entry.label.c_str(),
			entry.source_name.c_str());

		if (entry.source_name == current)
			current_found = true;
	}

	if (!current.empty() && !current_found) {
		obs_property_list_add_string(
			property,
			current.c_str(),
			current.c_str());
	}
}

static bool heartbeat_reaction_scene_has_source(
	const std::string &scene_name,
	const std::string &source_name)
{
	if (scene_name.empty() || source_name.empty())
		return false;

	std::vector<HeartbeatSceneItemListEntry> entries;

	obs_source_t *scene_source =
		obs_get_source_by_name(scene_name.c_str());

	if (!scene_source)
		return false;

	obs_scene_t *scene =
		obs_scene_from_source(scene_source);

	if (scene) {
		HeartbeatSceneItemCollectContext ctx;
		ctx.entries = &entries;

		obs_scene_enum_items(
			scene,
			heartbeat_reaction_collect_scene_item,
			&ctx);
	}

	obs_source_release(scene_source);

	for (const auto &entry : entries) {
		if (entry.source_name == source_name)
			return true;
	}

	return false;
}
struct HeartbeatFilterListContext {
	obs_property_t *property = nullptr;
	std::string current;
	bool current_found = false;
};

static void heartbeat_reaction_add_filter_item(obs_source_t *,
						obs_source_t *filter,
						void *data)
{
	auto *ctx =
		static_cast<HeartbeatFilterListContext *>(data);

	if (!ctx || !ctx->property || !filter)
		return;

	const char *name = obs_source_get_name(filter);
	if (!name || !*name)
		return;

	obs_property_list_add_string(
		ctx->property,
		name,
		name);

	if (ctx->current == name)
		ctx->current_found = true;
}

static void heartbeat_reaction_fill_filter_list(obs_property_t *property,
						const std::string &source_name,
						const std::string &current_filter)
{
	if (!property)
		return;

	obs_property_list_clear(property);
	obs_property_list_add_string(
		property,
		obs_module_text("MugenHeartbeat.ReactionChooseFilter"),
		"");

	HeartbeatFilterListContext ctx;
	ctx.property = property;
	ctx.current = current_filter;

	if (!source_name.empty()) {
		obs_source_t *source =
			obs_get_source_by_name(source_name.c_str());

		if (source) {
			obs_source_enum_filters(
				source,
				heartbeat_reaction_add_filter_item,
				&ctx);

			obs_source_release(source);
		}
	}

	if (!current_filter.empty() && !ctx.current_found) {
		obs_property_list_add_string(
			property,
			current_filter.c_str(),
			current_filter.c_str());
	}
}

static size_t heartbeat_reaction_index_from_priv(void *priv)
{
	const uintptr_t value =
		reinterpret_cast<uintptr_t>(priv);

	if (value == 0)
		return 0;

	return static_cast<size_t>(value - 1);
}

static bool heartbeat_reaction_action_modified(void *priv,
					       obs_properties_t *props,
					       obs_property_t *,
					       obs_data_t *settings)
{
	const size_t index =
		heartbeat_reaction_index_from_priv(priv);

	if (index >= HEARTBEAT_REACTION_COUNT)
		return false;

	const char *action =
		obs_data_get_string(
			settings,
			REACTION_ACTION_KEYS[index]);

	const bool filter_action =
		heartbeat_reaction_is_filter(
			action ? action : "");

	obs_property_t *filter_prop =
		obs_properties_get(
			props,
			REACTION_FILTER_KEYS[index]);

	if (filter_prop)
		obs_property_set_visible(
			filter_prop,
			filter_action);

	return true;
}

static bool heartbeat_reaction_scene_modified(void *priv,
					      obs_properties_t *props,
					      obs_property_t *,
					      obs_data_t *settings)
{
	const size_t index =
		heartbeat_reaction_index_from_priv(priv);

	if (index >= HEARTBEAT_REACTION_COUNT)
		return false;

	obs_property_t *source_prop =
		obs_properties_get(
			props,
			REACTION_SOURCE_KEYS[index]);

	obs_property_t *filter_prop =
		obs_properties_get(
			props,
			REACTION_FILTER_KEYS[index]);

	const char *scene_value =
		obs_data_get_string(
			settings,
			REACTION_SCENE_KEYS[index]);

	const char *source_value =
		obs_data_get_string(
			settings,
			REACTION_SOURCE_KEYS[index]);

	const char *filter_value =
		obs_data_get_string(
			settings,
			REACTION_FILTER_KEYS[index]);

	const std::string scene_name =
		scene_value ? scene_value : "";

	const std::string current_source =
		source_value ? source_value : "";

	const std::string current_filter =
		filter_value ? filter_value : "";

	/* OBS also calls modified callbacks while rebuilding the Properties UI.
	   Do not interpret that as a user changing scenes. Preserve the saved
	   child selection whenever it is still a valid scene item.

	   If the user really changed to a scene where that source does not
	   exist, then (and only then) clear source + filter. */
	const bool source_is_valid =
		heartbeat_reaction_scene_has_source(
			scene_name,
			current_source);

	if (!current_source.empty() && !source_is_valid) {
		obs_data_set_string(
			settings,
			REACTION_SOURCE_KEYS[index],
			"");

		obs_data_set_string(
			settings,
			REACTION_FILTER_KEYS[index],
			"");
	}

	const std::string effective_source =
		source_is_valid ? current_source : "";

	const std::string effective_filter =
		source_is_valid ? current_filter : "";

	if (source_prop) {
		heartbeat_reaction_fill_source_list(
			source_prop,
			scene_name,
			effective_source);
	}

	if (filter_prop) {
		heartbeat_reaction_fill_filter_list(
			filter_prop,
			effective_source,
			effective_filter);
	}

	return true;
}

static bool heartbeat_reaction_source_modified(void *priv,
					       obs_properties_t *props,
					       obs_property_t *,
					       obs_data_t *settings)
{
	const size_t index =
		heartbeat_reaction_index_from_priv(priv);

	if (index >= HEARTBEAT_REACTION_COUNT)
		return false;

	obs_property_t *filter_prop =
		obs_properties_get(
			props,
			REACTION_FILTER_KEYS[index]);

	if (!filter_prop)
		return false;

	const char *source_name =
		obs_data_get_string(
			settings,
			REACTION_SOURCE_KEYS[index]);

	const char *filter_name =
		obs_data_get_string(
			settings,
			REACTION_FILTER_KEYS[index]);

	heartbeat_reaction_fill_filter_list(
		filter_prop,
		source_name ? source_name : "",
		filter_name ? filter_name : "");

	return true;
}

/* OBS 33's button callback refreshes the EXISTING properties object, it
   does not call get_properties() again. Update the group's visibility on
   the current object, not only the per-source state for future reloads.
   The UI-only fold state never changes the persisted reaction enable flag. */
static std::string heartbeat_reaction_editor_button_label(
	const mugen_heartbeat_source *ctx, size_t index)
{
	const bool expanded = ctx && ctx->reaction_ui_expanded[index];
	std::string label = obs_module_text(
		expanded ? "MugenHeartbeat.ReactionCollapse"
			 : "MugenHeartbeat.ReactionConfigure");
	label += " — ";
	label += obs_module_text(REACTION_GROUP_LOCALE_KEYS[index]);

	if (ctx && ctx->reactions[index].enabled) {
		label += " (";
		label += obs_module_text("MugenHeartbeat.ReactionActive");
		label += ")";
	}

	return label;
}

static bool heartbeat_reaction_toggle_ui_clicked(
	obs_properties_t *props, obs_property_t *property, void *priv)
{
	auto *ctx = static_cast<mugen_heartbeat_source *>(priv);
	if (!ctx || !property || !props)
		return false;

	const char *name = obs_property_name(property);
	if (!name)
		return false;

	for (size_t i = 0; i < HEARTBEAT_REACTION_COUNT; ++i) {
		if (std::string(name) != REACTION_UI_TOGGLE_KEYS[i])
			continue;

		obs_property_t *group = obs_properties_get(
			props, REACTION_ENABLED_KEYS[i]);
		if (!group)
			return false;

		ctx->reaction_ui_expanded[i] = !ctx->reaction_ui_expanded[i];
		obs_property_set_visible(group, ctx->reaction_ui_expanded[i]);

		const std::string label =
			heartbeat_reaction_editor_button_label(ctx, i);
		obs_property_set_description(property, label.c_str());

		/* OBS will now redraw the mutated current properties object. */
		return true;
	}

	return false;
}

static void heartbeat_reaction_add_properties(obs_properties_t *props,
					      mugen_heartbeat_source *ctx,
					      size_t index)
{
	if (!props || index >= HEARTBEAT_REACTION_COUNT)
		return;

	obs_properties_t *content = obs_properties_create();

	auto *action_prop = obs_properties_add_list(
		content,
		REACTION_ACTION_KEYS[index],
		obs_module_text("MugenHeartbeat.ReactionAction"),
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(
		action_prop,
		obs_module_text("MugenHeartbeat.ReactionShowSource"),
		"show_source");

	obs_property_list_add_string(
		action_prop,
		obs_module_text("MugenHeartbeat.ReactionHideSource"),
		"hide_source");

	obs_property_list_add_string(
		action_prop,
		obs_module_text("MugenHeartbeat.ReactionEnableFilter"),
		"enable_filter");

	obs_property_list_add_string(
		action_prop,
		obs_module_text("MugenHeartbeat.ReactionDisableFilter"),
		"disable_filter");

	auto *scene_prop = obs_properties_add_list(
		content,
		REACTION_SCENE_KEYS[index],
		obs_module_text("MugenHeartbeat.ReactionScene"),
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);

	const std::string current_scene =
		ctx ? ctx->reactions[index].target_scene
		    : std::string{};

	heartbeat_reaction_fill_scene_list(
		scene_prop,
		current_scene);

	auto *source_prop = obs_properties_add_list(
		content,
		REACTION_SOURCE_KEYS[index],
		obs_module_text("MugenHeartbeat.ReactionSource"),
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);

	const std::string current_source =
		ctx ? ctx->reactions[index].target_source
		    : std::string{};

	heartbeat_reaction_fill_source_list(
		source_prop,
		current_scene,
		current_source);

	auto *filter_prop = obs_properties_add_list(
		content,
		REACTION_FILTER_KEYS[index],
		obs_module_text("MugenHeartbeat.ReactionFilter"),
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);

	const std::string current_filter =
		ctx ? ctx->reactions[index].target_filter
		    : std::string{};

	heartbeat_reaction_fill_filter_list(
		filter_prop,
		current_source,
		current_filter);

	const std::string current_action =
		ctx ? ctx->reactions[index].action
		    : std::string("show_source");

	obs_property_set_visible(
		filter_prop,
		heartbeat_reaction_is_filter(current_action));

	auto *on_bpm = obs_properties_add_int(
		content,
		REACTION_ON_BPM_KEYS[index],
		obs_module_text("MugenHeartbeat.ReactionOnBpm"),
		30, 240, 1);
	obs_property_int_set_suffix(on_bpm, " BPM");

	auto *off_bpm = obs_properties_add_int(
		content,
		REACTION_OFF_BPM_KEYS[index],
		obs_module_text("MugenHeartbeat.ReactionOffBpm"),
		30, 240, 1);
	obs_property_int_set_suffix(off_bpm, " BPM");

	auto *on_delay = obs_properties_add_float_slider(
		content,
		REACTION_ON_DELAY_KEYS[index],
		obs_module_text("MugenHeartbeat.ReactionOnDelay"),
		0.0, 10.0, 0.1);
	obs_property_float_set_suffix(on_delay, " s");

	auto *off_delay = obs_properties_add_float_slider(
		content,
		REACTION_OFF_DELAY_KEYS[index],
		obs_module_text("MugenHeartbeat.ReactionOffDelay"),
		0.0, 10.0, 0.1);
	obs_property_float_set_suffix(off_delay, " s");

	void *callback_index =
		reinterpret_cast<void *>(
			static_cast<uintptr_t>(index + 1));

	obs_property_set_modified_callback2(
		action_prop,
		heartbeat_reaction_action_modified,
		callback_index);

	obs_property_set_modified_callback2(
		scene_prop,
		heartbeat_reaction_scene_modified,
		callback_index);

	obs_property_set_modified_callback2(
		source_prop,
		heartbeat_reaction_source_modified,
		callback_index);

	/* The button controls visibility only; the existing checkable group
	   remains the actual saved enable/disable setting. */
	const bool expanded = ctx && ctx->reaction_ui_expanded[index];
	const std::string editor_button_label =
		heartbeat_reaction_editor_button_label(ctx, index);

	obs_properties_add_button2(
		props,
		REACTION_UI_TOGGLE_KEYS[index],
		editor_button_label.c_str(),
		heartbeat_reaction_toggle_ui_clicked,
		ctx);

	obs_property_t *reaction_group = obs_properties_add_group(
		props,
		REACTION_ENABLED_KEYS[index],
		obs_module_text(REACTION_GROUP_LOCALE_KEYS[index]),
		OBS_GROUP_CHECKABLE,
		content);
	if (reaction_group)
		obs_property_set_visible(reaction_group, expanded);
}

static bool mugen_heartbeat_display_format_modified(obs_properties_t *props,
						 obs_property_t *,
						 obs_data_t *settings)
{
	if (!props || !settings)
		return false;

	const char *format =
		obs_data_get_string(settings, SETTING_DISPLAY_FORMAT);

	const bool custom =
		format && std::string(format) == "custom";

	obs_property_t *custom_prop =
		obs_properties_get(props, SETTING_CUSTOM_FORMAT);

	if (custom_prop)
		obs_property_set_visible(custom_prop, custom);

	return true;
}

static bool mugen_heartbeat_test_mode_modified(
	obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	if (!props || !settings)
		return false;

	auto *test_bpm_prop = obs_properties_get(props, SETTING_TEST_BPM);
	if (test_bpm_prop)
		obs_property_set_visible(
			test_bpm_prop, obs_data_get_bool(settings, SETTING_TEST_MODE));
	return true;
}

static obs_properties_t *mugen_heartbeat_get_properties(void *data)
{
	auto *ctx = static_cast<mugen_heartbeat_source *>(data);
	obs_properties_t *props = obs_properties_create_param(ctx, nullptr);

	/* Connect the heart-rate device before diving into appearance/reactions. */
	obs_properties_add_text(props, "ble_heading",
				obs_module_text("MugenHeartbeat.BleHeading"), OBS_TEXT_INFO);

	auto *device_prop = obs_properties_add_list(
		props, SETTING_DEVICE_ADDRESS, obs_module_text("MugenHeartbeat.Device"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_set_modified_callback(
		device_prop, mugen_heartbeat_device_ui_modified);

	bool selected_address_is_listed = false;

	if (g_ble) {
		for (auto const &device : g_ble->devices()) {
			HeartRateDeviceInfo shown = device;
			shown.name = preferred_ble_name(
				shown.name,
				g_ble->known_name_for(device.address));

			if (ctx && device.address == ctx->selected_address &&
			    !ctx->selected_device_name.empty()) {
				shown.name = preferred_ble_name(
					shown.name,
					ctx->selected_device_name);
			}

			if (g_ble->connected() &&
			    g_ble->connected_address() == device.address) {
				shown.name = preferred_ble_name(
					shown.name,
					g_ble->connected_name());
			}

			std::string label = display_device(shown);
			std::string value = bluetooth_address_value(device.address);
			obs_property_list_add_string(device_prop, label.c_str(), value.c_str());

			if (ctx && device.address == ctx->selected_address)
				selected_address_is_listed = true;
		}
	}

	/* OBS persists the selected address even when no scan has happened yet.
	   Keep that value in the combo so the user can see what will auto-connect. */
	if (ctx && ctx->selected_address != 0 && !selected_address_is_listed) {
		std::string saved_name =
			!ctx->selected_device_name.empty()
				? ctx->selected_device_name
				: obs_module_text("MugenHeartbeat.SavedDevice");

		if (g_ble) {
			saved_name = preferred_ble_name(
				saved_name,
				g_ble->known_name_for(ctx->selected_address));

			if (g_ble->connected() &&
			    g_ble->connected_address() == ctx->selected_address) {
				saved_name = preferred_ble_name(
					saved_name,
					g_ble->connected_name());
			}
		}

		HeartRateDeviceInfo saved{ctx->selected_address, saved_name, 0, 0, BluetoothAddressType::Unspecified, false};
		std::string label = display_device(saved);
		std::string value = bluetooth_address_value(saved.address);
		obs_property_list_add_string(device_prop, label.c_str(), value.c_str());
	}

	auto *auto_connect_prop =
		obs_properties_add_bool(props, SETTING_AUTO_CONNECT,
					obs_module_text("MugenHeartbeat.AutoConnect"));
	obs_property_set_modified_callback(
		auto_connect_prop, mugen_heartbeat_auto_connect_ui_modified);

	obs_properties_add_button(
		props, "start_scan",
		obs_module_text("MugenHeartbeat.StartScan"), scan_clicked);

	obs_properties_add_button(
		props, "connection_action",
		obs_module_text(mugen_heartbeat_connection_button_locale_key(ctx)),
		mugen_heartbeat_connection_button_clicked);

	std::string status = localized_ble_status(ctx);
	if (ctx && ctx->auto_connect && ctx->auto_connect_paused &&
	    g_ble && !g_ble->connected()) {
		status += " — ";
		status += obs_module_text("MugenHeartbeat.AutoConnectPaused");
	}

	obs_properties_add_text(props, "ble_status", status.c_str(), OBS_TEXT_INFO);

	const std::string other_auto_source =
		mugen_heartbeat_other_auto_connect_source_name(ctx);
	std::string other_auto_notice =
		obs_module_text("MugenHeartbeat.OtherSourceAutoConnect");
	if (!other_auto_source.empty())
		other_auto_notice += ": " + other_auto_source;
	auto *other_notice_prop = obs_properties_add_text(
		props, "ble_shared_auto_notice", other_auto_notice.c_str(),
		OBS_TEXT_INFO);
	obs_property_set_visible(
		other_notice_prop,
		ctx && !ctx->auto_connect && !other_auto_source.empty());

	obs_properties_add_text(props, "ble_help",
				obs_module_text("MugenHeartbeat.BleHelp"), OBS_TEXT_INFO);
	obs_properties_add_text(props, "ble_xiaomi_help",
				obs_module_text("MugenHeartbeat.XiaomiHelp"), OBS_TEXT_INFO);
	mugen_heartbeat_sync_ble_pairing_help(
		props, ctx, ctx ? ctx->selected_address : 0);

	auto *test_mode_prop = obs_properties_add_bool(
		props, SETTING_TEST_MODE,
		obs_module_text("MugenHeartbeat.TestMode"));
	auto *test_bpm_prop = obs_properties_add_int_slider(
		props, SETTING_TEST_BPM,
		obs_module_text("MugenHeartbeat.TestBpm"), 30, 240, 1);
	obs_property_set_visible(test_bpm_prop, ctx && ctx->test_mode);
	obs_property_set_modified_callback(
		test_mode_prop, mugen_heartbeat_test_mode_modified);

	auto *visual_mode_prop = obs_properties_add_list(
		props,
		SETTING_VISUAL_MODE,
		obs_module_text("MugenHeartbeat.VisualMode"),
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(
		visual_mode_prop,
		obs_module_text("MugenHeartbeat.VisualModeText"),
		"text");

	obs_property_list_add_string(
		visual_mode_prop,
		obs_module_text("MugenHeartbeat.VisualModeSkin"),
		"skin");

	obs_properties_t *skin_props =
		obs_properties_create();

	/* Do not reload the skin catalog from get_properties().
	   BLE state/device revisions call obs_source_update_properties(), so doing
	   reload_catalog() here marks the active skin for force_reload during
	   scan/connect and makes its animation visibly hitch. The catalog is
	   already loaded by HeartbeatSkinRenderer's constructor and can be
	   refreshed explicitly with the "Refresh skins" button. */

	auto *skin_prop = obs_properties_add_list(
		skin_props,
		SETTING_SKIN_ID,
		obs_module_text("MugenHeartbeat.Skin"),
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);

	bool current_skin_listed = false;

	if (ctx && ctx->skin_renderer) {
		for (const auto &info :
		     ctx->skin_renderer->catalog()) {
			std::string label = info.name;

			if (!info.author.empty()) {
				label += " - ";
				label += info.author;
			}

			obs_property_list_add_string(
				skin_prop,
				label.c_str(),
				info.id.c_str());

			if (info.id == ctx->skin_id)
				current_skin_listed = true;
		}
	}

	if (ctx && !ctx->skin_id.empty() &&
	    !current_skin_listed) {
		obs_property_list_add_string(
			skin_prop,
			ctx->skin_id.c_str(),
			ctx->skin_id.c_str());
	}

	HeartbeatSkinInfo skin_info_value;
	const bool have_skin_info =
		ctx && ctx->skin_renderer &&
		ctx->skin_renderer->info_for(
			ctx->skin_id,
			skin_info_value);

	const std::string skin_info_text =
		mugen_heartbeat_skin_info_text(
			have_skin_info
				? &skin_info_value
				: nullptr);

	obs_properties_add_text(
		skin_props,
		"skin_info",
		skin_info_text.c_str(),
		OBS_TEXT_INFO);

	auto *skin_background_opacity =
		obs_properties_add_int_slider(
			skin_props,
			SETTING_SKIN_BACKGROUND_OPACITY,
			obs_module_text(
				"MugenHeartbeat.SkinBackgroundOpacity"),
			0,
			100,
			1);

	obs_property_int_set_suffix(
		skin_background_opacity,
		"%");

	obs_property_set_visible(
		skin_background_opacity,
		have_skin_info &&
			skin_info_value.background_opacity_customizable);

	obs_property_set_modified_callback(
		skin_background_opacity,
		mugen_heartbeat_skin_background_opacity_modified);

	obs_properties_add_button(
		skin_props,
		"skin_open_folder",
		obs_module_text(
			"MugenHeartbeat.SkinOpenFolder"),
		mugen_heartbeat_skin_open_folder_clicked);

	obs_properties_add_button(
		skin_props,
		"skin_refresh",
		obs_module_text(
			"MugenHeartbeat.SkinRefresh"),
		mugen_heartbeat_skin_refresh_clicked);

	obs_properties_add_button(
		skin_props,
		"skin_gallery",
		obs_module_text(
			"MugenHeartbeat.SkinGallery"),
		mugen_heartbeat_skin_gallery_clicked);

	obs_properties_add_text(
		skin_props,
		"skin_help",
		obs_module_text(
			"MugenHeartbeat.SkinHelp"),
		OBS_TEXT_INFO);

	obs_properties_add_group(
		props,
		"skin_group",
		obs_module_text(
			"MugenHeartbeat.Skins"),
		OBS_GROUP_NORMAL,
		skin_props);

	const bool skin_mode =
		ctx && ctx->visual_mode == "skin";

	obs_property_set_visible(
		obs_properties_get(
			props, "skin_group"),
		skin_mode);

	obs_property_set_modified_callback(
		visual_mode_prop,
		mugen_heartbeat_visual_mode_modified);

	obs_property_set_modified_callback(
		skin_prop,
		mugen_heartbeat_skin_modified);

	obs_properties_t *appearance = obs_properties_create();

	auto *format_prop = obs_properties_add_list(
		appearance, SETTING_DISPLAY_FORMAT,
		obs_module_text("MugenHeartbeat.DisplayFormat"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(format_prop,
				     obs_module_text("MugenHeartbeat.FormatHeartBpm"),
				     "heart_bpm");
	obs_property_list_add_string(format_prop,
				     obs_module_text("MugenHeartbeat.FormatHeart"),
				     "heart");
	obs_property_list_add_string(format_prop,
				     obs_module_text("MugenHeartbeat.FormatBpm"),
				     "bpm");
	obs_property_list_add_string(format_prop,
				     obs_module_text("MugenHeartbeat.FormatNumber"),
				     "number");
	obs_property_list_add_string(format_prop,
				     obs_module_text("MugenHeartbeat.FormatCustom"),
				     "custom");

	auto *custom_format_prop = obs_properties_add_text(
		appearance,
		SETTING_CUSTOM_FORMAT,
		obs_module_text("MugenHeartbeat.CustomFormat"),
		OBS_TEXT_DEFAULT);

	obs_property_set_visible(
		custom_format_prop,
		ctx && ctx->display_format == "custom");

	obs_property_set_modified_callback(
		format_prop,
		mugen_heartbeat_display_format_modified);

	obs_properties_add_font(appearance, SETTING_FONT,
				obs_module_text("MugenHeartbeat.Font"));
	obs_properties_add_color(appearance, SETTING_TEXT_COLOR,
				 obs_module_text("MugenHeartbeat.TextColor"));
	obs_properties_add_int_slider(appearance, SETTING_TEXT_OPACITY,
				      obs_module_text("MugenHeartbeat.TextOpacity"),
				      0, 100, 1);

	obs_properties_add_bool(appearance, SETTING_OUTLINE_ENABLED,
				obs_module_text("MugenHeartbeat.Outline"));
	obs_properties_add_int_slider(appearance, SETTING_OUTLINE_SIZE,
				      obs_module_text("MugenHeartbeat.OutlineSize"),
				      1, 20, 1);
	obs_properties_add_color(appearance, SETTING_OUTLINE_COLOR,
				 obs_module_text("MugenHeartbeat.OutlineColor"));
	obs_properties_add_int_slider(appearance, SETTING_OUTLINE_OPACITY,
				      obs_module_text("MugenHeartbeat.OutlineOpacity"),
				      0, 100, 1);

	obs_properties_add_bool(appearance, SETTING_SHADOW_ENABLED,
				obs_module_text("MugenHeartbeat.Shadow"));
	obs_properties_add_int_slider(appearance, SETTING_SHADOW_X,
				      obs_module_text("MugenHeartbeat.ShadowX"),
				      0, 30, 1);
	obs_properties_add_int_slider(appearance, SETTING_SHADOW_Y,
				      obs_module_text("MugenHeartbeat.ShadowY"),
				      0, 30, 1);
	obs_properties_add_color(appearance, SETTING_SHADOW_COLOR,
				 obs_module_text("MugenHeartbeat.ShadowColor"));
	obs_properties_add_int_slider(appearance, SETTING_SHADOW_OPACITY,
				      obs_module_text("MugenHeartbeat.ShadowOpacity"),
				      0, 100, 1);

	obs_properties_add_group(props, "appearance_group",
				 obs_module_text("MugenHeartbeat.Appearance"),
				 OBS_GROUP_NORMAL, appearance);

	obs_property_set_visible(
		obs_properties_get(
			props, "appearance_group"),
		!skin_mode);

	obs_properties_t *beat_animation =
		obs_properties_create();

	auto *beat_strength =
		obs_properties_add_int_slider(
			beat_animation,
			SETTING_BEAT_STRENGTH,
			obs_module_text(
				"MugenHeartbeat.BeatStrength"),
			1, 25, 1);
	obs_property_int_set_suffix(beat_strength, "%");

	obs_properties_add_text(
		beat_animation,
		"beat_animation_help",
		obs_module_text(
			"MugenHeartbeat.BeatAnimationHelp"),
		OBS_TEXT_INFO);

	obs_properties_add_group(
		props,
		SETTING_BEAT_ANIMATION,
		obs_module_text(
			"MugenHeartbeat.BeatAnimation"),
		OBS_GROUP_CHECKABLE,
		beat_animation);

	obs_properties_add_text(
		props,
		"reaction_heading",
		obs_module_text("MugenHeartbeat.Reactions"),
		OBS_TEXT_INFO);

	obs_properties_add_text(
		props,
		"reaction_help",
		obs_module_text("MugenHeartbeat.ReactionsHelp"),
		OBS_TEXT_INFO);

	for (size_t i = 0; i < HEARTBEAT_REACTION_COUNT; ++i)
		heartbeat_reaction_add_properties(props, ctx, i);

	/* Project links belong after the functional source settings. */
	obs_properties_t *about = obs_properties_create();
	const std::string about_info =
		std::string("Mugen Heartbeat ") + PLUGIN_VERSION + "\n" +
		obs_module_text("MugenHeartbeat.About.ProjectBy") + "\n" +
		obs_module_text("MugenHeartbeat.About.FreeSoftware");
	obs_properties_add_text(
		about, "about_info", about_info.c_str(), OBS_TEXT_INFO);
	obs_properties_add_button(
		about, "about_project_page",
		obs_module_text("MugenHeartbeat.About.ProjectPage"),
		mugen_heartbeat_about_project_clicked);
	obs_properties_add_button(
		about, "about_website",
		obs_module_text("MugenHeartbeat.About.MugenArtLab"),
		mugen_heartbeat_about_website_clicked);
	obs_properties_add_group(
		props, "about", obs_module_text("MugenHeartbeat.About"),
		OBS_GROUP_NORMAL, about);


	return props;
}

static uint32_t mugen_heartbeat_get_width_gdiplus(void *data)
{
	auto *ctx = static_cast<mugen_heartbeat_source *>(data);
	if (!ctx || !ctx->text_sources[RENDER_LEVEL_NATIVE])
		return 0;

	uint32_t width =
		obs_source_get_width(ctx->text_sources[RENDER_LEVEL_NATIVE]);

	if (ctx->shadow_enabled && ctx->shadow_sources[RENDER_LEVEL_NATIVE]) {
		const uint32_t shadow_width =
			obs_source_get_width(ctx->shadow_sources[RENDER_LEVEL_NATIVE]) +
			static_cast<uint32_t>(ctx->shadow_x);

		if (shadow_width > width)
			width = shadow_width;
	}

	return width;
}

static uint32_t mugen_heartbeat_get_height_gdiplus(void *data)
{
	auto *ctx = static_cast<mugen_heartbeat_source *>(data);
	if (!ctx || !ctx->text_sources[RENDER_LEVEL_NATIVE])
		return 0;

	uint32_t height =
		obs_source_get_height(ctx->text_sources[RENDER_LEVEL_NATIVE]);

	if (ctx->shadow_enabled && ctx->shadow_sources[RENDER_LEVEL_NATIVE]) {
		const uint32_t shadow_height =
			obs_source_get_height(ctx->shadow_sources[RENDER_LEVEL_NATIVE]) +
			static_cast<uint32_t>(ctx->shadow_y);

		if (shadow_height > height)
			height = shadow_height;
	}

	return height;
}


/* MTSDF primary size wrapper; GDI+ remains an automatic fallback. */
static uint32_t mugen_heartbeat_get_width(void *data)
{
	auto *ctx = static_cast<mugen_heartbeat_source *>(data);

	if (ctx &&
	    ctx->visual_mode == "skin" &&
	    ctx->skin_renderer &&
	    ctx->skin_renderer->ready()) {
		const uint32_t width =
			ctx->skin_renderer->width();

		if (width > 0)
			return width;
	}

	if (ctx && ctx->mtsdf_renderer && ctx->mtsdf_renderer->ready()) {
		const uint32_t width = ctx->mtsdf_renderer->width();
		if (width > 0)
			return width;
	}

	return mugen_heartbeat_get_width_gdiplus(data);
}

static uint32_t mugen_heartbeat_get_height(void *data)
{
	auto *ctx = static_cast<mugen_heartbeat_source *>(data);

	if (ctx &&
	    ctx->visual_mode == "skin" &&
	    ctx->skin_renderer &&
	    ctx->skin_renderer->ready()) {
		const uint32_t height =
			ctx->skin_renderer->height();

		if (height > 0)
			return height;
	}

	if (ctx && ctx->mtsdf_renderer && ctx->mtsdf_renderer->ready()) {
		const uint32_t height = ctx->mtsdf_renderer->height();
		if (height > 0)
			return height;
	}

	return mugen_heartbeat_get_height_gdiplus(data);
}
static void mugen_heartbeat_tick(void *data, float)
{
	auto *ctx = static_cast<mugen_heartbeat_source *>(data);
	if (!ctx)
		return;

	if (g_ble) {
		/* A live manually connected Band is allowed to keep streaming.
		   Only checked auto-connect sources may ask Windows to reconnect
		   when a physical BLE link disappears. */
		const uint64_t active_band = g_ble->active_address();
		if (active_band)
			g_ble->set_connection_maintenance(
				active_band,
				mugen_heartbeat_auto_maintenance_wanted(active_band));

		const uint64_t revision = g_ble->revision();
		if (revision != ctx->last_ble_revision) {
			ctx->last_ble_revision = revision;
			mugen_heartbeat_queue_properties_refresh(ctx->source, ctx->selected_address);
		}

			/* A BLE link can remain/reappear as Connected while Heart Rate
	   notifications are dead. Trust fresh measurements, not the link flag.
	   After 15 seconds without fresh BPM, recycle the session through the
	   already proven fast-disconnect path and let normal auto-reconnect scan. */
	constexpr int64_t HEART_RATE_WATCHDOG_MS = 15000;
	constexpr int64_t HEART_RATE_WATCHDOG_COOLDOWN_MS = 5000;

	const int64_t watchdog_now = mugen_heartbeat_now_ms();

	const bool watchdog_owns_connected_band =
		ctx->selected_address != 0 &&
		g_ble &&
		g_ble->connected() &&
		g_ble->connected_address() == ctx->selected_address;

	if (ctx->test_mode || !watchdog_owns_connected_band) {
		ctx->ble_watchdog_stale_since_ms = 0;
	} else if (g_ble->bpm_fresh()) {
		ctx->ble_watchdog_stale_since_ms = 0;
	} else if (watchdog_now >=
		   ctx->ble_watchdog_cooldown_until_ms) {
		if (ctx->ble_watchdog_stale_since_ms == 0) {
			ctx->ble_watchdog_stale_since_ms =
				watchdog_now;
		} else if (watchdog_now -
				   ctx->ble_watchdog_stale_since_ms >=
			   HEART_RATE_WATCHDOG_MS) {
			blog(
				LOG_WARNING,
				"[Mugen Heartbeat] Heart Rate measurement watchdog: "
				"BLE still reports Connected but no fresh HR measurement "
				"arrived for 15 seconds; releasing the stale session.");

			ctx->ble_watchdog_stale_since_ms = 0;
			ctx->ble_watchdog_cooldown_until_ms =
				watchdog_now +
				HEART_RATE_WATCHDOG_COOLDOWN_MS;

			/* Do not synchronously unsubscribe a possibly stale CCCD.
			   disconnect_fast() is the same non-blocking release path
			   already used for safe source/session teardown. */
			g_ble->disconnect_fast();

			/* Give the release a moment, then normal presence-gated
			   auto-reconnect is allowed to scan for the selected band. */
			ctx->next_auto_connect_ms =
				watchdog_now + 1000;

			blog(
				LOG_INFO,
				"[Mugen Heartbeat] Heart Rate measurement watchdog: "
				"stale session released; waiting for a fresh advertisement.");
		}
	}
/* Manual Connect works without auto-connect, but only for a finite
		   period after the user's explicit button click. */
		if (ctx->manual_connect_pending &&
		    (ctx->manual_connect_deadline_ms == 0 ||
		     mugen_heartbeat_now_ms() >=
			     ctx->manual_connect_deadline_ms)) {
			ctx->manual_connect_pending = false;
			ctx->manual_connect_deadline_ms = 0;
			blog(LOG_INFO,
			     "[Mugen Heartbeat] manual Connect request expired; "
			     "waiting for another explicit Connect click.");
		}
		if (ctx->manual_connect_pending &&
		    !ctx->test_mode &&
		    ctx->selected_address != 0 &&
		    !g_ble->connected()) {
			const BleState manual_state = g_ble->state();
			const int64_t manual_now = mugen_heartbeat_now_ms();
			constexpr int64_t MANUAL_FRESH_ADVERTISEMENT_MS = 7000;

			if (g_ble->seen_recently(
				    ctx->selected_address,
				    MANUAL_FRESH_ADVERTISEMENT_MS)) {
				ctx->manual_connect_pending = false;
				ctx->manual_connect_deadline_ms = 0;
				ctx->next_auto_connect_ms = manual_now + 5000;

				blog(LOG_INFO,
				     "[Mugen Heartbeat] manual Connect: fresh advertisement arrived; connecting.");

				g_ble->connect(
					ctx->selected_address,
					mugen_heartbeat_auto_maintenance_wanted(
						ctx->selected_address));
			} else if ((manual_state == BleState::Idle ||
				    manual_state == BleState::Error) &&
				   manual_now >= ctx->next_auto_connect_ms) {
				ctx->next_auto_connect_ms = manual_now + 6000;

				blog(LOG_INFO,
				     "[Mugen Heartbeat] manual Connect: still waiting for selected Band; rescanning.");

				g_ble->start_scan(ctx->selected_address);
			}
		}
		/* Auto-connect is opt-in.
		   After a real range loss, refresh Windows' BLE view first.
		   Only attempt GATT after the selected Band was heard recently. */
		if (ctx->auto_connect && !ctx->auto_connect_paused &&
		    !ctx->manual_connect_pending &&
		    !ctx->test_mode && ctx->selected_address != 0 &&
		    !g_ble->connected()) {
			const BleState state = g_ble->state();
			const int64_t now = mugen_heartbeat_now_ms();

			if ((state == BleState::Idle || state == BleState::Error) &&
			    now >= ctx->next_auto_connect_ms) {
				constexpr int64_t FRESH_ADVERTISEMENT_MS = 7000;

				if (g_ble->seen_recently(
					    ctx->selected_address,
					    FRESH_ADVERTISEMENT_MS)) {
					ctx->next_auto_connect_ms = now + 5000;
					blog(LOG_INFO,
					     "[Mugen Heartbeat] BLE auto-reconnect: fresh selected-band advertisement; connecting.");
					g_ble->connect(ctx->selected_address, true);
				} else {
					ctx->next_auto_connect_ms = now + 6000;
					blog(LOG_INFO,
					     "[Mugen Heartbeat] BLE auto-reconnect: selected Band not freshly seen; starting presence scan.");
					g_ble->start_scan(ctx->selected_address);
				}
			}
		}
	}

	mugen_heartbeat_update_reactions(ctx);
	mugen_heartbeat_update_beat_animation(ctx);
	mugen_heartbeat_update_text(ctx);
}

static float mugen_heartbeat_current_render_scale()
{
	struct matrix4 matrix;
	gs_matrix_get(&matrix);

	const float sx = std::sqrt(matrix.x.x * matrix.x.x +
				   matrix.x.y * matrix.x.y +
				   matrix.x.z * matrix.x.z);

	const float sy = std::sqrt(matrix.y.x * matrix.y.x +
				   matrix.y.y * matrix.y.y +
				   matrix.y.z * matrix.y.z);

	const float scale = sx > sy ? sx : sy;

	if (!std::isfinite(scale) || scale <= 0.0f)
		return 1.0f;

	return scale;
}

static size_t mugen_heartbeat_choose_render_level(float scene_scale)
{
	/* Keep roughly 3x source pixels per final scene pixel. */
	const float target = scene_scale * 3.0f;

	size_t best = 0;
	float best_distance = 1000.0f;

	for (size_t i = 0; i < RENDER_LEVEL_COUNT; ++i) {
		const float ratio = RENDER_SCALES[i] / target;
		const float distance = std::fabs(std::log2(ratio));

		if (distance < best_distance) {
			best_distance = distance;
			best = i;
		}
	}

	return best;
}

static void mugen_heartbeat_render_gdiplus(void *data, gs_effect_t *)
{
	auto *ctx = static_cast<mugen_heartbeat_source *>(data);
	if (!ctx)
		return;

	const float scene_scale = mugen_heartbeat_current_render_scale();
	const size_t level =
		mugen_heartbeat_choose_render_level(scene_scale);

	const float render_scale = RENDER_SCALES[level];
	const float inverse_scale = 1.0f / render_scale;

	obs_source_t *text_source = ctx->text_sources[level];
	obs_source_t *shadow_source = ctx->shadow_sources[level];

	if (!text_source)
		return;

	gs_blend_state_push();
	gs_reset_blend_state();

	if (ctx->shadow_enabled &&
	    shadow_source &&
	    ctx->shadow_opacity > 0) {
		gs_matrix_push();

		gs_matrix_translate3f(static_cast<float>(ctx->shadow_x),
				      static_cast<float>(ctx->shadow_y), 0.0f);
		gs_matrix_scale3f(inverse_scale, inverse_scale, 1.0f);

		obs_source_video_render(shadow_source);
		gs_matrix_pop();
	}

	gs_matrix_push();
	gs_matrix_scale3f(inverse_scale, inverse_scale, 1.0f);
	obs_source_video_render(text_source);
	gs_matrix_pop();

	gs_blend_state_pop();
}


/* MTSDF primary render wrapper; GDI+ remains an automatic fallback.
   v0.1.23 deforms tagged glyph vertices inside the authoritative layout.
   No duplicate text is drawn, so there is no stationary ghost behind a
   [pulse] fragment and every animated glyph keeps the DirectWrite baseline. */
static void mugen_heartbeat_render(
	void *data,
	gs_effect_t *effect)
{
	auto *ctx =
		static_cast<mugen_heartbeat_source *>(data);

	if (!ctx)
		return;

	const float beat_scale =
		mugen_heartbeat_current_beat_scale(ctx);

	if (ctx->visual_mode == "skin" &&
	    ctx->skin_renderer &&
	    ctx->skin_renderer->ready() &&
	    ctx->skin_renderer->render(
		    beat_scale,
		    ctx->beat_animation_enabled,
		    ctx->skin_background_opacity)) {
		return;
	}

	/* With valid [pulse] markup the renderer itself transforms only tagged
	   glyph quads around the shared bounds of each pulse range. */
	if (ctx->pulse_markup_active &&
	    ctx->mtsdf_renderer &&
	    ctx->mtsdf_renderer->ready() &&
	    ctx->mtsdf_renderer->render(
		    beat_scale)) {
		return;
	}

	/* No markup (or selective MTSDF fallback): preserve the proven
	   whole-source pulse behavior from v0.1.19. */
	const bool transformed =
		std::isfinite(beat_scale) &&
		std::fabs(beat_scale - 1.0f) >
			0.0001f;

	if (transformed) {
		const float width =
			static_cast<float>(
				mugen_heartbeat_get_width(ctx));

		const float height =
			static_cast<float>(
				mugen_heartbeat_get_height(ctx));

		gs_matrix_push();

		gs_matrix_translate3f(
			width * 0.5f,
			height * 0.5f,
			0.0f);

		gs_matrix_scale3f(
			beat_scale,
			beat_scale,
			1.0f);

		gs_matrix_translate3f(
			-width * 0.5f,
			-height * 0.5f,
			0.0f);
	}

	bool rendered = false;

	if (ctx->mtsdf_renderer &&
	    ctx->mtsdf_renderer->ready()) {
		rendered =
			ctx->mtsdf_renderer->render();
	}

	if (!rendered)
		mugen_heartbeat_render_gdiplus(
			data, effect);

	if (transformed)
		gs_matrix_pop();
}
static void mugen_heartbeat_enum_active_sources(void *data,
						obs_source_enum_proc_t enum_callback,
						void *param)
{
	auto *ctx = static_cast<mugen_heartbeat_source *>(data);
	if (!ctx)
		return;

	for (size_t i = 0; i < RENDER_LEVEL_COUNT; ++i) {
		if (ctx->shadow_sources[i])
			enum_callback(ctx->source, ctx->shadow_sources[i], param);
		if (ctx->text_sources[i])
			enum_callback(ctx->source, ctx->text_sources[i], param);
	}
}

static bool mugen_heartbeat_audio_render(void *, uint64_t *,
					 struct obs_source_audio_mix *,
					 uint32_t, size_t, size_t)
{
	return false;
}

static obs_source_info mugen_heartbeat_source_info = {};

bool obs_module_load(void)
{
	g_ble = new BleHeartRateClient();
	g_ble->load_known_names_from_disk();

	mugen_heartbeat_source_info.id = "mugen_heartbeat_source";
	mugen_heartbeat_source_info.type = OBS_SOURCE_TYPE_INPUT;
	mugen_heartbeat_source_info.output_flags =
		OBS_SOURCE_VIDEO | OBS_SOURCE_COMPOSITE | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB;
	mugen_heartbeat_source_info.get_name = mugen_heartbeat_get_name;
	mugen_heartbeat_source_info.create = mugen_heartbeat_create;
	mugen_heartbeat_source_info.destroy = mugen_heartbeat_destroy;
	mugen_heartbeat_source_info.update = mugen_heartbeat_update;
	mugen_heartbeat_source_info.get_defaults = mugen_heartbeat_get_defaults;
	mugen_heartbeat_source_info.get_properties = mugen_heartbeat_get_properties;
	mugen_heartbeat_source_info.get_width = mugen_heartbeat_get_width;
	mugen_heartbeat_source_info.get_height = mugen_heartbeat_get_height;
	mugen_heartbeat_source_info.video_tick = mugen_heartbeat_tick;
	mugen_heartbeat_source_info.video_render = mugen_heartbeat_render;
	mugen_heartbeat_source_info.enum_active_sources = mugen_heartbeat_enum_active_sources;
	mugen_heartbeat_source_info.audio_render = mugen_heartbeat_audio_render;
	mugen_heartbeat_source_info.icon_type = OBS_ICON_TYPE_TEXT;

	obs_register_source(&mugen_heartbeat_source_info);
	blog(LOG_INFO, "[Mugen Heartbeat] plugin loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	{
		std::scoped_lock lock(g_source_registry_mutex);
		g_source_registry.clear();
	}

	if (g_ble) {
		g_ble->save_known_names_to_disk();
		g_ble->shutdown();
		delete g_ble;
		g_ble = nullptr;
	}
	blog(LOG_INFO, "[Mugen Heartbeat] plugin unloaded");
}






















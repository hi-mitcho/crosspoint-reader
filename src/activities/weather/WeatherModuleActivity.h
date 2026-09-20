#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "activities/Activity.h"
#include "activities/ActivityResult.h"

// Weather Module: fetches current conditions + today's hi/lo from Open-Meteo
// (SLO-8) for a manually-entered location (SLO-8: no GPS on this hardware).
// The location prompt takes a US zip code, resolved once to "lat,lon" via
// Zippopotam.us and stored under the hood; a literal "lat,lon" also works as
// a fallback for exact coordinates. Refreshes on entry, skipping the network
// call if the cached reading is under an hour old (SLO-8's battery-conscious
// refresh cadence).
//
// WiFi: Settings > Network only manages saved credentials and always tears
// down + restarts afterward (SettingsActivity.cpp), so a connection made
// there never survives to a later Activity. Like ClockSyncActivity, this
// Module launches its own WifiSelectionActivity when it needs a connection,
// and restarts on exit if it was the one that brought WiFi up (mirrors
// ClockSyncActivity's rationale: avoid repeated WiFi driver bring-up/teardown
// heap fragmentation on this RAM-constrained device).
class WeatherModuleActivity final : public Activity {
 public:
  explicit WeatherModuleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Weather", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State { LOADING, SHOWING };
  State state = State::LOADING;
  bool noWifi = false;
  bool fetchFailed = false;
  bool locationLookupFailed = false;
  bool shouldTearDownWifiOnExit = false;

  void promptForLocation();
  void onLocationEntered(const ActivityResult& result);
  void ensureWifiConnected(std::function<void()> onConnected);
  void beginRefresh();
  bool resolveZipToLatLon(const std::string& zip, double& lat, double& lon);
  void refreshIfNeeded();
  static bool cacheIsFresh();
};

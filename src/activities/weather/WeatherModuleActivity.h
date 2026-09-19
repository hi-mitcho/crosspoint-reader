#pragma once
#include "activities/Activity.h"

// Skeleton Module for SLO-5: proves the static-registration pattern (Activity
// subclass + ActivityManager::goTo*()) before the Weather API integration
// (SLO-8) or the new Home Screen grid (SLO-7) exist. Renders a placeholder
// screen only.
class WeatherModuleActivity final : public Activity {
 public:
  explicit WeatherModuleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Weather", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};

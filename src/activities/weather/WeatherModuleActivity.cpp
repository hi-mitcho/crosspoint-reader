#include "WeatherModuleActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UITheme.h"
#include "fontIds.h"

void WeatherModuleActivity::onEnter() {
  Activity::onEnter();
  requestUpdateAndWait();
}

void WeatherModuleActivity::loop() {
  int x = 0;
  int y = 0;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
    finish();
  }
}

void WeatherModuleActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_WEATHER_MODULE_TITLE));

  const auto y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 4;
  renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_WEATHER_MODULE_COMING_SOON));

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

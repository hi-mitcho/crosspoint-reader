#include "HomeActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/blocks.h"
#include "components/icons/folder.h"
#include "components/icons/library.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "fontIds.h"

namespace {
constexpr int QUICK_LINK_ICON_SIZE = 32;
constexpr int QUICK_LINK_ROW_HEIGHT = 56;

// Fractions of the available content area, derived from the SLO-7 mockup
// (480x800 portrait): a narrower left column of stacked info cards next to a
// wider right column carrying the Current Read cover and Next Reader
// Articles cards. Expressed as ratios (not fixed pixels) so the grid scales
// with actual screen dimensions instead of assuming 480x800.
constexpr float SIDE_MARGIN_RATIO = 16.0f / 480.0f;
constexpr float GUTTER_RATIO = 16.0f / 480.0f;
constexpr float COL_A_WIDTH_RATIO = 180.0f / 480.0f;
constexpr float COL_B_WIDTH_RATIO = 252.0f / 480.0f;
constexpr float ROW_GAP_RATIO = 12.0f / 686.0f;

// Left column: logo, weather, top-3 reminders, decorative strip.
constexpr float LOGO_HEIGHT_RATIO = 110.0f / 686.0f;
constexpr float WEATHER_HEIGHT_RATIO = 149.0f / 686.0f;
constexpr float REMINDERS_HEIGHT_RATIO = 324.0f / 686.0f;
// Decorative strip takes whatever remains after the three ratios above.

// Right column: Current Read cover, Next Reader Articles.
constexpr float CURRENT_READ_HEIGHT_RATIO = 337.0f / 686.0f;
// Articles card takes whatever remains after the gap.
}  // namespace

int HomeActivity::getSelectableCount() const { return CARD_COUNT + quickLinkCount(hasOpdsServers); }

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  // The card grid shows a single Current Read card, not a carousel.
  loadRecentBooks(1);

  selectorIndex =
      initialMenuItem == HomeMenuItem::NONE ? 0 : CARD_COUNT + menuItemToIndex(initialMenuItem, hasOpdsServers);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() { Activity::onExit(); }

void HomeActivity::loop() {
  const int selectableCount = getSelectableCount();
  const auto& metrics = UITheme::getInstance().getMetrics();

  auto activateSelection = [this] {
    switch (selectorIndex) {
      case 0:
        if (!recentBooks.empty()) {
          onSelectBook(recentBooks[0].path);
        }
        return;
      case 1:
        activityManager.goToWeatherModule();
        return;
      case 2:
        activityManager.goToArticleModule();
        return;
      default:
        break;
    }
    const int quickLinkIndex = selectorIndex - CARD_COUNT;
    switch (indexToMenuItem(quickLinkIndex, hasOpdsServers)) {
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::LIBRARY:
        onLibraryOpen();
        break;
      case HomeMenuItem::OPDS_BROWSER:
        onOpdsBrowserOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      default:
        break;
    }
  };

  buttonNavigator.onNext([this, selectableCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, selectableCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, selectableCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, selectableCount);
    requestUpdate();
  });

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, selectableCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, selectableCount);
    requestUpdate();
    return;
  }

  // Back is otherwise unused on the home screen: open the most recently read
  // book directly (recentBooks is pruned of files missing from the SD card).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && !recentBooks.empty()) {
    onSelectBook(recentBooks[0].path);
    return;
  }

  // Card taps: rects recomputed here to match render()'s geometry (kept
  // local since it's specific to this fixed 2-column layout).
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int sideMargin = static_cast<int>(pageWidth * SIDE_MARGIN_RATIO);
  const int gutter = static_cast<int>(pageWidth * GUTTER_RATIO);
  const int colAWidth = static_cast<int>(pageWidth * COL_A_WIDTH_RATIO);
  const int colBWidth = static_cast<int>(pageWidth * COL_B_WIDTH_RATIO);
  const int colBX = sideMargin + colAWidth + gutter;

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int quickLinkAreaHeight = QUICK_LINK_ROW_HEIGHT + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - quickLinkAreaHeight;
  const int contentHeight = contentBottom - contentTop;
  const int rowGap = static_cast<int>(contentHeight * ROW_GAP_RATIO);

  const int currentReadHeight = static_cast<int>(contentHeight * CURRENT_READ_HEIGHT_RATIO);
  const Rect weatherRect{sideMargin, contentTop + static_cast<int>(contentHeight * LOGO_HEIGHT_RATIO) + rowGap,
                         colAWidth, static_cast<int>(contentHeight * WEATHER_HEIGHT_RATIO)};
  const Rect articlesRect{colBX, contentTop + currentReadHeight + rowGap, colBWidth,
                          contentBottom - (contentTop + currentReadHeight + rowGap)};

  int touchedCard = -1;
  if (mappedInput.wasTapInRect(colBX, contentTop, colBWidth, currentReadHeight)) {
    touchedCard = 0;
  } else if (mappedInput.wasTapInRect(weatherRect.x, weatherRect.y, weatherRect.width, weatherRect.height)) {
    touchedCard = 1;
  } else if (mappedInput.wasTapInRect(articlesRect.x, articlesRect.y, articlesRect.width, articlesRect.height)) {
    touchedCard = 2;
  }
  if (touchedCard != -1) {
    selectorIndex = touchedCard;
    activateSelection();
    return;
  }

  const int quickLinkTop = contentBottom + metrics.verticalSpacing;
  int quickLinkCol = -1;
  const int quickLinkButtonWidth = (pageWidth - 2 * sideMargin) / quickLinkCount(hasOpdsServers);
  const auto quickLinkTouch =
      mappedInput.colTouch(quickLinkCol, sideMargin, quickLinkButtonWidth, quickLinkCount(hasOpdsServers), quickLinkTop,
                           quickLinkTop + QUICK_LINK_ROW_HEIGHT, quickLinkButtonWidth);
  if (quickLinkTouch != MappedInputManager::RowTouch::None) {
    const int touchedIndex = CARD_COUNT + quickLinkCol;
    if (quickLinkTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedIndex;
      activateSelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

int HomeActivity::drawWrappedTitle(const int x, const int y, const int maxWidth, const char* title) const {
  const auto lines = renderer.wrappedText(RESPONDER_18_FONT_ID, title, maxWidth, 2, EpdFontFamily::BOLD);
  int lineY = y;
  for (const auto& line : lines) {
    renderer.drawText(RESPONDER_18_FONT_ID, x, lineY, line.c_str(), true, EpdFontFamily::BOLD);
    lineY += renderer.getLineHeight(RESPONDER_18_FONT_ID);
  }
  return lineY;
}

void HomeActivity::drawLogoCard(const Rect& rect) const {
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, 8, true);
  // drawCenteredText centers on the full screen width, not a rect, so we
  // center this narrow card's title manually to keep it inside the card.
  const char* title = tr(STR_HOME_LOGO_TITLE);
  const auto lines = renderer.wrappedText(RESPONDER_18_FONT_ID, title, rect.width - 16, 2, EpdFontFamily::BOLD);
  const int lineHeight = renderer.getLineHeight(RESPONDER_18_FONT_ID);
  int lineY = rect.y + (rect.height - lineHeight * static_cast<int>(lines.size())) / 2;
  for (const auto& line : lines) {
    const int lineWidth = renderer.getTextWidth(RESPONDER_18_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(RESPONDER_18_FONT_ID, rect.x + (rect.width - lineWidth) / 2, lineY, line.c_str(), true,
                      EpdFontFamily::BOLD);
    lineY += lineHeight;
  }
}

void HomeActivity::drawWeatherCard(const Rect& rect, bool selected) const {
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, selected ? 2 : 1, 8, true);

  char tempLabel[8] = "--°";
  if (SETTINGS.weatherLastFetchUnix != 0) {
    snprintf(tempLabel, sizeof(tempLabel), "%d°", SETTINGS.weatherLastTempF);
  }
  const int textWidth = renderer.getTextWidth(RESPONDER_18_FONT_ID, tempLabel, EpdFontFamily::BOLD);

  // Simple circle glyph, standing in for the real conditions icon until
  // per-condition icon assets are added. Laid out side by side with the
  // temperature as one horizontally centered group.
  const int radius = std::min(rect.width, rect.height) / 8;
  constexpr int iconTextGap = 10;
  const int groupWidth = radius * 2 + iconTextGap + textWidth;
  const int groupX = rect.x + (rect.width - groupWidth) / 2;
  const int groupY = rect.y + rect.height / 2;

  const int cx = groupX + radius;
  renderer.drawArc(radius, cx, groupY, 1, 1, 2, true);
  renderer.drawArc(radius, cx, groupY, -1, 1, 2, true);
  renderer.drawArc(radius, cx, groupY, 1, -1, 2, true);
  renderer.drawArc(radius, cx, groupY, -1, -1, 2, true);

  const int textX = groupX + radius * 2 + iconTextGap;
  const int textY = groupY - renderer.getLineHeight(RESPONDER_18_FONT_ID) / 2;
  renderer.drawText(RESPONDER_18_FONT_ID, textX, textY, tempLabel, true, EpdFontFamily::BOLD);
}

void HomeActivity::drawRemindersCard(const Rect& rect) const {
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, 8, true);

  constexpr int padding = 14;
  int textY =
      drawWrappedTitle(rect.x + padding, rect.y + padding, rect.width - padding * 2, tr(STR_HOME_REMINDERS_TITLE));
  textY += padding;

  const auto lines =
      renderer.wrappedText(UI_10_FONT_ID, tr(STR_HOME_REMINDERS_PLACEHOLDER), rect.width - padding * 2, 4);
  for (const auto& line : lines) {
    renderer.drawText(UI_10_FONT_ID, rect.x + padding, textY, line.c_str());
    textY += renderer.getLineHeight(UI_10_FONT_ID);
  }
}

void HomeActivity::drawDecorativeStrip(const Rect& rect) const {
  const int midY = rect.y + rect.height / 2;
  const int amplitude = rect.height / 3;
  const int waveLength = std::max(20, rect.width / 6);
  int x = rect.x;
  int prevX = x;
  int prevY = midY;
  bool up = true;
  while (x < rect.x + rect.width) {
    x = std::min(x + waveLength / 2, rect.x + rect.width);
    const int y = midY + (up ? -amplitude : amplitude);
    renderer.drawLine(prevX, prevY, x, y, 3, true);
    prevX = x;
    prevY = y;
    up = !up;
  }
}

void HomeActivity::drawArticlesCard(const Rect& rect, bool selected) const {
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, selected ? 2 : 1, 8, true);

  constexpr int padding = 16;
  int textY =
      drawWrappedTitle(rect.x + padding, rect.y + padding, rect.width - padding * 2, tr(STR_HOME_ARTICLES_TITLE));
  textY += padding;

  const auto lines = renderer.wrappedText(UI_10_FONT_ID, tr(STR_ARTICLE_EMPTY), rect.width - padding * 2, 4);
  for (const auto& line : lines) {
    renderer.drawText(UI_10_FONT_ID, rect.x + padding, textY, line.c_str());
    textY += renderer.getLineHeight(UI_10_FONT_ID);
  }
}

void HomeActivity::drawCurrentReadCard(const Rect& rect, bool selected) const {
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, selected ? 2 : 1, 8, true);

  constexpr int padding = 16;
  int textY =
      drawWrappedTitle(rect.x + padding, rect.y + padding, rect.width - padding * 2, tr(STR_HOME_CURRENT_READ_TITLE));
  textY += padding;

  if (recentBooks.empty()) {
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, tr(STR_NO_OPEN_BOOK), rect.width - padding * 2, 2);
    for (const auto& line : lines) {
      renderer.drawText(UI_10_FONT_ID, rect.x + padding, textY, line.c_str());
      textY += renderer.getLineHeight(UI_10_FONT_ID);
    }
    return;
  }

  const auto lines = renderer.wrappedText(UI_12_FONT_ID, recentBooks[0].title.c_str(), rect.width - padding * 2, 4);
  for (const auto& line : lines) {
    renderer.drawText(UI_12_FONT_ID, rect.x + padding, textY, line.c_str());
    textY += renderer.getLineHeight(UI_12_FONT_ID);
  }
}

void HomeActivity::drawQuickLinkStrip(const Rect& rect, int selectedIndex) const {
  const int count = quickLinkCount(hasOpdsServers);
  const int buttonWidth = rect.width / count;

  std::vector<UIIcon> icons = {Folder, Library};
  if (hasOpdsServers) icons.push_back(Blocks);
  icons.push_back(Transfer);
  icons.push_back(Settings);

  for (int i = 0; i < count; ++i) {
    const int buttonX = rect.x + i * buttonWidth;
    const bool selected = selectedIndex == i;
    if (selected) {
      renderer.fillRoundedRect(buttonX + 4, rect.y, buttonWidth - 8, rect.height, 8, Color::LightGray);
    }

    const uint8_t* iconBitmap = nullptr;
    switch (icons[i]) {
      case UIIcon::Folder:
        iconBitmap = FolderIcon;
        break;
      case UIIcon::Library:
        iconBitmap = LibraryIcon;
        break;
      case UIIcon::Blocks:
        iconBitmap = BlocksIcon;
        break;
      case UIIcon::Transfer:
        iconBitmap = TransferIcon;
        break;
      case UIIcon::Settings:
        iconBitmap = Settings2Icon;
        break;
      default:
        break;
    }
    if (iconBitmap == nullptr) continue;

    const int iconX = buttonX + (buttonWidth - QUICK_LINK_ICON_SIZE) / 2;
    const int iconY = rect.y + (rect.height - QUICK_LINK_ICON_SIZE) / 2;
    renderer.drawIcon(iconBitmap, iconX, iconY, QUICK_LINK_ICON_SIZE);
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, nullptr);

  const int sideMargin = static_cast<int>(pageWidth * SIDE_MARGIN_RATIO);
  const int gutter = static_cast<int>(pageWidth * GUTTER_RATIO);
  const int colAWidth = static_cast<int>(pageWidth * COL_A_WIDTH_RATIO);
  const int colBWidth = static_cast<int>(pageWidth * COL_B_WIDTH_RATIO);
  const int colBX = sideMargin + colAWidth + gutter;

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int quickLinkAreaHeight = QUICK_LINK_ROW_HEIGHT + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - quickLinkAreaHeight;
  const int contentHeight = contentBottom - contentTop;
  const int rowGap = static_cast<int>(contentHeight * ROW_GAP_RATIO);

  // --- Left column: logo, weather, reminders, decorative strip ---
  int leftY = contentTop;
  const int logoHeight = static_cast<int>(contentHeight * LOGO_HEIGHT_RATIO);
  drawLogoCard(Rect{sideMargin, leftY, colAWidth, logoHeight});
  leftY += logoHeight + rowGap;

  const int weatherHeight = static_cast<int>(contentHeight * WEATHER_HEIGHT_RATIO);
  drawWeatherCard(Rect{sideMargin, leftY, colAWidth, weatherHeight}, selectorIndex == 1);
  leftY += weatherHeight + rowGap;

  const int remindersHeight = static_cast<int>(contentHeight * REMINDERS_HEIGHT_RATIO);
  drawRemindersCard(Rect{sideMargin, leftY, colAWidth, remindersHeight});
  leftY += remindersHeight + rowGap;

  const int decorHeight = contentBottom - leftY;
  drawDecorativeStrip(Rect{sideMargin, leftY, colAWidth, decorHeight});

  // --- Right column: Current Read, Next Reader Articles ---
  const int currentReadHeight = static_cast<int>(contentHeight * CURRENT_READ_HEIGHT_RATIO);
  drawCurrentReadCard(Rect{colBX, contentTop, colBWidth, currentReadHeight}, selectorIndex == 0);

  const int articlesY = contentTop + currentReadHeight + rowGap;
  drawArticlesCard(Rect{colBX, articlesY, colBWidth, contentBottom - articlesY}, selectorIndex == 2);

  // --- Quick-link strip below the card grid ---
  const int quickLinkTop = contentBottom + metrics.verticalSpacing;
  drawQuickLinkStrip(Rect{sideMargin, quickLinkTop, pageWidth - 2 * sideMargin, QUICK_LINK_ROW_HEIGHT},
                     selectorIndex - CARD_COUNT);

  const auto labels = mappedInput.mapLabels(recentBooks.empty() ? "" : tr(STR_RESUME), tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // The card grid fills much more of the screen than the old single-tile
  // layout, so any leftover ghosting from whatever was on the panel before
  // Home is entered is far more visible. FAST_REFRESH only touches pixels
  // the new frame changes, so it can't clear stale dark pixels outside the
  // areas this activity draws into. Force a full waveform on Home's first
  // paint regardless of the caller's flag to guarantee a clean panel.
  renderer.displayBuffer(!firstRenderDone ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);

  firstRenderDone = true;
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onLibraryOpen() { activityManager.goToLibrary(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

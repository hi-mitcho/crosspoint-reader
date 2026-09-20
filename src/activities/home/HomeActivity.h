#pragma once
#include <functional>
#include <vector>

#include "./FileBrowserActivity.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

class HomeActivity final : public Activity {
  // Selectable cards ahead of the quick-link strip: Current Read, Weather,
  // Next Reader Articles. The Top 3 Reminders card and the logo/decorative
  // slots are display-only (no data source wired yet), so they're skipped in
  // the selection order.
  static constexpr int CARD_COUNT = 3;

  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool firstRenderDone = false;
  bool hasOpdsServers = false;
  std::vector<RecentBook> recentBooks;
  const HomeMenuItem initialMenuItem;
  const bool cleanInitialRefresh;

  // Convert HomeMenuItem to a quick-link strip index (used in onEnter)
  static int menuItemToIndex(HomeMenuItem item, bool hasOpdsUrl) {
    int i = 0;
    if (item == HomeMenuItem::FILE_BROWSER) return i;
    ++i;
    if (item == HomeMenuItem::LIBRARY) return i;
    ++i;
    if (item == HomeMenuItem::OPDS_BROWSER) return hasOpdsUrl ? i : 0;
    if (hasOpdsUrl) ++i;
    if (item == HomeMenuItem::FILE_TRANSFER) return i;
    ++i;
    if (item == HomeMenuItem::SETTINGS_MENU) return i;
    return 0;
  }

  // Convert a quick-link strip index back to a HomeMenuItem (used in loop)
  static HomeMenuItem indexToMenuItem(int idx, bool hasOpdsUrl) {
    int i = 0;
    if (idx == i++) return HomeMenuItem::FILE_BROWSER;
    if (idx == i++) return HomeMenuItem::LIBRARY;
    if (hasOpdsUrl && idx == i++) return HomeMenuItem::OPDS_BROWSER;
    if (idx == i++) return HomeMenuItem::FILE_TRANSFER;
    if (idx == i) return HomeMenuItem::SETTINGS_MENU;
    return HomeMenuItem::NONE;
  }
  static int quickLinkCount(bool hasOpdsUrl) { return hasOpdsUrl ? 5 : 4; }

  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onLibraryOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();

  int getSelectableCount() const;
  void loadRecentBooks(int maxBooks);

  // Card grid drawing helpers (render.cpp keeps geometry and drawing together
  // since both are specific to this activity's fixed 2-column layout).
  void drawLogoCard(const Rect& rect) const;
  void drawWeatherCard(const Rect& rect, bool selected) const;
  void drawRemindersCard(const Rect& rect) const;
  void drawDecorativeStrip(const Rect& rect) const;
  void drawArticlesCard(const Rect& rect, bool selected) const;
  // Placeholder card: title + book name if one is in progress, otherwise a
  // "no open book" message. Skips real cover art for now (BaseTheme's shared
  // drawRecentBookCover assumes a full-width tile and its cover-buffer
  // snapshot/restore breaks when fed this layout's narrower card rect).
  void drawCurrentReadCard(const Rect& rect, bool selected) const;
  // Compact icon-only row replacing the full-size vertical button list, so
  // the card grid gets most of the screen. rect spans the whole row; icons
  // are evenly spaced within it.
  void drawQuickLinkStrip(const Rect& rect, int selectedIndex) const;
  // Draws a card title wrapped to at most 2 lines instead of a single
  // unwrapped line, so longer strings (translations, "Continue Reading")
  // don't run past the card's edge. Returns the y position immediately
  // below the title block, for body text drawn under it.
  int drawWrappedTitle(int x, int y, int maxWidth, const char* title) const;

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE, bool cleanInitialRefresh = false)
      : Activity("Home", renderer, mappedInput),
        initialMenuItem(initialMenuItemValue),
        cleanInitialRefresh(cleanInitialRefresh) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
};

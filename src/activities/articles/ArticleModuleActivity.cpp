#include "ArticleModuleActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <variant>

#include "ArticleDetailActivity.h"
#include "CrossPointSettings.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void ArticleModuleActivity::onEnter() {
  UiListActivity::onEnter();
  noWifi = false;
  syncFailed = false;
  nav.selected = 0;

  if (SETTINGS.articleModuleToken[0] == '\0') {
    promptForToken();
    return;
  }

  beginSync();
}

void ArticleModuleActivity::onExit() {
  Activity::onExit();
  if (shouldTearDownWifiOnExit && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void ArticleModuleActivity::promptForToken() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ARTICLE_TOKEN_PROMPT), "", 63,
                                              InputType::Password),
      [this](const ActivityResult& result) { onTokenEntered(result); });
}

void ArticleModuleActivity::onTokenEntered(const ActivityResult& result) {
  if (result.isCancelled) {
    finish();
    return;
  }

  const auto& kb = std::get<KeyboardResult>(result.data);
  strncpy(SETTINGS.articleModuleToken, kb.text.c_str(), sizeof(SETTINGS.articleModuleToken) - 1);
  SETTINGS.articleModuleToken[sizeof(SETTINGS.articleModuleToken) - 1] = '\0';
  SETTINGS.saveToFile();

  beginSync();
}

void ArticleModuleActivity::ensureWifiConnected(std::function<void()> onConnected) {
  if (WiFi.status() == WL_CONNECTED) {
    onConnected();
    return;
  }

  shouldTearDownWifiOnExit = true;
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this, onConnected](const ActivityResult& result) {
                           if (result.isCancelled) {
                             noWifi = true;
                             state = State::LIST;
                             requestUpdate();
                             return;
                           }
                           onConnected();
                         });
}

void ArticleModuleActivity::beginSync() {
  ensureWifiConnected([this] {
    state = State::LOADING;
    requestUpdate();
  });
}

void ArticleModuleActivity::syncArticles() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_INF("ARTM", "WiFi dropped before sync could run");
    noWifi = true;
    return;
  }

  const auto err = ReadwiseClient::listUnarchived(articles);
  if (err != ReadwiseClient::OK) {
    LOG_ERR("ARTM", "Sync failed: error %d (http %d)", err, ReadwiseClient::lastHttpCode);
    syncFailed = true;
    return;
  }

  rebuildRowItems();
}

void ArticleModuleActivity::loop() {
  if (state == State::LOADING) {
    // First-tick: render "Loading..." before the (blocking) network call.
    requestUpdateAndWait();
    syncArticles();
    state = State::LIST;
    requestUpdate();
    return;
  }

  UiListActivity::loop();
}

int ArticleModuleActivity::listCount() const { return state == State::LIST ? static_cast<int>(articles.size()) : 0; }

void ArticleModuleActivity::rebuildRowItems() {
  rowItems_.clear();
  rowItems_.reserve(articles.size());
  for (const auto& a : articles) {
    fui::ListItem item;
    item.label = a.title.empty() ? tr(STR_ARTICLE_UNTITLED) : a.title.c_str();
    if (!a.author.empty()) item.subtitle = a.author.c_str();
    rowItems_.push_back(item);
  }
}

void ArticleModuleActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (state == State::LOADING) {
    screen.centeredText(tr(STR_ARTICLE_LOADING), screen.theme().bodyText);
    return;
  }

  if (articles.empty()) {
    const char* msg =
        noWifi ? tr(STR_ARTICLE_NO_WIFI) : syncFailed ? tr(STR_ARTICLE_SYNC_FAILED) : tr(STR_ARTICLE_EMPTY);
    screen.centeredText(msg, screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

void ArticleModuleActivity::activateIndex(int index) {
  if (index < 0 || index >= static_cast<int>(articles.size())) return;
  startActivityForResult(std::make_unique<ArticleDetailActivity>(renderer, mappedInput, articles[index]),
                         [this, index](const ActivityResult& result) { onDetailClosed(index, result); });
}

void ArticleModuleActivity::onDetailClosed(int index, const ActivityResult& result) {
  if (!result.isCancelled) {
    const auto* detail = std::get_if<ArticleDetailResult>(&result.data);
    if (detail && detail->archived && index >= 0 && index < static_cast<int>(articles.size())) {
      articles.erase(articles.begin() + index);
      rebuildRowItems();
      if (nav.selected >= static_cast<int>(articles.size())) {
        nav.selected = std::max(0, static_cast<int>(articles.size()) - 1);
      }
    }
  }
  requestUpdate();
}

const char* ArticleModuleActivity::headerTitle() const { return tr(STR_ARTICLE_MODULE_TITLE); }

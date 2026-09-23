#include "ArticleModuleActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <variant>

#include "ArticleDetailActivity.h"
#include "CrossPointSettings.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/weather/WeatherModuleActivity.h"
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
    // Piggyback weather's hourly refresh here, not on the WiFi connect path:
    // by now this activity's own Readwise TLS traffic is done and its
    // buffers are freed, so this doesn't stack a second handshake on top of
    // the first and risk a heap-fragmentation abort. No-op when the cache is
    // still fresh (SLO-21).
    refreshWeatherIfWifiConnected();
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void ArticleModuleActivity::promptForToken() {
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ARTICLE_TOKEN_PROMPT),
                                                                 "", 63, InputType::Password),
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
  // ensureWifiConnected()'s callback fires as soon as WiFi.status() first
  // reports WL_CONNECTED, which can be momentarily ahead of the connection
  // actually settling (DHCP/DNS) — most visible right after a silent restart
  // (SLO-15's read flow), where there's been no time for anything else to
  // run in between. Give it a brief grace window to catch up before treating
  // a not-yet-connected read as a genuine drop.
  constexpr uint32_t WIFI_SETTLE_TIMEOUT_MS = 1500;
  const uint32_t settleDeadline = millis() + WIFI_SETTLE_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && millis() < settleDeadline) {
    delay(50);
  }
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
  cacheSyncResultForHomeScreen();
}

void ArticleModuleActivity::cacheSyncResultForHomeScreen() {
  const uint8_t count =
      std::min(static_cast<uint8_t>(articles.size()), CrossPointSettings::ARTICLES_CACHED_TITLE_COUNT);
  for (uint8_t i = 0; i < count; i++) {
    const char* title = articles[i].title.empty() ? tr(STR_ARTICLE_UNTITLED) : articles[i].title.c_str();
    strncpy(SETTINGS.articlesTitles[i], title, sizeof(SETTINGS.articlesTitles[0]) - 1);
    SETTINGS.articlesTitles[i][sizeof(SETTINGS.articlesTitles[0]) - 1] = '\0';
    strncpy(SETTINGS.articlesIds[i], articles[i].id.c_str(), sizeof(SETTINGS.articlesIds[0]) - 1);
    SETTINGS.articlesIds[i][sizeof(SETTINGS.articlesIds[0]) - 1] = '\0';
  }
  SETTINGS.articlesCachedTitleCount = count;
  SETTINGS.articlesLastSyncUnix = static_cast<uint32_t>(time(nullptr));
  SETTINGS.saveToFile();
}

void ArticleModuleActivity::loop() {
  if (state == State::CONNECTING) {
    // Waiting on ensureWifiConnected()'s callback (see the State comment in
    // the header) — nothing to do yet.
    return;
  }
  if (state == State::LOADING) {
    // First-tick: render "Loading..." before the (blocking) network call.
    requestUpdateAndWait();
    syncArticles();
    state = State::LIST;
    requestUpdate();
    openPendingArticleIfPresent();
    return;
  }

  UiListActivity::loop();
}

void ArticleModuleActivity::openPendingArticleIfPresent() {
  if (pendingArticleId.empty()) return;
  const std::string id = std::move(pendingArticleId);
  pendingArticleId.clear();
  const bool autoRead = pendingArticleAutoRead;
  pendingArticleAutoRead = false;

  const auto it =
      std::find_if(articles.begin(), articles.end(), [&id](const ReadwiseArticle& a) { return a.id == id; });
  if (it != articles.end()) {
    activateIndexWithAutoRead(static_cast<int>(it - articles.begin()), autoRead);
  }
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

  if (state == State::CONNECTING || state == State::LOADING) {
    screen.centeredText(tr(STR_ARTICLE_LOADING), screen.theme().bodyText);
    return;
  }

  if (articles.empty()) {
    const char* msg = noWifi       ? tr(STR_ARTICLE_NO_WIFI)
                      : syncFailed ? tr(STR_ARTICLE_SYNC_FAILED)
                                   : tr(STR_ARTICLE_EMPTY);
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

void ArticleModuleActivity::activateIndex(int index) { activateIndexWithAutoRead(index, false); }

void ArticleModuleActivity::activateIndexWithAutoRead(int index, bool autoRead) {
  if (index < 0 || index >= static_cast<int>(articles.size())) return;
  startActivityForResult(std::make_unique<ArticleDetailActivity>(renderer, mappedInput, articles[index], autoRead),
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

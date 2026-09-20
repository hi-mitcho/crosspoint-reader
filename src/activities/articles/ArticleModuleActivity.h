#pragma once
#include <functional>
#include <string>
#include <vector>

#include "activities/ActivityResult.h"
#include "activities/UiListActivity.h"
#include "network/ReadwiseClient.h"

// Article Module (SLO-6): syncs the Readwise Reader "later" queue and lists
// it. Manual/on-demand only — syncs on entry, no periodic polling. Selecting
// a row opens ArticleDetailActivity for metadata + archive; full HTML
// rendering is a separate follow-up (see ArticleDetailActivity).
//
// WiFi: brings up its own connection via WifiSelectionActivity when needed
// and restarts on exit if it did (see WeatherModuleActivity's header comment
// for the full rationale — Settings > Network never leaves a connection for
// a later Activity to inherit).
class ArticleModuleActivity final : public UiListActivity {
 public:
  explicit ArticleModuleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("ArticleModule", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

 private:
  enum class State { LOADING, LIST };
  State state = State::LOADING;
  bool noWifi = false;
  bool syncFailed = false;
  bool shouldTearDownWifiOnExit = false;
  std::vector<ReadwiseArticle> articles;
  std::vector<freeink::ui::ListItem> rowItems_;

  void promptForToken();
  void onTokenEntered(const ActivityResult& result);
  void ensureWifiConnected(std::function<void()> onConnected);
  void beginSync();
  void syncArticles();
  void rebuildRowItems();
  void onDetailClosed(int index, const ActivityResult& result);
};

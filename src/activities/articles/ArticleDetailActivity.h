#pragma once
#include <utility>

#include "activities/Activity.h"
#include "network/ReadwiseClient.h"

// Metadata view for one Readwise Reader article (SLO-6), with Confirm now
// opening the full article body (SLO-15) via ArticleReaderActivity: fetches
// html_content on demand (not cached — see ArticleReaderActivity's header
// comment), then hands it to the EPUB chapter parser / plain-text fallback.
// Archive is a secondary action (Right button) so it doesn't collide with
// Confirm's new meaning.
//
// Reading always goes through silentRestartToArticleRead() first rather than
// fetching directly: by the time this screen exists, the article list sync
// has already run WiFi/TLS at least once this boot, which reliably
// fragments this device's heap (see ArticleModuleActivity::onExit()'s own
// restart-after-WiFi comment) badly enough that the fetch or the subsequent
// HTML layout can fail outright. Rebooting first makes both run on a clean
// heap. autoRead skips straight to that fetch (state = LOADING) instead of
// showing the summary and waiting for Confirm — set when this activity is
// reconstructed on the far side of that reboot (see main.cpp's
// SILENT_REBOOT_TARGET_ARTICLE_READ routing).
class ArticleDetailActivity final : public Activity {
 public:
  ArticleDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, ReadwiseArticle article,
                        bool autoRead = false)
      : Activity("ArticleDetail", renderer, mappedInput), article(std::move(article)), autoRead(autoRead) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State { VIEWING, ARCHIVING, LOADING };
  void openReader();
  ReadwiseArticle article;
  const bool autoRead;
  State state = State::VIEWING;
  bool archiveFailed = false;
  bool readFailed = false;
};

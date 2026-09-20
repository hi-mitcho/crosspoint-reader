#pragma once
#include <utility>

#include "activities/Activity.h"
#include "network/ReadwiseClient.h"

// Metadata view for one Readwise Reader article (SLO-6 first slice). Shows
// title/author/word count/summary and offers Archive; does not render the
// article's full HTML content — that's a separate, larger follow-up closer
// to EpubReaderActivity's rendering pipeline.
class ArticleDetailActivity final : public Activity {
 public:
  ArticleDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, ReadwiseArticle article)
      : Activity("ArticleDetail", renderer, mappedInput), article(std::move(article)) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State { VIEWING, ARCHIVING };
  ReadwiseArticle article;
  State state = State::VIEWING;
  bool archiveFailed = false;
};

#include "ReadwiseClient.h"

#include <ArduinoJson.h>
#include <HalMemory.h>
#include <Logging.h>
#include <SecureHttpClient.h>

#include <cstdio>
#include <utility>

#include "CrossPointSettings.h"
#include "ReadwiseHtmlExtractor.h"

int ReadwiseClient::lastHttpCode = 0;

namespace {

// wolfSSL uses the default allocator, which can use PSRAM on supported builds.
// Keep a free-space floor and room for a full TLS record when the server does
// not negotiate our smaller record limit. These are preflight margins, not a
// guarantee that a handshake will fit.
constexpr uint32_t MIN_FREE_FOR_TLS = 35000;
constexpr uint32_t MIN_BLOCK_FOR_TLS = 20000;

// Runaway-safety net for fetchHtmlContent's streaming extraction: checked
// every chunk while the article body accumulates in RAM (see the comment at
// its call site). MIN_FREE_DURING_EXTRACTION is lower than MIN_FREE_FOR_TLS
// because it's measured mid-connection, after wolfSSL's own session state is
// already live and counted against free heap, not as a preflight check.
constexpr size_t MAX_ARTICLE_HTML_BYTES = 128 * 1024;
constexpr size_t MIN_FREE_DURING_EXTRACTION = 15000;

bool insufficientHeap() {
  const auto heap = HalMemory::getDefaultHeap();
  if (heap.freeBytes < MIN_FREE_FOR_TLS || heap.largestBlockBytes < MIN_BLOCK_FOR_TLS) {
    LOG_ERR("READWISE",
            "Insufficient allocatable heap for TLS handshake: %zu bytes free (need %u), %zu max alloc (need %u)",
            heap.freeBytes, MIN_FREE_FOR_TLS, heap.largestBlockBytes, MIN_BLOCK_FOR_TLS);
    return true;
  }
  return false;
}

void applyAuthHeader(freeink::SecureHttpClient& http) {
  http.addHeader("Authorization", std::string("Token ") + SETTINGS.articleModuleToken);
}

}  // namespace

ReadwiseClient::Error ReadwiseClient::listUnarchived(std::vector<ReadwiseArticle>& outArticles, int limit) {
  lastHttpCode = 0;
  outArticles.clear();

  if (SETTINGS.articleModuleToken[0] == '\0') return NO_TOKEN;
  if (insufficientHeap()) return LOW_MEMORY;

  char url[128];
  snprintf(url, sizeof(url), "https://readwise.io/api/v3/list/?limit=%d", limit);

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("READWISE", "Bad URL: %s", url);
    return NETWORK_ERROR;
  }
  applyAuthHeader(http);
  const int httpCode = http.GET();
  const std::string body = http.getString();
  http.end();
  lastHttpCode = httpCode;

  if (httpCode <= 0) return NETWORK_ERROR;
  if (httpCode == 401 || httpCode == 403) return AUTH_FAILED;
  if (httpCode < 200 || httpCode >= 300) return SERVER_ERROR;

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    LOG_ERR("READWISE", "List response JSON parse failed");
    return SERVER_ERROR;
  }

  for (JsonObject item : doc["results"].as<JsonArray>()) {
    const std::string location = item["location"] | "";
    if (location == "archive" || location == "feed") continue;  // not a reading-queue item

    const std::string category = item["category"] | "";
    if (category == "highlight" || category == "note") continue;  // annotations, not readable articles

    ReadwiseArticle article;
    article.id = item["id"] | "";
    if (article.id.empty()) continue;
    article.title = item["title"] | "";
    article.author = item["author"] | "";
    article.summary = item["summary"] | "";
    article.location = location;
    article.wordCount = item["word_count"] | 0;
    outArticles.push_back(std::move(article));
  }

  return OK;
}

ReadwiseClient::Error ReadwiseClient::archive(const std::string& documentId) {
  lastHttpCode = 0;

  if (SETTINGS.articleModuleToken[0] == '\0') return NO_TOKEN;
  if (insufficientHeap()) return LOW_MEMORY;

  const std::string url = "https://readwise.io/api/v3/update/" + documentId + "/";

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("READWISE", "Bad URL: %s", url.c_str());
    return NETWORK_ERROR;
  }
  applyAuthHeader(http);
  http.addHeader("Content-Type", "application/json");
  const int httpCode = http.sendRequest("PATCH", std::string("{\"location\":\"archive\"}"));
  http.end();
  lastHttpCode = httpCode;

  if (httpCode <= 0) return NETWORK_ERROR;
  if (httpCode == 401 || httpCode == 403) return AUTH_FAILED;
  if (httpCode < 200 || httpCode >= 300) return SERVER_ERROR;
  return OK;
}

ReadwiseClient::Error ReadwiseClient::fetchHtmlContent(const std::string& documentId, std::string& outHtml) {
  lastHttpCode = 0;
  outHtml.clear();

  if (SETTINGS.articleModuleToken[0] == '\0') return NO_TOKEN;
  if (insufficientHeap()) return LOW_MEMORY;

  char url[192];
  snprintf(url, sizeof(url), "https://readwise.io/api/v3/list/?id=%s&withHtmlContent=true", documentId.c_str());

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("READWISE", "Bad URL: %s", url);
    return NETWORK_ERROR;
  }
  applyAuthHeader(http);

  // Scans the (still JSON-escaped) response as it arrives for the
  // "html_content" field and unescapes it straight into outHtml, so the raw
  // body — which can be far larger than any other field this client
  // handles — never needs to be buffered whole. Aborting the transfer once
  // the value is captured skips whatever JSON follows it in the response.
  //
  // The extractor's internal std::string still grows unbounded as chunks
  // arrive, though: with -fno-exceptions, a failed allocation calls abort()
  // instead of throwing (see firmware/CLAUDE.md's `new` rule — it applies to
  // STL container growth too, not just explicit `new`). Both caps below are
  // checked every chunk so a huge or pathological article degrades to
  // LOW_MEMORY instead of crashing the device.
  bool tooLarge = false;
  ReadwiseHtmlExtractor extractor;
  const int httpCode = http.GET([&extractor, &tooLarge](const uint8_t* data, size_t len) {
    extractor.feed(reinterpret_cast<const char*>(data), len);
    if (extractor.done()) return false;
    if (extractor.size() > MAX_ARTICLE_HTML_BYTES ||
        HalMemory::getDefaultHeap().freeBytes < MIN_FREE_DURING_EXTRACTION) {
      tooLarge = true;
      return false;
    }
    return true;
  });
  http.end();
  lastHttpCode = httpCode;

  if (tooLarge) {
    LOG_ERR("READWISE", "Article too large or low memory while fetching id %s (%zu bytes captured)", documentId.c_str(),
            extractor.size());
    return LOW_MEMORY;
  }
  if (httpCode <= 0) return NETWORK_ERROR;
  if (httpCode == 401 || httpCode == 403) return AUTH_FAILED;
  if (httpCode < 200 || httpCode >= 300) return SERVER_ERROR;
  if (!extractor.done()) {
    LOG_ERR("READWISE", "html_content missing from response for id %s", documentId.c_str());
    return SERVER_ERROR;
  }

  outHtml = extractor.takeHtml();
  return OK;
}

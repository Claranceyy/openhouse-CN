#pragma once

#include <cstddef>
#include <string>
#include <string_view>

struct ImFontAtlas;
struct ImFontConfig;

namespace lh::i18n {

// Temporarily bypasses translation on the current thread (used inside ImGui hooks
// to prevent double-translation and to protect InputText user buffers).
struct ScopedBypass {
  ScopedBypass();
  ~ScopedBypass();
  ScopedBypass(const ScopedBypass&) = delete;
  ScopedBypass& operator=(const ScopedBypass&) = delete;
};

bool InBypass();

// Translates a text span [text, text_end). If text_end is nullptr, text is treated
// as null-terminated. Returns a view valid for immediate consumption on the calling
// thread; if no translation matches, returns the original span unchanged.
std::string_view TranslateSpan(const char* text, const char* text_end = nullptr);

// Translates a printf-style format string (only if the translated format preserves
// the exact printf specifier signature). Returns the original pointer if unmatched.
const char* TranslateFormat(const char* fmt);

// Convenience helper returning an owning std::string (for native dialogs, etc.).
std::string Translate(std::string_view text);

#ifdef _WIN32
std::wstring Utf8ToWide(std::string_view utf8);
#endif

// Merges a cached system CJK fallback font into the most recently added font in atlas.
void MergeCjkFallbackFont(ImFontAtlas& atlas, const ImFontConfig* base_cfg = nullptr);

// Unit-test helpers (not used at runtime).
void LoadJsonForTest(std::string_view json_text, bool clear_existing = true);
void ResetForTest();

}  // namespace lh::i18n

#include "i18n.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#if __has_include("lh/i18n_builtin_zh_cn.h")
#include "lh/i18n_builtin_zh_cn.h"
#define LH_I18N_HAS_BUILTIN 1
#else
#define LH_I18N_HAS_BUILTIN 0
#endif

namespace lh::i18n {
namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

thread_local int g_bypass_depth = 0;

struct TransparentHash {
  using is_transparent = void;
  size_t operator()(std::string_view sv) const noexcept { return std::hash<std::string_view>{}(sv); }
  size_t operator()(const std::string& s) const noexcept { return std::hash<std::string_view>{}(s); }
  size_t operator()(const char* s) const noexcept { return std::hash<std::string_view>{}(s); }
};

struct PatternPart {
  bool is_slot = false;
  int slot_index = 0;     // 1-based slot number
  bool recursive = false; // true for {@1}
  std::string literal;
};

struct PatternRule {
  std::vector<std::string> literals; // literals.size() == match_slots.size() + 1
  std::vector<int> match_slots;      // 1-based slot IDs in match order
  std::vector<PatternPart> replace_parts;
  int max_slot = 0;
  size_t min_len = 0;
};

// Extracts a normalized printf argument type signature so format translations can
// never cause mismatched-type UB in vsnprintf.
bool ExtractPrintfSignature(std::string_view fmt, std::string& out_sig) {
  out_sig.clear();
  for (size_t i = 0; i < fmt.size(); ++i) {
    if (fmt[i] != '%') continue;
    ++i;
    if (i >= fmt.size()) return false;
    if (fmt[i] == '%') continue;  // escaped %%

    // Flags
    while (i < fmt.size() && (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#' || fmt[i] == '0')) ++i;
    // Width
    if (i < fmt.size() && fmt[i] == '*') {
      out_sig.push_back('W');  // int width arg
      ++i;
    } else {
      while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i]))) ++i;
    }
    // Precision
    if (i < fmt.size() && fmt[i] == '.') {
      ++i;
      if (i < fmt.size() && fmt[i] == '*') {
        out_sig.push_back('P');  // int precision arg
        ++i;
      } else {
        while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i]))) ++i;
      }
    }
    // Length modifier
    char len_mod = ' ';
    if (i + 1 < fmt.size() && fmt[i] == 'h' && fmt[i + 1] == 'h') { len_mod = 'H'; i += 2; }
    else if (i + 1 < fmt.size() && fmt[i] == 'l' && fmt[i + 1] == 'l') { len_mod = 'L'; i += 2; }
    else if (i < fmt.size() && (fmt[i] == 'h' || fmt[i] == 'l' || fmt[i] == 'j' || fmt[i] == 'z' || fmt[i] == 't' || fmt[i] == 'L')) {
      len_mod = fmt[i];
      ++i;
    }
    if (i >= fmt.size()) return false;
    const char spec = fmt[i];
    // Canonicalize equivalent integer/float specifiers
    char kind = spec;
    if (spec == 'i' || spec == 'd') kind = 'd';
    else if (spec == 'u' || spec == 'o' || spec == 'x' || spec == 'X') kind = 'u';
    else if (spec == 'f' || spec == 'F' || spec == 'e' || spec == 'E' || spec == 'g' || spec == 'G' || spec == 'a' || spec == 'A') kind = 'f';
    else if (spec == 's' || spec == 'c' || spec == 'p') kind = spec;
    else return false;  // reject %n or unknown specifiers

    out_sig.push_back(len_mod);
    out_sig.push_back(kind);
    out_sig.push_back(';');
  }
  return true;
}

bool ParsePattern(std::string_view match_str, std::string_view replace_str, PatternRule& rule) {
  rule = PatternRule{};
  size_t pos = 0;
  std::string current_lit;
  while (pos < match_str.size()) {
    if (match_str[pos] == '{') {
      size_t end = match_str.find('}', pos + 1);
      if (end != std::string_view::npos && end > pos + 1) {
        size_t num_start = pos + 1;
        if (match_str[num_start] == '@') ++num_start;  // allow {@1} in match too
        bool digits = num_start < end;
        int id = 0;
        for (size_t k = num_start; k < end; ++k) {
          if (!std::isdigit(static_cast<unsigned char>(match_str[k]))) { digits = false; break; }
          id = id * 10 + (match_str[k] - '0');
        }
        if (digits && id >= 1 && id <= 9) {
          rule.literals.push_back(std::move(current_lit));
          current_lit.clear();
          rule.match_slots.push_back(id);
          rule.max_slot = std::max(rule.max_slot, id);
          pos = end + 1;
          continue;
        }
      }
    }
    current_lit.push_back(match_str[pos++]);
  }
  rule.literals.push_back(std::move(current_lit));
  if (rule.match_slots.empty()) return false;

  rule.min_len = 0;
  for (const auto& lit : rule.literals) rule.min_len += lit.size();
  // Require at least one non-empty literal so a bare "{1}" rule can't swallow everything.
  if (rule.min_len == 0) return false;

  // Parse replace template
  pos = 0;
  current_lit.clear();
  while (pos < replace_str.size()) {
    if (replace_str[pos] == '{') {
      size_t end = replace_str.find('}', pos + 1);
      if (end != std::string_view::npos && end > pos + 1) {
        size_t num_start = pos + 1;
        bool rec = false;
        if (replace_str[num_start] == '@') { rec = true; ++num_start; }
        bool digits = num_start < end;
        int id = 0;
        for (size_t k = num_start; k < end; ++k) {
          if (!std::isdigit(static_cast<unsigned char>(replace_str[k]))) { digits = false; break; }
          id = id * 10 + (replace_str[k] - '0');
        }
        if (digits && id >= 1 && id <= rule.max_slot) {
          if (!current_lit.empty()) {
            PatternPart lp;
            lp.is_slot = false;
            lp.literal = std::move(current_lit);
            current_lit.clear();
            rule.replace_parts.push_back(std::move(lp));
          }
          PatternPart sp;
          sp.is_slot = true;
          sp.slot_index = id;
          sp.recursive = rec;
          rule.replace_parts.push_back(std::move(sp));
          pos = end + 1;
          continue;
        }
      }
    }
    current_lit.push_back(replace_str[pos++]);
  }
  if (!current_lit.empty()) {
    PatternPart lp;
    lp.is_slot = false;
    lp.literal = std::move(current_lit);
    rule.replace_parts.push_back(std::move(lp));
  }
  return true;
}

bool MatchRule(const PatternRule& rule, std::string_view text, std::array<std::string_view, 10>& captures) {
  if (text.size() < rule.min_len) return false;
  const std::string& first = rule.literals.front();
  if (!first.empty() && text.rfind(first, 0) != 0) return false;
  const std::string& last = rule.literals.back();
  if (!last.empty()) {
    if (text.size() < last.size() || text.substr(text.size() - last.size()) != last) return false;
  }

  const size_t limit = text.size() - last.size();
  size_t cursor = first.size();
  const size_t nslots = rule.match_slots.size();
  for (size_t i = 0; i < nslots; ++i) {
    const int slot_id = rule.match_slots[i];
    if (i + 1 == nslots) {
      if (cursor > limit) return false;
      captures[slot_id] = text.substr(cursor, limit - cursor);
      return true;
    }
    const std::string& sep = rule.literals[i + 1];
    if (sep.empty()) return false;
    const size_t found = text.find(sep, cursor);
    if (found == std::string_view::npos || found + sep.size() > limit) return false;
    captures[slot_id] = text.substr(cursor, found - cursor);
    cursor = found + sep.size();
  }
  return false;
}

bool ContainsCjkUtf8(std::string_view sv) {
  for (size_t i = 0; i + 2 < sv.size(); ++i) {
    const unsigned char b0 = static_cast<unsigned char>(sv[i]);
    if (b0 >= 0xE4 && b0 <= 0xE9) {
      const unsigned char b1 = static_cast<unsigned char>(sv[i + 1]);
      const unsigned char b2 = static_cast<unsigned char>(sv[i + 2]);
      if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) return true;
    }
  }
  return false;
}

bool HasAsciiLetter(std::string_view sv) {
  for (char c : sv) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return true;
  }
  return false;
}

bool LooksLikeLogOrPath(std::string_view sv) {
  if (sv.size() < 2 || sv.size() > 240) return true;
  if (sv.rfind("http://", 0) == 0 || sv.rfind("https://", 0) == 0) return true;
  if (sv.rfind("LHR-", 0) == 0 || sv.rfind("OHDG-", 0) == 0 || sv.rfind("LHB-", 0) == 0) return true;
  if (sv.size() > 3 && std::isalpha(static_cast<unsigned char>(sv[0])) && sv[1] == ':' && (sv[2] == '\\' || sv[2] == '/')) return true;
  return false;
}

fs::path ExeDir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 4]{};
  const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
  if (n > 0 && n < std::size(buf)) return fs::path(std::wstring(buf, n)).parent_path();
  return {};
#else
  std::error_code ec;
  const fs::path p = fs::read_symlink("/proc/self/exe", ec);
  return ec ? fs::path{} : p.parent_path();
#endif
}

fs::path ConfigDir() {
#ifdef _WIN32
  if (const wchar_t* appdata = _wgetenv(L"APPDATA"); appdata && *appdata) return fs::path(appdata) / L"lh";
  return {};
#else
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return fs::path(xdg) / "lh";
  if (const char* home = std::getenv("HOME"); home && *home) return fs::path(home) / ".config" / "lh";
  return {};
#endif
}

struct Engine {
  bool disabled = false;
  bool dump_missing = false;
  std::unordered_map<std::string, std::string, TransparentHash, std::equal_to<>> exact;
  std::unordered_map<std::string, std::string, TransparentHash, std::equal_to<>> format;
  std::vector<PatternRule> patterns;
  std::array<std::vector<size_t>, 256> pattern_buckets;
  std::vector<size_t> wildcard_patterns;

  std::mutex missing_mutex;
  std::set<std::string> missing_exact;
  std::set<std::string> missing_format;

  Engine() { LoadDefault(); }

  ~Engine() {
    if (dump_missing) DumpMissingToDisk();
  }

  void Clear() {
    exact.clear();
    format.clear();
    patterns.clear();
    for (auto& b : pattern_buckets) b.clear();
    wildcard_patterns.clear();
  }

  void RebuildPatternBuckets() {
    for (auto& b : pattern_buckets) b.clear();
    wildcard_patterns.clear();
    for (size_t i = 0; i < patterns.size(); ++i) {
      if (!patterns[i].literals.empty() && !patterns[i].literals[0].empty()) {
        const unsigned char first = static_cast<unsigned char>(patterns[i].literals[0][0]);
        pattern_buckets[first].push_back(i);
      } else {
        wildcard_patterns.push_back(i);
      }
    }
  }

  void MergeJson(std::string_view text) {
    if (text.empty()) return;
    json j = json::parse(text.begin(), text.end(), nullptr, false);
    if (!j.is_object()) return;

    if (const auto it = j.find("exact"); it != j.end() && it->is_object()) {
      for (const auto& [k, v] : it->items()) {
        if (v.is_string()) {
          const std::string val = v.get<std::string>();
          if (!val.empty()) exact[k] = val;
        }
      }
    }
    if (const auto it = j.find("format"); it != j.end() && it->is_object()) {
      for (const auto& [k, v] : it->items()) {
        if (!v.is_string()) continue;
        const std::string val = v.get<std::string>();
        if (val.empty()) continue;
        std::string sig_src, sig_dst;
        if (ExtractPrintfSignature(k, sig_src) && ExtractPrintfSignature(val, sig_dst) && sig_src == sig_dst) {
          format[k] = val;
        }
      }
    }
    if (const auto it = j.find("patterns"); it != j.end() && it->is_array()) {
      for (const auto& entry : *it) {
        if (!entry.is_object()) continue;
        const auto m = entry.find("match");
        const auto r = entry.find("replace");
        if (m == entry.end() || r == entry.end() || !m->is_string() || !r->is_string()) continue;
        PatternRule rule;
        if (ParsePattern(m->get<std::string>(), r->get<std::string>(), rule)) {
          patterns.push_back(std::move(rule));
        }
      }
      RebuildPatternBuckets();
    }
  }

  void MergeFileIfExists(const fs::path& p) {
    if (p.empty()) return;
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return;
    std::ifstream in(p, std::ios::binary);
    if (!in) return;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    MergeJson(content);
  }

  void LoadDefault() {
    Clear();
    if (const char* dis = std::getenv("OPENHOUSE_I18N_DISABLE"); dis && dis[0] == '1') {
      disabled = true;
      return;
    }
    if (const char* dmp = std::getenv("OPENHOUSE_I18N_DUMP"); dmp && dmp[0] == '1') {
      dump_missing = true;
    }

#if LH_I18N_HAS_BUILTIN
    MergeJson(std::string_view(reinterpret_cast<const char*>(kBuiltinZhCnJson), kBuiltinZhCnJsonSize));
#endif

    // External overlay dictionary (allows hot-editing translations without recompiling).
    const fs::path exe = ExeDir();
    const fs::path cfg = ConfigDir();
    MergeFileIfExists(cfg / "zh_CN.json");
    if (!exe.empty()) {
      MergeFileIfExists(exe.parent_path() / "zh_CN.json");
      MergeFileIfExists(exe / "zh_CN.json");
    }
    if (const char* env_file = std::getenv("OPENHOUSE_I18N_FILE"); env_file && *env_file) {
      MergeFileIfExists(fs::path(env_file));
    }
  }

  void RecordMissing(std::string_view sv, bool is_format) {
    if (!dump_missing || LooksLikeLogOrPath(sv)) return;
    std::lock_guard<std::mutex> lock(missing_mutex);
    if (is_format) missing_format.emplace(sv);
    else missing_exact.emplace(sv);
  }

  void DumpMissingToDisk() {
    std::lock_guard<std::mutex> lock(missing_mutex);
    if (missing_exact.empty() && missing_format.empty()) return;
    fs::path dir = ConfigDir();
    if (dir.empty()) dir = ExeDir();
    if (dir.empty()) return;
    std::error_code ec;
    fs::create_directories(dir, ec);
    json out;
    json ex = json::object();
    for (const auto& s : missing_exact) ex[s] = "";
    json fm = json::object();
    for (const auto& s : missing_format) fm[s] = "";
    out["missing_exact"] = std::move(ex);
    out["missing_format"] = std::move(fm);
    std::ofstream f(dir / "i18n_missing.json", std::ios::binary);
    if (f) f << out.dump(2) << "\n";
  }

  bool TryMatchPatterns(std::string_view sv, std::string& out, int depth) const {
    if (sv.empty() || patterns.empty()) return false;
    std::array<std::string_view, 10> caps{};
    auto try_idx = [&](size_t idx) -> bool {
      const PatternRule& rule = patterns[idx];
      if (!MatchRule(rule, sv, caps)) return false;
      out.clear();
      out.reserve(sv.size() + 32);
      for (const auto& part : rule.replace_parts) {
        if (!part.is_slot) {
          out.append(part.literal);
        } else {
          const std::string_view cap = caps[part.slot_index];
          if (part.recursive && depth < 3) {
            std::string sub;
            if (TranslateDynamic(cap, sub, depth + 1)) out.append(sub);
            else out.append(cap);
          } else {
            out.append(cap);
          }
        }
      }
      return true;
    };

    const unsigned char first = static_cast<unsigned char>(sv[0]);
    for (size_t idx : pattern_buckets[first]) {
      if (try_idx(idx)) return true;
    }
    for (size_t idx : wildcard_patterns) {
      if (try_idx(idx)) return true;
    }
    return false;
  }

  bool TranslateDynamic(std::string_view sv, std::string& out, int depth) const {
    if (sv.empty() || sv.size() > 4096 || !HasAsciiLetter(sv) || ContainsCjkUtf8(sv)) return false;
    if (const auto it = exact.find(sv); it != exact.end()) {
      out = it->second;
      return true;
    }
    if (TryMatchPatterns(sv, out, depth)) return true;

    // Multi-line fallback: translate line-by-line when whole block didn't match.
    if (depth == 0 && sv.find('\n') != std::string_view::npos) {
      size_t line_count = 0;
      for (char c : sv) if (c == '\n') ++line_count;
      if (line_count > 0 && line_count <= 32) {
        std::string rebuilt;
        rebuilt.reserve(sv.size() + 64);
        bool any_changed = false;
        size_t pos = 0;
        while (pos <= sv.size()) {
          const size_t nl = sv.find('\n', pos);
          const std::string_view line = (nl == std::string_view::npos) ? sv.substr(pos) : sv.substr(pos, nl - pos);
          std::string translated_line;
          if (!line.empty() && TranslateDynamic(line, translated_line, depth + 1)) {
            rebuilt.append(translated_line);
            any_changed = true;
          } else {
            rebuilt.append(line);
          }
          if (nl == std::string_view::npos) break;
          rebuilt.push_back('\n');
          pos = nl + 1;
        }
        if (any_changed) {
          out = std::move(rebuilt);
          return true;
        }
      }
    }
    return false;
  }
};

Engine& GetEngine() {
  static Engine engine;
  return engine;
}

// Per-thread dynamic result ring buffer and small cache so TranslateSpan returns
// a safe, fast string_view without heap churn every ImGui frame.
struct ThreadCache {
  static constexpr size_t kRingSize = 16;
  static constexpr size_t kMaxCache = 2048;
  std::array<std::string, kRingSize> ring;
  size_t ring_idx = 0;
  uint64_t epoch = 0;
  std::unordered_map<std::string, std::string, TransparentHash, std::equal_to<>> hits;
  std::unordered_map<std::string, bool, TransparentHash, std::equal_to<>> misses;

  std::string_view StoreInRing(std::string s) {
    std::string& slot = ring[ring_idx++ & (kRingSize - 1)];
    slot = std::move(s);
    return slot;
  }
};

uint64_t g_test_epoch = 1;

ThreadCache& GetThreadCache() {
  thread_local ThreadCache cache;
  if (cache.epoch != g_test_epoch) {
    cache.hits.clear();
    cache.misses.clear();
    cache.epoch = g_test_epoch;
  }
  return cache;
}

}  // namespace

ScopedBypass::ScopedBypass() { ++g_bypass_depth; }
ScopedBypass::~ScopedBypass() { --g_bypass_depth; }
bool InBypass() { return g_bypass_depth > 0; }

std::string_view TranslateSpan(const char* text, const char* text_end) {
  if (!text) return {};
  const size_t len = text_end ? static_cast<size_t>(text_end - text) : std::strlen(text);
  const std::string_view sv(text, len);
  if (len == 0 || g_bypass_depth > 0) return sv;

  Engine& eng = GetEngine();
  if (eng.disabled || len > 4096 || !HasAsciiLetter(sv) || ContainsCjkUtf8(sv)) return sv;

  // Fast path: direct O(1) exact dictionary hit, zero allocation.
  if (const auto it = eng.exact.find(sv); it != eng.exact.end()) {
    return it->second;
  }

  // Dynamic path (patterns / multiline) with per-thread cache.
  ThreadCache& cache = GetThreadCache();
  if (const auto it = cache.hits.find(sv); it != cache.hits.end()) {
    return it->second;
  }
  if (cache.misses.find(sv) != cache.misses.end()) {
    return sv;
  }

  std::string translated;
  if (eng.TranslateDynamic(sv, translated, 0)) {
    if (cache.hits.size() >= ThreadCache::kMaxCache) {
      cache.hits.clear();
      return cache.StoreInRing(std::move(translated));
    }
    auto [it, _] = cache.hits.emplace(std::string(sv), std::move(translated));
    return it->second;
  }

  if (cache.misses.size() >= ThreadCache::kMaxCache) cache.misses.clear();
  cache.misses.emplace(std::string(sv), true);
  eng.RecordMissing(sv, false);
  return sv;
}

const char* TranslateFormat(const char* fmt) {
  if (!fmt || !fmt[0] || g_bypass_depth > 0) return fmt;
  Engine& eng = GetEngine();
  if (eng.disabled) return fmt;
  const std::string_view sv(fmt);
  if (!HasAsciiLetter(sv) || ContainsCjkUtf8(sv)) return fmt;
  if (const auto it = eng.format.find(sv); it != eng.format.end()) {
    return it->second.c_str();
  }
  if (sv.find('%') != std::string_view::npos) {
    eng.RecordMissing(sv, true);
  }
  return fmt;
}

std::string Translate(std::string_view text) {
  const std::string_view out = TranslateSpan(text.data(), text.data() + text.size());
  return std::string(out);
}

#ifdef _WIN32
std::wstring Utf8ToWide(std::string_view utf8) {
  if (utf8.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring wide(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), n);
  return wide;
}
#endif

void LoadJsonForTest(std::string_view json_text, bool clear_existing) {
  Engine& eng = GetEngine();
  eng.disabled = false;
  if (clear_existing) eng.Clear();
  eng.MergeJson(json_text);
  ++g_test_epoch;
}

void ResetForTest() {
  GetEngine().LoadDefault();
  ++g_test_epoch;
}

}  // namespace lh::i18n

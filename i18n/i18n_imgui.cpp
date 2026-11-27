#include "i18n.hpp"

#include <imgui.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace lh::i18n {
namespace {

namespace fs = std::filesystem;

struct CachedFontData {
  std::vector<unsigned char> bytes;
  bool loaded = false;
};

const CachedFontData& LoadSystemCjkFontOnce() {
  static CachedFontData data;
  static std::once_flag once;
  std::call_once(once, [] {
    std::vector<fs::path> candidates;
    if (const char* env_font = std::getenv("OPENHOUSE_CJK_FONT"); env_font && *env_font) {
      candidates.emplace_back(env_font);
    }
#ifdef _WIN32
    wchar_t win_dir[MAX_PATH]{};
    const UINT n = GetWindowsDirectoryW(win_dir, MAX_PATH);
    const fs::path fonts = (n > 0 && n < MAX_PATH ? fs::path(win_dir) : fs::path(L"C:\\Windows")) / L"Fonts";
    candidates.push_back(fonts / L"msyh.ttc");
    candidates.push_back(fonts / L"simhei.ttf");
    candidates.push_back(fonts / L"msyhbd.ttc");
    candidates.push_back(fonts / L"simsun.ttc");
    candidates.push_back(fonts / L"mingliub.ttc");
#else
    candidates.emplace_back("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
    candidates.emplace_back("/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc");
    candidates.emplace_back("/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc");
    candidates.emplace_back("/usr/share/fonts/google-noto-sans-cjk-vf-fonts/NotoSansCJK-VF.ttc");
    candidates.emplace_back("/usr/share/fonts/truetype/wqy/wqy-microhei.ttc");
    candidates.emplace_back("/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc");
    candidates.emplace_back("/usr/share/fonts/wqy-microhei/wqy-microhei.ttc");
    candidates.emplace_back("/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc");
    candidates.emplace_back("/usr/share/fonts/truetype/arphic/uming.ttc");
#endif
    std::error_code ec;
    for (const auto& path : candidates) {
      if (!fs::is_regular_file(path, ec)) continue;
      std::ifstream in(path, std::ios::binary | std::ios::ate);
      if (!in) continue;
      const auto sz = in.tellg();
      if (sz <= 0 || sz > 64 * 1024 * 1024) continue;
      data.bytes.resize(static_cast<size_t>(sz));
      in.seekg(0);
      if (in.read(reinterpret_cast<char*>(data.bytes.data()), sz)) {
        data.loaded = true;
        return;
      }
      data.bytes.clear();
    }
  });
  return data;
}

}  // namespace

void MergeCjkFallbackFont(ImFontAtlas& atlas, const ImFontConfig* base_cfg) {
  (void)base_cfg;
  const CachedFontData& font = LoadSystemCjkFontOnce();
  if (!font.loaded || font.bytes.empty()) return;

  ImFontConfig merge;
  merge.MergeMode = true;
  merge.FontDataOwnedByAtlas = false;  // Share one cached buffer across default/title/huge fonts
  merge.Flags = ImFontFlags_NoLoadError;
  merge.OversampleH = 2;
  merge.OversampleV = 1;
  merge.ExtraSizeScale = 1.16f;
  atlas.AddFontFromMemoryTTF(const_cast<unsigned char*>(font.bytes.data()),
                             static_cast<int>(font.bytes.size()), 0.0f, &merge);
}

}  // namespace lh::i18n

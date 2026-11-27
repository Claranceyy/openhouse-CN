#include "i18n.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

namespace {

TEST(I18nTest, BuiltinDictionaryLoadsAndTranslatesCoreStrings) {
  lh::i18n::ResetForTest();
  EXPECT_EQ(lh::i18n::Translate("Install Driver"), "安装驱动");
  EXPECT_EQ(lh::i18n::Translate("Base stations"), "基站");
  EXPECT_EQ(lh::i18n::Translate("Overview"), "概览");
  EXPECT_EQ(lh::i18n::Translate("A SteamVR update replaced the openhouse driver."), "SteamVR 更新已覆盖 openhouse 驱动程序。");
}

TEST(I18nTest, ExactAndFormatAndSafetyValidation) {
  lh::i18n::LoadJsonForTest(R"({
    "exact": {
      "Ready": "就绪",
      "Tracking": "追踪中",
      "Hello": "你好"
    },
    "format": {
      "Firmware %04X": "固件 %04X",
      "Bad %d %s": "错误顺序 %s %d",
      "BadCount %d": "参数过多 %d %d"
    },
    "patterns": [
      { "match": "Room: {@1} ({2} / {3} placed)", "replace": "房间：{@1}（已定位 {2} / {3}）" },
      { "match": "Placed ({1} mm)", "replace": "已定位 ({1} mm)" }
    ]
  })");

  // Exact
  EXPECT_EQ(lh::i18n::Translate("Hello"), "你好");

  // Valid format preserved
  const char* ok_fmt = lh::i18n::TranslateFormat("Firmware %04X");
  EXPECT_STREQ(ok_fmt, "固件 %04X");
  char buf[64];
  std::snprintf(buf, sizeof buf, ok_fmt, 0x002C);
  EXPECT_STREQ(buf, "固件 002C");

  // Mismatched printf signatures must be rejected safely
  EXPECT_STREQ(lh::i18n::TranslateFormat("Bad %d %s"), "Bad %d %s");
  EXPECT_STREQ(lh::i18n::TranslateFormat("BadCount %d"), "BadCount %d");

  // Patterns with recursive {@1}
  EXPECT_EQ(lh::i18n::Translate("Room: Ready (2 / 2 placed)"), "房间：就绪（已定位 2 / 2）");
  EXPECT_EQ(lh::i18n::Translate("Placed (1.4 mm)"), "已定位 (1.4 mm)");

  // Multi-line automatic fallback
  EXPECT_EQ(lh::i18n::Translate("Hello\nPlaced (2.5 mm)"), "你好\n已定位 (2.5 mm)");

  // ScopedBypass disables translation cleanly
  {
    lh::i18n::ScopedBypass bypass;
    EXPECT_TRUE(lh::i18n::InBypass());
    EXPECT_EQ(lh::i18n::Translate("Hello"), "Hello");
    EXPECT_STREQ(lh::i18n::TranslateFormat("Firmware %04X"), "Firmware %04X");
  }
  EXPECT_FALSE(lh::i18n::InBypass());
  EXPECT_EQ(lh::i18n::Translate("Hello"), "你好");

  // Untranslated & non-alpha strings pass through untouched
  EXPECT_EQ(lh::i18n::Translate("LHR-12345678"), "LHR-12345678");
  EXPECT_EQ(lh::i18n::Translate("12.3 ms"), "12.3 ms");
  EXPECT_EQ(lh::i18n::Translate("已经是中文"), "已经是中文");
}

TEST(I18nTest, BuiltinPatternsWorkOnRealDynamicStrings) {
  lh::i18n::ResetForTest();
  EXPECT_EQ(lh::i18n::Translate("Room: Learning (1 / 2 placed)"), "房间：学习中（已定位 1 / 2）");
  EXPECT_EQ(lh::i18n::Translate("openhouse v1.0.5 is available (you have v1.0.4)."), "发现新版 openhouse v1.0.5（当前版本 v1.0.4）。");
  EXPECT_EQ(lh::i18n::Translate("Pairing into slot 2  -  45 s left"), "正在配对到槽位 2  -  剩余 45 秒");
  EXPECT_EQ(lh::i18n::Translate("Channel 3 (D7B1AD76)"), "信道 3 (D7B1AD76)");
  EXPECT_EQ(lh::i18n::Translate("4.5 sensors, 12 sweeps"), "4.5 个传感器，12 次扫描");
  EXPECT_EQ(lh::i18n::Translate("Uninstall openhouse?\n\nVR will shut down. These will be closed:\n- Steam\n- SteamVR\n\nYour room and settings are kept."),
            "确定要卸载 openhouse 吗？\n\nVR 将会关闭，以下程序会被退出：\n- Steam\n- SteamVR\n\n你的房间地图和设置将会保留。");
}

}  // namespace

// Shared desktop UI scaling for openhouse's GLFW + ImGui apps (installer, overlay desktop,
// dongle manager, viewer). Preserves each app's fixed aspect ratio and ProggyClean pixel font:
// windows are non-resizable and switch between discrete scale tiers, defaulting to the system
// monitor DPI clamped to the current monitor's workarea.
#pragma once

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "platform/platform.hpp"

namespace lh::ui {

inline constexpr std::array<float, 7> kScalePresets = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f};

struct ScaleChoice {
  bool automatic = true;
  float scale = 1.0f;
};

struct MonitorWorkarea {
  int x = 0, y = 0, w = 1920, h = 1080;
  float contentScale = 1.0f;
};

inline std::filesystem::path UiScaleSettingsPath() {
  return lh::platform::FsPath(lh::platform::ConfigDirectory()) / "ui.json";
}

inline float SnapToPreset(float v) {
  float best = kScalePresets.front();
  float bestDiff = std::fabs(v - best);
  for (float p : kScalePresets) {
    const float d = std::fabs(v - p);
    if (d < bestDiff) {
      bestDiff = d;
      best = p;
    }
  }
  return best;
}

inline ScaleChoice LoadScaleChoice() {
  ScaleChoice out;
  std::ifstream in(UiScaleSettingsPath());
  const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
  if (!j.is_object() || !j.contains("scale")) return out;
  const auto& v = j["scale"];
  if (v.is_string() && v.get<std::string>() == "auto") {
    out.automatic = true;
  } else if (v.is_number()) {
    const float f = v.get<float>();
    if (f >= 0.75f && f <= 4.0f) {
      out.automatic = false;
      out.scale = SnapToPreset(f);
    }
  }
  return out;
}

inline void SaveScaleChoice(const ScaleChoice& c) {
  nlohmann::json j = nlohmann::json::object();
  {
    std::ifstream in(UiScaleSettingsPath());
    const nlohmann::json existing = nlohmann::json::parse(in, nullptr, false);
    if (existing.is_object()) j = existing;
  }
  if (c.automatic) j["scale"] = "auto";
  else j["scale"] = c.scale;
  std::error_code ec;
  std::filesystem::create_directories(UiScaleSettingsPath().parent_path(), ec);
  std::ofstream(UiScaleSettingsPath()) << j.dump(2) << "\n";
}

// Monitor containing the window's centre (or the primary monitor before creation).
inline GLFWmonitor* CurrentMonitor(GLFWwindow* window = nullptr) {
  GLFWmonitor* primary = glfwGetPrimaryMonitor();
  if (!window) return primary;
  int wx = 0, wy = 0, ww = 0, wh = 0;
  glfwGetWindowPos(window, &wx, &wy);
  glfwGetWindowSize(window, &ww, &wh);
  const int cx = wx + ww / 2, cy = wy + wh / 2;
  int count = 0;
  GLFWmonitor** monitors = glfwGetMonitors(&count);
  for (int i = 0; i < count; ++i) {
    int mx = 0, my = 0, mw = 0, mh = 0;
    glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
    if (cx >= mx && cx < mx + mw && cy >= my && cy < my + mh) return monitors[i];
  }
  return primary;
}

inline MonitorWorkarea QueryMonitor(GLFWwindow* window = nullptr) {
  MonitorWorkarea m;
  GLFWmonitor* mon = CurrentMonitor(window);
  if (!mon) return m;
  glfwGetMonitorWorkarea(mon, &m.x, &m.y, &m.w, &m.h);
  float xs = 1.0f, ys = 1.0f;
  glfwGetMonitorContentScale(mon, &xs, &ys);
  if (xs > 0.5f && xs <= 4.0f) m.contentScale = xs;
  return m;
}

// Largest scale factor that keeps (baseW * s, baseH * s) comfortably inside the monitor workarea.
inline float MaxFittingScale(int baseW, int baseH, GLFWwindow* window = nullptr) {
  const MonitorWorkarea m = QueryMonitor(window);
  if (m.w <= 0 || m.h <= 0 || baseW <= 0 || baseH <= 0) return 1.0f;
  const float maxW = float(m.w) * 0.94f / float(baseW);
  const float maxH = float(m.h) * 0.94f / float(baseH);
  return std::max(1.0f, std::min(maxW, maxH));
}

inline float ClampToFittingPreset(float desired, int baseW, int baseH, GLFWwindow* window = nullptr) {
  const float maxFit = MaxFittingScale(baseW, baseH, window);
  float best = kScalePresets.front();
  float bestDiff = 1e9f;
  for (float p : kScalePresets) {
    if (p > maxFit + 0.01f) continue;
    const float d = std::fabs(desired - p);
    if (d < bestDiff) {
      bestDiff = d;
      best = p;
    }
  }
  return best;
}

inline float AutoScaleFor(int baseW, int baseH, GLFWwindow* window = nullptr) {
  const MonitorWorkarea m = QueryMonitor(window);
  return ClampToFittingPreset(m.contentScale, baseW, baseH, window);
}

inline bool ParseEnvScale(const char* envName, ScaleChoice& out) {
  if (!envName) return false;
  const char* raw = std::getenv(envName);
  if (!raw || !*raw) return false;
  const std::string s(raw);
  if (s == "auto" || s == "AUTO") {
    out.automatic = true;
    return true;
  }
  const float v = std::strtof(raw, nullptr);
  if (v >= 0.75f && v <= 4.0f) {
    out.automatic = false;
    out.scale = SnapToPreset(v);
    return true;
  }
  return false;
}

// Resolves the active scale for startup or runtime changes.
// Priority: snapshotMode (deterministic) -> OPENHOUSE_UI_SCALE -> legacyEnv -> saved choice -> Auto DPI.
inline float ResolveScale(const ScaleChoice& choice, int baseW, int baseH, const char* legacyEnv = nullptr,
                          bool snapshotMode = false, GLFWwindow* window = nullptr) {
  ScaleChoice envChoice;
  if (snapshotMode) {
    if (ParseEnvScale(legacyEnv, envChoice) && !envChoice.automatic) return envChoice.scale;
    return 1.0f;
  }
  if (ParseEnvScale("OPENHOUSE_UI_SCALE", envChoice)) {
    return envChoice.automatic ? AutoScaleFor(baseW, baseH, window)
                               : ClampToFittingPreset(envChoice.scale, baseW, baseH, window);
  }
  if (ParseEnvScale(legacyEnv, envChoice)) {
    return envChoice.automatic ? AutoScaleFor(baseW, baseH, window)
                               : ClampToFittingPreset(envChoice.scale, baseW, baseH, window);
  }
  if (choice.automatic) return AutoScaleFor(baseW, baseH, window);
  return ClampToFittingPreset(choice.scale, baseW, baseH, window);
}

inline void ScaledWindowSize(int baseW, int baseH, float scale, int& outW, int& outH) {
  outW = std::max(1, int(std::lround(float(baseW) * scale)));
  outH = std::max(1, int(std::lround(float(baseH) * scale)));
}

// Creates a fixed-aspect-ratio (non-resizable) desktop window sized to baseW*scale x baseH*scale,
// centred within the primary monitor's workarea.
inline GLFWwindow* CreateFixedAspectWindow(int baseW, int baseH, float scale, const char* title) {
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  int w = baseW, h = baseH;
  ScaledWindowSize(baseW, baseH, scale, w, h);
  GLFWwindow* win = glfwCreateWindow(w, h, title, nullptr, nullptr);
  if (!win) return nullptr;
  const MonitorWorkarea m = QueryMonitor(nullptr);
  if (m.w > 0 && m.h > 0) {
    const int x = m.x + std::max(0, (m.w - w) / 2);
    const int y = m.y + std::max(0, (m.h - h) / 2);
    glfwSetWindowPos(win, x, y);
  }
  return win;
}

// Resizes the window to exact baseW:baseH at `scale` and keeps it inside the current workarea.
inline void ApplyWindowScale(GLFWwindow* win, int baseW, int baseH, float scale) {
  if (!win) return;
  int oldX = 0, oldY = 0, oldW = 0, oldH = 0;
  glfwGetWindowPos(win, &oldX, &oldY);
  glfwGetWindowSize(win, &oldW, &oldH);
  int newW = baseW, newH = baseH;
  ScaledWindowSize(baseW, baseH, scale, newW, newH);
  const MonitorWorkarea m = QueryMonitor(win);
  int newX = oldX + (oldW - newW) / 2;
  int newY = oldY + (oldH - newH) / 2;
  if (m.w > 0 && m.h > 0) {
    newX = std::clamp(newX, m.x, m.x + std::max(0, m.w - newW));
    newY = std::clamp(newY, m.y, m.y + std::max(0, m.h - newH));
  }
  glfwSetWindowSize(win, newW, newH);
  glfwSetWindowPos(win, newX, newY);
}

inline int PresetIndex(float scale) {
  int best = 0;
  float bestDiff = 1e9f;
  for (size_t i = 0; i < kScalePresets.size(); ++i) {
    const float d = std::fabs(scale - kScalePresets[i]);
    if (d < bestDiff) {
      bestDiff = d;
      best = int(i);
    }
  }
  return best;
}

// Ctrl+ / Ctrl- / Ctrl0 keyboard shortcuts to step between fitting presets.
inline bool HandleScaleShortcuts(ScaleChoice& choice, float activeScale, int baseW, int baseH, GLFWwindow* win) {
  const ImGuiIO& io = ImGui::GetIO();
  if (!io.KeyCtrl || io.WantTextInput) return false;
  const float maxFit = MaxFittingScale(baseW, baseH, win);
  const int cur = PresetIndex(activeScale);
  if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) {
    for (int i = cur + 1; i < int(kScalePresets.size()); ++i) {
      if (kScalePresets[size_t(i)] <= maxFit + 0.01f) {
        choice.automatic = false;
        choice.scale = kScalePresets[size_t(i)];
        return true;
      }
    }
  } else if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) {
    if (cur > 0) {
      choice.automatic = false;
      choice.scale = kScalePresets[size_t(cur - 1)];
      return true;
    }
  } else if (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) {
    if (!choice.automatic) {
      choice.automatic = true;
      return true;
    }
  }
  return false;
}

// Compact scale selector combo; returns true if the user picked a new scale choice.
inline bool DrawScaleCombo(const char* id, ScaleChoice& choice, float activeScale, int baseW, int baseH, GLFWwindow* win) {
  const float autoScale = AutoScaleFor(baseW, baseH, win);
  const float maxFit = MaxFittingScale(baseW, baseH, win);
  char preview[32];
  if (choice.automatic) std::snprintf(preview, sizeof preview, "Auto (%d%%)", int(std::lround(activeScale * 100.f)));
  else std::snprintf(preview, sizeof preview, "%d%%", int(std::lround(activeScale * 100.f)));

  bool changed = false;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.2f);
  if (ImGui::BeginCombo(id, preview, ImGuiComboFlags_HeightLarge)) {
    char autoLabel[40];
    std::snprintf(autoLabel, sizeof autoLabel, "Auto (%d%%)", int(std::lround(autoScale * 100.f)));
    if (ImGui::Selectable(autoLabel, choice.automatic)) {
      if (!choice.automatic) {
        choice.automatic = true;
        changed = true;
      }
    }
    ImGui::Separator();
    for (float p : kScalePresets) {
      const bool fits = p <= maxFit + 0.01f;
      int pw = 0, ph = 0;
      ScaledWindowSize(baseW, baseH, p, pw, ph);
      char label[48];
      std::snprintf(label, sizeof label, "%d%%  (%dx%d)", int(std::lround(p * 100.f)), pw, ph);
      ImGui::BeginDisabled(!fits);
      const bool selected = !choice.automatic && std::fabs(choice.scale - p) < 0.01f;
      if (ImGui::Selectable(label, selected)) {
        choice.automatic = false;
        choice.scale = p;
        changed = true;
      }
      ImGui::EndDisabled();
      if (!fits && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Larger than this display's workarea");
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Window scale (Ctrl +/- / Ctrl 0)");
  return changed;
}

// Menu items for openhouse-viewer's View menu; returns true if the user picked a new scale choice.
inline bool DrawScaleMenuItems(ScaleChoice& choice, int baseW, int baseH, GLFWwindow* win) {
  const float autoScale = AutoScaleFor(baseW, baseH, win);
  const float maxFit = MaxFittingScale(baseW, baseH, win);
  bool changed = false;
  char autoLabel[40];
  std::snprintf(autoLabel, sizeof autoLabel, "Auto (%d%%)", int(std::lround(autoScale * 100.f)));
  if (ImGui::MenuItem(autoLabel, "Ctrl+0", choice.automatic)) {
    if (!choice.automatic) {
      choice.automatic = true;
      changed = true;
    }
  }
  ImGui::Separator();
  for (float p : kScalePresets) {
    const bool fits = p <= maxFit + 0.01f;
    int pw = 0, ph = 0;
    ScaledWindowSize(baseW, baseH, p, pw, ph);
    char label[48];
    std::snprintf(label, sizeof label, "%d%% (%dx%d)", int(std::lround(p * 100.f)), pw, ph);
    const bool selected = !choice.automatic && std::fabs(choice.scale - p) < 0.01f;
    if (ImGui::MenuItem(label, nullptr, selected, fits)) {
      choice.automatic = false;
      choice.scale = p;
      changed = true;
    }
  }
  return changed;
}

}  // namespace lh::ui

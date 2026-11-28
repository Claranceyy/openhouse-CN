// openhouse-installer: installs and uninstalls the openhouse driver, and flashes openhouse's
// firmware (1.0 base stations, doorman dongles, trackers' and controllers' radios), with a guided
// window for each. Windows and Linux; everything it needs is linked in, and the firmware files
// come from the package's firmwares/ folder.
#include <string>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl2.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <iterator>
#include <vector>

#include "common/crash_log.hpp"
#include "common/desktop_identity.hpp"
#include "common/ui_scale.hpp"
#include "logo_rgba.h"
#include "ui.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
using namespace openhouse::installer;

GLuint UploadRgba(const uint8_t* rgba, int w, int h) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  return texture;
}

int Run(const std::string& mode) {
  if (!glfwInit()) {
#ifdef _WIN32
    MessageBoxW(nullptr, L"openhouse-installer couldn't open its window.", L"openhouse", MB_ICONERROR);
#else
    std::fprintf(stderr, "openhouse-installer: no display\n");
#endif
    return 1;
  }
  constexpr int kBaseW = 1000, kBaseH = 720;
  const char* snapshot = std::getenv("OPENHOUSE_INSTALLER_SNAPSHOT");
  lh::ui::ScaleChoice scaleChoice = lh::ui::LoadScaleChoice();
  float activeScale = lh::ui::ResolveScale(scaleChoice, kBaseW, kBaseH, nullptr, snapshot != nullptr);
  lh::ui::DesktopIdentity("openhouse-installer", "openhouse.png", "openhouse installer");   // Wayland: the icon comes from the menu entry of that name
  GLFWwindow* window = lh::ui::CreateFixedAspectWindow(kBaseW, kBaseH, activeScale, "openhouse installer");
  if (!window) { glfwTerminate(); return 1; }
  bool scaleDirty = false, scaleSave = false;
  glfwSetWindowUserPointer(window, &scaleDirty);
  glfwSetWindowContentScaleCallback(window, [](GLFWwindow* w, float, float) {
    if (auto* dirty = static_cast<bool*>(glfwGetWindowUserPointer(w))) *dirty = true;
  });
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  GLFWimage icon{openhouse::overlay::kLogoSize, openhouse::overlay::kLogoSize, const_cast<unsigned char*>(openhouse::overlay::kLogoRgba)};
  glfwSetWindowIcon(window, 1, &icon);
  ImGui::CreateContext();
  Ui::SetUpImGui(activeScale);
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL2_Init();
  {
    Ui ui;
    ui.SetLogo(UploadRgba(openhouse::overlay::kLogoRgba, openhouse::overlay::kLogoSize, openhouse::overlay::kLogoSize));
    if (!snapshot) {
      ui.SetScaleDrawer([&] {
        if (lh::ui::DrawScaleCombo("##scale", scaleChoice, activeScale, kBaseW, kBaseH, window)) {
          scaleDirty = true;
          scaleSave = true;
        }
      });
    }
    // Testing without a screen: OPENHOUSE_INSTALLER_PAGE picks the page, OPENHOUSE_INSTALLER_SNAPSHOT=FILE.ppm
    // writes the frame drawn after ~2 s and exits.
    if (const char* page = std::getenv("OPENHOUSE_INSTALLER_PAGE")) ui.ShowForTest(page);
    if (mode == "--update") ui.StartSelfUpdate();
    else if (mode == "--install" || mode == "--repair") ui.StartAutoInstall();
    else if (mode == "--uninstall") ui.StartAutoUninstall();
    else if (mode == "--dongles") ui.StartDongleUpdate();
    int frame = 0;
    while (!glfwWindowShouldClose(window) && !ui.QuitRequested()) {
      glfwWaitEventsTimeout(1.0 / 30);  // keeps redrawing for progress bars and device lists
      if (scaleDirty && !snapshot) {
        scaleDirty = false;
        const float nextScale = lh::ui::ResolveScale(scaleChoice, kBaseW, kBaseH, nullptr, false, window);
        if (std::fabs(nextScale - activeScale) > 0.001f) {
          activeScale = nextScale;
          lh::ui::ApplyWindowScale(window, kBaseW, kBaseH, activeScale);
          Ui::SetUpImGui(activeScale);
        }
        if (scaleSave) {
          lh::ui::SaveScaleChoice(scaleChoice);
          scaleSave = false;
        }
      }
      ImGui_ImplOpenGL2_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
      if (!snapshot && lh::ui::HandleScaleShortcuts(scaleChoice, activeScale, kBaseW, kBaseH, window)) {
        scaleDirty = true;
        scaleSave = true;
      }
      ui.Draw(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
      ImGui::Render();
      int w, h;
      glfwGetFramebufferSize(window, &w, &h);
      glViewport(0, 0, w, h);
      glClearColor(0.067f, 0.075f, 0.098f, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
      static const int kSnapFrame = std::getenv("OPENHOUSE_INSTALLER_SNAPSHOT_FRAMES") ? std::atoi(std::getenv("OPENHOUSE_INSTALLER_SNAPSHOT_FRAMES")) : 60;
      if (snapshot && ++frame == kSnapFrame) {
        std::vector<unsigned char> rgb(size_t(w) * h * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
        if (FILE* f = std::fopen(snapshot, "wb")) {
          std::fprintf(f, "P6\n%d %d\n255\n", w, h);
          for (int y = h - 1; y >= 0; --y) std::fwrite(rgb.data() + size_t(y) * w * 3, 1, size_t(w) * 3, f);   // GL rows are bottom-up
          std::fclose(f);
        }
        break;
      }
      glfwSwapBuffers(window);
    }
  }
  // Gone from the screen at once: what's left below can take a moment (on Windows a USB call can).
  glfwHideWindow(window);
  // Scans finish on their own threads: let the last one end before the process does (bounded: a scan
  // checks g_closing between requests, so it stops within one).
  openhouse::installer::g_closing = true;
  std::unique_lock devices(openhouse::installer::g_deviceMutex, std::defer_lock);
  for (int i = 0; i < 30 && !devices.try_lock(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ImGui_ImplOpenGL2_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
}  // namespace

// --update (the overlay's Update / Reinstall): download the latest release and hand over to its
// installer; --install (that installer) and --repair (the overlay's Fix, from the package kept at
// install): install right away; --uninstall (its Uninstall openhouse): uninstall right away;
// --dongles (the overlay's firmware warning): the dongle update, closing SteamVR / Monado for it.
// --url LINK: an openhouse:// link (SteamVR's firmware Update button on a device): the dongle update.
int main(int argc, char** argv) {
  lh::crash::Install("openhouse-installer");
  std::string mode = argc > 1 ? argv[1] : "";
  if (mode == "--url") {
#ifdef _WIN32
    // Started by SteamVR: out of its job first, or closing SteamVR for the update closes this too.
    wchar_t self[MAX_PATH * 2]{};
    GetModuleFileNameW(nullptr, self, DWORD(std::size(self)));
    std::wstring command = L"\"" + std::wstring(self) + L"\" --dongles";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_BREAKAWAY_FROM_JOB, nullptr, nullptr, &si, &pi)) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      return 0;
    }
#endif
    mode = "--dongles";   // not in a job (or no breakaway allowed): run here
  }
  return Run(mode);
}

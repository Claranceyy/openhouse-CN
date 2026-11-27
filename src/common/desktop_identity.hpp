// Which app a window is, for the desktop's icon and task list.
//
// Wayland ignores an icon set on the window (glfwSetWindowIcon): the compositor takes the window's app
// id and shows the icon of the .desktop file with that name. So each app sets its id before creating its
// window (also the X11 class), the same name as its menu entry (the installer's and the Nix package's).
// Run unpacked, before anything is installed, there is no such entry and the compositor shows a generic
// icon: then a hidden entry is written for it (~/.local/share/applications/<id>.desktop, NoDisplay),
// with the icon that ships next to the program. Not when an entry of that name exists anywhere on the
// data path: a user entry would shadow it (hiding the Nix package's menu entry, say).
#pragma once

#include <GLFW/glfw3.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace lh::ui {

#ifndef _WIN32
namespace detail {
inline std::vector<std::filesystem::path> DesktopEntryFolders() {
  std::vector<std::filesystem::path> out;
  const char* home = std::getenv("HOME");
  const char* user = std::getenv("XDG_DATA_HOME");
  if (user && *user) out.emplace_back(std::filesystem::path(user) / "applications");
  else if (home && *home) out.emplace_back(std::filesystem::path(home) / ".local" / "share" / "applications");
  const char* dirs = std::getenv("XDG_DATA_DIRS");
  std::stringstream all(dirs && *dirs ? dirs : "/usr/local/share:/usr/share");
  for (std::string d; std::getline(all, d, ':');)
    if (!d.empty()) out.emplace_back(std::filesystem::path(d) / "applications");
  return out;
}
}  // namespace detail
#endif

// Before glfwCreateWindow. appId: "openhouse", "doorman-dongle-manager", "openhouse-installer";
// icon: the PNG's file name in the package ("openhouse.png"); name: the entry's Name.
inline void DesktopIdentity(const char* appId, const char* icon, const char* name) {
#ifdef _WIN32
  (void)appId; (void)icon; (void)name;   // Windows takes each program's own icon
#else
  glfwWindowHintString(GLFW_WAYLAND_APP_ID, appId);
  glfwWindowHintString(GLFW_X11_CLASS_NAME, appId);
  glfwWindowHintString(GLFW_X11_INSTANCE_NAME, appId);
  namespace fs = std::filesystem;
  std::error_code ec;
  const auto folders = detail::DesktopEntryFolders();
  if (folders.empty()) return;
  for (const auto& f : folders)
    if (fs::exists(f / (std::string(appId) + ".desktop"), ec)) return;   // installed, or the Nix package's
  const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
  if (ec) return;
  // Next to it in the package (apps/icons) or an install (icons), or the package root's apps/icons.
  fs::path png;
  for (const fs::path& p : {exe.parent_path() / "icons" / icon, exe.parent_path() / "apps" / "icons" / icon})
    if (fs::exists(p, ec)) { png = p; break; }
  if (png.empty()) return;
  fs::create_directories(folders.front(), ec);
  std::ofstream out(folders.front() / (std::string(appId) + ".desktop"));
  out << "[Desktop Entry]\nType=Application\nName=" << name << "\nExec=\"" << exe.string() << "\"\nIcon=" << png.string()
      << "\nStartupWMClass=" << appId << "\nNoDisplay=true\nTerminal=false\n";
#endif
}

}  // namespace lh::ui

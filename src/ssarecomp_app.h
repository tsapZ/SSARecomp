// ssarecomp - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#include <rex/system/gpu_plugin.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>

#include "dev/input_script.h"
#include "game/portal_prompt_watch.h"
#include "game/game_text.h"
#include "game/hats.h"
#include "game/ui_images.h"
#include "dev/gpu_diagnostics.h"
#include "localization.h"
#include "portal_rex/portal_menu.h"
#include "portal_rex/portal_menu_input.h"
#include "portal_rex/player_menu.h"
#include "portal_rex/settings_menu.h"
#include "settings_pages.h"
#include "portal_rex/portal_rex.h"
#include "xex_verify.h"

// Manual override for the ImGui overlay's size, since there's no reliable way to auto-detect "the
// default looks too small/big" across platforms: the SDK's own physical-to-logical DPI conversion
// (ImGuiDrawer::Draw/UpdateMousePosition) already compensates for actual OS display scaling, so
// this is on top of that -- for e.g. a large 4K monitor running at 100% OS scaling, which the SDK
// can't distinguish from a small one (both report the same "DPI").
REXCVAR_DEFINE_STRING(gpu_backend, "any", "GPU",
                      "Graphics backend: any (D3D12 where available), d3d12 or vulkan (only in "
                      "builds with Vulkan compiled in)")
    .allowed({"any", "d3d12", "vulkan"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(config_file, "", "Paths",
                      "Developer option: read and write this settings file instead of "
                      "ssarecomp.toml next to the executable, so test runs leave the real "
                      "one alone")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_DOUBLE(ui_scale, 1.0, "UI",
                     "Scales the ImGui overlay's font size and widget sizing. Increase for large "
                     "screens where the default looks small.")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

class SsarecompApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<SsarecompApp>(new SsarecompApp(ctx, "ssarecomp",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  void OnShutdown() override {
    ssarecomp::StopPortalPromptWatch();
    // The menu's textures belong to the immediate drawer, which goes away right after this.
    skylanders::ShutdownPortalMenu();
    skylanders::ShutdownPlayerMenu();
    skylanders::ShutdownSettingsMenu();
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    // Before the GPU plugin creates its device (testing only, gpu_crash_diagnostics).
    ssarecomp::EnableGpuCrashDiagnostics();
    // The Xenos GPU plugin renders the Xbox 360 command stream; without it nothing is drawn.
    if (config.gpu_plugin.empty()) config.gpu_plugin = "xenos";
    // A specific graphics backend (the plugin otherwise takes D3D12 when it has it). Vulkan only
    // exists in builds configured with REXGLUE_USE_VULKAN=ON.
    if (const std::string backend = REXCVAR_GET(gpu_backend); backend != "any" && !config.graphics) {
      config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin, backend);
    }
    // Some of the game's text (e.g. the autosave warning dialog) is already resolved by the time
    // OnPostSetup runs -- apply this as early as possible, before the XEX even loads.
    ssarecomp::ApplyLanguageSetting();
    // The portal menu wraps every input driver: it notices its open button on any controller,
    // knows which player that controller is, and hides input from the game while it is open.
    config.input_factory = skylanders::CreatePortalMenuInputSystem;
    // The Guide button opens the portal menu (the menu hides it from the game). SDL drops Guide
    // presses unless this is on.
    SetDefault("guide_button", "true");
    // Spyro's Adventure knows only its own figures (later games' figures of the same characters
    // carry their own toy ids too).
    SetDefault("portal_menu_games", "Spyro's Adventure");
  }

  void OnPostSetup() override {
    // One figure folder for every game (Documents\Skylanders\Figures), like the real figures next
    // to the portal; the first start copies this game's old per-game folder into it.
    skylanders::InstallPortal(skylanders::SharedFiguresDir(user_data_root()));
    ssarecomp::InstallInputScript(
        runtime()->input_system(), runtime()->memory(),
        [this]() -> uint64_t {
          auto* graphics_system = runtime() ? runtime()->graphics_system() : nullptr;
          return graphics_system ? graphics_system->guest_frame_count() : 0;
        },
        [this](std::function<void()> f) { app_context().CallInUIThread(std::move(f)); });
    // The F3 debug overlay's FPS line only shows once something calls this; nothing did, so it
    // silently stayed blank. Sampled from the SDK's dedicated guest-swap counter (which only
    // advances on an actual presented guest frame -- see CommandProcessor::guest_frame_count())
    // rather than counting how often the overlay itself gets redrawn: the overlay is repainted at
    // the host's presentation rate, which is a different clock than the guest's and would have
    // reported host FPS mislabeled as "Guest".
    SetGuestFrameStats([this] { return ComputeFrameStats(); });
    // Opens the portal menu by itself when the game shows its "place a Skylander" prompt.
    ssarecomp::StartPortalPromptWatch(runtime()->memory());
    // Items go on and off the portal only once the player's Skylander is back in the game.
    skylanders::SetGameBusyProvider([] { return ssarecomp::PortalPromptActive(); });
    // Pictures for the portal menu from the game's own UI archive: HUD portraits, element symbols.
    ssarecomp::InstallUiImages(game_data_root_ / "uigameimage.str");
    // Names of magic items and adventure packs from the game's text, in its language.
    ssarecomp::InstallGameText(game_data_root_ / "text");
    // The details view's hat: the game's name, picture and bonuses.
    ssarecomp::InstallHats();
    // Default to 16x anisotropic filtering (the SDK defaults to 4x): ground and floor textures seen
    // at a shallow angle stay sharp, for a negligible cost on PC GPUs. Only when the user hasn't set
    // it themselves.
    if (!rex::cvar::HasNonDefaultValue("anisotropic_override") &&
        !rex::cvar::SetFlagByName("anisotropic_override", "5")) {
      REXLOG_WARN("Could not set anisotropic_override to 16x");
    }
    // Estimate the EDRAM extent of draws without clipping by running their vertex shader on the
    // CPU (Xenia's default; the SDK defaults to off). In Swap Force, without it, the faster RTV
    // render target path let a full-screen float pass wrap around the EDRAM onto the lit scene and
    // every character composited from it turned black (SwapForceRecomp docs/STATUS.md,
    // 2026-10-17). Kept here as the safe default until this game is checked.
    if (!rex::cvar::HasNonDefaultValue("execute_unclipped_draw_vs_on_cpu") &&
        !rex::cvar::SetFlagByName("execute_unclipped_draw_vs_on_cpu", "true")) {
      REXLOG_WARN("Could not enable execute_unclipped_draw_vs_on_cpu");
    }
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override {
    // The SDK's built-in font (ProggyTiny, forced OversampleH/V=1 + PixelSnapH) is a tiny debug
    // font with no antialiasing. Add a real, readable, antialiased font (ImFontConfig's default
    // oversampling applies since we don't override it) and make it the default everywhere,
    // including the SDK's own F3/console/F4/F7 overlays.
    const float font_size = 18.0f * float(REXCVAR_GET(ui_scale));
    ImFont* font = nullptr;
#ifdef _WIN32
    font = atlas->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", font_size);
#else
    // No single well-known path across distros; try common ones in order and keep the first that
    // exists. Falls through to the SDK's built-in ProggyTiny font if none of these are installed.
    static constexpr const char* kLinuxFontCandidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    };
    for (const char* path : kLinuxFontCandidates) {
      if (!std::filesystem::exists(path)) continue;
      font = atlas->AddFontFromFileTTF(path, font_size);
      if (font) break;
    }
#endif
    if (font) ImGui::GetIO().FontDefault = font;
    skylanders::AddPortalMenuFonts(atlas);
  }

  void OnConfigureStyle(ImGuiStyle& imgui_style, rex::ui::Style& ui_style) override {
    (void)ui_style;
    // Replace the SDK's green terminal-console theme with ImGui's normal dark theme, plus a
    // light blue accent so it still has some identity.
    ImGui::StyleColorsDark(&imgui_style);
    imgui_style.WindowRounding = 4.0f;
    imgui_style.FrameRounding = 3.0f;
    imgui_style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.45f, 0.80f, 0.75f);
    imgui_style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.55f, 0.90f, 0.90f);
    imgui_style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.38f, 0.70f, 1.00f);
    imgui_style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.45f, 0.80f, 0.55f);
    imgui_style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.55f, 0.90f, 0.70f);
    imgui_style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.16f, 0.38f, 0.70f, 0.85f);
    imgui_style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.38f, 0.70f, 1.00f);
    imgui_style.ScaleAllSizes(float(REXCVAR_GET(ui_scale)));
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    skylanders::RegisterPortalMenu(drawer, &app_context(), immediate_drawer());
    // The portal menu's options (shown games, only my figures) go into the settings file.
    skylanders::SetPortalMenuConfigPath(settings_path_);
    // L3 + R3 or F9: which controller plays as which player.
    skylanders::RegisterPlayerMenu(drawer, &app_context());
    // The settings menu takes F4 (and Back + RB); the SDK's full list of every setting moves to
    // F10 and stays reachable from the menu's last page.
    SetDefault("bind_settings", "F10");
    skylanders::RegisterSettingsMenu(
        drawer, &app_context(), ssarecomp::BuildSettingsPages([] {
          // Through the SDK overlay's own key bind.
          const auto key = rex::ui::ParseVirtualKey(rex::cvar::GetFlagByName("bind_settings"));
          rex::ui::KeyEvent event(nullptr, key, 1, false, false, false, false, false);
          rex::ui::ProcessKeyEvent(event);
        }),
        settings_path_);
    skylanders::RegisterPortalOverlay(drawer);
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    // game_data_root is read from the cvar registry before the config file loads (see
    // ReXApp::SetupEnvironment), so --game_data_root can't be set via ssarecomp.toml the way
    // portal_mode etc. can. Defaulting to "rom" (relative to the working directory, matching every
    // documented way of running this game) means most users need neither the flag nor a config
    // entry for it.
    if (paths.game_data_root.empty()) paths.game_data_root = "rom";
    game_data_root_ = paths.game_data_root;
    if (!REXCVAR_GET(config_file).empty()) {
      paths.config_path = std::filesystem::u8path(REXCVAR_GET(config_file));
    }
    settings_path_ = paths.config_path;
  }

  void OnPostInitLogging() override {
    // ReXApp reports a missing --game_data_root itself when it builds the runtime.
    if (game_data_root_.empty()) return;
    const auto xex = game_data_root_ / "default.xex";
    // Utf8() and wide messages, not path::string(): that throws for characters outside the ANSI
    // code page, and this runs on every startup.
    REXLOG_INFO("Checking {} against the pinned SHA-256", ssarecomp::Utf8(xex));
    const auto result = ssarecomp::VerifyXex(xex, SSARECOMP_XEX_SHA256);
    const auto problem = ssarecomp::DescribeXexProblem(xex, result, SSARECOMP_XEX_SHA256);
    if (problem.empty()) {
      REXLOG_INFO("default.xex verified");
      return;
    }
    Fatal(problem);
  }

 private:
  // Sets a setting's default without overriding what the user put in the settings file.
  static void SetDefault(std::string_view name, std::string_view value) {
    if (!rex::cvar::HasNonDefaultValue(name) && !rex::cvar::SetFlagByName(name, value)) {
      REXLOG_WARN("Could not set {} to {}", name, value);
    }
  }

  [[noreturn]] static void Fatal(const std::string& message) {
    REXLOG_ERROR("{}", message);
#ifdef _WIN32
    // message is UTF-8; std::filesystem::path(std::string) assumes the ANSI code page on Windows
    // and mangles anything outside it, so go through the char8_t overload instead (see Utf8()'s
    // own comment for why path::string() has the same trap in the other direction).
    const std::u8string u8(reinterpret_cast<const char8_t*>(message.data()), message.size());
    const auto wide = std::filesystem::path(u8).wstring();
    MessageBoxW(nullptr, wide.c_str(), L"SSARecomp", MB_OK | MB_ICONERROR);
#endif
    std::exit(2);
  }

  // Averaged over kFpsRefreshInterval rather than a raw frame-to-frame delta: at 60-240Hz a raw
  // instantaneous value changes every single call and is unreadable.
  static constexpr double kFpsRefreshIntervalSeconds = 0.5;

  // Called once per host-rendered overlay frame (see OnPostSetup's comment above), which is a
  // different, faster-or-slower clock than the guest's. So rather than counting these calls, each
  // one just samples the SDK's presented-guest-frame counter and computes guest FPS from how much
  // *that* advanced over the window -- the call frequency of this function doesn't matter.
  rex::ui::FrameStats ComputeFrameStats() {
    const auto now = std::chrono::steady_clock::now();
    uint64_t guest_frame_count = 0;
    if (auto* graphics_system = runtime() ? runtime()->graphics_system() : nullptr) {
      guest_frame_count = graphics_system->guest_frame_count();
    }

    if (last_frame_stats_time_) {
      double elapsed_seconds = std::chrono::duration<double>(now - *last_frame_stats_time_).count();
      if (elapsed_seconds >= kFpsRefreshIntervalSeconds) {
        uint64_t delta_frames = guest_frame_count - last_guest_frame_count_;
        // Guard against a zero (or negative/wrapped) delta: with the overlay open but the guest
        // stalled/paused/loading, no new frames were presented in this window, so leave the
        // cached values as-is rather than dividing by a frame count of zero.
        if (delta_frames > 0 && guest_frame_count > last_guest_frame_count_) {
          cached_fps_ = double(delta_frames) / elapsed_seconds;
          cached_frame_time_ms_ = (elapsed_seconds / double(delta_frames)) * 1000.0;
        }
        last_guest_frame_count_ = guest_frame_count;
        last_frame_stats_time_ = now;
      }
    } else {
      last_guest_frame_count_ = guest_frame_count;
      last_frame_stats_time_ = now;
    }

    rex::ui::FrameStats stats;
    stats.fps = cached_fps_;
    stats.frame_time_ms = cached_frame_time_ms_;
    stats.frame_count = guest_frame_count;
    return stats;
  }

  std::filesystem::path game_data_root_;
  std::filesystem::path settings_path_;  // ssarecomp.toml
  std::optional<std::chrono::steady_clock::time_point> last_frame_stats_time_;
  uint64_t last_guest_frame_count_ = 0;
  double cached_fps_ = 0.0;
  double cached_frame_time_ms_ = 0.0;
};

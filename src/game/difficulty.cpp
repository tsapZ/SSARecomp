// Difficulty levels (difficulty.h): the choice per save, and the picker a new game opens.
//
// The picker follows the difficulty screens of the later games (Giants, Swap Force, Trap Team): a
// title, the levels side by side from the mildest to the hardest, the chosen one raised with its
// description below it, and A to start. It draws in the menus' own look (portal_rex/menu_draw.h)
// and hides the controllers from the game like the other menus. On a new game the game waits at
// its save until a level is chosen (the save call is held on the game's thread, the picker runs on
// the UI thread, which keeps drawing), so nothing happens behind it.

#include "game/difficulty.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/input/input.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/windowed_app_context.h>

#include "portal_rex/menu_draw.h"
#include "portal_rex/portal_menu_input.h"
#include "portal_rex/settings_menu.h"

REXCVAR_DEFINE_BOOL(difficulty_ask_new_game, true, "Game",
                    "A new game asks for a difficulty (Easy, Adventurer, Hard, Nightmare); off: "
                    "new games are Adventurer, the game as it is");

namespace ssarecomp {
namespace {

using namespace skylanders::menu_draw;
using Clock = std::chrono::steady_clock;

// The saves' title folder and content type (the SDK's content root: <profile>\415608AB\00000001).
constexpr const char* kTitleFolder = "415608AB";
constexpr const char* kSavedGameFolder = "00000001";
constexpr const char* kStoreFile = "difficulty.txt";

// The values come from the later games' own difficulty tables, read from their data (2026-11-25;
// docs/difficulty.md): Swap Force's data/systemdata.mua (globalTuningData) for the enemies' damage
// (_difficultyFixedEnemyDamageScale), their health (_difficultyFixedEnemyHealthScale), food
// (_difficultyFixedFoodHealthGainScale) and the level-up heal (_difficultyLevelUpHealthRecovery;
// Adventurer keeps Spyro's Adventure's full heal), and Imaginators' systemData.igz for the rewards
// (_xpScalar* and _moneyScalar*: x1, x1, x1.25, x1.5, the "+50% Rewards" of its Nightmare). Trap
// Team works its multipliers out in its visual scripts, which couldn't be read.
struct Level {
  const char* key;
  float damage;    // the damage a player's Skylander takes
  float health;    // the health of everything else (enemies, bosses)
  float food;      // the part of its health a food pickup gives
  float level_up;  // the part of the full health a level-up gives back
  float reward;    // XP and gold
  const char* en;
  const char* de;
  const char* tagline_en;
  const char* tagline_de;
  const char* portrait;  // the game's picture for its circle (ui_images.cpp, "picture:<name>")
  ImU32 colour;          // the circle's colour, from calm to fierce
};

constexpr std::array<Level, kDifficultyCount> kLevels{{
    {"easy", 0.4f, 0.6f, 1.0f, 1.0f, 1.0f, "Easy", "Leicht",
     "For new Portal Masters and younger players!",
     "F\xC3\xBCr neue Portal-Meister und j\xC3\xBCngere Spieler!",
     "picture:toy_gillgrunt_hud", IM_COL32(40, 110, 210, 255)},
    {"adventurer", 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, "Adventurer", "Abenteurer",
     "The adventure as it was made, just like on the console!",
     "Das Abenteuer, wie es gemacht wurde, genau wie auf der Konsole!",
     "picture:toy_stealthelf_hud", IM_COL32(64, 170, 40, 255)},
    {"hard", 2.1f, 1.35f, 0.75f, 0.2f, 1.25f, "Hard", "Schwer",
     "For experienced Portal Masters!",
     "F\xC3\xBCr erfahrene Portal-Meister!",
     "picture:toy_spyrojr_hud", IM_COL32(120, 50, 190, 255)},
    {"nightmare", 5.0f, 2.5f, 0.6f, 0.1f, 1.5f, "Nightmare", "Albtraum",
     "For advanced players with a vast army of Skylanders!",
     "F\xC3\xBCr Profis mit einer riesigen Armee von Skylandern!",
     "picture:kaos_icon", IM_COL32(200, 40, 30, 255)},
}};

const Level& LevelOf(Difficulty d) { return kLevels[std::clamp(int(d), 0, kDifficultyCount - 1)]; }

bool g_german = false;
const char* Tr(const char* en, const char* de) { return g_german ? de : en; }

// --- The choices, by slot save -----------------------------------------------------------------

std::mutex g_mutex;  // the members below
std::filesystem::path g_user_data;
std::map<std::string, Difficulty> g_saved;  // slot save folder -> level
std::string g_active;                       // the slot save played now, "" before any
std::string g_asked;  // the slot the picker came up for in this stay on the main menu
std::atomic<int> g_level{int(Difficulty::kAdventurer)};  // the save played now

bool IsSlotSave(const std::string& name) { return name.find("Slot") != std::string::npos; }

std::optional<Difficulty> Parse(std::string key) {
  for (auto& c : key) c = char(std::tolower(static_cast<unsigned char>(c)));
  for (int i = 0; i < kDifficultyCount; ++i) {
    if (key == kLevels[i].key) return Difficulty(i);
  }
  return std::nullopt;
}

std::string Trim(std::string s) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

// With g_mutex held.
void LoadLocked() {
  g_saved.clear();
  std::ifstream in(g_user_data / kStoreFile);
  std::string line;
  while (std::getline(in, line)) {
    line = Trim(line);
    if (line.empty() || line[0] == '#') continue;
    const auto eq = line.rfind('=');
    if (eq == std::string::npos) continue;
    if (auto d = Parse(Trim(line.substr(eq + 1)))) g_saved[Trim(line.substr(0, eq))] = *d;
  }
}

// With g_mutex held. Written to a new file first, so a crash never leaves half of it.
void StoreLocked() {
  if (g_user_data.empty()) return;
  const auto path = g_user_data / kStoreFile;
  auto temp = path;
  temp += ".new";
  {
    std::ofstream out(temp, std::ios::trunc);
    if (!out) {
      REXLOG_WARN("Difficulty: could not write {}", rex::path_to_utf8(path));
      return;
    }
    out << "# Spyro's Adventure Recompiled: the difficulty each save is played on\n"
           "# (easy, adventurer, hard, nightmare). F4 > Extras changes it.\n";
    for (const auto& [slot, d] : g_saved) out << slot << "=" << LevelOf(d).key << "\n";
  }
  std::error_code ec;
  std::filesystem::rename(temp, path, ec);
  if (ec) REXLOG_WARN("Difficulty: could not write {}: {}", rex::path_to_utf8(path), ec.message());
}

// With g_mutex held.
Difficulty ActiveLocked() {
  const auto it = g_saved.find(g_active);
  return it == g_saved.end() ? Difficulty::kAdventurer : it->second;
}

// With g_mutex held.
void SetActiveLocked(const std::string& slot, bool log) {
  const bool changed = log && slot != g_active;
  g_active = slot;
  const Difficulty d = ActiveLocked();
  g_level.store(int(d));
  if (changed) {
    REXLOG_INFO("Difficulty: {} is played on {} (damage taken x{}, enemy health x{}, rewards x{})", slot,
                LevelOf(d).en, LevelOf(d).damage, LevelOf(d).health, LevelOf(d).reward);
  }
}

// Whether the save's folder is there in any profile (<user data>\<profile>\415608AB\00000001\<name>).
bool SaveExists(const std::string& name) {
  std::error_code ec;
  for (const auto& profile : std::filesystem::directory_iterator(g_user_data, ec)) {
    std::error_code ec2;
    const auto folder = profile.path() / kTitleFolder / kSavedGameFolder / rex::to_path(name);
    if (std::filesystem::exists(folder, ec2)) return true;
  }
  return false;
}

void Choose(const std::string& slot, Difficulty d) {
  std::lock_guard lock(g_mutex);
  g_saved[slot] = d;
  StoreLocked();
  REXLOG_INFO("Difficulty: {} chosen for {} (damage taken x{}, enemy health x{}, rewards x{})",
              LevelOf(d).en, slot, LevelOf(d).damage, LevelOf(d).health, LevelOf(d).reward);
  if (slot == g_active) g_level.store(int(d));
}

// --- The game waiting for a new game's choice -----------------------------------------------------

std::mutex g_wait_mutex;
std::condition_variable g_wait_cv;
bool g_waiting = false;  // a new game's save waits for the picker
std::atomic<bool> g_shutdown{false};

void ReleaseGame() {
  {
    std::lock_guard lock(g_wait_mutex);
    g_waiting = false;
  }
  g_wait_cv.notify_all();
}

// --- The picker ------------------------------------------------------------------------------------

struct PadEdges {
  bool left = false, right = false, accept = false, back = false;
};

class PadState {
 public:
  PadEdges Read(const rex::input::X_INPUT_GAMEPAD& pad) {
    PadEdges e;
    const uint16_t buttons = pad.buttons;
    auto pressed = [&](uint16_t bit) { return (buttons & bit) && !(prev_ & bit); };
    const float lx = float(int16_t(pad.thumb_lx)) / 32767.0f;
    const bool left = (buttons & rex::input::X_INPUT_GAMEPAD_DPAD_LEFT) || lx < -0.55f;
    const bool right = (buttons & rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT) || lx > 0.55f;
    e.left = left && !prev_left_;
    e.right = right && !prev_right_;
    e.accept = pressed(rex::input::X_INPUT_GAMEPAD_A) || pressed(rex::input::X_INPUT_GAMEPAD_START);
    e.back = pressed(rex::input::X_INPUT_GAMEPAD_B);
    prev_ = buttons;
    prev_left_ = left;
    prev_right_ = right;
    return e;
  }
  static bool Released(const rex::input::X_INPUT_GAMEPAD& pad) {
    return uint16_t(pad.buttons) == 0 && pad.left_trigger < 30 && pad.right_trigger < 30;
  }

 private:
  uint16_t prev_ = 0;
  bool prev_left_ = false, prev_right_ = false;
};

ImU32 Scaled(ImU32 colour, float k, int alpha = 255) {
  const auto c = [&](int shift) { return std::min(255, int(float(colour >> shift & 0xFF) * k)); };
  return IM_COL32(c(IM_COL32_R_SHIFT), c(IM_COL32_G_SHIFT), c(IM_COL32_B_SHIFT), alpha);
}

// A text line centred on `cx`.
void Centred(ImDrawList* dl, ImFont* font, float size, float cx, float y, ImU32 col, const char* text) {
  const ImVec2 ts = TextSize(font, size, text);
  Label(dl, font, size, {cx - ts.x / 2, y}, col, text);
}

// The starry blue backdrop of the later games' menus: a deep blue gradient, a brighter band where the
// portraits stand, and soft stars that twinkle.
void DrawBackdrop(ImDrawList* dl, float W, float H, float t) {
  GradientRect(dl, {0, 0}, {W, H}, Rgba(8, 22, 70, 248), Rgba(4, 10, 40, 248), 0.0f);
  const float band = H * 0.45f;
  for (int k = 0; k < 12; ++k) {
    const float r = H * (0.42f - k * 0.03f);
    dl->AddCircleFilled({W * 0.5f, band}, r, Rgba(40, 90, 200, 10), 64);
  }
  // Fixed stars (a small hash of their number), each twinkling at its own pace.
  for (int i = 0; i < 90; ++i) {
    const uint32_t h = uint32_t(i) * 2654435761u;
    const float x = float(h % 1000) / 1000.0f * W, y = float((h >> 10) % 1000) / 1000.0f * H;
    const float tw = 0.5f + 0.5f * std::sin(t * (1.0f + (h >> 20) % 5) + float(i));
    const float s = S(1.0f + float((h >> 24) % 3));
    dl->AddCircleFilled({x, y}, s * 2.5f, Rgba(120, 170, 255, int(18 * tw)), 12);
    dl->AddCircleFilled({x, y}, s * 0.8f, Rgba(200, 225, 255, int(60 + 120 * tw)), 8);
  }
}

// A portrait in its gold-ringed circle; `focus` from 0 (resting) to 1 (chosen).
void DrawPortrait(ImDrawList* dl, ImVec2 c, float r, const Level& level, float focus) {
  // The glow behind the chosen one.
  for (int k = 6; k >= 1; --k) {
    dl->AddCircleFilled(c, r + S(k * 5.0f) * focus, Rgba(255, 220, 120, int(14 * focus)), 64);
  }
  dl->AddCircleFilled(c, r, Scaled(level.colour, 0.55f + 0.45f * focus), 64);
  // A lighter centre, as a lit disc.
  dl->AddCircleFilled({c.x - r * 0.15f, c.y - r * 0.2f}, r * 0.62f,
                      Scaled(level.colour, 0.85f + 0.4f * focus, int(110 + 60 * focus)), 48);
  if (ImTextureID tex = GameArtTexture(level.portrait)) {
    const float inner = r * 0.9f;
    const ImU32 tint = Scaled(IM_COL32(255, 255, 255, 255), 0.55f + 0.45f * focus);
    dl->AddImageRounded(tex, {c.x - inner, c.y - inner}, {c.x + inner, c.y + inner}, {0, 0}, {1, 1},
                        tint, inner);
  }
  // The gold rings: dark outside, bright inside.
  dl->AddCircle(c, r + S(2), kGoldDark, 64, S(7));
  dl->AddCircle(c, r, focus > 0.5f ? kGold : Scaled(kGold, 0.75f), 64, S(4.5f));
}

float PillWidth(const char* label) {
  return S(26) + S(17) * 2 + S(14) + TextSize(BoldFont(), S(30), label).x + S(30);
}

// A blue pill button with a round face-button glyph, as under the later games' menus. Returns its
// width.
float PillButton(ImDrawList* dl, ImVec2 pos, const char* glyph, const char* label, bool* clicked) {
  const float h = S(58), glyph_r = S(17);
  const float w = PillWidth(label);
  const ImVec2 p1{pos.x + w, pos.y + h};
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const bool hover = mouse.x >= pos.x && mouse.x < p1.x && mouse.y >= pos.y && mouse.y < p1.y;
  GradientRect(dl, pos, p1, hover ? Rgba(70, 150, 255) : Rgba(46, 124, 232),
               hover ? Rgba(26, 84, 200) : Rgba(18, 70, 180), S(12));
  dl->AddRect(pos, p1, Rgba(150, 200, 255, 120), S(12), 0, S(2));
  const ImVec2 gc{pos.x + S(26) + glyph_r, pos.y + h / 2};
  // The game's own button picture (ui_images.cpp: its HUD's a_button / b_button); a drawn one until
  // it is loaded.
  const std::string art = glyph[0] == 'B' ? "ui:button_b" : "ui:button_a";
  if (ImTextureID tex = GameArtTexture(art)) {
    const float g = glyph_r * 1.3f;
    dl->AddImage(tex, {gc.x - g, gc.y - g}, {gc.x + g, gc.y + g});
  } else {
    dl->AddCircleFilled(gc, glyph_r, glyph[0] == 'B' ? kBackRed : kAcceptGreen, 32);
    const ImVec2 gs = TextSize(BoldFont(), S(24), glyph);
    Label(dl, BoldFont(), S(24), {gc.x - gs.x / 2, CapCentredY(gc.y, S(24))}, kWhite, glyph);
  }
  OutlinedLabel(dl, BoldFont(), S(30), {gc.x + glyph_r + S(14), CapCentredY(gc.y, S(30))}, kGold, label,
                S(1.5f));
  if (clicked) *clicked = hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  return w;
}

class DifficultyDialog final : public rex::ui::ImGuiDialog {
 public:
  // `new_game`: the game waits for the choice, and there's no way back.
  DifficultyDialog(rex::ui::ImGuiDrawer* drawer, std::string slot, Difficulty current, bool new_game,
                   std::function<void()> on_done)
      : ImGuiDialog(drawer),
        slot_(std::move(slot)),
        chosen_(int(current)),
        new_game_(new_game),
        on_done_(std::move(on_done)) {
    g_german = GermanText();
    skylanders::menu_input::SetMenuOpen(true);
    REXLOG_INFO("Difficulty: the picker opened for {}{}", slot_, new_game_ ? " (new game)" : "");
  }

  ~DifficultyDialog() override {
    if (!done_) {
      skylanders::menu_input::SetMenuOpen(false);
      if (new_game_) ReleaseGame();
    }
  }

  void Finish(std::optional<Difficulty> choice) {
    if (done_) return;
    done_ = true;
    if (choice) Choose(slot_, *choice);
    skylanders::menu_input::SetMenuOpen(false);
    if (new_game_) ReleaseGame();
    if (on_done_) on_done_();
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (done_) return;
    // Another menu that opened over this one and closed again gave the controllers back to the game.
    if (!skylanders::menu_input::MenuOpen()) skylanders::menu_input::SetMenuOpen(true);
    SetScale(io.DisplaySize.y / 1080.0f);
    HandleInput();
    if (done_) return;

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    if (first_frame_) {
      ImGui::SetNextWindowFocus();
      first_frame_ = false;
    }
    ImGui::Begin("##difficulty_menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int first_vertex = dl->VtxBuffer.Size;
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    const float t = std::chrono::duration<float>(Clock::now() - open_time_).count();
    DrawBackdrop(dl, W, H, t);
    DrawVignette(dl, W, H);

    // Title, as the later games' "CHOOSE YOUR DIFFICULTY".
    const char* title = Tr("CHOOSE YOUR DIFFICULTY", "W\xC3\x84HLE DEINEN SCHWIERIGKEITSGRAD");
    const float title_size = std::min(S(78), W * 0.9f / std::max(1.0f, TextSize(TitleFont(), 1.0f, title).x));
    const ImVec2 ts = TextSize(TitleFont(), title_size, title);
    OutlinedLabel(dl, TitleFont(), title_size, {(W - ts.x) / 2, H * 0.11f}, kGold, title, S(3.5f));

    DrawPortraits(dl, W, H * 0.41f);
    DrawDescription(dl, W, H);

    // The buttons, centred under the banner.
    const char* accept = Tr("Accept", "Annehmen");
    const char* back = Tr("Back", "Zur\xC3\xBC" "ck");
    const float aw = PillWidth(accept);
    const float bw = new_game_ ? 0.0f : PillWidth(back);
    const float gap = new_game_ ? 0.0f : S(28);
    float x = (W - aw - bw - gap) / 2;
    const float y = H * 0.85f;
    bool accept_clicked = false, back_clicked = false;
    x += PillButton(dl, {x, y}, "A", accept, &accept_clicked) + gap;
    if (!new_game_) PillButton(dl, {x, y}, "B", back, &back_clicked);
    if (!waiting_for_release_) {
      if (accept_clicked) clicked_accept_ = true;
      if (back_clicked) clicked_back_ = true;
    }
    FadeVertices(dl, first_vertex, std::min(1.0f, t / 0.25f));
    ImGui::End();
  }

 private:
  void Move(int delta) { chosen_ = std::clamp(chosen_ + delta, 0, kDifficultyCount - 1); }

  void HandleInput() {
    bool released = true, accept = false, back = false;
    int delta = 0;
    for (const auto& device : skylanders::menu_input::ConnectedDevices()) {
      if (!device.pollable) continue;
      const auto pad = skylanders::menu_input::ReadPad(device.id);
      if (!pad) continue;
      released = released && PadState::Released(*pad);
      const PadEdges e = pads_[uint64_t(device.id)].Read(*pad);
      if (waiting_for_release_) continue;
      if (e.left || e.right || e.accept || e.back) UseKeyboardHints(device.keyboard);
      delta += (e.right ? 1 : 0) - (e.left ? 1 : 0);
      accept |= e.accept;
      back |= e.back;
    }
    if (waiting_for_release_) {
      // The press that started the game (A on the save slot) is still down: nothing counts yet.
      if (released && !ImGui::IsKeyDown(ImGuiKey_Enter) && !ImGui::IsKeyDown(ImGuiKey_Space)) {
        waiting_for_release_ = false;
      }
      return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) --delta;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) ++delta;
    accept |= ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false);
    back |= ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (delta) Move(delta > 0 ? 1 : -1);
    // The mouse: pointing at a portrait picks it, a click on it or on Accept starts.
    const ImVec2 md = ImGui::GetIO().MouseDelta;
    if (hovered_ >= 0 && (md.x != 0.0f || md.y != 0.0f)) chosen_ = hovered_;
    if (hovered_ >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      chosen_ = hovered_;
      accept = true;
    }
    accept |= clicked_accept_;
    back |= clicked_back_;
    clicked_accept_ = clicked_back_ = false;
    if (accept) {
      Finish(Difficulty(chosen_));
    } else if (back && !new_game_) {
      Finish(std::nullopt);
    }
  }

  // The four portraits in a row; the chosen one grows (eased) and glows, the others rest smaller.
  void DrawPortraits(ImDrawList* dl, float W, float cy) {
    hovered_ = -1;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float dt = std::min(0.1f, ImGui::GetIO().DeltaTime);
    const float spacing = std::min(S(330), W / (kDifficultyCount + 0.6f));
    const float x0 = W / 2 - spacing * (kDifficultyCount - 1) / 2;
    // The resting ones first, the chosen one last, so it is drawn over its neighbours.
    int order[kDifficultyCount];
    for (int i = 0, n = 0; i < kDifficultyCount; ++i) {
      if (i != chosen_) order[n++] = i;
    }
    order[kDifficultyCount - 1] = chosen_;
    for (int i = 0; i < kDifficultyCount; ++i) {
      const float target = i == chosen_ ? 1.0f : 0.0f;
      focus_[i] += (target - focus_[i]) * std::min(1.0f, dt * 12.0f);
    }
    for (const int i : order) {
      const ImVec2 c{x0 + spacing * float(i), cy};
      const float r = S(112) + S(38) * focus_[i];
      const float dx = mouse.x - c.x, dy = mouse.y - c.y;
      if (dx * dx + dy * dy <= r * r) hovered_ = i;
      DrawPortrait(dl, c, r, kLevels[i], focus_[i]);
    }
  }

  // The chosen difficulty's name, and its description in a red banner with a double gold frame.
  void DrawDescription(ImDrawList* dl, float W, float H) {
    const Level& level = kLevels[chosen_];
    const float cx = W / 2;
    const char* name = Tr(level.en, level.de);
    std::string upper(name);
    for (auto& ch : upper) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    const ImVec2 ns = TextSize(TitleFont(), S(64), upper);
    OutlinedLabel(dl, TitleFont(), S(64), {cx - ns.x / 2, H * 0.585f}, kGold, upper, S(3));

    // The lines: who it is for, what it changes, and the rewards.
    char change[200];
    if (Difficulty(chosen_) == Difficulty::kAdventurer) {
      std::snprintf(change, sizeof(change), "%s",
                    Tr("Enemies and healing as in the game.", "Gegner und Heilung wie im Spiel."));
    } else if (level.food >= 1.0f) {
      std::snprintf(change, sizeof(change),
                    Tr("Enemies deal %d%% damage and have %d%% health.",
                       "Gegner machen %d%% Schaden und haben %d%% Lebenspunkte."),
                    int(std::lround(level.damage * 100)), int(std::lround(level.health * 100)));
    } else {
      std::snprintf(change, sizeof(change),
                    Tr("Enemies deal %d%% damage and have %d%% health. Food heals %d%%.",
                       "Gegner machen %d%% Schaden und haben %d%% Lebenspunkte. Essen heilt %d%%."),
                    int(std::lround(level.damage * 100)), int(std::lround(level.health * 100)),
                    int(std::lround(level.food * 100)));
    }
    char rewards[64] = "";
    if (level.reward > 1.0f) {
      std::snprintf(rewards, sizeof(rewards), Tr("+%d%% Rewards", "+%d%% Belohnungen"),
                    int(std::lround((level.reward - 1.0f) * 100)));
    }
    const char* lines[3] = {Tr(level.tagline_en, level.tagline_de), change, rewards};
    const int count = rewards[0] ? 3 : 2;
    const float line_h = S(44), text_size = S(31);
    float text_w = 0.0f;
    for (int i = 0; i < count; ++i) text_w = std::max(text_w, TextSize(BoldFont(), text_size, lines[i]).x);
    const float bw = std::min(W - S(60), std::max(S(1000), text_w + S(120)));
    const ImVec2 b0{cx - bw / 2, H * 0.655f}, b1{cx + bw / 2, H * 0.655f + S(36) + line_h * float(count)};
    // Gold outer frame, a dark line, gold inner frame, then the red field.
    dl->AddRectFilled({b0.x - S(9), b0.y - S(9)}, {b1.x + S(9), b1.y + S(9)}, kGold, S(40));
    dl->AddRectFilled({b0.x - S(5), b0.y - S(5)}, {b1.x + S(5), b1.y + S(5)}, kGoldDark, S(36));
    dl->AddRectFilled({b0.x - S(3), b0.y - S(3)}, {b1.x + S(3), b1.y + S(3)}, Rgba(255, 214, 120), S(34));
    GradientRect(dl, b0, b1, Rgba(190, 40, 30), Rgba(130, 20, 18), S(32));
    // The curls at the banner's ends.
    for (const float side : {-1.0f, 1.0f}) {
      const ImVec2 c{side < 0 ? b0.x + S(30) : b1.x - S(30), b0.y + S(24)};
      dl->PathArcTo(c, S(13), 0.0f, 4.7f, 16);
      dl->PathStroke(kGold, 0, S(3.5f));
      const ImVec2 d{side < 0 ? b0.x + S(30) : b1.x - S(30), b1.y - S(24)};
      dl->PathArcTo(d, S(13), 3.14f, 7.9f, 16);
      dl->PathStroke(kGold, 0, S(3.5f));
    }
    for (int i = 0; i < count; ++i) {
      const ImVec2 ls = TextSize(BoldFont(), text_size, lines[i]);
      OutlinedLabel(dl, BoldFont(), text_size, {cx - ls.x / 2, b0.y + S(18) + line_h * float(i)},
                    i == 0 ? kWhite : Rgba(255, 236, 170), lines[i], S(1.5f));
    }
  }

  std::string slot_;
  int chosen_;
  bool new_game_;
  std::function<void()> on_done_;
  bool done_ = false;
  bool first_frame_ = true;
  bool waiting_for_release_ = true;
  bool clicked_accept_ = false, clicked_back_ = false;
  int hovered_ = -1;
  float focus_[kDifficultyCount] = {};
  float vignette_[4] = {};  // the vignette's colour, easing to the chosen one's (r, g, b, strength)

  // The screen's edges fade into the chosen difficulty's colour, a coloured vignette, which eases to
  // the next colour when the choice changes.
  void DrawVignette(ImDrawList* dl, float W, float H) {
    const ImU32 c = kLevels[chosen_].colour;
    const float target[4] = {float(c >> IM_COL32_R_SHIFT & 0xFF), float(c >> IM_COL32_G_SHIFT & 0xFF),
                             float(c >> IM_COL32_B_SHIFT & 0xFF), 1.0f};
    if (vignette_[3] == 0.0f) std::copy(target, target + 3, vignette_);  // the first colour at once
    const float k = std::min(1.0f, ImGui::GetIO().DeltaTime * 8.0f);
    for (int i = 0; i < 4; ++i) vignette_[i] += (target[i] - vignette_[i]) * k;
    const auto col = [&](float a) {
      return IM_COL32(int(vignette_[0]), int(vignette_[1]), int(vignette_[2]),
                      int(std::clamp(a * vignette_[3], 0.0f, 1.0f) * 255));
    };
    // Each edge a band whose colour fades to nothing inwards; layered for a soft, rounder falloff.
    const float bands[] = {0.32f, 0.2f, 0.1f};
    const float alphas[] = {0.35f, 0.35f, 0.35f};
    for (int b = 0; b < 3; ++b) {
      const float dx = W * bands[b], dy = H * bands[b];
      const ImU32 edge = col(alphas[b]), none = col(0.0f);
      dl->AddRectFilledMultiColor({0, 0}, {dx, H}, edge, none, none, edge);
      dl->AddRectFilledMultiColor({W - dx, 0}, {W, H}, none, edge, edge, none);
      dl->AddRectFilledMultiColor({0, 0}, {W, dy}, edge, edge, none, none);
      dl->AddRectFilledMultiColor({0, H - dy}, {W, H}, none, none, edge, edge);
    }
  }
  Clock::time_point open_time_ = Clock::now();
  std::unordered_map<uint64_t, PadState> pads_;
};

// UI thread only.
std::unique_ptr<DifficultyDialog> g_menu;
rex::ui::ImGuiDrawer* g_drawer = nullptr;
rex::ui::WindowedAppContext* g_app_context = nullptr;

void OpenPicker(std::string slot, bool new_game) {
  if (g_shutdown.load() || !g_drawer) {
    if (new_game) ReleaseGame();
    return;
  }
  if (g_menu) {
    if (new_game) ReleaseGame();
    return;
  }
  // Another menu is open (the portal menu, the settings): the picker comes when it closes.
  if (skylanders::menu_input::MenuOpen()) {
    std::thread([slot = std::move(slot), new_game] {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      if (g_shutdown.load() || !g_app_context) {
        if (new_game) ReleaseGame();
        return;
      }
      g_app_context->CallInUIThread([slot, new_game] { OpenPicker(slot, new_game); });
    }).detach();
    return;
  }
  Difficulty current;
  {
    std::lock_guard lock(g_mutex);
    const auto it = g_saved.find(slot);
    current = it == g_saved.end() ? Difficulty::kAdventurer : it->second;
  }
  g_menu = std::make_unique<DifficultyDialog>(g_drawer, std::move(slot), current, new_game, [] {
    // The dialog finishes inside its own OnDraw; destroy it afterwards.
    g_app_context->CallInUIThreadDeferred([] { g_menu.reset(); });
  });
}

}  // namespace

const char* DifficultyKey(Difficulty difficulty) { return LevelOf(difficulty).key; }

void InstallDifficulty(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext* app_context,
                       const std::filesystem::path& user_data_root) {
  g_drawer = drawer;
  g_app_context = app_context;
  std::lock_guard lock(g_mutex);
  g_user_data = user_data_root;
  LoadLocked();
  REXLOG_INFO("Difficulty: {} save(s) with a chosen difficulty", g_saved.size());
}

void ShutdownDifficulty() {
  g_shutdown.store(true);
  g_menu.reset();
  ReleaseGame();
}

void OnSavePackage(const std::string& name, uint32_t how, bool on_main_menu) {
  if (!IsSlotSave(name)) return;
  // 1 create new, 2 create always, 5 truncate: the game writes the save; 3 / 4 open it.
  const bool write = how == 1 || how == 2 || how == 5;
  bool new_game = false;
  {
    std::lock_guard lock(g_mutex);
    if (g_user_data.empty()) return;
    const bool exists = SaveExists(name);
    if (write && (on_main_menu || !exists)) {  // in play the game saves often: not logged
      REXLOG_INFO("Difficulty: the game writes {} (flags {}, {}, {})", name, how,
                  exists ? "over an old one" : "a new one", on_main_menu ? "main menu" : "in a level");
    }
    // A new game: a slot save written from the main menu (the game writes it there when a new game
    // starts, over an old one in that slot too; in play it saves only inside the levels), or one that
    // may be created and isn't there yet. Once per stay on the main menu.
    if (write && !on_main_menu) g_asked.clear();
    new_game = ((write && on_main_menu) || ((write || how == 4) && !exists)) && g_asked != name;
    if (new_game) {
      g_asked = name;
      // Whatever an earlier save in this slot had goes; Adventurer until chosen.
      g_saved[name] = Difficulty::kAdventurer;
      StoreLocked();
      REXLOG_INFO("Difficulty: new game in {}", name);
    }
    SetActiveLocked(name, write || new_game);
  }
  if (!new_game || !REXCVAR_GET(difficulty_ask_new_game) || !g_app_context || g_shutdown.load()) return;
  // The game waits here, at its first save, until the picker has its answer.
  {
    std::lock_guard lock(g_wait_mutex);
    g_waiting = true;
  }
  g_app_context->CallInUIThread([name] { OpenPicker(name, true); });
  std::unique_lock lock(g_wait_mutex);
  g_wait_cv.wait(lock, [] { return !g_waiting || g_shutdown.load(); });
  REXLOG_INFO("Difficulty: the game goes on");
}

float DamageTakenScale() { return LevelOf(Difficulty(g_level.load())).damage; }
float EnemyHealthScale() { return LevelOf(Difficulty(g_level.load())).health; }
float RewardScale() { return LevelOf(Difficulty(g_level.load())).reward; }
float FoodHealScale() { return LevelOf(Difficulty(g_level.load())).food; }
float LevelUpHealPart() { return LevelOf(Difficulty(g_level.load())).level_up; }

skylanders::Setting ChangeDifficultySetting() {
  skylanders::Setting s;
  s.en = "Change this game's difficulty";
  s.de = "Schwierigkeitsgrad dieses Spiels \xC3\xA4ndern";
  s.help_en =
      "Easy, Adventurer (as on the console), Hard or Nightmare for the save you are playing: enemy "
      "damage and health, food, and more XP and gold on Hard and Nightmare. A new game asks by itself.";
  s.help_de =
      "Leicht, Abenteurer (wie auf der Konsole), Schwer oder Albtraum f\xC3\xBCr den Spielstand, den du "
      "spielst: Schaden und Lebenspunkte der Gegner, Essen, und mehr EP und Gold auf Schwer und "
      "Albtraum. Ein neues Spiel fragt von selbst danach.";
  s.action = [] {
    std::string slot;
    {
      std::lock_guard lock(g_mutex);
      slot = g_active;
    }
    if (slot.empty()) {
      REXLOG_INFO("Difficulty: no save is being played yet; load one first");
      return;
    }
    // The settings menu closes first; the picker comes up right after.
    if (g_app_context) g_app_context->CallInUIThreadDeferred([slot] { OpenPicker(slot, false); });
  };
  return s;
}

}  // namespace ssarecomp

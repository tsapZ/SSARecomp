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

struct Level {
  const char* key;
  float multiplier;  // enemies' damage and health
  // From Swap Force's difficulty table (data/systemdata.mua, globalTuningData): food heals this
  // part of what it would (_difficultyFixedFoodHealthGainScale), and a level-up gives back this
  // part of the full health (_difficultyLevelUpHealthRecovery; Spyro's Adventure gives it all).
  float food;
  float level_up;
  const char* en;
  const char* de;
  const char* blurb_en;
  const char* blurb_de;
  ImU32 colour;  // the card's colour, from calm to fierce
};

constexpr std::array<Level, kDifficultyCount> kLevels{{
    {"easy", 0.6f, 1.0f, 1.0f, "Easy", "Leicht",
     "Enemies hit softer and go down faster. For new Portal Masters and younger players.",
     "Gegner treffen schw\xC3\xA4" "cher und fallen schneller. F\xC3\xBCr neue Portal-Meister und "
     "j\xC3\xBCngere Spieler.",
     IM_COL32(76, 175, 80, 255)},
    {"adventurer", 1.0f, 1.0f, 1.0f, "Adventurer", "Abenteurer",
     "The adventure as it was made: the game as it plays on the console.",
     "Das Abenteuer, wie es gemacht wurde: das Spiel wie auf der Konsole.",
     IM_COL32(46, 134, 222, 255)},
    {"hard", 2.0f, 0.75f, 0.2f, "Hard", "Schwer",
     "Enemies hit twice as hard and take twice the hits, and food heals less. For seasoned Portal "
     "Masters.",
     "Gegner treffen doppelt so hart und halten doppelt so viel aus, und Essen heilt weniger. "
     "F\xC3\xBCr erfahrene Portal-Meister.",
     IM_COL32(240, 140, 30, 255)},
    {"nightmare", 4.0f, 0.6f, 0.1f, "Nightmare", "Albtraum",
     "Enemies hit four times as hard and take four times the hits, and food heals even less. Only "
     "the bravest Skylanders make it through.",
     "Gegner treffen viermal so hart und halten viermal so viel aus, und Essen heilt noch weniger. "
     "Nur die mutigsten Skylander kommen durch.",
     IM_COL32(170, 40, 160, 255)},
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
           "# (easy x0.6, adventurer x1, hard x2, nightmare x4). F4 > Extras changes it.\n";
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
    REXLOG_INFO("Difficulty: {} is played on {} (enemies x{:.1f})", slot, LevelOf(d).en,
                LevelOf(d).multiplier);
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
  REXLOG_INFO("Difficulty: {} chosen for {} (enemies x{:.1f})", LevelOf(d).en, slot,
              LevelOf(d).multiplier);
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

// The level's emblem: a shield with one to four flames, more of them the harder it is.
void DrawEmblem(ImDrawList* dl, ImVec2 c, float size, int level, ImU32 colour, bool chosen) {
  const float w = size * 0.5f, h = size * 0.6f;
  // Shield: a rounded top and a point at the bottom.
  const ImVec2 pts[] = {{c.x - w, c.y - h * 0.75f}, {c.x + w, c.y - h * 0.75f},
                        {c.x + w, c.y + h * 0.05f}, {c.x, c.y + h},
                        {c.x - w, c.y + h * 0.05f}};
  const ImU32 fill = chosen ? colour : IM_COL32((colour >> IM_COL32_R_SHIFT & 0xFF) / 2,
                                                (colour >> IM_COL32_G_SHIFT & 0xFF) / 2,
                                                (colour >> IM_COL32_B_SHIFT & 0xFF) / 2, 255);
  dl->AddConvexPolyFilled(pts, 5, fill);
  dl->AddPolyline(pts, 5, chosen ? kGold : kGoldDark, ImDrawFlags_Closed, S(chosen ? 4.0f : 2.5f));
  // Flames: teardrops pointing up, side by side.
  const int n = level + 1;
  const float fw = w * (n <= 2 ? 0.42f : 0.34f);
  const float gap = fw * 0.25f;
  const float total = n * fw * 2 + (n - 1) * gap;
  for (int i = 0; i < n; ++i) {
    const float x = c.x - total / 2 + fw + i * (fw * 2 + gap);
    const float y = c.y - h * 0.05f;
    const ImU32 flame = chosen ? kWhite : kDim;
    dl->AddCircleFilled({x, y + fw * 0.35f}, fw, flame, 20);
    dl->AddTriangleFilled({x - fw * 0.97f, y + fw * 0.15f}, {x + fw * 0.97f, y + fw * 0.15f},
                          {x, y - fw * 1.9f}, flame);
  }
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
    dl->AddRectFilled({0, 0}, {W, H}, Rgba(6, 10, 30, new_game_ ? 200 : 150));
    const float pw = std::min(W - S(60), S(1560));
    const ImVec2 p0{(W - pw) / 2, H * 0.12f}, p1{(W + pw) / 2, H * 0.88f};
    DrawPanel(dl, p0, p1);

    // Title, centred, as in the later games' "Select difficulty".
    const char* title = Tr("CHOOSE YOUR DIFFICULTY", "W\xC3\x84HLE DEINEN SCHWIERIGKEITSGRAD");
    const ImVec2 ts = TextSize(TitleFont(), S(50), title);
    OutlinedLabel(dl, TitleFont(), S(50), {(p0.x + p1.x - ts.x) / 2, p0.y + S(22)}, kGold, title,
                  S(2.5f));
    const char* sub = new_game_
                          ? Tr("How tough should Kaos' minions be in this adventure?",
                               "Wie stark sollen Kaos' Schergen in diesem Abenteuer sein?")
                          : Tr("Change how tough the enemies are in this adventure.",
                               "\xC3\x84ndere, wie stark die Gegner in diesem Abenteuer sind.");
    const ImVec2 ss = TextSize(BoldFont(), S(22), sub);
    Label(dl, BoldFont(), S(22), {(p0.x + p1.x - ss.x) / 2, p0.y + S(86)}, kDim, sub);

    DrawCards(dl, {p0.x + S(40), p0.y + S(140)}, {p1.x - S(40), p0.y + S(140) + S(400)});
    DrawDescription(dl, {p0.x + S(60), p0.y + S(570)}, {p1.x - S(60), p1.y - S(80)});

    float x = p0.x + S(40);
    const float y = p1.y - S(54);
    x += Hint(dl, {x, y}, "< / >", kHintGrey, Tr("Choose", "W\xC3\xA4hlen"), false);
    x += Hint(dl, {x, y}, "A", kAcceptGreen, new_game_ ? Tr("Start", "Los geht's") : Tr("Select", "W\xC3\xA4hlen"),
              true, "Enter");
    if (!new_game_) Hint(dl, {x, y}, "B", kBackRed, Tr("Back", "Zur\xC3\xBC" "ck"), true, "Esc");
    FadeVertices(dl, first_vertex,
                 std::min(1.0f, std::chrono::duration<float>(Clock::now() - open_time_).count() / 0.2f));
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
    // The mouse: pointing at a card picks it, a click starts.
    if (hovered_ >= 0 && ImGui::GetIO().MouseDelta.x != 0.0f) chosen_ = hovered_;
    if (hovered_ >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      chosen_ = hovered_;
      accept = true;
    }
    if (accept) {
      Finish(Difficulty(chosen_));
    } else if (back && !new_game_) {
      Finish(std::nullopt);
    }
  }

  void DrawCards(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    const float gap = S(26);
    const float card_w = (b.x - a.x - gap * (kDifficultyCount - 1)) / kDifficultyCount;
    hovered_ = -1;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (int i = 0; i < kDifficultyCount; ++i) {
      const Level& level = kLevels[i];
      const bool chosen = i == chosen_;
      // The chosen card stands a little taller, as in the later games.
      const float lift = chosen ? S(16) : 0.0f;
      const ImVec2 c0{a.x + i * (card_w + gap), a.y + S(20) - lift}, c1{c0.x + card_w, b.y - S(20) + lift};
      if (mouse.x >= c0.x && mouse.x < c1.x && mouse.y >= c0.y && mouse.y < c1.y) hovered_ = i;
      const ImU32 r = level.colour >> IM_COL32_R_SHIFT & 0xFF, g = level.colour >> IM_COL32_G_SHIFT & 0xFF,
                  bl = level.colour >> IM_COL32_B_SHIFT & 0xFF;
      GradientRect(dl, c0, c1, chosen ? IM_COL32(r * 3 / 5, g * 3 / 5, bl * 3 / 5, 245) : Rgba(44, 52, 104, 220),
                   chosen ? IM_COL32(r / 5, g / 5, bl / 5, 245) : Rgba(22, 26, 62, 220), S(20));
      if (chosen) {
        // A glow around the chosen card.
        for (int k = 1; k <= 4; ++k) {
          dl->AddRect({c0.x - S(k * 2.0f), c0.y - S(k * 2.0f)}, {c1.x + S(k * 2.0f), c1.y + S(k * 2.0f)},
                      IM_COL32(244, 196, 84, 70 - k * 15), S(20 + k * 2.0f), 0, S(2));
        }
      }
      dl->AddRect(c0, c1, chosen ? kGold : Rgba(255, 255, 255, 50), S(20), 0, chosen ? S(4) : S(1.5f));
      // Colour band at the top.
      dl->AddRectFilled(c0, {c1.x, c0.y + S(12)}, chosen ? level.colour : IM_COL32(r / 2, g / 2, bl / 2, 255),
                        S(20), ImDrawFlags_RoundCornersTop);

      const float cx = (c0.x + c1.x) / 2;
      DrawEmblem(dl, {cx, c0.y + S(120)}, S(chosen ? 150 : 128), i, level.colour, chosen);

      const char* name = Tr(level.en, level.de);
      const float name_size = S(chosen ? 40 : 34);
      const ImVec2 ns = TextSize(TitleFont(), name_size, name);
      OutlinedLabel(dl, TitleFont(), name_size, {cx - ns.x / 2, c0.y + S(220)}, chosen ? kWhite : kDim,
                    name, S(2));

      char strength[48];
      std::snprintf(strength, sizeof(strength), "%s x%.1f", Tr("Enemies", "Gegner"), level.multiplier);
      const ImVec2 ms = TextSize(BoldFont(), S(24), strength);
      Label(dl, BoldFont(), S(24), {cx - ms.x / 2, c0.y + S(276)}, chosen ? kGold : kDim, strength);

      if (Difficulty(i) == Difficulty::kAdventurer) {
        const char* tag = Tr("As on the console", "Wie auf der Konsole");
        const ImVec2 tsz = TextSize(BoldFont(), S(18), tag);
        const ImVec2 t0{cx - tsz.x / 2 - S(12), c1.y - S(52)}, t1{cx + tsz.x / 2 + S(12), c1.y - S(20)};
        dl->AddRectFilled(t0, t1, Rgba(255, 255, 255, chosen ? 40 : 22), S(16));
        Label(dl, BoldFont(), S(18), {t0.x + S(12), CapCentredY((t0.y + t1.y) / 2, S(18))}, kWhite, tag);
      }
    }
  }

  void DrawDescription(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    const Level& level = kLevels[chosen_];
    dl->AddRectFilled(a, b, Rgba(255, 255, 255, 14), S(16));
    const float wrap = b.x - a.x - S(48);
    Label(dl, BoldFont(), S(26), {a.x + S(24), a.y + S(18)}, kWhite, Tr(level.blurb_en, level.blurb_de), wrap);
    char detail[160];
    if (level.multiplier == 1.0f) {
      std::snprintf(detail, sizeof(detail), "%s",
                    Tr("Enemy damage and toughness as in the game.",
                       "Schaden und Ausdauer der Gegner wie im Spiel."));
    } else {
      std::snprintf(detail, sizeof(detail), Tr("Enemy damage x%.1f  \xC2\xB7  Enemy toughness x%.1f",
                                               "Schaden der Gegner x%.1f  \xC2\xB7  Ausdauer der Gegner x%.1f"),
                    level.multiplier, level.multiplier);
    }
    Label(dl, BoldFont(), S(21), {a.x + S(24), b.y - S(76)}, level.colour == kLevels[1].colour ? kDim : kGold,
          detail);
    Label(dl, BoldFont(), S(18), {a.x + S(24), b.y - S(40)}, Rgba(205, 214, 238, 150),
          Tr("The save keeps it. F4 > Extras changes it later.",
             "Der Spielstand beh\xC3\xA4lt ihn. F4 > Extras \xC3\xA4ndert ihn sp\xC3\xA4ter."));
  }

  std::string slot_;
  int chosen_;
  bool new_game_;
  std::function<void()> on_done_;
  bool done_ = false;
  bool first_frame_ = true;
  bool waiting_for_release_ = true;
  int hovered_ = -1;
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

float DifficultyMultiplier(Difficulty difficulty) { return LevelOf(difficulty).multiplier; }
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

float EnemyMultiplier() { return LevelOf(Difficulty(g_level.load())).multiplier; }
float FoodHealScale() { return LevelOf(Difficulty(g_level.load())).food; }
float LevelUpHealPart() { return LevelOf(Difficulty(g_level.load())).level_up; }

skylanders::Setting ChangeDifficultySetting() {
  skylanders::Setting s;
  s.en = "Change this game's difficulty";
  s.de = "Schwierigkeitsgrad dieses Spiels \xC3\xA4ndern";
  s.help_en =
      "Easy (enemies x0.6), Adventurer (x1, as on the console), Hard (x2) or Nightmare (x4) for the "
      "save you are playing. A new game asks for one by itself.";
  s.help_de =
      "Leicht (Gegner x0,6), Abenteurer (x1, wie auf der Konsole), Schwer (x2) oder Albtraum (x4) "
      "f\xC3\xBCr den Spielstand, den du spielst. Ein neues Spiel fragt von selbst danach.";
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

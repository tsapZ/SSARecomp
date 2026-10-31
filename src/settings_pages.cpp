#include "settings_pages.h"

#include <string>
#include <utility>

#include <rex/cvar.h>

#include "portal_rex/achievements_ui.h"
#include "portal_rex/discord_presence.h"
#include "portal_rex/player_menu.h"
#include "portal_rex/settings_schema.h"

namespace ssarecomp {
namespace {

using skylanders::Setting;
using skylanders::SettingOption;
using skylanders::SettingsPage;

// UTF-8 is written as escapes, split where a hex digit follows ("n\xC3\xA4" "chsten").

// Resolution scale and present effect together. The FPS are the hub measurements of 2026-10-19
// (#50: 2560x1440 output, RTV, no frame rate limit): scale 1 + FSR 169, scale 2 + CAS 83,
// scale 3 + CAS 40.
Setting PresetSetting() {
  Setting s;
  s.en = "Preset";
  s.de = "Voreinstellung";
  s.help_en =
      "Sets the internal resolution and sharpening together.\n"
      "Performance: the game's own 720p picture, upscaled with FSR. For weaker PCs.\n"
      "Quality (recommended): twice the resolution, sharpened. Sharp on a 1440p screen.\n"
      "Best: three times the resolution, for 4K screens. Needs a strong graphics card.\n"
      "\n"
      "Each step up roughly halves the frame rate.";
  s.help_de =
      "Stellt Aufl\xC3\xB6sung und Sch\xC3\xA4rfe zusammen ein.\n"
      "Leistung: das 720p-Bild des Spiels, mit FSR hochgerechnet. F\xC3\xBCr schw\xC3\xA4" "chere "
      "PCs.\n"
      "Qualit\xC3\xA4t (empfohlen): doppelte Aufl\xC3\xB6sung, nachgesch\xC3\xA4rft. Scharf auf "
      "einem 1440p-Bildschirm.\n"
      "Beste: dreifache Aufl\xC3\xB6sung, f\xC3\xBCr 4K-Bildschirme. Braucht eine starke "
      "Grafikkarte.\n"
      "\n"
      "Jede Stufe h\xC3\xB6her halbiert ungef\xC3\xA4hr die Bildrate.";
  s.options = {{"performance", "Performance", "Leistung"},
               {"quality", "Quality", "Qualit\xC3\xA4t"},
               {"best", "Best", "Beste"}};
  s.restart = true;
  s.current = [] {
    const std::string scale = rex::cvar::GetFlagByName("resolution_scale");
    const std::string effect = rex::cvar::GetFlagByName("present_effect");
    if (scale == "1" && effect == "fsr") return std::string("performance");
    if (scale == "2" && effect == "cas") return std::string("quality");
    if (scale == "3" && effect == "cas") return std::string("best");
    return std::string();
  };
  s.preset = [](const std::string& value) -> std::vector<std::pair<std::string, std::string>> {
    if (value == "performance") return {{"resolution_scale", "1"}, {"present_effect", "fsr"}};
    if (value == "best") return {{"resolution_scale", "3"}, {"present_effect", "cas"}};
    return {{"resolution_scale", "2"}, {"present_effect", "cas"}};
  };
  return s;
}

SettingsPage GraphicsPage() {
  SettingsPage page{"Graphics", "Grafik", {}};
  page.settings.push_back(PresetSetting());
  page.settings.push_back(
      {"resolution_scale", "Internal resolution", "Interne Aufl\xC3\xB6sung",
       "How finely the game draws its 3D scenes, as a multiple of its own 1280x720.\n"
       "2x: right for a 1440p screen.\n"
       "3x: right for a 4K screen.\n"
       "\n"
       "Higher is sharper but costs a lot: each step roughly halves the frame rate.",
       "Wie fein das Spiel seine 3D-Szenen zeichnet, als Vielfaches seiner eigenen 1280x720.\n"
       "2x: passt zu einem 1440p-Bildschirm.\n"
       "3x: passt zu einem 4K-Bildschirm.\n"
       "\n"
       "H\xC3\xB6her ist sch\xC3\xA4rfer, kostet aber viel: Jede Stufe halbiert ungef\xC3\xA4hr die "
       "Bildrate.",
       {{"1", "1x (720p)", "1x (720p)"},
        {"2", "2x (1440p)", "2x (1440p)"},
        {"3", "3x (4K)", "3x (4K)"},
        {"4", "4x", "4x"}}});
  Setting scene{"scene_size", "Native 3D picture (experimental)",
                "Natives 3D-Bild (experimentell)",
                "Draws the 3D scene at the screen's size instead of a smaller one.\n"
                "Off: as on the Xbox 360, the scene at 1120x704 stretched to 720p, a bit soft.\n"
                "Native: the scene at 1264x704, just under the 1280x720 screen. Sharper at every "
                "internal resolution.\n"
                "\n"
                "Costs some frame rate. Needs D3D12.",
                "Zeichnet die 3D-Szene in Bildschirmgr\xC3\xB6\xC3\x9F" "e statt kleiner.\n"
                "Aus: wie auf der Xbox 360, die Szene in 1120x704 auf 720p gestreckt, etwas weich.\n"
                "Nativ: die Szene in 1264x704, knapp unter den 1280x720 des Bildschirms. "
                "Sch\xC3\xA4rfer bei jeder internen Aufl\xC3\xB6sung.\n"
                "\n"
                "Kostet etwas Bildrate. Braucht D3D12.",
                {{"", "Off (1120x704)", "Aus (1120x704)"},
                 {"1280x720", "Native (720p)", "Nativ (720p)"}}};
  scene.restart = true;
  page.settings.push_back(scene);
  page.settings.push_back(
      {"present_effect", "Sharpening", "Sch\xC3\xA4rfe",
       "How the finished picture is scaled to your window.\n"
       "Soft: plain scaling, no sharpening.\n"
       "Sharpened (CAS): best when the internal resolution fits your screen.\n"
       "FSR 1: enlarges a smaller picture and sharpens it, good with 1x.\n"
       "\n"
       "Costs almost nothing.",
       "Wie das fertige Bild auf dein Fenster skaliert wird.\n"
       "Weich: einfaches Skalieren ohne Sch\xC3\xA4rfen.\n"
       "Gesch\xC3\xA4rft (CAS): am besten, wenn die interne Aufl\xC3\xB6sung zum Bildschirm passt.\n"
       "FSR 1: vergr\xC3\xB6\xC3\x9F" "ert ein kleineres Bild und sch\xC3\xA4rft es nach, gut bei "
       "1x.\n"
       "\n"
       "Kostet fast nichts.",
       {{"bilinear", "Soft", "Weich"},
        {"cas", "Sharpened (CAS)", "Gesch\xC3\xA4rft (CAS)"},
        {"fsr", "FSR 1 upscaling", "FSR 1 (hochskaliert)"}}});
  // Two of the SDK's presenter settings the menu didn't offer (presenter.cpp): CAS's strength and
  // dithering the 10-bit picture down to the window's 8 bits.
  Setting cas_strength{"present_cas_additional_sharpness", "Sharpening strength", "Sch\xC3\xA4rfe-St\xC3\xA4rke",
                       "How strongly Sharpened (CAS) sharpens.\n"
                       "Normal: gentle.\n"
                       "More: brings out texture detail and edges.\n"
                       "Maximum: can look grainy.",
                       "Wie stark \xE2\x80\x9EGesch\xC3\xA4rft (CAS)\xE2\x80\x9C sch\xC3\xA4rft.\n"
                       "Normal: sanft.\n"
                       "Mehr: holt Details und Kanten heraus.\n"
                       "Maximal: kann k\xC3\xB6rnig wirken.",
                       {{"0", "Normal", "Normal"}, {"0.5", "More", "Mehr"}, {"1", "Maximum", "Maximal"}}};
  cas_strength.restart = true;
  page.settings.push_back(cas_strength);
  Setting dither{"present_dither", "Smooth gradients (dithering)", "Farbverl\xC3\xA4ufe gl\xC3\xA4tten (Dithering)",
                 "Hides colour bands in skies and fog.\n"
                 "On: noise too fine to see smooths them out.\n"
                 "Off: skies and fog can fall into visible bands.\n"
                 "\n"
                 "The game draws in 10 bits per colour, the window shows 8.",
                 "Verdeckt Farbstreifen in Himmel und Nebel.\n"
                 "An: unsichtbar feines Rauschen gl\xC3\xA4ttet sie.\n"
                 "Aus: Himmel und Nebel k\xC3\xB6nnen in sichtbare Streifen zerfallen.\n"
                 "\n"
                 "Das Spiel rechnet mit 10 Bit pro Farbe, das Fenster zeigt 8."};
  dither.restart = true;
  page.settings.push_back(dither);
  page.settings.push_back(
      {"anisotropic_override", "Texture filtering", "Texturfilter",
       "Keeps floors and walls sharp when you look at them at a shallow angle.\n"
       "Off: they blur in the distance.\n"
       "16x (recommended): sharp into the distance.\n"
       "\n"
       "Costs about 1 % of the frame rate.",
       "H\xC3\xA4lt B\xC3\xB6" "den und W\xC3\xA4nde scharf, wenn man flach darauf schaut.\n"
       "Aus: Sie verschwimmen in der Ferne.\n"
       "16x (empfohlen): scharf bis in die Ferne.\n"
       "\n"
       "Kostet etwa 1 % Bildrate.",
       {{"0", "Off", "Aus"}, {"3", "4x", "4x"}, {"4", "8x", "8x"}, {"5", "16x", "16x"}}});
  page.settings.push_back(skylanders::HdTexturesSetting());  // the pack's on / off (all six games)
  page.settings.push_back(
      {"swap_post_effect", "Extra edge smoothing", "Zus\xC3\xA4tzliche Kantengl\xC3\xA4ttung",
       "A second edge smoothing over the whole picture, on top of the game's own.\n"
       "Off (recommended).\n"
       "FXAA, FXAA strong: smoother edges, but for now also a darker, paler picture (a known bug).",
       "Eine zweite Kantengl\xC3\xA4ttung \xC3\xBC" "ber das ganze Bild, zus\xC3\xA4tzlich zu der "
       "des Spiels.\n"
       "Aus (empfohlen).\n"
       "FXAA, FXAA stark: glattere Kanten, aber zurzeit auch ein dunkleres, blasseres Bild "
       "(bekannter Fehler).",
       {{"none", "Off", "Aus"}, {"fxaa", "FXAA", "FXAA"}, {"fxaa_extreme", "FXAA strong",
                                                         "FXAA stark"}}});
  page.settings.push_back(
      {"bloom_strength", "Glow (bloom)", "Leuchten (Bloom)",
       "A soft glow around bright areas and lights.\n"
       "Off: the original look (Spyro's Adventure has the effect but leaves it off).\n"
       "Weak to Strong: the game's own glow switched on, a look it was never tuned for.\n"
       "\n"
       "Costs nothing.",
       "Ein weicher Schein um helle Stellen und Lichter.\n"
       "Aus: der Original-Look (Spyro's Adventure hat den Effekt, l\xC3\xA4sst ihn aber aus).\n"
       "Schwach bis Stark: der Schein des Spiels eingeschaltet, ein Look, auf den es nie abgestimmt "
       "wurde.\n"
       "\n"
       "Kostet nichts.",
       {{"0", "Off (as in the game)", "Aus (wie im Spiel)"},
        {"0.5", "Weak", "Schwach"},
        {"1", "Medium", "Mittel"},
        {"1.5", "Strong", "Stark"}}});
  Setting fullscreen{"fullscreen", "Fullscreen", "Vollbild",
                     "Starts the game in fullscreen or in a window.",
                     "Startet das Spiel im Vollbild oder im Fenster."};
  fullscreen.restart = true;
  page.settings.push_back(fullscreen);
  page.settings.push_back(
      {"resolution", "Window size", "Fenstergr\xC3\xB6\xC3\x9F" "e",
       "The window's size outside fullscreen, and the picture size the game works with.\n"
       "Wider than 16:9 (ultrawide): shows more to the sides.",
       "Die Gr\xC3\xB6\xC3\x9F" "e des Fensters au\xC3\x9F" "erhalb des Vollbilds, zugleich die "
       "Bildgr\xC3\xB6\xC3\x9F" "e, mit der das Spiel arbeitet.\n"
       "Breiter als 16:9 (Ultrawide): zeigt an den Seiten mehr.",
       {{"", "Automatic", "Automatisch"},
        {"1280x720", "1280x720", "1280x720"},
        {"1920x1080", "1920x1080", "1920x1080"},
        {"2560x1080", "2560x1080 (21:9)", "2560x1080 (21:9)"},
        {"2560x1440", "2560x1440", "2560x1440"},
        {"3440x1440", "3440x1440 (21:9)", "3440x1440 (21:9)"},
        {"3840x1600", "3840x1600 (24:10)", "3840x1600 (24:10)"},
        {"3840x2160", "3840x2160", "3840x2160"},
        {"5120x1440", "5120x1440 (32:9)", "5120x1440 (32:9)"}}});
  page.settings.push_back(
      {"ultrawide_hud", "HUD on ultrawide (experimental)", "HUD bei Ultrawide (experimentell)",
       "For windows wider than 16:9 (ultrawide).\n"
       "On: the HUD, the dialogue box and the menus in their own proportions instead of stretched.\n"
       "Off: stretched to the window's width.\n"
       "\n"
       "With it on, two notices the game keeps off-screen show at the left edge, and the pause "
       "menu's dimming leaves the sides light.",
       "F\xC3\xBCr Fenster breiter als 16:9 (Ultrawide).\n"
       "An: HUD, Dialogfenster und Men\xC3\xBCs in ihren eigenen Proportionen statt gestreckt.\n"
       "Aus: auf die Fensterbreite gestreckt.\n"
       "\n"
       "Mit An erscheinen zwei Hinweise, die das Spiel au\xC3\x9F" "erhalb des Bildes h\xC3\xA4lt, "
       "am linken Rand, und die Abdunklung im Pausenmen\xC3\xBC l\xC3\xA4sst die Seiten hell."});
  return page;
}

SettingsPage FrameRatePage() {
  SettingsPage page{"Frame rate", "Bildrate", {}};
  // The game swaps with presentation interval 1; its 30 FPS come from the emulated vblank, which
  // the SDK runs at 30 Hz without a limit (and a frame that misses a 60 Hz vblank waits for the
  // next). With a higher limit it runs as fast as the PC allows: 50-91 FPS in Chapter 1 here
  // (1080p, scale 2), the opening cutscenes take the same time (72.6 s at 30 FPS, 71.5 s), and
  // walking covers the same ground. docs/STATUS.md, 2026-10-20. The portal screen's light and
  // sparks step once per frame (#26), but the owner's video of the console shows that screen as
  // fast; at 60 the owner found the bounce pads and coins as in Xbox 360 videos (2026-10-21), so
  // 60 is the default (app header).
  page.settings.push_back(
      {"frame_rate_limit", "Frame rate", "Bildrate",
       "The game was made for 30 FPS and keeps its normal speed with more.\n"
       "Best: your screen's refresh rate. If your PC can't reach it, the game simply runs as fast "
       "as it can.\n"
       "\n"
       "A few effects (the shine over \"Press Start\", the portal screen's light) run faster above "
       "30.",
       "Das Spiel ist f\xC3\xBCr 30 FPS gemacht und l\xC3\xA4uft mit mehr normal schnell.\n"
       "Am besten: die Bildwiederholrate deines Bildschirms. Schafft dein PC das nicht, "
       "l\xC3\xA4uft das Spiel einfach so schnell, wie er kann.\n"
       "\n"
       "Einige Effekte (der Glanz \xC3\xBC" "ber \xE2\x80\x9E" "Dr\xC3\xBC" "cke Start\xE2\x80\x9C, "
       "das Licht am Portal-Bildschirm) laufen \xC3\xBC" "ber 30 schneller.",
       {{"30", "30 (as on the console)", "30 (wie auf der Konsole)"},
        {"60", "60", "60"},
        {"120", "120", "120"},
        {"144", "144", "144"},
        {"165", "165", "165"},
        {"200", "200", "200"},
        {"240", "240", "240"}}});
  page.settings.push_back({"vsync", "VSync", "VSync",
                           "Waits for the screen before showing a frame.\n"
                           "On: the picture never tears in two when the camera moves.\n"
                           "Off: reacts a tiny bit faster but may tear.",
                           "Wartet vor jedem Bild auf den Bildschirm.\n"
                           "An: Das Bild rei\xC3\x9Ft bei Kamerabewegungen nie in zwei Teile.\n"
                           "Aus: reagiert minimal schneller, kann aber rei\xC3\x9F" "en."});
  page.settings.push_back(
      {"render_target_path_d3d12", "Render path", "Renderpfad",
       "How the Xbox 360's graphics memory is copied.\n"
       "Fast (RTV): the one to use.\n"
       "Exact (ROV): bit for bit, about a third fewer frames. Only for comparing graphics errors.",
       "Wie der Grafikspeicher der Xbox 360 nachgebildet wird.\n"
       "Schnell (RTV): der richtige.\n"
       "Genau (ROV): Bit f\xC3\xBCr Bit, rund ein Drittel weniger Bilder. Nur zum Vergleichen bei "
       "Grafikfehlern.",
       {{"rtv", "Fast (RTV)", "Schnell (RTV)"}, {"rov", "Exact (ROV)", "Genau (ROV)"}}});
  page.settings.push_back(
      {"d3d12_adapter", "Graphics card", "Grafikkarte",
       "Which graphics card draws the game, for PCs with more than one.",
       "Welche Grafikkarte das Spiel zeichnet, f\xC3\xBCr PCs mit mehreren.",
       {{"-1", "Automatic", "Automatisch"}, {"0", "GPU 1", "GPU 1"}, {"1", "GPU 2", "GPU 2"}}});
  page.settings.push_back(
      {"gpu_backend", "Graphics interface", "Grafikschnittstelle",
       "How the game talks to the graphics card.\n"
       "D3D12: the tested one, and the only one with the render path above.\n"
       "Vulkan: just as fast, to try if D3D12 causes trouble (crashes, graphics errors).",
       "Wie das Spiel mit der Grafikkarte spricht.\n"
       "D3D12: die getestete und die einzige mit dem Renderpfad oben.\n"
       "Vulkan: genauso schnell, zum Ausprobieren, falls D3D12 Probleme macht (Abst\xC3\xBCrze, "
       "Grafikfehler).",
       {{"any", "Automatic (D3D12)", "Automatisch (D3D12)"}, {"d3d12", "D3D12", "D3D12"},
        {"vulkan", "Vulkan", "Vulkan"}}});
  return page;
}

SettingsPage GamePage() {
  SettingsPage page{"Game", "Spiel", {}};
  // The disc has text and voices in ten languages (text\<lang>, audio\<lang>); the five here
  // are the Xbox 360 language ids the SDK knows. English and German checked in game (menus,
  // subtitles, voices). "auto" follows the Windows display language.
  Setting language{"language", "Language", "Sprache",
                   "Text and voices.\n"
                   "Automatic: the language of Windows, if the game has it.",
                   "Texte und Stimmen.\n"
                   "Automatisch: die Sprache von Windows, wenn das Spiel sie hat.",
                   {{"auto", "Automatic", "Automatisch"},
                    {"english", "English", "English"},
                    {"german", "Deutsch", "Deutsch"},
                    {"french", "Fran\xC3\xA7" "ais", "Fran\xC3\xA7" "ais"},
                    {"spanish", "Espa\xC3\xB1ol", "Espa\xC3\xB1ol"},
                    {"italian", "Italiano", "Italiano"}}};
  language.restart = true;
  page.settings.push_back(language);
  Setting intro{"skip_intro", "Skip logo movies", "Logo-Videos \xC3\xBC" "berspringen",
                "Skips the Activision, XPEC and Toys for Bob logo videos and the legal notice at "
                "start-up.",
                "\xC3\x9C" "berspringt die Logo-Videos von Activision, XPEC und Toys for Bob und den "
                "rechtlichen Hinweis beim Start."};
  intro.restart = true;
  page.settings.push_back(intro);
  page.settings.push_back({"skip_opening_movie", "Skip the opening movie",
                           "Er\xC3\xB6" "ffnungsfilm \xC3\xBC" "berspringen",
                           "Skips the movie at the start of a new game (Eon, about a minute).",
                           "\xC3\x9C" "berspringt den Film beim Start eines neuen Spiels (Eon, etwa "
                           "eine Minute)."});
  page.settings.push_back(skylanders::AchievementsSetting());
  page.settings.push_back(skylanders::DiscordPresenceSetting());
  // Spiel beenden / neu starten: a controller alone can't press Alt+F4.
  skylanders::AddQuitSettings(page);
  return page;
}

SettingsPage PortalPage() {
  SettingsPage page{"Portal", "Portal", {}};
  Setting mode{"portal_mode", "Portal", "Portal",
               "Where the figures come from.\n"
               "Virtual: the portal menu (Xbox button) puts figures on.\n"
               "Both: the menu and a real Portal of Power together, for the figures you have and "
               "the ones you don't.\n"
               "USB only: just the real portal.\n"
               "None: no portal at all.\n"
               "\n"
               "Real figures are backed up in figures\\real before the game writes to them.",
               "Woher die Figuren kommen.\n"
               "Virtuell: Das Portal-Men\xC3\xBC (Xbox-Taste) stellt Figuren auf.\n"
               "Beides: das Men\xC3\xBC und ein echtes Portal of Power zusammen, f\xC3\xBCr die "
               "Figuren, die du hast, und die, die du nicht hast.\n"
               "Nur USB: nur das echte Portal.\n"
               "Keins: gar kein Portal.\n"
               "\n"
               "Echte Figuren werden in figures\\real gesichert, bevor das Spiel auf sie schreibt.",
               {{"software", "Virtual (menu)", "Virtuell (Men\xC3\xBC)"},
                {"hybrid", "Both (menu + real portal)", "Beides (Men\xC3\xBC + echtes Portal)"},
                {"usb", "Real USB portal only", "Nur echtes USB-Portal"},
                {"none", "None", "Keins"}}};
  mode.restart = true;
  page.settings.push_back(mode);
  page.settings.push_back(
      {"portal_menu_auto_open", "Menu opens by itself", "Men\xC3\xBC \xC3\xB6" "ffnet sich selbst",
       "When the game asks for a Skylander, the portal menu comes up for that player.",
       "Wenn das Spiel nach einem Skylander fragt, \xC3\xB6" "ffnet sich das Portal-Men\xC3\xBC "
       "f\xC3\xBCr diesen Spieler."});
  page.settings.push_back(
      {"portal_menu_owned_only", "Only my figures", "Nur eigene Figuren",
       "Every menu button on the controller.\n"
       "\n"
       "The game's own buttons (jump, attacks) are shown by the game: Pause > Options > Controls.",
       "Alle Men\xC3\xBC-Tasten am Controller.\n"
       "\n"
       "Die Tasten des Spiels selbst (Sprung, Angriffe) zeigt das Spiel: Pause > Optionen > "
       "Steuerung."});
  page.settings.push_back(
      {"portal_menu_later_versions", "Later games' figures", "Figuren sp\xC3\xA4terer Spiele",
       "Also shows the tabs of later games, with figures of the Skylanders this game knows.\n"
       "\n"
       "Those figures are among each Skylander's versions (Y) anyway; the game plays them as its "
       "own.",
       "Zeigt auch die Reiter sp\xC3\xA4terer Spiele, mit Figuren der Skylander, die dieses Spiel "
       "kennt.\n"
       "\n"
       "Diese Figuren stehen ohnehin bei den Versionen jedes Skylanders (Y); das Spiel spielt sie "
       "wie seine eigenen."});
  return page;
}

// The menus' controller buttons. Each choice is a value of the *_buttons settings; the info
// panel shows it on the controller picture (Setting::buttons). They count for every controller and
// apply at once. The choices keep apart from each other and from the game's own use of a button
// alone (Back: Skylander stats, Start: pause), so no two menus share a chord.
Setting ButtonSetting(const char* cvar, const char* en, const char* de, const char* help_en,
                      const char* help_de, std::vector<SettingOption> options) {
  Setting s{cvar, en, de, help_en, help_de, std::move(options)};
  s.buttons = true;
  return s;
}

SettingsPage ControlsPage() {
  SettingsPage page{"Controls", "Steuerung", {}};
  Setting overview;
  overview.en = "Overview";
  overview.de = "\xC3\x9C" "bersicht";
  overview.help_en =
      "Every menu button on the controller. The game's own buttons (jump, attacks) are shown by "
      "the game: Pause > Options > Controls.";
  overview.help_de =
      "Alle Men\xC3\xBC-Tasten am Controller. Die Tasten des Spiels selbst (Sprung, Angriffe) "
      "zeigt das Spiel: Pause > Optionen > Steuerung.";
  overview.buttons = true;
  page.settings.push_back(std::move(overview));
  page.settings.push_back(ButtonSetting(
      "portal_menu_open_buttons", "Portal menu", "Portal-Men\xC3\xBC",
      "What opens the portal menu on a controller (keyboard: F6). Applies to every controller.\n"
      "\n"
      "If the Xbox button also opens the Xbox Game Bar, turn the Game Bar off in Windows.",
      "Womit sich das Portal-Men\xC3\xBC am Controller \xC3\xB6" "ffnet (Tastatur: F6). Gilt "
      "f\xC3\xBCr jeden Controller.\n"
      "\n"
      "\xC3\x96" "ffnet die Xbox-Taste auch die Xbox Game Bar, die Game Bar in Windows ausschalten.",
      {{"guide,back+start,hold_back", "Xbox, Back+Start, hold Back",
        "Xbox, Back+Start, Back halten"},
       {"guide", "Xbox button only", "Nur Xbox-Taste"},
       {"back+start,hold_back", "Back+Start, hold Back", "Back+Start, Back halten"},
       {"back+start", "Back+Start only", "Nur Back+Start"}}));
  page.settings.push_back(ButtonSetting(
      "portal_item_off_buttons", "Portal overview", "Portal-\xC3\x9C" "bersicht",
      "Opens the portal overview: what is on the portal, each figure taken off on its own, the "
      "magic item too (keyboard: F5).",
      "\xC3\x96" "ffnet die Portal-\xC3\x9C" "bersicht: was auf dem Portal steht, jede Figur "
      "einzeln herunterzunehmen, das magische Objekt auch (Tastatur: F5).",
      {{"back+y", "Back+Y", "Back+Y"}, {"back+b", "Back+B", "Back+B"}, {"", "Off", "Aus"}}));
  page.settings.push_back(ButtonSetting(
      "player_menu_buttons", "Player menu", "Spieler-Men\xC3\xBC",
      "Opens the player menu: which controller plays as which player (keyboard: F9).",
      "\xC3\x96" "ffnet das Spieler-Men\xC3\xBC: welcher Controller welcher Spieler ist "
      "(Tastatur: F9).",
      {{"ls+rs", "L3+R3 (press both sticks)", "L3+R3 (beide Sticks dr\xC3\xBC" "cken)"},
       {"back+lb", "Back+LB", "Back+LB"},
       {"", "Off", "Aus"}}));
  // The player menu from here too (its buttons can be switched off).
  page.settings.push_back(skylanders::PlayerMenuSetting());
  page.settings.push_back(ButtonSetting(
      "settings_menu_buttons", "Settings", "Einstellungen",
      "Opens this menu (keyboard: F4).", "\xC3\x96" "ffnet dieses Men\xC3\xBC (Tastatur: F4).",
      {{"back+rb", "Back+RB", "Back+RB"}, {"back+x", "Back+X", "Back+X"}}));
  // No "Aus" here (it was until 2026-10-23): with it a controller could no longer open this
  // menu, nor its Erfolge or Spiel beenden.
  page.settings.push_back(
      {"mnk_mode", "Keyboard and mouse", "Tastatur und Maus",
       "Play with the keyboard as a controller.", "Mit der Tastatur als Controller spielen."});
  Setting keyboard_player{
      "keyboard_player", "Keyboard plays as", "Tastatur spielt als",
      "Which player the keyboard is. Controllers are assigned in the player menu (L3 + R3).",
      "Welcher Spieler die Tastatur ist. Controller teilst du im Spieler-Men\xC3\xBC zu (L3 + R3).",
      {{"1", "Player 1", "Spieler 1"},
       {"2", "Player 2", "Spieler 2"}}};
  keyboard_player.enabled_by = "mnk_mode";  // greyed while the keyboard is off
  page.settings.push_back(keyboard_player);
  return page;
}

}  // namespace

std::vector<SettingsPage> BuildSettingsPages(std::function<void()> open_all_settings) {
  std::vector<SettingsPage> pages = {GraphicsPage(), FrameRatePage(), GamePage(), PortalPage(),
                                     ControlsPage()};
  SettingsPage advanced{"Advanced", "Erweitert", {}};
  Setting all;
  all.en = "All settings (developers, keyboard and mouse)";
  all.de = "Alle Einstellungen (Entwickler, Tastatur und Maus)";
  all.help_en =
      "The SDK's full list of every setting (F10). Its \"Save to config\" button rewrites the "
      "whole settings file and drops its comments; this menu saves only what you change.";
  all.help_de =
      "Die vollst\xC3\xA4ndige Liste aller Einstellungen des SDK (F10). Sein \"Save to config\" "
      "schreibt die ganze Einstellungsdatei neu und verwirft ihre Kommentare; dieses Men\xC3\xBC "
      "speichert nur, was du \xC3\xA4nderst.";
  all.action = std::move(open_all_settings);
  advanced.settings.push_back(std::move(all));
  pages.push_back(std::move(advanced));
  return pages;
}

}  // namespace ssarecomp

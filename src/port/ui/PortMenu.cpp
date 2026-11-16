#include "PortMenu.h"
#include "UIWidgets.h"
#include "port/Game.h"
#include "port/CrashMonitor.h"
#include "ship/window/gui/GuiMenuBar.h"
#include "ship/window/gui/GuiElement.h"
#include <cmath>
#include <variant>
#include "ship/utils/StringHelper.h"
#include <spdlog/fmt/fmt.h>
#include <variant>
#include <tuple>
#include "ResolutionEditor.h"
#include "port/LocalMultiplayer.h"
#include "port/ExtraRacers.h"
#include "port/MultiplayerDisplays.h"
#include <fast/backends/gfx_screen_config.h>

#include "engine/tracks/Track.h"
#include "engine/tracks/KalimariDesert.h"
#include "engine/tracks/ToadsTurnpike.h"

#ifdef __SWITCH__
#include <ship/port/switch/SwitchImpl.h>
#endif

extern "C" {
extern s32 gGamestateNext;
extern s32 gMenuSelection;
#include "audio/external.h"
#include "defines.h"
#include "save.h"
#include "main.h"
}

namespace GameUI {
extern std::shared_ptr<PortMenu> mPortMenu;

using namespace UIWidgets;

void PortMenu::AddSidebarEntry(std::string sectionName, std::string sidebarName, uint32_t columnCount) {
    assert(!sectionName.empty());
    assert(!sidebarName.empty());
    menuEntries.at(sectionName).sidebars.emplace(sidebarName, SidebarEntry{ .columnCount = columnCount });
    menuEntries.at(sectionName).sidebarOrder.push_back(sidebarName);
}

WidgetInfo& PortMenu::AddWidget(WidgetPath& pathInfo, std::string widgetName, WidgetType widgetType) {
    assert(!widgetName.empty());                        // Must be unique
    assert(menuEntries.contains(pathInfo.sectionName)); // Section/header must already exist
    assert(menuEntries.at(pathInfo.sectionName).sidebars.contains(pathInfo.sidebarName)); // Sidebar must already exist
    std::unordered_map<std::string, SidebarEntry>& sidebar = menuEntries.at(pathInfo.sectionName).sidebars;
    uint8_t column = pathInfo.column;
    if (sidebar.contains(pathInfo.sidebarName)) {
        while (sidebar.at(pathInfo.sidebarName).columnWidgets.size() < column + 1) {
            sidebar.at(pathInfo.sidebarName).columnWidgets.push_back({});
        }
    }
    SidebarEntry& entry = sidebar.at(pathInfo.sidebarName);
    entry.columnWidgets.at(column).push_back({ .name = widgetName, .type = widgetType });
    WidgetInfo& widget = entry.columnWidgets.at(column).back();
    switch (widgetType) {
        case WIDGET_CHECKBOX:
        case WIDGET_CVAR_CHECKBOX:
            widget.options = std::make_shared<CheckboxOptions>();
            break;
        case WIDGET_SLIDER_FLOAT:
        case WIDGET_CVAR_SLIDER_FLOAT:
            widget.options = std::make_shared<FloatSliderOptions>();
            break;
        case WIDGET_SLIDER_INT:
        case WIDGET_CVAR_SLIDER_INT:
            widget.options = std::make_shared<IntSliderOptions>();
            break;
        case WIDGET_COMBOBOX:
        case WIDGET_CVAR_COMBOBOX:
        case WIDGET_AUDIO_BACKEND:
        case WIDGET_VIDEO_BACKEND:
            widget.options = std::make_shared<ComboboxOptions>();
            break;
        case WIDGET_BUTTON:
            widget.options = std::make_shared<ButtonOptions>();
            break;
        case WIDGET_WINDOW_BUTTON:
            widget.options = std::make_shared<ButtonOptions>(ButtonOptions{ .size = Sizes::Inline });
            break;
        case WIDGET_COLOR_24:
        case WIDGET_COLOR_32:
            break;
        case WIDGET_SEARCH:
        case WIDGET_SEPARATOR:
        case WIDGET_SEPARATOR_TEXT:
        case WIDGET_TEXT:
        default:
            widget.options = std::make_shared<WidgetOptions>();
    }
    return widget;
}

void PortMenu::AddSettings() {
    // Add Settings menu
    AddMenuEntry("Settings", "gSettings.Menu.SettingsSidebarSection");
    // General Settings
    AddSidebarEntry("Settings", "General", 3);
    WidgetPath path = { "Settings", "General", SECTION_COLUMN_1 };

    static bool isFullscreen = false;
#ifndef __SWITCH__
    AddWidget(path, "Toggle Fullscreen", WIDGET_CHECKBOX)
        .ValuePointer(&isFullscreen)
        .PreFunc([](WidgetInfo& info) { isFullscreen = Ship::Context::GetRawInstance()->GetWindow()->IsFullscreen(); })
        .Callback([](WidgetInfo& info) { Ship::Context::GetRawInstance()->GetWindow()->ToggleFullscreen(); })
        .Options(CheckboxOptions().Tooltip("Toggles Fullscreen On/Off."));
#endif

    AddWidget(path, "Startup Behaviour", WIDGET_CVAR_COMBOBOX)
        .CVar("gSkipIntro")
        .Options(ComboboxOptions()
            .ComboMap(introBehaviourOptions)
            .Tooltip("Select which scene or menu the game launch to."));

    AddWidget(path, "Menu Theme", WIDGET_CVAR_COMBOBOX)
        .CVar("gSettings.Menu.Theme")
        .Options(ComboboxOptions()
                     .Tooltip("Changes the Theme of the Menu Widgets.")
                     .ComboMap(menuThemeOptions)
                     .DefaultIndex(Colors::LightBlue));
    AddWidget(path, "Automatically fit PC menu", WIDGET_CVAR_CHECKBOX)
        .CVar("gSettings.Menu.AutoFit")
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Use most of the available window and automatically scale text to its size. "
            "Menu Scale adjusts readability further. Updates immediately when the window changes size."));
    AddWidget(path, "Menu Extent", WIDGET_CVAR_COMBOBOX)
        .CVar("gSettings.Menu.Extent")
        .PreFunc([](WidgetInfo& info) { info.isHidden = CVarGetInteger("gSettings.Menu.AutoFit", 1); })
        .Options(ComboboxOptions()
                     .Tooltip("Changes the extent of the onscreen menu")
                     .ComboMap(menuExtentOptions)
                     .DefaultIndex(MenuExtent::Condensed));

    AddWidget(path, "Menu Scale: %.2fx", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar("gSettings.Menu.Scale")
        .Options(FloatSliderOptions()
                     .Tooltip("Adjust menu text size. With automatic fitting enabled, this multiplies the automatic scale. Ctrl-click the slider to type a value.")
                     .Min(0.75f)
                     .Max(2.0f)
                     .DefaultValue(1.0f)
                     .Format("%.1f")
                     .Step(0.1f));
    AddWidget(path, "Controller pak screen", WIDGET_CVAR_CHECKBOX)
        .CVar("gControllerPakScreen")
        .Options(CheckboxOptions().Tooltip(
            "Enables the Controller Pak screen when starting the game."));
#if not defined(__SWITCH__) and not defined(__WIIU__)
    AddWidget(path, "Menu Controller Navigation", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_IMGUI_CONTROLLER_NAV)
        .Options(CheckboxOptions().Tooltip(
            "Allows controller navigation of the Spaghetti menu (Settings, Enhancements,...)\nCAUTION: "
            "This will disable game inputs while the menu is visible.\n\nD-pad to move between "
            "items, A to select, B to move up in scope."));
    AddWidget(path, "Hide mouse", WIDGET_CVAR_CHECKBOX)
        .CVar("gSettings.HideMouse")
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Hides the mouse cursor during gameplay. The cursor remains visible in the PC menu."));
#endif
    AddWidget(path, "Search In Sidebar", WIDGET_CVAR_CHECKBOX)
        .CVar("gSettings.Menu.SidebarSearch")
        .Callback([](WidgetInfo& info) {
            if (CVarGetInteger("gSettings.Menu.SidebarSearch", 0)) {
                mPortMenu->InsertSidebarSearch();
            } else {
                mPortMenu->RemoveSidebarSearch();
            }
        })
        .Options(CheckboxOptions().Tooltip(
            "Displays the Search menu as a sidebar entry in Settings instead of in the header."));
    AddWidget(path, "Search Input Autofocus", WIDGET_CVAR_CHECKBOX)
        .CVar("gSettings.Menu.SearchAutofocus")
        .Options(CheckboxOptions().Tooltip(
            "Search input box gets autofocus when visible. Does not affect using other widgets."));
    AddWidget(path, "Show FPS counter", WIDGET_CVAR_CHECKBOX)
        .CVar("gShowFpsCounter")
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Shows the current rendered frames per second in the top-left corner."));
    AddWidget(path, "Enable Tab fast-forward", WIDGET_CVAR_CHECKBOX)
        .CVar("gTabFastForward")
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Hold Tab while the game has keyboard focus to run gameplay as fast as the hardware allows. "
            "Temporarily bypasses FPS pacing, VSync waits and interpolation frames; audio is muted to avoid backlog. "
            "Release Tab to restore normal speed. Takes priority over the alternate-assets Tab hotkey. Saved graphics settings are preserved."))
        .PostFunc([](WidgetInfo&) {
            static int ticks = 2;
            static bool editing = false;
            if (!editing) ticks = std::clamp(CVarGetInteger("gTabFastForwardTicks", 2), 1, 120);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
            ImGui::InputInt("TAB speed up tick rate", &ticks, 1, 2);
            const bool active = ImGui::IsItemActive();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Gameplay logic ticks per rendered frame while holding Tab with fast-forward enabled.\n"
                "2 = normal tick count; 4 = twice the logic work; 8 = four times. Range: 1-120.\n"
                "Applies to race/Battle intros, countdowns and gameplay. Release Tab to restore normal timing.\n"
                "Type an integer, then press Enter or click away to save. Higher values use more CPU time per frame.");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                ticks = std::clamp(ticks, 1, 120);
                CVarSetInteger("gTabFastForwardTicks", ticks);
                Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
            editing = active;
        });
    AddWidget(path, "Alt Assets Tab hotkey", WIDGET_CVAR_CHECKBOX)
        .CVar("gEnhancements.Mods.AlternateAssetsHotkey")
        .Options(
            CheckboxOptions().Tooltip("Allows pressing the Tab key to toggle alternate assets").DefaultValue(true));
#ifndef __SWITCH__
    AddWidget(path, "Open App Files Folder", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) {
            std::string filesPath = Ship::Context::GetRawInstance()->GetAppDirectoryPath();
            SDL_OpenURL(std::string("file:///" + std::filesystem::absolute(filesPath).string()).c_str());
        })
        .Options(ButtonOptions().Tooltip("Opens the folder that contains the save and mods folders, etc."));
#endif

    // Audio Settings
    path.sidebarName = "Audio";
    AddSidebarEntry("Settings", "Audio", 3);
    AddWidget(path, "Master Volume: %.0f%%", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar("gGameMasterVolume")
        .Options(FloatSliderOptions()
                     .Tooltip("Adjust the overall sound volume.")
                     .ShowButtons(false)
                     .Format("")
                     .IsPercentage());
    AddWidget(path, "Main Music Volume: %.0f%%", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar("gMainMusicVolume")
        .Options(FloatSliderOptions()
                     .Tooltip("Adjust the background music volume.")
                     .ShowButtons(false)
                     .Format("")
                     .IsPercentage());
    AddWidget(path, "Sound Effects Volume: %.0f%%", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar("gSFXMusicVolume")
        .Options(FloatSliderOptions()
                     .Tooltip("Adjust the sound effects volume.")
                     .ShowButtons(false)
                     .Format("")
                     .IsPercentage());
    AddWidget(path, "Environment Volume: %.0f%%", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar("gEnvironmentVolume")
        .Options(FloatSliderOptions()
                     .Tooltip("Adjust the environment volume.")
                     .ShowButtons(false)
                     .Format("")
                     .IsPercentage());
    AddWidget(path, "Audio API", WIDGET_AUDIO_BACKEND);

    // Graphics Settings
    static int32_t maxFps;
    const char* tooltip = "";
    if (Ship::Context::GetRawInstance()->GetWindow()->GetWindowBackend() == Fast::WindowBackend::FAST3D_DXGI_DX11) {
        maxFps = MAX_FPS;
        tooltip = "Uses Matrix Interpolation to create extra frames, resulting in smoother graphics. This is "
                  "purely visual and does not impact game logic, execution of glitches etc.\n\nA higher target "
                  "FPS than your monitor's refresh rate will waste resources, and might give a worse result.";
    } else {
        maxFps = Ship::Context::GetRawInstance()->GetWindow()->GetCurrentRefreshRate();
        tooltip = "Uses Matrix Interpolation to create extra frames, resulting in smoother graphics. This is "
                  "purely visual and does not impact game logic, execution of glitches etc.";
    }
    path.sidebarName = "Graphics";
    AddSidebarEntry("Settings", "Graphics", 3);
    AddWidget(path, "Renderer API (Needs reload)", WIDGET_VIDEO_BACKEND);

    AddWidget(path, "Widescreen menu backgrounds", WIDGET_CVAR_CHECKBOX)
        .CVar("gGraphics.WidescreenMenuBackgrounds")
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Extend the original title, game-select, player-select and course-select artwork for widescreen. "
            "Turn off to use the original backgrounds and keep the character grid inside the 4:3 area. Changes apply immediately."));

    AddWidget(path, "Racer render distance: %.2fx", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_RACER_RENDER_DISTANCE)
        .Options(FloatSliderOptions().Min(1.f).Max(5.f).Step(0.05f).DefaultValue(2.f).Format("%.2fx").Tooltip(
            "Keep racers visible farther away. 1x restores the original visibility check. "
            "Drag the slider or type a multiplier below. Higher settings can reduce performance."))
        .PostFunc([](WidgetInfo&) {
            static float multiplier = 2.f;
            static bool editing = false;
            if (!editing) multiplier = std::clamp(CVarGetFloat(CVAR_RACER_RENDER_DISTANCE, 2.f), 1.f, 5.f);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
            ImGui::InputFloat("Racer render distance multiplier", &multiplier, 0.f, 0.f, "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit() && std::isfinite(multiplier)) {
                CVarSetFloat(CVAR_RACER_RENDER_DISTANCE, std::clamp(multiplier, 1.f, 5.f));
                Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
            editing = ImGui::IsItemActive();
        });

    AddWidget(path, "Internal Resolution: %.0f%%", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_INTERNAL_RESOLUTION)
        .Callback([](WidgetInfo& info) {
            Ship::Context::GetRawInstance()->GetWindow()->SetResolutionMultiplier(
                CVarGetFloat(CVAR_INTERNAL_RESOLUTION, 1));
        })
        .PreFunc([](WidgetInfo& info) {
            if (mPortMenu->disabledMap.at(DISABLE_FOR_ADVANCED_RESOLUTION_ON).active &&
                mPortMenu->disabledMap.at(DISABLE_FOR_VERTICAL_RES_TOGGLE_ON).active) {
                info.activeDisables.push_back(DISABLE_FOR_ADVANCED_RESOLUTION_ON);
                info.activeDisables.push_back(DISABLE_FOR_VERTICAL_RES_TOGGLE_ON);
            } else if (mPortMenu->disabledMap.at(DISABLE_FOR_LOW_RES_MODE_ON).active) {
                info.activeDisables.push_back(DISABLE_FOR_LOW_RES_MODE_ON);
            }
        })
        .Options(
            FloatSliderOptions()
                .Tooltip("Multiplies your output resolution by the value inputted, as a more intensive but effective "
                         "form of anti-aliasing.")
                .ShowButtons(false)
                .IsPercentage()
                .Format("")
                .Min(0.5f)
                .Max(4.0f));

#ifndef __WIIU__
    AddWidget(path, "Anti-aliasing (MSAA): %d", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_MSAA_VALUE)
        .Callback([](WidgetInfo& info) {
            Ship::Context::GetRawInstance()->GetWindow()->SetMsaaLevel(CVarGetInteger(CVAR_MSAA_VALUE, 1));
        })
        .Options(
            IntSliderOptions()
                .Tooltip("Activates MSAA (multi-sample anti-aliasing) from 2x up to 8x, to smooth the edges of "
                         "rendered geometry.\n"
                         "Higher sample count will result in smoother edges on models, but may reduce performance.")
                .Min(1)
                .Max(8)
                .DefaultValue(1));
#endif

    AddWidget(path, "Current FPS: %d", WIDGET_CVAR_SLIDER_INT)
        .CVar("gInterpolationFPS")
        .Callback([](WidgetInfo& info) {
            int32_t defaultValue = std::static_pointer_cast<IntSliderOptions>(info.options)->defaultValue;
            if (CVarGetInteger(info.cVar, defaultValue) == defaultValue) {
                info.name = "Current FPS: Original (%d)";
            } else {
                info.name = "Current FPS: %d";
            }
        })
        .PreFunc([](WidgetInfo& info) {
            if (mPortMenu->disabledMap.at(DISABLE_FOR_MATCH_REFRESH_RATE_ON).active)
                info.activeDisables.push_back(DISABLE_FOR_MATCH_REFRESH_RATE_ON);
        })
        .Options(IntSliderOptions().Tooltip(tooltip).Min(30).Max(maxFps).DefaultValue(30));
    AddWidget(path, "Match Refresh Rate", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) {
            int hz = Ship::Context::GetRawInstance()->GetWindow()->GetCurrentRefreshRate();
            if (hz >= 30 && hz <= MAX_FPS) {
                CVarSetInteger("gInterpolationFPS", hz);
                Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
        })
        .PreFunc([](WidgetInfo& info) { info.isHidden = mPortMenu->disabledMap.at(DISABLE_FOR_NOT_DIRECTX).active; })
        .Options(ButtonOptions().Tooltip("Matches interpolation value to the current game's window refresh rate."));
    AddWidget(path, "Match Refresh Rate", WIDGET_CVAR_CHECKBOX)
        .CVar("gMatchRefreshRate")
        .PreFunc([](WidgetInfo& info) { info.isHidden = mPortMenu->disabledMap.at(DISABLE_FOR_DIRECTX).active; })
        .Options(CheckboxOptions().Tooltip("Matches interpolation value to the current game's window refresh rate."));
    AddWidget(path, "Enable Vsync", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_VSYNC_ENABLED)
        .PreFunc([](WidgetInfo& info) { info.isHidden = mPortMenu->disabledMap.at(DISABLE_FOR_NO_VSYNC).active; })
        .Options(CheckboxOptions().Tooltip("Enables Vsync."));
    AddWidget(path, "Windowed Fullscreen", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SDL_WINDOWED_FULLSCREEN)
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = mPortMenu->disabledMap.at(DISABLE_FOR_NO_WINDOWED_FULLSCREEN).active;
        })
        .Options(CheckboxOptions().Tooltip("Enables Windowed Fullscreen Mode."));
    AddWidget(path, "Allow multiple ImGui windows", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENABLE_MULTI_VIEWPORTS)
        .PreFunc(
            [](WidgetInfo& info) { info.isHidden = mPortMenu->disabledMap.at(DISABLE_FOR_NO_MULTI_VIEWPORT).active; })
        .Options(CheckboxOptions()
                     .Tooltip("Allows multiple ImGui windows to be opened at once (Does not effect the game or the splitscreen modes). Requires a reload to take effect.")
                     .DefaultValue(true));
    AddWidget(path, "Texture Filter (Needs reload)", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_TEXTURE_FILTER)
        .Options(ComboboxOptions().Tooltip("Sets the applied Texture Filtering.").ComboMap(textureFilteringMap));

    path.sidebarName = "Controls";
    AddSidebarEntry("Settings", "Controls", 1);
    AddWidget(path,
              "This interface can be a little daunting. Please bear with us as we work to improve the experience "
              "and address some known issues.\n"
              "\n"
              "At first glance, you may notice several input devices displayed below the 'Clear All' button. "
              "Some of these might be other controllers connected to your computer, while others may be "
              "duplicated controllers (a known issue). We recommend clicking on the box with the " ICON_FA_EYE
              " icon and the name of any disconnected or unused controllers to hide their inputs. Make sure the "
              "target controller remains visible.\n"
              "\n"
              "If you encounter issues connecting your controller or registering inputs, try closing Steam or "
              "any other external input software. Alternatively, test a different controller to determine if "
              "it's a compatibility issue.\n",
              WIDGET_TEXT);
    AddWidget(path, "Bindings", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Popout Bindings Window", WIDGET_WINDOW_BUTTON)
        .CVar(CVAR_CONTROLLER_CONFIGURATION_WINDOW_OPEN)
        .WindowName("Input Editor")
        .Options(ButtonOptions().Tooltip("Enables the separate Bindings Window.").Size(Sizes::Inline));
}
int32_t motionBlurStrength;

void PortMenu::AddMultiplayer() {
    AddMenuEntry("Gameplay", "gSettings.Menu.MultiplayerSidebarSection");
    AddSidebarEntry("Gameplay", "Local play", 1);
    WidgetPath path = { "Gameplay", "Local play", SECTION_COLUMN_1 };
    AddWidget(path, "200cc mode", WIDGET_CVAR_CHECKBOX)
        .CVar("gEnable200cc")
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Add 200cc to the engine class selection for Grand Prix and VS."));
    AddWidget(path, "Enable Custom CC", WIDGET_CVAR_CHECKBOX).CVar("gEnableCustomCC");
    AddWidget(path, "Custom CC value", WIDGET_CUSTOM)
        .PreFunc([](WidgetInfo& info) { info.isHidden = !CVarGetInteger("gEnableCustomCC", 0); })
        .CustomFunction([](WidgetInfo&) {
            static int value = 150;
            static bool editing = false;
            if (!editing) {
                const double saved = CVarGetFloat("gCustomCC", 150.0f);
                value = static_cast<int>(std::clamp(saved, 1.0, 2147483647.0));
            }
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.f);
            ImGui::InputInt("Custom CC", &value, 0, 0);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                value = std::max(value, 1);
                CVarSetFloat("gCustomCC", static_cast<float>(value));
                Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
            editing = ImGui::IsItemActive();
        });


    static const auto outsideRace = [](WidgetInfo& info) {
        info.options->disabled = !MultiplayerOptions_CanChange() && !gDemoMode;
        info.options->disabledTooltip = "Return to the game menu to change multiplayer options.";
    };
    AddWidget(path, "Enable 8 players", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_MULTIPLAYER_EIGHT)
        .PreFunc(outsideRace)
        .Callback([](WidgetInfo&) {
            if (CVarGetInteger(CVAR_MULTIPLAYER_EIGHT, 1)) CVarSetInteger(CVAR_MULTIPLAYER_SIXTEEN, 0);
            MultiplayerOptions_OnEightChanged();
        })
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Adds 5P-8P choices to GAME SELECT: move right past 4P. Use the game controls to choose mode, class, characters and cup. "
            "F8 opens the optional PC lobby. Screen layouts automatically fill spare row space."));
    AddWidget(path, "Enable 16 players", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_MULTIPLAYER_SIXTEEN)
        .PreFunc(outsideRace)
        .Callback([](WidgetInfo&) {
            if (MultiplayerOptions_SixteenEnabled()) CVarSetInteger(CVAR_MULTIPLAYER_EIGHT, 0);
            MultiplayerOptions_OnSixteenChanged();
        })
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Adds playable 9P-16P choices after 8P in GAME SELECT. Up to 16 humans can race; "
            "unused race slots are filled with CPU bots. Battle supports up to 16 humans, with no bots. Item boxes recover twice as fast in every mode. "
            "Time Trials with bots do not save records or ghosts. Applies to a new race or cup."));
    AddWidget(path, "Enable extra maps", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_EXTRA_MAPS)
        .PreFunc(outsideRace)
        .Callback([](WidgetInfo&) { if (!gDemoMode) ExtraMaps_OnChanged(); })
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Show experimental Amped Up cups in Grand Prix, VS and Time Trials. Extra-track Time Trial records and ghosts are not saved. Some course graphics, hazards and lap behavior are still being developed."));
    AddWidget(path, "Enable extra characters", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_EXTRA_CHARACTERS)
        .PreFunc(outsideRace)
        .Callback([](WidgetInfo&) { ExtraRacers_OnChanged(); })
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Adds the imported Deluxe and Dragon Ball Kart racers alongside the original eight. "
            "Turn off to restore our previous eight-character selection screen. "
            "Changing this option returns to the main game menu and is saved. Change it outside a race."));
    AddWidget(path, "Deluxe characters", WIDGET_CVAR_CHECKBOX)
        .CVar("gRacerSets.Deluxe")
        .PreFunc(outsideRace)
        .Callback([](WidgetInfo&) { ExtraRacers_OnChanged(); })
        .Options(CheckboxOptions().DefaultValue(true).Tooltip("Show this character set in selection and random racers. Changing it returns to the main menu."));
    AddWidget(path, "Dragon Ball characters", WIDGET_CVAR_CHECKBOX)
        .CVar("gRacerSets.DragonBall")
        .PreFunc(outsideRace)
        .Callback([](WidgetInfo&) { ExtraRacers_OnChanged(); })
        .Options(CheckboxOptions().DefaultValue(true).Tooltip("Show this character set in selection and random racers. Changing it returns to the main menu."));
    AddWidget(path, "Pokemon characters", WIDGET_CVAR_CHECKBOX)
        .CVar("gRacerSets.Pokemon")
        .PreFunc(outsideRace)
        .Callback([](WidgetInfo&) { ExtraRacers_OnChanged(); })
        .Options(CheckboxOptions().DefaultValue(true).Tooltip("Show this character set in selection and random racers. Changing it returns to the main menu."));
    AddWidget(path, "GTA San Andreas characters", WIDGET_CVAR_CHECKBOX)
        .CVar("gRacerSets.GTA")
        .PreFunc(outsideRace)
        .Callback([](WidgetInfo&) { ExtraRacers_OnChanged(); })
        .Options(CheckboxOptions().DefaultValue(true).Tooltip("Show the GTA San Andreas character set. Changes return to the main menu."));
    AddWidget(path, "Simpsons characters", WIDGET_CVAR_CHECKBOX)
        .CVar("gRacerSets.Simpsons").PreFunc(outsideRace)
        .Callback([](WidgetInfo&) { ExtraRacers_OnChanged(); })
        .Options(CheckboxOptions().DefaultValue(true).Tooltip("Show the Simpsons character set. Changes return to the main menu."));
    AddWidget(path, "Slasher characters", WIDGET_CVAR_CHECKBOX)
        .CVar("gRacerSets.Slasher").PreFunc(outsideRace)
        .Callback([](WidgetInfo&) { ExtraRacers_OnChanged(); })
        .Options(CheckboxOptions().DefaultValue(true).Tooltip("Show the Slasher Kart character set. Changes return to the main menu."));
    AddWidget(path, "Evangelion characters", WIDGET_CVAR_CHECKBOX).CVar("gRacerSets.Eva").PreFunc(outsideRace).Callback([](WidgetInfo&) { ExtraRacers_OnChanged(); }).Options(CheckboxOptions().DefaultValue(true));
    AddWidget(path, "Evangelion Rebuild characters", WIDGET_CVAR_CHECKBOX).CVar("gRacerSets.EvaRebuild").PreFunc(outsideRace).Callback([](WidgetInfo&) { ExtraRacers_OnChanged(); }).Options(CheckboxOptions().DefaultValue(true));
    AddWidget(path, "Angry Birds characters", WIDGET_CVAR_CHECKBOX).CVar("gRacerSets.Birds").PreFunc(outsideRace).Callback([](WidgetInfo&) { ExtraRacers_OnChanged(); }).Options(CheckboxOptions().DefaultValue(true));
    static const std::unordered_map<int32_t, const char*> displayOptions = {
        {1, "1 screen"}, {2, "2 screens"}, {3, "3 screens"}
    };
    AddWidget(path, "Number of screens", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_MULTIPLAYER_SCREENS)
        .PreFunc(outsideRace)
        .Options(ComboboxOptions().ComboMap(displayOptions).DefaultIndex(1).Tooltip(
            "At 16 humans: one screen uses 4 rows by 4 columns; two screens use 2 rows by 4 columns each; "
            "three screens split players 6 / 5 / 5 in two rows per screen. "
            "On Windows, the game window spans the selected extended-desktop monitors during races, "
            "then returns to its previous size in the game menu. Configure Windows to extend these displays."));
    AddWidget(path, "Display status", WIDGET_TEXT)
        .PreFunc([](WidgetInfo& info) { info.name = MultiplayerDisplays_Status(); });
    AddWidget(path, "Apply screen layout", WIDGET_BUTTON)
        .Callback([](WidgetInfo&) { MultiplayerDisplays_Refresh(); })
        .Options(ButtonOptions().Tooltip("Re-detect connected displays and apply the selected layout during a race."));
    static const auto extraBotsTooltip =
        fmt::format("Adds CPU racers to the next Grand Prix or VS race, up to {} total racers.", MAX_RACE_GRID);
    AddWidget(path, "Enable extra bots", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_EXTRA_BOTS_ENABLED)
        .PreFunc(outsideRace)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(extraBotsTooltip.c_str()))
        .PostFunc([](WidgetInfo&) {
            const int baseWithExtras = MultiplayerOptions_ExtraBotBaseGrid(1);
            const int baseWithoutExtras = MultiplayerOptions_ExtraBotBaseGrid(0);
            const int maxExtra = MAX_RACE_GRID - baseWithExtras;
            const auto getExtra = [maxExtra] {
                return std::clamp(CVarGetInteger(CVAR_EXTRA_BOT_PLAYERS, 0), 0, maxExtra);
            };
            const auto saveExtra = [](int extra) {
                if (extra != CVarGetInteger(CVAR_EXTRA_BOT_PLAYERS, 0)) {
                    CVarSetInteger(CVAR_EXTRA_BOT_PLAYERS, extra);
                    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
                }
            };

            ImGui::BeginDisabled((!MultiplayerOptions_CanChange() && !gDemoMode) || !CVarGetInteger(CVAR_EXTRA_BOTS_ENABLED, 0));
            static int extra = 0;
            static int total = 0;
            static bool editingExtra = false;
            static bool editingTotal = false;

            if (!editingExtra) extra = getExtra();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
            ImGui::InputInt("Extra racers", &extra, 0, 0);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                saveExtra(std::clamp(extra, 0, maxExtra));
            }
            editingExtra = ImGui::IsItemActive();

            if (!editingTotal) {
                const int savedExtra = getExtra();
                total = savedExtra == 0 ? baseWithoutExtras : baseWithExtras + savedExtra;
            }
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
            ImGui::InputInt("Total racers", &total, 0, 0);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const int boundedTotal = std::clamp(total, baseWithoutExtras, MAX_RACE_GRID);
                saveExtra(std::max(0, boundedTotal - baseWithExtras));
            }
            editingTotal = ImGui::IsItemActive();
            ImGui::EndDisabled();
        });
    AddWidget(path, "Enable Single-Player VS", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SOLO_VS)
        .PreFunc(outsideRace)
        .Callback([](WidgetInfo&) { MultiplayerOptions_OnBotsChanged(); })
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Adds VS to 1P Game. Pick one course and race against bots. "
            "Uses an eight-racer grid, or sixteen with Enable 16 players, plus any extra bots. "
            "Changing this option returns to the main game menu."));
    AddWidget(path, "Enable bots in 3+ players", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_MULTIPLAYER_BOTS)
        .PreFunc(outsideRace)
        .Callback([](WidgetInfo&) { MultiplayerOptions_OnBotsChanged(); })
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Adds Grand Prix with bots to the normal 3/4-player menu. With Enable 8 players on, GP for 5-7 humans also fills the grid to eight. "
            "Changing this option returns to the main game menu. Both multiplayer options can be enabled together."));
    AddWidget(path, "Turn AFK players into bots after X seconds", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_AFK_BOTS)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Temporarily auto-play an inactive human player's kart. Any held button or stick input outside the dead zone "
            "immediately gives control back. Counts active gameplay only; countdowns, pauses and results do not count. "
            "Race modes use the CPU driver; Battle uses basic opponent-seeking steering and item use. Applies immediately and is saved."))
        .PostFunc([](WidgetInfo&) {
            static int seconds = 10;
            static bool editing = false;
            if (!editing) seconds = std::clamp(CVarGetInteger(CVAR_AFK_SECONDS, 10), 0, 86400);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
            ImGui::InputInt("AFK timeout (seconds)", &seconds, 0, 0);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 seconds enables immediate takeover while idle. Any controller input restores human control.");
            const bool active = ImGui::IsItemActive();
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                seconds = std::clamp(seconds, 0, 86400);
                CVarSetInteger(CVAR_AFK_SECONDS, seconds);
                Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
            editing = active;
        });
    AddWidget(path, "Enable race bomb karts", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_MULTIPLAYER_RACE_BOMBS)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Turn the bomb-kart obstacles on race tracks on or off, including in single-player VS. "
            "Applies immediately and is saved. Eliminated-player bombs in Battle are controlled by the normal Battle rules."));
    AddWidget(path, "Options are saved. Move right past 4P in GAME SELECT for 5P-8P, then past 8P for 9P-16P. "
                    "Enable 16 players supports up to 16 humans in Grand Prix and VS, with bots in unused slots. "
                    "Battle keeps its current player count. With 16 players off, the Grand Prix bots option fills to eight.", WIDGET_TEXT);
    const auto minimapNumberEntry = [](WidgetInfo& info) {
        const auto options = std::static_pointer_cast<IntSliderOptions>(info.options);
        struct NumericState { int value = 0; bool editing = false; };
        static std::unordered_map<std::string, NumericState> states;
        auto& state = states[info.cVar];
        if (!state.editing) state.value = CVarGetInteger(info.cVar, options->defaultValue);
        ImGui::PushID(info.cVar);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
        ImGui::InputInt("Numeric value (%)", &state.value, 0, 0);
        const bool active = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            state.value = std::clamp(state.value, options->min, options->max);
            CVarSetInteger(info.cVar, state.value);
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
        state.editing = active;
        ImGui::PopID();
    };
    AddWidget(path, "Minimap size: %d%%", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_MINIMAP_SIZE)
        .Options(IntSliderOptions().Min(10).Max(300).Step(5).DefaultValue(85).Tooltip(
            "Scale the course/arena minimap and its player markers. 100% keeps the normal size. "
            "Drag the slider or type a percentage below, then press Enter or click away. Applies immediately and is saved."))
        .PreFunc([](WidgetInfo& info) {
            std::static_pointer_cast<IntSliderOptions>(info.options)->flags = ImGuiSliderFlags_AlwaysClamp;
        })
        .PostFunc(minimapNumberEntry);
    AddWidget(path, "Minimap transparency: %d%%", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_MINIMAP_TRANSPARENCY)
        .Options(IntSliderOptions().Min(0).Max(100).Step(5).DefaultValue(25).Tooltip(
            "Fade the entire course/arena minimap, including player markers and finish line. "
            "0% is opaque; 100% hides it. Drag the slider or enter a percentage below. Applies immediately and is saved."))
        .PreFunc([](WidgetInfo& info) {
            std::static_pointer_cast<IntSliderOptions>(info.options)->flags = ImGuiSliderFlags_AlwaysClamp;
        })
        .PostFunc(minimapNumberEntry);
}

void PortMenu::AddEnhancements() {
    AddMenuEntry("Enhancements", "gSettings.Menu.EnhancementsSidebarSection");
    WidgetPath path = { "Enhancements", "General", SECTION_COLUMN_1 };
    AddSidebarEntry("Enhancements", "General", 3);
    AddWidget(path, "No multiplayer feature cuts", WIDGET_CVAR_CHECKBOX)
        .CVar("gMultiplayerNoFeatureCuts")
        .Options(CheckboxOptions().Tooltip("Allows full train and jumbotron in multiplayer, etc."));
    AddWidget(path, "Widescreen portrait spacing", WIDGET_CVAR_CHECKBOX)
        .CVar("gBetterResultPortraits")
        .Options(CheckboxOptions().Tooltip("Alters result portrait spacing for better aesthetics on widescreen"));
    AddWidget(path, "No Level of Detail (LOD)", WIDGET_CVAR_CHECKBOX)
        .CVar("gDisableLod")
        .Options(CheckboxOptions().Tooltip(
            "Disable Level of Detail (LOD) to avoid models using lower poly versions at a distance"));
    AddWidget(path, "Disable Culling", WIDGET_CVAR_CHECKBOX)
        .CVar("gNoCulling")
        .Options(CheckboxOptions().Tooltip("Disable original culling of mk64"));
    AddWidget(path, "Disable Rubberbanding", WIDGET_CVAR_CHECKBOX)
        .CVar("gDisableRubberbanding")
        .Options(CheckboxOptions().Tooltip("Disable rubberbanding in the game."));
    AddWidget(path, "Far Frustrum", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar("gFarFrustrum")
        .PreFunc([](WidgetInfo& info) { info.isHidden = !CVarGetInteger("gNoCulling", 0); })
        .Options(FloatSliderOptions()
                     .Min(0.0f)
                     .Max(10000.0f)
                     .DefaultValue(10000.0f)
                     .Tooltip("Say how Far the Frustrum are when 'Disable Culling' are enable")
                     .Step(10.0f));
    AddWidget(path, "Enable Digital Speedometer", WIDGET_CVAR_CHECKBOX)
        .CVar("gEnableDigitalSpeedometer")
        .Options(CheckboxOptions().Tooltip("Welcome to the modern era"));

    AddWidget(path, "Harder CPU", WIDGET_CVAR_CHECKBOX).CVar("gHarderCPU");

    AddWidget(path, "Show Spaghetti version", WIDGET_CVAR_CHECKBOX)
        .CVar("gShowSpaghettiVersion")
        .Options(CheckboxOptions().Tooltip("Show the Spaghetti Kart version on the Mario Kart menu").DefaultValue(true));

    AddWidget(path, "Enable Look Behind Camera", WIDGET_CVAR_CHECKBOX)
        .CVar("gLookBehind")
        .Options(CheckboxOptions().Tooltip("Press C-Left to look behind you"));
    AddWidget(path, "Camera always stays in front of walls", WIDGET_CVAR_CHECKBOX)
        .CVar("gCameraStayInFrontOfWalls")
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Moves the gameplay camera closer to the kart when track geometry blocks it. "
            "Returns to normal distance as the wall clears. Applies to every player's normal and look-behind camera."));
    AddWidget(path, "Fix Visuals", WIDGET_CVAR_CHECKBOX)
        .CVar("gFixVisuals")
        .Options(CheckboxOptions().Tooltip("Fixes the second last lamp glow in Banshee Boardwalk"));

    AddRulesets();

    path = { "Enhancements", "Cheats", SECTION_COLUMN_1 };
    AddSidebarEntry("Enhancements", "Cheats", 3);
    AddWidget(path, "Item boxes will only roll selected items", WIDGET_CVAR_CHECKBOX)
        .CVar("gCheats.ItemFilterEnabled")
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Choose items below. Every race position and CPU can roll any selected item with equal probability. With no items selected, boxes give nothing."));
    static const struct { const char* label; const char* cvar; } itemChoices[] = {
        {"Banana", "gCheats.ItemBanana"}, {"Banana bunch", "gCheats.ItemBananaBunch"},
        {"Green shell", "gCheats.ItemGreenShell"}, {"Triple green shells", "gCheats.ItemTripleGreenShell"},
        {"Red shell", "gCheats.ItemRedShell"}, {"Triple red shells", "gCheats.ItemTripleRedShell"},
        {"Blue shell", "gCheats.ItemBlueShell"}, {"Lightning", "gCheats.ItemLightning"},
        {"Fake item box", "gCheats.ItemFakeBox"}, {"Star", "gCheats.ItemStar"},
        {"Boo", "gCheats.ItemBoo"}, {"Mushroom", "gCheats.ItemMushroom"},
        {"Double mushrooms", "gCheats.ItemDoubleMushroom"}, {"Triple mushrooms", "gCheats.ItemTripleMushroom"},
        {"Golden mushroom", "gCheats.ItemGoldenMushroom"}
    };
    for (const auto& item : itemChoices) {
        AddWidget(path, item.label, WIDGET_CVAR_CHECKBOX).CVar(item.cvar)
            .PreFunc([](WidgetInfo& info) { info.isHidden = !CVarGetInteger("gCheats.ItemFilterEnabled", 0); })
            .Options(CheckboxOptions().DefaultValue(true));
    }
    AddWidget(path, "Skip to Grand Prix finale", WIDGET_BUTTON)
        .PreFunc([](WidgetInfo& info) {
            info.options->disabled = !LocalMP_CanSkipGrandPrixFinale();
            info.options->disabledTooltip = "Start a Grand Prix with 3 to 16 human players first.";
        })
        .Callback([](WidgetInfo&) { LocalMP_SkipGrandPrixFinale(); })
        .Options(ButtonOptions().Tooltip(
            "Skip directly to the award ceremony using current cup points. Equal points use controller order. "
            "Works in 3-16 player Grand Prix, with or without bots. Does not award saved trophies."));
    AddWidget(path, "All tracks completed", WIDGET_CVAR_CHECKBOX)
        .CVar("gAllTracksCompleted")
        .Options(CheckboxOptions().Tooltip(
            "Shows gold in all cups and unlocks Mirror mode and the alternate background while enabled. "
            "Disable to use your actual saved progress. Return to the title/main menu to refresh the background."));
    AddWidget(path, "Moon Jump", WIDGET_CVAR_CHECKBOX).CVar("gEnableMoonJump");
    AddWidget(path, "Disable Wall Collision", WIDGET_CVAR_CHECKBOX)
        .CVar("gNoWallColision")
        .Options(CheckboxOptions().Tooltip("Disable wall collision."));
    AddWidget(path, "Min Height", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar("gMinHeight")
        .PreFunc([](WidgetInfo& info) { info.isHidden = !CVarGetInteger("gNoWallColision", 0); })
        .Options(FloatSliderOptions().Min(-50.0f).Max(50.0f).DefaultValue(0.0f).Tooltip(
            "When Disable Wall Collision are enable what is the minimal height you can get."));
}

#ifdef __SWITCH__
static const std::unordered_map<int32_t, const char*> switchCPUProfiles = {
    { Ship::SwitchProfiles::MAXIMUM, "Maximum Performance" },
    { Ship::SwitchProfiles::HIGH, "High Performance" },
    { Ship::SwitchProfiles::BOOST, "Boost Performance" },
    { Ship::SwitchProfiles::STOCK, "Stock Performance" },
    { Ship::SwitchProfiles::POWERSAVINGM1, "Powersaving Mode 1" },
    { Ship::SwitchProfiles::POWERSAVINGM2, "Powersaving Mode 2" },
    { Ship::SwitchProfiles::POWERSAVINGM3, "Powersaving Mode 3" }
};
#endif

void PortMenu::AddRulesets() {
    WidgetPath path = { "Enhancements", "Rulesets", SECTION_COLUMN_1 };
    AddSidebarEntry("Enhancements", "Rulesets", 3);

    // Requires more testing
    // AddWidget(path, "Number of Laps", WIDGET_CVAR_SLIDER_INT)
    //     .CVar("gNumLaps")
    //     .Options(UIWidgets::IntSliderOptions().Min().Max(20).Step(1).DefaultValue(3));
    AddWidget(path, "Unique Character Selections", WIDGET_CVAR_CHECKBOX)
        .CVar("gUniqueCharacterSelections")
        .Options(CheckboxOptions()
          .Tooltip(
            "Require different characters in the normal 2-4 player menu. Off by default so players can share a character, like expanded multiplayer.")
          .DefaultValue(false)
        );
    AddWidget(path, "No Itemboxes", WIDGET_CVAR_CHECKBOX)
        .CVar("gDisableItemboxes")
        .Options(CheckboxOptions().Tooltip(
            "Prevents Itemboxes from spawning"));
    AddWidget(path, "All Thwomps are Marty", WIDGET_CVAR_CHECKBOX)
        .CVar("gAllThwompsAreMarty")
        .Options(CheckboxOptions().Tooltip(
            "All Thwomps are Marty"));
    AddWidget(path, "All Bomb Karts in Chase Mode", WIDGET_CVAR_CHECKBOX)
        .CVar("gAllBombKartsChase")
        .Options(CheckboxOptions().Tooltip(
            "These karts will chase you!!!"));
    AddWidget(path, "Get the trophies!", WIDGET_CVAR_CHECKBOX)
        .CVar("gGoFish")
        .Options(CheckboxOptions().Tooltip(
            "Collect as many trophies as you can. Racer with the most trophies wins!"));
    AddWidget(path, "Shells Shoot Straight", WIDGET_CVAR_CHECKBOX)
        .CVar("gShellsShootStraight")
        .Options(CheckboxOptions().Tooltip("Fixes a logic bug in the game code. No evidence if this is intended gameplay or not."));
    AddWidget(path, "Track X Stretch", WIDGET_SLIDER_FLOAT)
        .ValuePointer(&gVtxStretch[0])
        .Options(UIWidgets::FloatSliderOptions().Min(0.1f).Max(10.0f).Step(0.1f).Format("%.2f"));
    AddWidget(path, "Track Y Stretch", WIDGET_SLIDER_FLOAT)
        .ValuePointer(&gVtxStretch[1])
        .Options(UIWidgets::FloatSliderOptions().Min(0.1f).Max(10.0f).Step(0.1f).Format("%.2f"));
    AddWidget(path, "Track Z Stretch", WIDGET_SLIDER_FLOAT)
        .ValuePointer(&gVtxStretch[2])
        .Options(UIWidgets::FloatSliderOptions().Min(0.1f).Max(10.0f).Step(0.1f).Format("%.2f"));


    AddWidget(path, "Trains", WIDGET_CVAR_SLIDER_INT)
        .CVar("gNumTrains")
        .Options(UIWidgets::IntSliderOptions().Min(0).Max(19).Step(1).DefaultValue(2));
    AddWidget(path, "Carriages", WIDGET_CVAR_SLIDER_INT)
        .CVar("gNumCarriages")
        .Options(UIWidgets::IntSliderOptions().Min(0).Max(74).Step(1).DefaultValue(5));
    AddWidget(path, "Train has a tender", WIDGET_CVAR_CHECKBOX)
        .CVar("gHasTender")
        .Options(UIWidgets::CheckboxOptions().DefaultValue(1)
        .Tooltip("This option is only valid if there are no carriages on the train"));

    AddWidget(path, "Trucks", WIDGET_CVAR_SLIDER_INT)
        .CVar("gNumTrucks")
        .Options(UIWidgets::IntSliderOptions().Min(0).Max(50).Step(1).DefaultValue(7));
    AddWidget(path, "Buses", WIDGET_CVAR_SLIDER_INT)
        .CVar("gNumBuses")
        .Options(UIWidgets::IntSliderOptions().Min(0).Max(50).Step(1).DefaultValue(7));
    AddWidget(path, "Tanker Trucks", WIDGET_CVAR_SLIDER_INT)
        .CVar("gNumTankerTrucks")
        .Options(UIWidgets::IntSliderOptions().Min(0).Max(50).Step(1).DefaultValue(7));
    AddWidget(path, "Cars", WIDGET_CVAR_SLIDER_INT)
        .CVar("gNumCars")
        .Options(UIWidgets::IntSliderOptions().Min(0).Max(50).Step(1).DefaultValue(7));
}

void PortMenu::AddDevTools() {
    AddMenuEntry("Developer", "gSettings.Menu.DevToolsSidebarSection");
    AddSidebarEntry("Developer", "General", 3);
    WidgetPath path = { "Developer", "General", SECTION_COLUMN_1 };
#ifdef __SWITCH__
    AddWidget(path, "Switch CPU Profile", WIDGET_CVAR_COMBOBOX)
        .CVar("gSwitchPerfMode")
        .Options(ComboboxOptions()
                     .Tooltip("Switches the CPU profile to a different one")
                     .ComboMap(switchCPUProfiles)
                     .DefaultIndex(Ship::SwitchProfiles::STOCK))
        .Callback([](WidgetInfo& info) { Ship::Switch::ApplyOverclock(); });
#endif
    AddWidget(path, "Popout Menu", WIDGET_CVAR_CHECKBOX)
        .CVar("gSettings.Menu.Popout")
        .Options(CheckboxOptions().Tooltip("Changes the menu display from overlay to windowed."));
    AddWidget(path, "Debug Mode", WIDGET_CVAR_CHECKBOX)
        .CVar("gEnableDebugMode")
        .Callback([](WidgetInfo& info) {
            SPDLOG_INFO(CVarGetInteger("gEnableDebugMode", 0) == 0 ? "Debug Mode deactivated" : "Debug Mode activated");
        })
        .Options(CheckboxOptions().Tooltip("Enables Debug Mode."));
    AddWidget(path, "Modify Interpolation Target FPS", WIDGET_CVAR_CHECKBOX)
        .CVar("gModifyInterpolationTargetFPS")
        .Options(CheckboxOptions().Tooltip("For testing frame interpolation."));
    AddWidget(path, "Experimental native-rate races", WIDGET_CVAR_CHECKBOX)
        .CVar("gExperimentalNativeRace")
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Unfinished development prototype: runs races at the presentation rate without generated interpolation frames. "
            "Physics, camera, items, audio and game states can behave incorrectly. Menus and cutscenes remain fixed-rate."));
    AddWidget(path, "Experimental native FPS cap (0 = uncapped): %d", WIDGET_CVAR_SLIDER_INT)
        .CVar("gExperimentalNativeFrameCap")
        .PreFunc([](WidgetInfo& info) { info.isHidden = !CVarGetInteger("gExperimentalNativeRace", 0); })
        .Options(IntSliderOptions().Min(0).Max(1000).DefaultValue(0).Tooltip(
            "Zero disables software frame pacing. VSync can still limit the presentation rate to your monitor."));
    AddWidget(path, "Interpolation Target FPS", WIDGET_CVAR_SLIDER_INT)
        .CVar("gInterpolationTargetFPS")
        .PreFunc([](WidgetInfo& info) { info.isHidden = !CVarGetInteger("gModifyInterpolationTargetFPS", 0); })
        .Options(IntSliderOptions()
                     .Tooltip("Sets the target FPS for interpolation. When Modify Interpolation Target FPS are enable")
                     .Min(15)
                     .Max(MAX_FPS)
                     .DefaultValue(60));
    AddWidget(path, "Render Collision", WIDGET_CVAR_CHECKBOX)
        .CVar("gRenderCollisionMesh")
        .Options(CheckboxOptions().Tooltip("Draws the collision mesh instead of the track mesh"));

    AddSceneVisibility();

    AddSidebarEntry("Developer", "Performance", 1);
    path = { "Developer", "Performance", SECTION_COLUMN_1 };
    AddWidget(path, "All large-race optimizations", WIDGET_CUSTOM)
        .CustomFunction([](WidgetInfo&) {
            static const char* const settings[] = {
                CVAR_DISABLE_DEBUG_LOGS, CVAR_LARGE_GRID_LOW_HZ_BOTS, CVAR_LARGE_GRID_FAST_RANKING, CVAR_LARGE_GRID_BOT_PATH_BATCHING, CVAR_LARGE_GRID_MINIMAP, CVAR_LARGE_GRID_PALETTES,
                CVAR_LARGE_GRID_CULLING, CVAR_LARGE_GRID_COLLISION_FILTER,
                CVAR_LARGE_GRID_SKIP_BOT_CONTACTS, CVAR_EXPANDED_TEXTURE_CACHE,
                CVAR_LARGE_GRID_DISTANT_BOTS,
#ifdef _WIN32
                CVAR_DX11_REUSE_DEPTH_STATES, CVAR_LARGE_GRID_DX11_QUEUE,
#endif
            };
            int enabled = 0;
            for (const char* setting : settings) enabled += CVarGetInteger(setting, 0) != 0;
            bool all = enabled == static_cast<int>(std::size(settings));
            if (enabled > 0 && !all) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
            const bool changed = ImGui::Checkbox("Enable all 500+ player optimizations", &all);
            if (enabled > 0 && enabled < static_cast<int>(std::size(settings))) ImGui::PopItemFlag();
            if (changed) {
                for (const char* setting : settings) CVarSetInteger(setting, all ? 1 : 0);
                Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
        });
    AddWidget(path, "Disable all debug logs", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_DISABLE_DEBUG_LOGS)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Turns off game logging, race snapshots, audio traces, and crash reports at any racer count."));
    AddWidget(path, "500+ players optimization - Fast race ranking", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_LARGE_GRID_FAST_RANKING)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Use a faster ranking algorithm for races with 500 or more racers."));
    AddWidget(path, "500+ players optimization - Lower bot logic rate", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_LARGE_GRID_LOW_HZ_BOTS)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Update decisions less often for bots away from human players. Nearby bots and movement still update each tick; distant behavior may be less precise."));
    AddWidget(path, "500+ players optimization - Distant bot path updates", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_LARGE_GRID_BOT_PATH_BATCHING)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Stagger path searches for distant bots while keeping their steering and acceleration updated. Nearby bots update normally."));
    AddWidget(path, "500+ players optimization - Minimap batching", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_LARGE_GRID_MINIMAP)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Reuse drawing state for adjacent minimap markers in races with 500 or more racers."));
    AddWidget(path, "500+ players optimization - Kart texture reuse", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_LARGE_GRID_PALETTES)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Share prepared kart textures when their sprite frames and palettes match in races with 500 or more racers."));
    AddWidget(path, "500+ players optimization - Offscreen kart culling", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_LARGE_GRID_CULLING)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Skip drawing karts fully outside the camera view in races with 500 or more racers."));
    AddWidget(path, "500+ players optimization - Kart collision filtering", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_LARGE_GRID_COLLISION_FILTER)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Skip detailed collision checks for karts that are too far apart to touch."));
    AddWidget(path, "500+ players optimization - Skip bot-to-bot contact", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_LARGE_GRID_SKIP_BOT_CONTACTS)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Bots pass through each other. Contact with human racers, items and track hazards still works."));
    AddWidget(path, "Graphics optimization - Larger texture cache", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_EXPANDED_TEXTURE_CACHE)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Keep more textures in graphics memory to reduce texture reuploads in crowded races."));
    AddWidget(path, "500+ players optimization - Limit distant bot drawing", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_LARGE_GRID_DISTANT_BOTS)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Hide distant bot karts and their effects. Nearby bots and local players keep their normal graphics. "
            "All racers continue racing and appear on the minimap."));
#ifdef _WIN32
    AddWidget(path, "DirectX 11 - Reuse depth states", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_DX11_REUSE_DEPTH_STATES)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Reuse existing depth-test settings when submitting draws in DirectX 11."));
    AddWidget(path, "500+ players optimization - DirectX 11 presentation queue", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_LARGE_GRID_DX11_QUEUE)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Allow one more frame in the presentation queue to overlap CPU and GPU work. "
            "May slightly increase input latency."));
#endif

    AddSidebarEntry("Developer", "Race tools", 1);
    path = { "Developer", "Race tools", SECTION_COLUMN_1 };
    static const auto outsideRace = [](WidgetInfo& info) {
        info.options->disabled = !MultiplayerOptions_CanChange() && !gDemoMode;
        info.options->disabledTooltip = "Return to the game menu to change bot balance.";
    };
    static std::string savedLocationStatus = "";
    AddWidget(path, "Save location", WIDGET_BUTTON)
        .Callback([](WidgetInfo&) { savedLocationStatus = Diagnostics_SaveLocation(); })
        .Options(ButtonOptions().Size(Sizes::Inline).Tooltip(
            "Save the track, race time, and every local player's position and camera to logs/saved-locations.txt."));
    AddWidget(path, "Saved location status", WIDGET_TEXT)
        .PreFunc([](WidgetInfo& info) { info.name = savedLocationStatus; });
    AddWidget(path, "Infinite Time Trial mushrooms", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_TT_INFINITE_MUSHROOMS)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "In Time Trials, a new mushroom appears as soon as you use one. "
            "Time Trial records and ghosts are not saved while this is on."));
    AddWidget(path, "Enable Luigi Raceway and Wario Stadium video screens in multiplayer", WIDGET_CVAR_CHECKBOX)
        .CVar("gMultiplayer.LuigiRacewayScreen")
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Show live gameplay on Luigi Raceway's and Wario Stadium's video screens in multiplayer. "
            "Turn off to restore the blank multiplayer screens. Applies immediately and is saved."));
    AddWidget(path, "Balanced Bots", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_BALANCED_BOTS)
        .PreFunc(outsideRace)
        .Callback([](WidgetInfo&) {
            if (CVarGetInteger(CVAR_BALANCED_BOTS, 0)) CVarSetInteger(CVAR_REAL_BOT_BALANCE, 0);
        })
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Gives each regular CPU racer a different pace target in Grand Prix and VS. "
            "The two rivals keep their original behavior. Harder CPU takes precedence."));
    AddWidget(path, "Real Balance", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_REAL_BOT_BALANCE)
        .PreFunc(outsideRace)
        .Callback([](WidgetInfo&) {
            if (CVarGetInteger(CVAR_REAL_BOT_BALANCE, 0)) CVarSetInteger(CVAR_BALANCED_BOTS, 0);
        })
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Spreads all CPU racers evenly from 100% to 86% of the course pace, including rivals and extra bots. "
            "Bots follow their assigned pace without rival catch-up or pack adjustments. Harder CPU takes precedence."));

#ifdef _WIN32
    AddSidebarEntry("Developer", "Diagnostics", 1);
    path = { "Developer", "Diagnostics", SECTION_COLUMN_1 };
    AddWidget(path, "Large race performance capture", WIDGET_CVAR_CHECKBOX)
        .CVar("gDiagnostics.PerformanceCapture")
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Records aggregate frame, game, CPU path and graphics submission timings every five seconds in logs/performance-capture.csv, even when debug logs are disabled."));
    AddWidget(path, "Detailed crash logging", WIDGET_CVAR_CHECKBOX)
        .CVar("gDiagnostics.DetailedCrashLogging")
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Records race state and Lakitu visibility every quarter second and starts a separate Windows crash monitor. "
            "Captures thread stacks and memory even for failures that bypass the game's crash handler. "
            "Applies immediately and is saved. Turn it off after reproducing the crash."));
    AddWidget(path, "Enable this before starting the race, then reproduce the crash. "
                    "Reports, memory dumps and race snapshots are saved in the game's logs folder. "
                    "The game may pause briefly while the crash dump is written.", WIDGET_TEXT);
    AddWidget(path, "Crash monitor status", WIDGET_TEXT)
        .PreFunc([](WidgetInfo& info) { info.name = CrashMonitor_Status(); });
    AddWidget(path, "Advanced audio debugging", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ADVANCED_AUDIO_DEBUGGING)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Records sound requests, channel starts, cancellations and mixer buffer levels in logs/audio-debug.log. "
            "Enable before reproducing an audio glitch, then turn off to finish the capture."));
    AddWidget(path, "Open logs folder", WIDGET_BUTTON)
        .Callback([](WidgetInfo&) {
            const auto directory = std::filesystem::absolute(Ship::Context::GetPathRelativeToAppDirectory("logs"));
            SDL_OpenURL(std::string("file:///" + directory.string()).c_str());
        })
        .Options(ButtonOptions().Size(Sizes::Inline));
#endif

    path = { "Developer", "Gfx Debugger", SECTION_COLUMN_1 };
    AddSidebarEntry("Developer", "Gfx Debugger", 1);
    AddWidget(path, "Popout Gfx Debugger", WIDGET_WINDOW_BUTTON)
        .CVar("gGfxDebuggerEnabled")
        .Options(ButtonOptions().Tooltip(
            "Enables the Gfx Debugger window, allowing you to input commands. Type help for some examples"))
        .WindowName("GfxDebuggerWindow");

    path = { "Developer", "Stats", SECTION_COLUMN_1 };
    AddSidebarEntry("Developer", "Stats", 1);
    AddWidget(path, "Popout Stats", WIDGET_WINDOW_BUTTON)
        .CVar("gStatsEnabled")
        .Options(ButtonOptions().Tooltip(
            "Shows the stats window, with your FPS and frametimes, and the OS you're playing on"))
        .WindowName("Stats");

    path = { "Developer", "Console", SECTION_COLUMN_1 };
    AddSidebarEntry("Developer", "Console", 1);
    AddWidget(path, "Popout Console", WIDGET_WINDOW_BUTTON)
        .CVar("gConsoleEnabled")
        .Options(ButtonOptions().Tooltip(
            "Enables the console window, allowing you to input commands. Type help for some examples"))
        .WindowName("Console");
}

void PortMenu::AddSceneVisibility() {
    WidgetPath path = { "Developer", "Scene Visibility", SECTION_COLUMN_1 };
    AddSidebarEntry("Developer", "Scene Visibility", 1);

    AddWidget(path, "Draw Sky", WIDGET_CVAR_CHECKBOX)
        .CVar("gDrawSky")
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true));

    AddWidget(path, "Draw Sky Actors", WIDGET_CVAR_CHECKBOX)
        .CVar("gDrawSkyActors")
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true));

    AddWidget(path, "Draw Track Geometry", WIDGET_CVAR_CHECKBOX)
        .CVar("gDrawTrackGeometry")
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true));

    AddWidget(path, "Draw Transparent Track", WIDGET_CVAR_CHECKBOX)
        .CVar("gDrawTransparentTrack")
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true));

    AddWidget(path, "Draw Players", WIDGET_CVAR_CHECKBOX)
        .CVar("gDrawPlayers")
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true));

    AddWidget(path, "Draw Item Boxes", WIDGET_CVAR_CHECKBOX)
        .CVar("gDrawItemBoxes")
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true));

    AddWidget(path, "Draw Original Actors", WIDGET_CVAR_CHECKBOX)
        .CVar("gDrawCActors")
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true));

    AddWidget(path, "Draw Rewrite Actors", WIDGET_CVAR_CHECKBOX)
        .CVar("gDrawCPPActors")
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true));

    AddWidget(path, "Draw Static Mesh", WIDGET_CVAR_CHECKBOX)
        .CVar("gDrawStaticMeshActors")
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true));

    AddWidget(path, "Draw Objects", WIDGET_CVAR_CHECKBOX)
        .CVar("gDrawObjects")
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true));

    AddWidget(path, "Draw Particles", WIDGET_CVAR_CHECKBOX)
        .CVar("gDrawParticles")
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true));

    AddWidget(path, "Draw HUD", WIDGET_CVAR_CHECKBOX)
        .CVar("gDrawHUD")
        .Options(UIWidgets::CheckboxOptions().DefaultValue(true));
}

PortMenu::PortMenu(const std::string& consoleVariable, const std::string& name)
    : Menu(consoleVariable, name, 0, UIWidgets::Colors::LightBlue) {
}

void PortMenu::InitElement() {
    Ship::Menu::InitElement();
    const int legacyDistance = CVarGetInteger(CVAR_RACER_RENDER_DISTANCE, 0);
    if (legacyDistance != 0) {
        CVarSetFloat(CVAR_RACER_RENDER_DISTANCE, static_cast<float>(std::clamp(legacyDistance, 1, 5)));
    }
    if (CVarGetInteger(CVAR_REAL_BOT_BALANCE, 0) && CVarGetInteger(CVAR_BALANCED_BOTS, 0)) {
        CVarSetInteger(CVAR_BALANCED_BOTS, 0);
    }
    AddSettings();
    AddEnhancements();
    AddMultiplayer();
    AddDevTools();

    AddMenuEntry("About", "gSettings.Menu.AboutSidebarSection");
    AddSidebarEntry("About", "Credits", 1);
    WidgetPath credits = { "About", "Credits", SECTION_COLUMN_1 };
    const auto addCreditLink = [&](const char* label, const char* url) {
        AddWidget(credits, label, WIDGET_BUTTON)
            .Callback([](WidgetInfo& widget) { SDL_OpenURL(widget.options->tooltip); })
            .Options(ButtonOptions().Tooltip(url));
    };
    AddWidget(credits, "Ricky Gameplays", WIDGET_SEPARATOR_TEXT);
    addCreditLink("Ricky Gameplays on YouTube", "https://www.youtube.com/@Ricky_Gameplays-Caballo");
    addCreditLink("Ricky Gameplays releases",
                  "https://github.com/Ricky69Gameplays69Caballo/Mario-kart-64-5-players-Ricky-Gameplays/releases");

    AddWidget(credits, "Original port and mods", WIDGET_SEPARATOR_TEXT);
    addCreditLink("SpaghettiKart by Harbour Masters", "https://github.com/HarbourMasters/SpaghettiKart");
    addCreditLink("Mario Kart 64 - Amped Up by Litronom", "https://gamebanana.com/mods/613207");
    addCreditLink("Dragon Ball Kart 64 by ImmaVegeta", "https://n64squid.com/homebrew/roms/dragon-ball-kart-64/");
    addCreditLink("Pokemon Kart 64 by shrugboat", "https://kotaku.com/games/pokemon-kart-64");
    addCreditLink("GTA San Andreas Kart 2.0 by TokattaMusic", "https://gamebanana.com/mods/553919");
    addCreditLink("Simpson Kart 64 by Smashedbrothersgaming", "https://gamebanana.com/mods/553917");
    addCreditLink("Slasher Kart 64 by smashedbrothers7", "https://gamebanana.com/mods/553911");
    addCreditLink("Evangelion Kart 64", "https://gamebanana.com/mods/644048");
    addCreditLink("Evangelion Kart 64 Rebuild Edition", "https://gamebanana.com/mods/644053");
    addCreditLink("Angry Birds Kart 64", "https://gamebanana.com/mods/644055");
    addCreditLink("Mario Kart 64 Deluxe (Sonic, Link and more)", "https://gamebanana.com/mods/336637");

    if (CVarGetInteger("gSettings.Menu.SidebarSearch", 0)) {
        InsertSidebarSearch();
    }

    for (auto& initFunc : MenuInit::GetInitFuncs()) {
        initFunc();
    }

    disabledMap = {
        { DISABLE_FOR_FREE_CAM_ON,
          { [](disabledInfo& info) -> bool { return CVarGetInteger("gFreecam", 0); }, "Freecam is Enabled" } },
        { DISABLE_FOR_FREE_CAM_OFF,
          { [](disabledInfo& info) -> bool { return !CVarGetInteger("gFreecam", 0); }, "Freecam is Disabled" } },
        { DISABLE_FOR_DEBUG_MODE_OFF,
          { [](disabledInfo& info) -> bool { return !CVarGetInteger("gEnableDebugMode", 0); },
            "Debug Mode is Disabled" } },
        { DISABLE_FOR_NO_VSYNC,
          { [](disabledInfo& info) -> bool {
               return !Ship::Context::GetRawInstance()->GetWindow()->CanDisableVerticalSync();
           },
            "Disabling VSync not supported" } },
        { DISABLE_FOR_NO_WINDOWED_FULLSCREEN,
          { [](disabledInfo& info) -> bool {
               return !Ship::Context::GetRawInstance()->GetWindow()->SupportsWindowedFullscreen();
           },
            "Windowed Fullscreen not supported" } },
        { DISABLE_FOR_NO_MULTI_VIEWPORT,
          { [](disabledInfo& info) -> bool {
               return !Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SupportsViewports();
           },
            "Multi-viewports not supported" } },
        { DISABLE_FOR_NOT_DIRECTX,
          { [](disabledInfo& info) -> bool {
               return Ship::Context::GetRawInstance()->GetWindow()->GetWindowBackend() !=
                      Fast::WindowBackend::FAST3D_DXGI_DX11;
           },
            "Available Only on DirectX" } },
        { DISABLE_FOR_DIRECTX,
          { [](disabledInfo& info) -> bool {
               return Ship::Context::GetRawInstance()->GetWindow()->GetWindowBackend() ==
                      Fast::WindowBackend::FAST3D_DXGI_DX11;
           },
            "Not Available on DirectX" } },
        { DISABLE_FOR_MATCH_REFRESH_RATE_ON,
          { [](disabledInfo& info) -> bool { return CVarGetInteger("gMatchRefreshRate", 0); },
            "Match Refresh Rate is Enabled" } },
        { DISABLE_FOR_ADVANCED_RESOLUTION_ON,
          { [](disabledInfo& info) -> bool { return CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".Enabled", 0); },
            "Advanced Resolution Enabled" } },
        { DISABLE_FOR_VERTICAL_RES_TOGGLE_ON,
          { [](disabledInfo& info) -> bool {
               return CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalResolutionToggle", 0);
           },
            "Vertical Resolution Toggle Enabled" } },
        { DISABLE_FOR_LOW_RES_MODE_ON,
          { [](disabledInfo& info) -> bool { return CVarGetInteger(CVAR_LOW_RES_MODE, 0); }, "N64 Mode Enabled" } },
        //{ DISABLE_FOR_MULTIPLAYER_CONNECTED,
        //  { CheckNetworkConnected, "Multiplayer Connected"}},
    };
}

void PortMenu::UpdateElement() {
    Ship::Menu::UpdateElement();
}

void PortMenu::Draw() {
    Ship::Menu::Draw();
}

void PortMenu::DrawElement() {
    Ship::Menu::DrawElement();
}
} // namespace GameUI

#include "port/LocalMultiplayer.h"
#include <libultraship.h>
#include <typeinfo>

#include "Game.h"
#include "CrashMonitor.h"
#include "port/Engine.h"

#include <fast/Fast3dWindow.h>
#include <memory>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <map>
#include <string>
#include <tuple>
#include <limits>
#include <spdlog/sinks/rotating_file_sink.h>
#include "engine/World.h"
#include "engine/AllTracks.h"

#include "engine/tracks/PodiumCeremony.h"

#include "engine/GarbageCollector.h"

#include "engine/TrainCrossing.h"
#include "engine/objects/BombKart.h"
#include "engine/objects/HotAirBalloon.h"
#include "engine/objects/Lakitu.h"

#include "engine/Smoke.h"

#include "engine/HM_Intro.h"

#include "engine/editor/Editor.h"
#include "engine/editor/SceneManager.h"
#include "engine/registry/RegisterContent.h"

#include "engine/cameras/GameCamera.h"
#include "engine/cameras/FreeCamera.h"
#include "engine/cameras/TourCamera.h"
#include "engine/cameras/LookBehindCamera.h"

#include "engine/TrackBrowser.h"
#include "engine/RandomItemTable.h"
#include "engine/sky/Sky.h"

#ifdef _WIN32
#include <locale.h>
#include <windows.h>
#include <DbgHelp.h>
#include <csignal>
#include <exception>
#include <filesystem>
#pragma comment(lib, "Dbghelp.lib")
#endif

extern "C" {
#include "main.h"
#include "audio/load.h"
#include "audio/external.h"
#include "racing/render_courses.h"
#include "racing/collision.h"
#include "menus.h"
#include "update_objects.h"
#include "profiler.h"
#include "spawn_players.h"
#include "src/enhancements/collision_viewer.h"
#include "code_800029B0.h"
#include "code_80005FD0.h"
#include "code_80057C60.h"
#include "objects.h"
// #include "engine/wasm.h"
}

bool gAudioDiagnosticsEnabled = false;

namespace {
using AudioEvent = std::tuple<std::string_view, u32, s32, s32, s32>;
std::map<AudioEvent, unsigned> sAudioEvents;
std::shared_ptr<spdlog::logger> sAudioTrace;
unsigned sAudioTraceOmitted = 0;

// The mixer thread writes this data. The game thread reads and clears it
// between EndAudioFrame and the next StartAudioFrame.
struct AudioBufferStats {
    unsigned blocks = 0;
    unsigned empty = 0;
    unsigned skipped = 0;
    int minimum = std::numeric_limits<int>::max();
    int maximum = 0;
    int target = 0;
    unsigned generated = 0;
};
AudioBufferStats sAudioBufferStats;
int sAudioFirstFrame = 0;

void FlushAudioDiagnostics() {
    if (!sAudioTrace) return;
    const auto& stats = sAudioBufferStats;
    if (!sAudioEvents.empty() || stats.blocks) {
        const int racers = LocalMP_RacerCount();
        sAudioTrace->info("frames={}-{} game={} race={} mode={} course={} humans={} racers={} race_time={:.2f} "
                          "sound_mode={} music_seq={} master_volume={:.2f}",
                          sAudioFirstFrame, gGlobalTimer, gGamestate, gRaceState, gModeSelection, gCurrentCourseId,
                          LocalMP_Count(), racers, gCourseTimer, gSoundMode, gCurrentMusicSeq,
                          CVarGetFloat("gGameMasterVolume", 1.0f));
        for (const auto& [event, count] : sAudioEvents) {
            const auto& [name, sound, player, listener, channel] = event;
            sAudioTrace->info("  {} sound=0x{:08X} player={} listener={} channel={} count={}",
                              name, sound, player, listener, channel, count);
        }
        if (stats.blocks) {
            sAudioTrace->info("  mixer blocks={} buffered_min={} buffered_max={} target={} empty_before_mix={} "
                              "generated_samples={} skipped_fast_forward={}",
                              stats.blocks, stats.minimum, stats.maximum, stats.target, stats.empty,
                              stats.generated, stats.skipped);
        }
        if (sAudioTraceOmitted) sAudioTrace->warn("  omitted_events={}", sAudioTraceOmitted);
    }
    sAudioTrace->flush();
    sAudioEvents.clear();
    sAudioTraceOmitted = 0;
    sAudioBufferStats = {};
    sAudioFirstFrame = gGlobalTimer;
}

void TickAudioDiagnostics() {
    static auto lastWrite = std::chrono::steady_clock::time_point{};
    static int lastGame = -1, lastRace = -1, lastCourse = -1;
    static bool openFailed = false;
    if (CVarGetInteger(CVAR_DISABLE_DEBUG_LOGS, 0)) {
        gAudioDiagnosticsEnabled = false;
        sAudioTrace.reset();
        sAudioEvents.clear();
        sAudioTraceOmitted = 0;
        sAudioBufferStats = {};
        openFailed = false;
        return;
    }
    const bool enabled = CVarGetInteger(CVAR_ADVANCED_AUDIO_DEBUGGING, 0) != 0;
    if (!enabled) {
        if (gAudioDiagnosticsEnabled) {
            FlushAudioDiagnostics();
            sAudioTrace->info("Advanced audio debugging disabled");
            sAudioTrace.reset();
            gAudioDiagnosticsEnabled = false;
        }
        openFailed = false;
        return;
    }
    if (openFailed) return;
    const auto now = std::chrono::steady_clock::now();
    if (!sAudioTrace) {
        try {
            const auto path = Ship::Context::GetPathRelativeToAppDirectory("logs/audio-debug.log");
            auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path, 4 * 1024 * 1024, 3);
            sAudioTrace = std::make_shared<spdlog::logger>("combined-audio-debug", sink);
            sAudioTrace->info("Advanced audio debugging enabled. Player and listener IDs are zero-based; -1 means unavailable.");
            sAudioTrace->flush();
        } catch (const std::exception& error) {
            openFailed = true;
            SPDLOG_ERROR("Could not start advanced audio debugging: {}", error.what());
            return;
        }
        gAudioDiagnosticsEnabled = true;
        lastWrite = now;
        sAudioFirstFrame = gGlobalTimer;
    }
    if (now - lastWrite >= std::chrono::seconds(1) ||
        lastGame != gGamestate || lastRace != gRaceState || lastCourse != gCurrentCourseId) {
        FlushAudioDiagnostics();
        lastWrite = now;
        lastGame = gGamestate;
        lastRace = gRaceState;
        lastCourse = gCurrentCourseId;
    }
}
}

extern "C" void AudioDiagnostics_Record(const char* event, u32 sound, s32 player, s32 listener, s32 channel) {
    const AudioEvent key{event, sound, player, listener, channel};
    auto entry = sAudioEvents.find(key);
    if (entry != sAudioEvents.end()) ++entry->second;
    else if (sAudioEvents.size() < 2048) sAudioEvents.emplace(key, 1);
    else ++sAudioTraceOmitted;
}

extern "C" void AudioDiagnostics_RecordBuffer(s32 buffered, s32 target, s32 generated, bool fastForward) {
    auto& stats = sAudioBufferStats;
    ++stats.blocks;
    stats.empty += buffered <= 0;
    stats.skipped += fastForward;
    stats.minimum = std::min(stats.minimum, buffered);
    stats.maximum = std::max(stats.maximum, buffered);
    stats.target = target;
    stats.generated += generated;
}

#ifdef _WIN32
namespace {
std::shared_ptr<spdlog::logger> sDiagnostics;
std::wstring sCrashDirectory;
LPTOP_LEVEL_EXCEPTION_FILTER sPreviousCrashFilter = nullptr;
volatile LONG sWritingCrash = 0;
const char* sDiagnosticStage = "startup";

void DiagnosticSnapshot(char* buffer, size_t capacity) {
    size_t used = 0;
    auto append = [&](const char* format, auto... args) {
        if (used >= capacity - 1) return;
        const int written = snprintf(buffer + used, capacity - used, format, args...);
        if (written > 0) used += std::min(static_cast<size_t>(written), capacity - used - 1);
    };
    const int humans = LocalMP_Count();
    const int racers = LocalMP_RacerCount();
    append("frame=%d raceFrame=%d raceTime=%.3f stage=%s detailed=%d\n",
           gGlobalTimer, gRaceFrameCounter, gCourseTimer, sDiagnosticStage,
           CVarGetInteger("gDiagnostics.DetailedCrashLogging", 0));
    append("game=%d next=%d race=%d mode=%d course=%d humans=%d racers=%d expanded=%d results=%d paused=%d\n",
           gGamestate, gGamestateNext, gRaceState, gModeSelection, gCurrentCourseId,
           humans, racers, LocalMP_IsActive(), LocalMP_RaceResultsOpen(), gIsGamePaused);
    if (gGamestate != RACING) return;
    if (CVarGetInteger("gDiagnostics.DetailedCrashLogging", 0)) {
        if (gRaceState == RACE_IN_PROGRESS && !gIsGamePaused) {
            double simulation, displayList, graphics;
            if (profiler_get_last_game_frame_ms(&simulation, &displayList, &graphics)) {
                append("Frame timing (ms): simulation=%.2f display_list=%.2f graphics_and_present=%.2f\n",
                       simulation, displayList, graphics);
            }
            const auto context = Ship::Context::GetRawInstance();
            if (auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(context->GetWindow())) {
                if (auto timing = window->GetLastPresentationTiming()) {
                    append("Last rendered frame (ms): draw_submission=%.2f finalize_and_present=%.2f\n",
                           timing->drawMs, timing->finalizeAndPresentMs);
                }
            }
        }
        int activeRacers = 0;
        int pending = 0;
        int struck = 0;
        int shrunk = 0;
        for (int i = 0; i < std::clamp(racers, 0, NUM_RACERS); ++i) {
            const Player& racer = gPlayers[i];
            if (!(racer.type & PLAYER_EXISTS)) continue;
            ++activeRacers;
            pending += (racer.triggers & LIGHTNING_STRIKE_TRIGGER) != 0;
            if (racer.effects & LIGHTNING_EFFECT) {
                ++struck;
                shrunk += racer.size < 1.0f;
            }
        }
        append("Race grid: active=%d total=%d\n", activeRacers, racers);
        if (pending || struck) {
            append("Lightning field: pending=%d struck=%d shrunk=%d racers=%d\n", pending, struck, shrunk, racers);
        }
        append("Lakitu render gates: draw_objects=%d hud_disabled=%d object_render_blocked=%d\n",
               CVarGetInteger("gDrawObjects", 1), gHUDDisable, D_8018D22C);
        append("Object pool: used=%d capacity=%d\n", func_80071FBC(), OBJECT_LIST_SIZE);
        if (const auto* world = GetWorld()) {
            for (int player = 0; player < std::clamp(humans, 0, MAX_HUMAN_PLAYERS); ++player) {
                const auto entry = world->Lakitus.find(player);
                if (entry != world->Lakitus.end() && entry->second) {
                    const auto state = entry->second->DiagnosticState();
                    append("  Lakitu %s\n", state.c_str());
                } else {
                    append("  Lakitu player=%d not spawned\n", player + 1);
                }
            }
            for (const auto& object : world->Objects) {
                if (const auto* balloon = dynamic_cast<const OHotAirBalloon*>(object.get())) {
                    const auto state = balloon->DiagnosticState();
                    append("  Hot-air balloon %s\n", state.c_str());
                }
            }
        }
    }
    for (int player = 0; player < std::clamp(racers, 0, NUM_RACERS); ++player) {
        const Player& racer = gPlayers[player];
        const int item = player < humans && player < MAX_HUMAN_PLAYERS ? gItemWindowObjectByPlayerId[player] : -1;
        const int itemState = item >= 0 && item < OBJECT_LIST_SIZE ? gObjectList[item].state : -1;
        append("slot=%d char=%d type=%04x effects=%08x rank=%d lap=%d pos=(%.2f,%.2f,%.2f) speed=%.2f item=%d state=%d\n",
               player + 1, racer.characterId, static_cast<unsigned>(racer.type),
               static_cast<unsigned>(racer.effects), racer.currentRank, gLapCountByPlayerId[player],
               racer.pos[0], racer.pos[1], racer.pos[2], racer.speed, item, itemState);
        if (CVarGetInteger("gDiagnostics.DetailedCrashLogging", 0)) {
            append(" velocity=(%.3f,%.3f,%.3f) previous=(%.3f,%.3f,%.3f) lakitu=%04x kart=%04x drift=%d surface=%d\n",
                   racer.velocity[0], racer.velocity[1], racer.velocity[2],
                   racer.oldPos[0], racer.oldPos[1], racer.oldPos[2],
                   static_cast<unsigned>(racer.lakituProps), static_cast<unsigned>(racer.kartProps),
                   racer.driftDuration, racer.surfaceType);
            if (player < humans && gScreenContexts[player].camera) {
                const auto& camera = *gScreenContexts[player].camera;
                append(" camera=(%.2f,%.2f,%.2f) lookAt=(%.2f,%.2f,%.2f) mode=%d flags=%04x section=%d playerSection=%d drawSection=%d floorDist=%.2f walls=(%.2f,%.2f)\n",
                    camera.pos[0], camera.pos[1], camera.pos[2], camera.lookAt[0], camera.lookAt[1], camera.lookAt[2],
                    camera.mode, static_cast<unsigned>(camera.someBitFlags),
                    get_track_section_id(camera.collision.meshIndexZX), get_track_section_id(racer.collision.meshIndexZX),
                    gScreenContexts[player].pathCounter, camera.collision.surfaceDistance[2],
                    camera.collision.surfaceDistance[0], camera.collision.surfaceDistance[1]);
            }
        }
    }
}

LONG WINAPI DiagnosticCrashFilter(EXCEPTION_POINTERS* exception) {
    if (CVarGetInteger(CVAR_DISABLE_DEBUG_LOGS, 0)) return EXCEPTION_EXECUTE_HANDLER;
    if (InterlockedCompareExchange(&sWritingCrash, 1, 0) != 0) return EXCEPTION_EXECUTE_HANDLER;
    SYSTEMTIME time;
    GetSystemTime(&time);
    wchar_t name[128];
    swprintf_s(name, L"crash-%04u%02u%02u-%02u%02u%02u-%03u-p%lu", time.wYear, time.wMonth,
               time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentProcessId());
    const std::wstring prefix = sCrashDirectory + L"/" + name;
    const bool detailed = CVarGetInteger("gDiagnostics.DetailedCrashLogging", 0) != 0;
    BOOL dumpWritten = FALSE;
    DWORD dumpError = ERROR_SUCCESS;
    if (detailed) {
        HANDLE dump = CreateFileW((prefix + L".dmp").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (dump != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
            dumpWritten = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo), &info, nullptr, nullptr);
            dumpError = dumpWritten ? ERROR_SUCCESS : GetLastError();
            FlushFileBuffers(dump);
            CloseHandle(dump);
        } else {
            dumpError = GetLastError();
        }
    }
    HANDLE report = CreateFileW((prefix + L".txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (report != INVALID_HANDLE_VALUE) {
        char text[16384] = {};
        int size = snprintf(text, sizeof(text),
            "Combined Windows crash\nUTC %04u-%02u-%02u %02u:%02u:%02u\nexception=0x%08lx address=%p thread=%lu dump=%s error=%lu\n",
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
            exception->ExceptionRecord->ExceptionCode, exception->ExceptionRecord->ExceptionAddress,
            GetCurrentThreadId(), detailed ? (dumpWritten ? "written" : "failed") : "disabled", dumpError);
        DiagnosticSnapshot(text + size, sizeof(text) - size);
        DWORD written;
        WriteFile(report, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
        FlushFileBuffers(report);
        CloseHandle(report);
    }
    // Reports and dumps are on disk before invoking the existing dialog/stack logger.
    return sPreviousCrashFilter ? sPreviousCrashFilter(exception) : EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void DiagnosticAbort(int) {
    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = 0xE0000001; // C++ terminate/SIGABRT, rather than a hardware fault.
#ifdef _WIN64
    record.ExceptionAddress = reinterpret_cast<void*>(context.Rip);
#else
    record.ExceptionAddress = reinterpret_cast<void*>(context.Eip);
#endif
    EXCEPTION_POINTERS exception{&record, &context};
    DiagnosticCrashFilter(&exception);
    ExitProcess(3);
}

void InitCrashDiagnostics() {
    if (CVarGetInteger(CVAR_DISABLE_DEBUG_LOGS, 0) || sDiagnostics) return;
    sCrashDirectory = std::filesystem::path(Ship::Context::GetPathRelativeToAppDirectory("logs")).wstring();
    std::filesystem::create_directories(sCrashDirectory);
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        Ship::Context::GetPathRelativeToAppDirectory("logs/diagnostics.log"), 16 * 1024 * 1024, 5);
    // Synchronous, infrequent state records survive abrupt process termination.
    sDiagnostics = std::make_shared<spdlog::logger>("combined-diagnostics", sink);
    sDiagnostics->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    sDiagnostics->flush_on(spdlog::level::info);
    sDiagnostics->info("Crash diagnostics enabled; pid={}; text crash reports in logs", GetCurrentProcessId());
    sPreviousCrashFilter = SetUnhandledExceptionFilter(DiagnosticCrashFilter);
    std::signal(SIGABRT, DiagnosticAbort);
    std::set_terminate([] { DiagnosticAbort(SIGABRT); });
}

void TickCrashDiagnostics() {
    if (CVarGetInteger(CVAR_DISABLE_DEBUG_LOGS, 0)) {
        CrashMonitor_SetEnabled(false);
        if (sDiagnostics) sDiagnostics->set_level(spdlog::level::off);
        return;
    }
    InitCrashDiagnostics();
    sDiagnostics->set_level(spdlog::level::info);
    static int lastGame = -1, lastRace = -1, lastCourse = -1;
    static int lastDetailed = -1;
    static auto lastWrite = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    const bool detailed = CVarGetInteger("gDiagnostics.DetailedCrashLogging", 0) != 0;
    CrashMonitor_SetEnabled(detailed);
    if (lastDetailed != detailed) {
        sDiagnostics->info("Detailed crash logging {}", detailed ? "enabled (250 ms snapshots; external crash monitor)" : "disabled");
        lastDetailed = detailed;
        lastGame = -1;
    }
    if (lastGame == gGamestate && lastRace == gRaceState && lastCourse == gCurrentCourseId &&
        now - lastWrite < std::chrono::milliseconds(detailed ? 250 : 5000)) return;
    lastGame = gGamestate;
    lastRace = gRaceState;
    lastCourse = gCurrentCourseId;
    lastWrite = now;
    char snapshot[16384] = {};
    DiagnosticSnapshot(snapshot, sizeof(snapshot));
    sDiagnostics->info("{}", snapshot);
}
}
#endif

namespace {
struct LightningMapStats { unsigned races = 0; unsigned uses = 0; };
std::map<std::string, LightningMapStats> sLightningMaps;
std::string sLightningMap;
unsigned sLightningRaceUses = 0;
bool sLightningRaceActive = false;

void LogLightningSummary(const char* reason) {
    if (!sLightningRaceActive) return;
    const auto& total = sLightningMaps.at(sLightningMap);
    const std::string line = fmt::format(
        "Lightning race summary: map=\"{}\" uses={} map_session_uses={} map_session_races={} reason={}",
        sLightningMap, sLightningRaceUses, total.uses, total.races, reason);
    SPDLOG_INFO("{}", line);
#ifdef _WIN32
    if (sDiagnostics) sDiagnostics->info("{}", line);
#endif
    sLightningRaceActive = false;
}
void TickLightningSummary() {
    if (sLightningRaceActive && (gGamestate != RACING || LocalMP_RaceResultsOpen() || gRaceState == RACE_FINISHED))
        LogLightningSummary(gGamestate != RACING ? "left race" : "race finished");
}
}

extern "C" const char* Diagnostics_SaveLocation(void) {
    if (gGamestate != RACING || !CM_GetProps()) return "Start a race before saving a location.";
    try {
        const auto directory = Ship::Context::GetPathRelativeToAppDirectory("logs");
        std::filesystem::create_directories(directory);
        const auto filePath = std::filesystem::path(directory) / "saved-locations.txt";
        std::ofstream out(filePath, std::ios::app);
        if (!out) return "Could not open logs/saved-locations.txt.";
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        out << fmt::format("\nSaved location (Unix time {}): track=\"{}\" course={} mode={} race_time={:.3f}\n",
                           static_cast<long long>(now), CM_GetProps()->Name, gCurrentCourseId, gModeSelection, gCourseTimer);
        const int humans = std::clamp(LocalMP_Count(), 1, MAX_HUMAN_PLAYERS);
        for (int i = 0; i < humans; ++i) {
            const Player& player = gPlayers[i];
            out << fmt::format("P{} position=({:.2f}, {:.2f}, {:.2f}) rotation=({}, {}, {}) speed={:.2f} surface={}\n",
                               i + 1, player.pos[0], player.pos[1], player.pos[2], player.rotation[0],
                               player.rotation[1], player.rotation[2], player.speed, player.surfaceType);
            if (gScreenContexts[i].camera) {
                const Camera& camera = *gScreenContexts[i].camera;
                out << fmt::format("   camera=({:.2f}, {:.2f}, {:.2f}) look_at=({:.2f}, {:.2f}, {:.2f})\n",
                                   camera.pos[0], camera.pos[1], camera.pos[2], camera.lookAt[0], camera.lookAt[1], camera.lookAt[2]);
            }
        }
        out.flush();
        if (!out) return "Could not write logs/saved-locations.txt.";
        return "Saved to logs/saved-locations.txt.";
    } catch (const std::exception&) {
        return "Could not write logs/saved-locations.txt.";
    }
}

extern "C" void Diagnostics_BeginLightningRace(void) {
    LogLightningSummary("next race or restart");
    if (gDemoMode || gGamestate != RACING) return;
    sLightningMap = CM_GetProps()->Name;
    sLightningRaceUses = 0;
    ++sLightningMaps[sLightningMap].races;
    sLightningRaceActive = true;
}

extern "C" void Diagnostics_RecordLightningUse(int playerId) {
    if (gDemoMode || gGamestate != RACING || gRaceState == RACE_FINISHED ||
        LocalMP_RaceResultsOpen() || playerId < 0 || playerId >= NUM_RACERS) return;
    if (!sLightningRaceActive) Diagnostics_BeginLightningRace();
    if (!sLightningRaceActive) return;
    const unsigned total = ++sLightningMaps[sLightningMap].uses;
    ++sLightningRaceUses;
    const std::string line = fmt::format(
        "Lightning used: map=\"{}\" course={} player={} race_time={:.3f} race_uses={} map_session_uses={}",
        sLightningMap, gCurrentCourseId, playerId + 1, gCourseTimer, sLightningRaceUses, total);
    SPDLOG_INFO("{}", line);
#ifdef _WIN32
    if (sDiagnostics) sDiagnostics->info("{}", line);
#endif
}

extern "C" void Graphics_PushFrame(Gfx* pool) {
    GameEngine::ProcessGfxCommands(pool);
}

// Create the world instance
static World sWorldInstance;

// Deferred cleaning when clearing all actors in the editor
bool bCleanWorld = false;

std::unique_ptr<Cup> gMushroomCup;
std::unique_ptr<Cup> gFlowerCup;
std::unique_ptr<Cup> gStarCup;
std::unique_ptr<Cup> gSpecialCup;
std::unique_ptr<Cup> gBattleCup;

HarbourMastersIntro gMenuIntro;

TrackEditor::Editor gEditor;

s32 gTrophyIndex = NULL_OBJECT_ID;

/** Spawner Registries **/
Registry<TrackInfo> gTrackRegistry;
Registry<ActorInfo, const SpawnParams&> gActorRegistry;
Registry<ItemInfo> gItemRegistry;

/** Data Registries **/
DataRegistry<RandomItemTable> gItemTableRegistry;

std::unique_ptr<TrackBrowser> gTrackBrowser;
std::unique_ptr<Sky> gSky;

World* GetWorld() {
    return World::Instance;
}

void CustomEngineInit() {
    // Close the editor because lus remembers if it was open
    // This also turns off freecam
    gEditor.Disable();

    gSky = std::make_unique<Sky>();
    RegisterTracks(gTrackRegistry);
    gTrackBrowser = std::make_unique<TrackBrowser>(gTrackRegistry);
    TrackBrowser::Instance->FindCustomTracks();
    TrackBrowser::Instance->Refresh(gTrackRegistry);

    gMushroomCup = std::make_unique<Cup>("mk:mushroom_cup", "Mushroom Cup", std::vector<std::string>{
        "mk:luigi_raceway", 
        "mk:moo_moo_farm", 
        "mk:koopa_troopa_beach", 
        "mk:kalimari_desert"
    });

    gFlowerCup = std::make_unique<Cup>("mk:flower_cup", "Flower Cup", std::vector<std::string>{
        "mk:toads_turnpike", 
        "mk:frappe_snowland", 
        "mk:choco_mountain", 
        "mk:mario_raceway"
    });

    gStarCup = std::make_unique<Cup>("mk:star_cup", "Star Cup", std::vector<std::string>{
        "mk:wario_stadium", 
        "mk:sherbet_land", 
        "mk:royal_raceway", 
        "mk:bowsers_castle"
    });

    gSpecialCup = std::make_unique<Cup>("mk:special_cup", "Special Cup", std::vector<std::string>{
        "mk:dk_jungle", 
        "mk:yoshi_valley", 
        "mk:banshee_boardwalk", 
        "mk:rainbow_road"
    });

    gBattleCup = std::make_unique<Cup>("mk:battle_cup", "Battle Cup", std::vector<std::string>{
        "mk:big_donut", 
        "mk:block_fort", 
        "mk:double_deck", 
        "mk:skyscraper"
    });

    /* Validate Cup Track IDs */
    gMushroomCup->ValidateTrackIds(gTrackRegistry);
    gFlowerCup->ValidateTrackIds(gTrackRegistry);
    gStarCup->ValidateTrackIds(gTrackRegistry);
    gSpecialCup->ValidateTrackIds(gTrackRegistry);
    gBattleCup->ValidateTrackIds(gTrackRegistry);

    /* Instantiate Cups */
    GetWorld()->AddCup(gMushroomCup.get());
    GetWorld()->AddCup(gFlowerCup.get());
    GetWorld()->AddCup(gStarCup.get());
    GetWorld()->AddCup(gSpecialCup.get());
    GetWorld()->AddCup(gBattleCup.get());
    GetWorld()->InitExtraCups();

    SetMarioRaceway();

    printf("[Game] Registering Game Content...\n");
    RegisterActors(gActorRegistry);
    RegisterItems(gItemRegistry);
    RegisterItemTables(gItemTableRegistry);
    printf("[Game] Game Content Registered!\n");
}

void CustomEngineDestroy() {
    gTrackRegistry.Clear();
    gActorRegistry.Clear();
    gMushroomCup.reset();
    gFlowerCup.reset();
    gStarCup.reset();
    gSpecialCup.reset();
    gBattleCup.reset();
}

extern "C" {

void HM_InitIntro() {
    gMenuIntro.HM_InitIntro();
}

void HM_TickIntro() {
    gMenuIntro.HM_TickIntro();
}

void HM_DrawIntro() {
    gMenuIntro.HM_DrawIntro();
}

// Set default track; mario raceway
void SetMarioRaceway(void) {
    SelectMarioRaceway();
    GetWorld()->SetCurrentCup(gMushroomCup.get());
    GetWorld()->GetCurrentCup()->CursorPosition = 3;
    GetWorld()->CupIndex = 0;
}

void ExtraMaps_OnChanged(void) {
    auto* world = GetWorld();
    if (!world || CVarGetInteger(CVAR_EXTRA_MAPS, 1)) return;
    const size_t index = world->GetCupIndex();
    world->SetExtraCupPage(false);
    if (index < world->Cups.size()) world->SetCurrentCup(world->Cups[index]);
    TrackBrowser_ResetSelectedTrack();
}
int ExtraMaps_Available(void) { return GetWorld()->ExtraCupsAvailable(); }
int ExtraMaps_Page(void) { return GetWorld()->IsExtraCupPage() && GetWorld()->ExtraCupsAvailable(); }
void ExtraMaps_SetPage(int enabled) { GetWorld()->SetExtraCupPage(enabled != 0); }
const char* ExtraMaps_CourseName(int cup, int row) {
    if (cup < 0 || cup >= 4 || row < 0 || row >= 4) return "";
    char name[32];
    snprintf(name, sizeof(name), "amped:course_%02d", cup * 4 + row + 1);
    const TrackInfo* info = gTrackRegistry.GetInfo(name);
    if (!info) return "";
    static std::array<std::string, 16> labels;
    auto& label = labels[cup * 4 + row];
    label = info->Name;
    const auto suffix = label.find(" (test)");
    if (suffix != std::string::npos) label.erase(suffix);
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) { return std::toupper(c); });
    return label.c_str();
}

u32 WorldNextCup(void) {
    return GetWorld()->NextCup();
}

u32 WorldPreviousCup(void) {
    return GetWorld()->PreviousCup();
}

void CM_SetCup(void* cup) {
    GetWorld()->SetCurrentCup((Cup*) cup);
}

void* GetCup() {
    return GetWorld()->GetCurrentCup();
}

u32 GetCupIndex(void) {
    return GetWorld()->GetCupIndex();
}

void CM_SetCupIndex(size_t index) {
    GetWorld()->SetCupIndex(index);
}

const char* GetCupName(void) {
    return GetWorld()->GetCurrentCup()->Name;
}

void LoadTrack() {
    GetWorld()->GetRaceManager().Load();
}

void CM_VehicleCollision(s32 playerId, Player* player) {
    for (auto& actor : GetWorld()->Actors) {
        if (actor) {
            actor->VehicleCollision(playerId, player);
        }
    }
}

void CM_BombKartsWaypoint(s32 cameraId) {
    for (auto& object : GetWorld()->Objects) {
        if (auto* kart = dynamic_cast<OBombKart*>(object.get())) {
            if (kart != nullptr) {
                kart->Waypoint(cameraId);
            }
        }
    }
}

void CM_DisplayBattleBombKart(s32 playerId, s32 primAlpha) {

    if ((playerId < 0) || (playerId >= ARRAY_COUNT(GetWorld()->mPlayerBombKart))) {
        return;
    }

    if (primAlpha == 0) {
        GetWorld()->mPlayerBombKart[playerId].state = PlayerBombKart::PlayerBombKartState::DISABLED;
        GetWorld()->mPlayerBombKart[playerId]._primAlpha = primAlpha;
    } else {
        GetWorld()->mPlayerBombKart[playerId].state = PlayerBombKart::PlayerBombKartState::ACTIVE;
        GetWorld()->mPlayerBombKart[playerId]._primAlpha = primAlpha;
    }
}

void CM_DrawBattleBombKarts(s32 cameraId) {
    for (size_t i = 0; i < LocalMP_Count(); i++) {
        GetWorld()->mPlayerBombKart[i].Draw(i, cameraId);
    }
}

void CM_ClearVehicles(void) {
    GetWorld()->Crossings.clear();
}

void CM_CrossingTrigger() {
    for (auto& crossing : GetWorld()->Crossings) {
        if (crossing) {
            crossing->CrossingTrigger();
        }
    }
}

void CM_AICrossingBehaviour(s32 playerId) {
    for (auto& crossing : GetWorld()->Crossings) {
        if (crossing) {
            crossing->AICrossingBehaviour(playerId);
        }
    }
}

s32 CM_GetCrossingOnTriggered(uintptr_t* crossing) {
    TrainCrossing* ptr = (TrainCrossing*) crossing;
    if (ptr) {
        return ptr->OnTriggered;
    }
    return 0;
}

/**
 * Tracks are rendered in two ways
 * 1) Track sections --> The scene is split into multiple sections and rendered piece by piece
 * 2) Full scene --> The entire scene is rendered at once
 * 
 * Custom tracks only use the Render() method, and they only render the full scene.
 * They do not use DrawCredits() and they do not use track sections.
 */
void CM_DrawTrack(ScreenContext* screen) {
    if (nullptr == GetWorld()->GetTrack()) {
        return;
    }

    // Check if collision mesh rendering is enabled via CVar
    if (CVarGetInteger("gRenderCollisionMesh", 0)) {
        render_collision();
        return;
    }

    // Custom tracks should never use DrawCredits();
    if (GetWorld()->GetTrack()->IsMod()) {
        switch(screen->camera->renderMode) {
            default:
                GetWorld()->GetTrack()->Draw(screen);
                break;
            case RENDER_COLLISION_MESH:
                render_collision();
                break;
        } 
    } else {
        switch(screen->camera->renderMode) {
            case RENDER_FULL_SCENE:
                if (gModeSelection == BATTLE) {
                    GetWorld()->GetTrack()->Draw(screen);
                } else {
                    GetWorld()->GetTrack()->DrawCredits();
                }
            case RENDER_TRACK_SECTIONS:
                GetWorld()->GetTrack()->Draw(screen);
                break;
            case RENDER_COLLISION_MESH:
                render_collision();
                break;
        }
    }
}

void CM_TickActors() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->TickActors();
    }
}

void CM_TickActors60fps() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->TickActors60fps();
    }
}

void CM_DrawActors(Camera* camera) {
    //AActor* a = GetWorld()->ConvertActorToAActor(actor);
    for (const auto& actor : GetWorld()->Actors) {
        if (actor->IsMod()) {
            actor->Draw(camera);
        }
    }

    for (auto& camera : GetWorld()->Cameras) {
        if (auto* tourCam = dynamic_cast<TourCamera*>(camera.get())) {
            if (tourCam->IsActive()) {
                tourCam->Draw();
            }
        }
    }
}

void CM_DrawStaticMeshActors() {
    GetWorld()->DrawStaticMeshActors();
}

void CM_BeginPlay() {
    static bool tour = false;
    auto track = GetWorld()->GetTrack();
    GetWorld()->CleanActors();
    
    if (nullptr == track) {
        return; 
    }

    if (tour) {
      //  GetWorld()->Cameras[2]->SetActive(true);
       // gScreenOneCtx->camera = GetWorld()->Cameras[2]->Get();
        if (reinterpret_cast<TourCamera*>(GetWorld()->Cameras[2].get())->IsTourComplete()) {
            tour = false;
            gScreenOneCtx->pendingCamera = &cameras[0];
        }
    }

    GetWorld()->GetRaceManager().PreInit();
    GetWorld()->GetRaceManager().BeginPlay();
    GetWorld()->GetRaceManager().PostInit();
}

Camera* CM_GetPlayerCamera(s32 playerIndex) {
    for (auto& cam : GetWorld()->Cameras) {
        // Make sure this is a player camera and not a different type of camera
        if (typeid(*cam) == typeid(GameCamera)) {
            Camera* camera = cam->Get();
            if (camera->playerId == playerIndex) {
                return camera;
            }
        }
    }
    return nullptr;
}

void CM_SetViewProjection(Camera* camera) {
    for (auto& gameCamera : GetWorld()->Cameras) {
        if (camera == gameCamera->Get()) {
            gameCamera->SetViewProjection();
        }
    }
}

void CM_TickCameras() {
    GetWorld()->TickCameras();
}

Camera* CM_AddCamera(Vec3f spawn, s16 rot, u32 mode) {
    if (GetWorld()->Cameras.size() >= NUM_CAMERAS) {
        printf("Reached the max number of cameras, %d\n", NUM_CAMERAS);
        return nullptr;
    }
    GetWorld()->Cameras.push_back(std::make_unique<GameCamera>(FVector(spawn[0], spawn[1], spawn[2]), rot, mode));
    return GetWorld()->Cameras.back()->Get();
}

Camera* CM_AddFreeCamera(Vec3f spawn, s16 rot, u32 mode) {
    if (GetWorld()->Cameras.size() >= NUM_CAMERAS) {
        printf("Reached the max number of cameras, %d\n", NUM_CAMERAS);
        return nullptr;
    }
    GetWorld()->Cameras.push_back(std::make_unique<FreeCamera>(FVector(spawn[0], spawn[1], spawn[2]), rot, mode));
    return GetWorld()->Cameras.back()->Get();
}

Camera* CM_AddTourCamera(Vec3f spawn, s16 rot, u32 mode) {
    if (GetWorld()->Cameras.size() >= NUM_CAMERAS) {
        // This is to prevent soft locking the game
        printf("Reached the max number of cameras, %d\n", NUM_CAMERAS);
        if (GetWorld()->GetTrack()->bTourEnabled) {
            spawn_and_set_player_spawns();
        }
        return nullptr;
    }

    if (nullptr == GetWorld()->GetTrack()) {
        // This is to prevent soft locking the game
        if (GetWorld()->GetTrack()->bTourEnabled) {
            spawn_and_set_player_spawns();
        }
        return nullptr;
    }

    if (GetWorld()->GetTrack()->TourShots.size() == 0) {
        // This is to prevent soft locking the game
        if (GetWorld()->GetTrack()->bTourEnabled) {
            spawn_and_set_player_spawns();
        }
        return nullptr;
    }

    GetWorld()->Cameras.push_back(std::make_unique<TourCamera>(FVector(spawn[0], spawn[1], spawn[2]), rot, mode));
    TourCamera* tour = static_cast<TourCamera*>(GetWorld()->Cameras.back().get());
    tour->SetActive(true);
    return tour->Get();
}

bool CM_IsTourEnabled() {
    if (nullptr != GetWorld()->GetTrack()) {
        if ((GetWorld()->GetTrack()->bTourEnabled) && (gTourComplete == false)) {
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

Camera* CM_AddLookBehindCamera(Vec3f spawn, s16 rot, u32 mode) {
    if (GetWorld()->Cameras.size() >= NUM_CAMERAS) {
        printf("Reached the max number of cameras, %d\n", NUM_CAMERAS);
        return nullptr;
    }
    GetWorld()->Cameras.push_back(std::make_unique<LookBehindCamera>(FVector(spawn[0], spawn[1], spawn[2]), rot, mode));
    return GetWorld()->Cameras.back()->Get();
}

void CM_AttachCamera(Camera* camera, s32 playerIdx) {
    camera->playerId = playerIdx;
}

void CM_CameraSetActive(size_t idx, bool state) {
    if (idx < GetWorld()->Cameras.size()) {
        GetWorld()->Cameras[idx]->SetActive(state);
    }
}

void CM_SetFreeCamera(bool state) {
    for (auto& cam : GetWorld()->Cameras) {
        if (cam->Get() == gScreenOneCtx->freeCamera) {
            if (state) {
                gScreenOneCtx->pendingCamera = gScreenOneCtx->freeCamera;
                cam->SetActive(true);
            } else {
                if (nullptr != gScreenOneCtx->raceCamera) {
                    if (gGamestate == RACING) {
                        gScreenOneCtx->pendingCamera = gScreenOneCtx->raceCamera;
                        cam->SetActive(false);
                    } else {
                        cam->SetActive(false);
                    }
                }
            }
        }
    }
}

void CM_ActivateTourCamera(Camera* camera) {
    for (auto& cam : GetWorld()->Cameras) {
        if (cam->Get() == camera) {
            cam->SetActive(true);
        }
    }
}

void CM_TickObjects() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->TickObjects();
    }
}

// A couple objects such as lakitu are ticked inside of process_game_tick which support 60fps.
// This is a fallback to support that.
void CM_TickObjects60fps() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->TickObjects60fps();
    }
}

void CM_DrawObjects(s32 cameraId) {
    if (GetWorld()->GetTrack()) {
        GetWorld()->DrawObjects(cameraId);
    }
}

void CM_TickEditor() {
    gEditor.Tick();
}

void CM_DrawEditor() {
    gEditor.Draw();
}

void CM_TickParticles() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->TickParticles();
    }
}

void CM_DrawParticles(s32 cameraId) {
    if (GetWorld()->GetTrack()) {
        GetWorld()->DrawParticles(cameraId);
    }
}

void CM_RaceDrawSky(ScreenContext* screen, s32 someId) {
    // if (bDrawSkybox) {
    if (CVarGetInteger("gDrawSky", true) == true) {
        Sky::Instance->Draw(screen);
        if (gGamestate != CREDITS_SEQUENCE) {
            func_80057FC4(screen, someId); // DrawSkyActors
        }
        Sky::Instance->DrawFloor(screen);
    }
}

void CM_Waypoints(Player* player, int16_t playerId) {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->Waypoints(player, playerId);
    }
}

void CM_SomeCollisionThing(Player* player, Vec3f arg1, Vec3f arg2, Vec3f arg3, f32* arg4, f32* arg5, f32* arg6,
                           f32* arg7) {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->SomeCollisionThing(player, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
    }
}

void CM_InitTrackObjects() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->InitTrackObjects();
    }
}

void CM_TickTrackObjects() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->TickTrackObjects();
    }
    TrainSmokeTick();
}

void CM_DrawTrackObjects(s32 cameraId) {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->DrawTrackObjects(cameraId);
    }

    TrainSmokeDraw(cameraId);
}

void CM_SomeSounds() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->SomeSounds();
    }
}

void CM_CreditsSpawnActors() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->CreditsSpawnActors();
    }
}

void CM_WhatDoesThisDo(Player* player, int16_t playerId) {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->WhatDoesThisDo(player, playerId);
    }
}

void CM_WhatDoesThisDoAI(Player* player, int16_t playerId) {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->WhatDoesThisDoAI(player, playerId);
    }
}

void CM_SetStaffGhost() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->SetStaffGhost();
    }
}

// This should only be used for checking if the track has changed
uintptr_t CM_GetTrack() {
    return (uintptr_t) (void*) GetWorld()->GetTrack();
}

Properties* CM_GetProps() {
    if (GetWorld()->GetTrack()) {
        return &GetWorld()->GetTrack()->Props;
    }
    return NULL;
}

void CM_TickTrack() {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->Tick();
    }
}

void CM_DrawTransparency(ScreenContext* screen, uint16_t pathCounter, uint16_t cameraRot,
                  uint16_t playerDirection) {
    if (GetWorld()->GetTrack()) {
        GetWorld()->GetTrack()->DrawTransparency(screen, pathCounter, cameraRot, playerDirection);
    }
}

/**
 * This should only be ran once per track, otherwise animation/timings might become sped up.
 */
void CM_SpawnStarterLakitu() {
    if ((gDemoMode) || (gGamestate == CREDITS_SEQUENCE)) {
        return;
    }

    const auto type = gQuickTimeTrial ? OLakitu::IDLE : OLakitu::STARTER;
    for (size_t i = 0; i < LocalMP_Count(); i++) {
        // Retry does not respawn actors, therefore, re-use lakitu.
        if (auto it = GetWorld()->Lakitus.find(i); it != GetWorld()->Lakitus.end()) {
            if (it->second) {
                it->second->Activate(type);
            }
            continue; // Already exists, skip spawning
        }

        auto lakitu = std::make_unique<OLakitu>(i, type);
        GetWorld()->Lakitus[i] = lakitu.get();
        GetWorld()->AddObject(std::move(lakitu));
    }
}

// Checkered flag lakitu
void CM_ActivateFinishLakitu(s32 playerId) {
    if ((gDemoMode) || (gGamestate == CREDITS_SEQUENCE)) {
        return;
    }
    GetWorld()->Lakitus[playerId]->Activate(OLakitu::LakituType::FINISH);
}

void CM_ActivateSecondLapLakitu(s32 playerId) {
    if ((gDemoMode) || (gGamestate == CREDITS_SEQUENCE)) {
        return;
    }
    GetWorld()->Lakitus[playerId]->Activate(OLakitu::LakituType::SECOND_LAP);
}

void CM_ActivateFinalLapLakitu(s32 playerId) {
    if ((gDemoMode) || (gGamestate == CREDITS_SEQUENCE)) {
        return;
    }
    GetWorld()->Lakitus[playerId]->Activate(OLakitu::LakituType::FINAL_LAP);
}

void CM_ActivateReverseLakitu(s32 playerId) {
    if ((gDemoMode) || (gGamestate == CREDITS_SEQUENCE)) {
        return;
    }
    GetWorld()->Lakitus[playerId]->Activate(OLakitu::LakituType::REVERSE);
}

size_t GetCupCursorPosition() {
    return GetWorld()->GetCurrentCup()->CursorPosition;
}

void SetCupCursorPosition(size_t position) {
    GetWorld()->GetCurrentCup()->SetTrack(position);
    // GetWorld()->CurrentCup->CursorPosition = position;
}

size_t GetCupSize() {
    return GetWorld()->GetCurrentCup()->GetSize();
}

void* GetTrack(void) {
    return GetWorld()->GetTrack();
}

struct Actor* CM_GetActor(size_t index) {
    if (index >= 0 && index < GetWorld()->Actors.size()) {
        AActor* actor = GetWorld()->Actors[index].get();
        return reinterpret_cast<struct Actor*>(reinterpret_cast<char*>(actor) + sizeof(void*));
    } else {
        throw std::runtime_error("GetActor() index out of bounds");
        return NULL;
    }
}

size_t CM_FindActorIndex(Actor* actor) {
    // Move the ptr back to look at the vtable.
    // This gets us the proper C++ class instead of just the variables used in C.
    AActor* a = reinterpret_cast<AActor*>(reinterpret_cast<char*>(actor) - sizeof(void*));
    auto& actors = GetWorld()->Actors;

    auto it = std::find_if(actors.begin(), actors.end(), [a](const std::unique_ptr<AActor>& ptr) {
        return ptr.get() == a;
    });

    if (it != actors.end()) {
        return std::distance(actors.begin(), it);
    }
    printf("FindActorIndex() actor not found\n");
    return -1;
}

void CM_DeleteActor(size_t index) {
    auto& actors = GetWorld()->Actors;
    if (index < actors.size()) {
        actors.erase(actors.begin() + index);
    }
}

/**
 * Clean up actors and other game objects.
 */
void CM_CleanWorld(void) {
    GetWorld()->CleanWorld();
}

void CM_CleanCameras(void) {
    GetWorld()->Cameras.clear();
}

struct Actor* CM_AddBaseActor() {
    return (struct Actor*) GetWorld()->AddBaseActor();
}

void CM_ActorBeginPlay(struct Actor* actor) {
    GetWorld()->ActorBeginPlay(actor);
}

void CM_ActorGenerateCollision(struct Actor* actor) {
    AActor* act = GetWorld()->ConvertActorToAActor(actor);

    if ((nullptr != act->Model) && (act->Model[0] != '\0')) {
        if (act->Triangles.size() == 0) {
            TrackEditor::GenerateCollisionMesh(act, (Gfx*)LOAD_ASSET_RAW(act->Model), 1.0f);
        }
    }
}

void Editor_AddLight(s8* direction) {
    static size_t i = 0;
    gEditor.AddLight(("Light "+std::to_string(i)).c_str(), nullptr, direction);
    i += 1;
}

void Editor_ClearMatrix() {
    gEditor.ClearMatrixPool();
}

void Editor_CleanWorld() {
    if (bCleanWorld) {
        CM_CleanWorld();
        bCleanWorld = false;
    }
}

size_t CM_GetActorSize() {
    return GetWorld()->Actors.size();
}

void CM_ActorCollision(Player* player, Actor* actor) {
    AActor* a = GetWorld()->ConvertActorToAActor(actor);

    if (a->IsMod()) {
        a->Collision(player, a);
    }
}

f32 CM_GetWaterLevel(Vec3f pos, struct Collision* collision) {
    FVector fPos = {pos[0], pos[1], pos[2]};
    return GetWorld()->GetTrack()->GetWaterLevel(fPos, collision);
}

// clang-format off
bool IsMarioRaceway()     { return dynamic_cast<MarioRaceway*>(GetWorld()->GetTrack()) != nullptr; }
bool IsLuigiRaceway()     { return dynamic_cast<LuigiRaceway*>(GetWorld()->GetTrack()) != nullptr; }
bool IsChocoMountain()    { return dynamic_cast<ChocoMountain*>(GetWorld()->GetTrack()) != nullptr; }
bool IsBowsersCastle()    { return dynamic_cast<BowsersCastle*>(GetWorld()->GetTrack()) != nullptr; }
bool IsBansheeBoardwalk() { return dynamic_cast<BansheeBoardwalk*>(GetWorld()->GetTrack()) != nullptr; }
bool IsYoshiValley()      { return dynamic_cast<YoshiValley*>(GetWorld()->GetTrack()) != nullptr; }
bool IsFrappeSnowland()   { return dynamic_cast<FrappeSnowland*>(GetWorld()->GetTrack()) != nullptr; }
bool IsKoopaTroopaBeach() { return dynamic_cast<KoopaTroopaBeach*>(GetWorld()->GetTrack()) != nullptr; }
bool IsRoyalRaceway()     { return dynamic_cast<RoyalRaceway*>(GetWorld()->GetTrack()) != nullptr; }
bool IsMooMooFarm()       { return dynamic_cast<MooMooFarm*>(GetWorld()->GetTrack()) != nullptr; }
bool IsToadsTurnpike()    { return dynamic_cast<ToadsTurnpike*>(GetWorld()->GetTrack()) != nullptr; }
bool IsKalimariDesert()   { return dynamic_cast<KalimariDesert*>(GetWorld()->GetTrack()) != nullptr; }
bool IsSherbetLand()      { return dynamic_cast<SherbetLand*>(GetWorld()->GetTrack()) != nullptr; }
bool IsRainbowRoad()      { return dynamic_cast<RainbowRoad*>(GetWorld()->GetTrack()) != nullptr; }
bool CM_IsTrackResource(const char* resourceName) {
    const auto* track = GetWorld()->GetTrack();
    return track && track->ResourceName == resourceName;
}

bool IsWarioStadium()     { return dynamic_cast<WarioStadium*>(GetWorld()->GetTrack()) != nullptr; }
bool IsBlockFort()        { return dynamic_cast<BlockFort*>(GetWorld()->GetTrack()) != nullptr; }
bool IsSkyscraper()       { return dynamic_cast<Skyscraper*>(GetWorld()->GetTrack()) != nullptr; }
bool IsDoubleDeck()       { return dynamic_cast<DoubleDeck*>(GetWorld()->GetTrack()) != nullptr; }
bool IsDkJungle()         { return dynamic_cast<DKJungle*>(GetWorld()->GetTrack()) != nullptr; }
bool IsBigDonut()         { return dynamic_cast<BigDonut*>(GetWorld()->GetTrack()) != nullptr; }
bool IsPodiumCeremony()   { return dynamic_cast<PodiumCeremony*>(GetWorld()->GetTrack()) != nullptr; }

void SelectMarioRaceway()       { GetWorld()->SetCurrentTrack(std::make_unique<MarioRaceway>()); }
void SelectLuigiRaceway()       { GetWorld()->SetCurrentTrack(std::make_unique<LuigiRaceway>()); }
void SelectChocoMountain()      { GetWorld()->SetCurrentTrack(std::make_unique<ChocoMountain>()); }
void SelectBowsersCastle()      { GetWorld()->SetCurrentTrack(std::make_unique<BowsersCastle>()); }
void SelectBansheeBoardwalk()   { GetWorld()->SetCurrentTrack(std::make_unique<BansheeBoardwalk>()); }
void SelectYoshiValley()        { GetWorld()->SetCurrentTrack(std::make_unique<YoshiValley>()); }
void SelectFrappeSnowland()     { GetWorld()->SetCurrentTrack(std::make_unique<FrappeSnowland>()); }
void SelectKoopaTroopaBeach()   { GetWorld()->SetCurrentTrack(std::make_unique<KoopaTroopaBeach>()); }
void SelectRoyalRaceway()       { GetWorld()->SetCurrentTrack(std::make_unique<RoyalRaceway>()); }
void SelectMooMooFarm()         { GetWorld()->SetCurrentTrack(std::make_unique<MooMooFarm>()); }
void SelectToadsTurnpike()      { GetWorld()->SetCurrentTrack(std::make_unique<ToadsTurnpike>()); }
void SelectKalimariDesert()     { GetWorld()->SetCurrentTrack(std::make_unique<KalimariDesert>()); }
void SelectSherbetLand()        { GetWorld()->SetCurrentTrack(std::make_unique<SherbetLand>()); }
void SelectRainbowRoad()        { GetWorld()->SetCurrentTrack(std::make_unique<RainbowRoad>()); }
void SelectWarioStadium()       { GetWorld()->SetCurrentTrack(std::make_unique<WarioStadium>()); }
void SelectBlockFort()          { GetWorld()->SetCurrentTrack(std::make_unique<BlockFort>()); }
void SelectSkyscraper()         { GetWorld()->SetCurrentTrack(std::make_unique<Skyscraper>()); }
void SelectDoubleDeck()         { GetWorld()->SetCurrentTrack(std::make_unique<DoubleDeck>()); }
void SelectDkJungle()           { GetWorld()->SetCurrentTrack(std::make_unique<DKJungle>()); }
void SelectBigDonut()           { GetWorld()->SetCurrentTrack(std::make_unique<BigDonut>()); }
void SelectPodiumCeremony()     { GetWorld()->SetCurrentTrack(std::make_unique<PodiumCeremony>()); }
// clang-format on

void* GetMushroomCup(void) {
    return gMushroomCup.get();
}

void* GetFlowerCup(void) {
    return gFlowerCup.get();
}

void* GetStarCup(void) {
    return gStarCup.get();
}

void* GetSpecialCup(void) {
    return gSpecialCup.get();
}

void* GetBattleCup(void) {
    return gBattleCup.get();
}

// End of frame cleanup of actors, objects, etc.
void CM_RunGarbageCollector(void) {
    RunGarbageCollector();
}

void CM_ResetAudio(void) {
    if(HMAS_IsPlaying(HMAS_MUSIC)){
        HMAS_AddEffect(HMAS_MUSIC, HMAS_EFFECT_VOLUME, HMAS_LINEAR, 10, 0);
        HMAS_AddEffect(HMAS_MUSIC, HMAS_EFFECT_STOP,   HMAS_INSTANT, 1, 0);
    }

    // Fade out music for all sequences and music player indexes 0, and 1
    for (size_t soundId = 0; soundId < MUSIC_SEQ_MAX; soundId++) {
        func_800C3448(0x10100000 | soundId);
        func_800C3448(0x11100000 | soundId);
    }
}
}

static std::atomic<bool> sResetRequested{ false };

void CM_RequestReset(void) {
    sResetRequested.store(true);
}

// The reset widget only requests; the reset is applied here at the top of the
// game frame. Applying it from the widget raced the menu state machine: a
// press landing mid-fade was re-advanced by the in-flight transition, and a
// repeat press rewrote the gamestate it had already set, which the != guard
// in main.c swallows while the audio fade still runs (silent no-op).
static void ApplyPendingReset() {
    if (!sResetRequested.exchange(false)) {
        return;
    }

    // The FROM_QUIT gamestates run identical inits; alternating keeps
    // gGamestateNext != gGamestate true so every press re-enters the menus.
    gGamestateNext = (gGamestate == MAIN_MENU_FROM_QUIT) ? START_MENU_FROM_QUIT : MAIN_MENU_FROM_QUIT;
    gIsGamePaused = 0;
    // Reset credits
    D_800DC5E4 = 0;
    gTourComplete = false;
    SetMarioRaceway();
    memset(&gGameModeMenuColumn, 0, sizeof(s8) * NUM_ROWS_GAME_MODE_MENU);
    memset(&gGameModeSubMenuColumn, 0, sizeof(s8) * NUM_COLUMN_GAME_MODE_SUB_MENU * NUM_ROWS_GAME_MODE_SUB_MENU);

    CM_ResetAudio();

    // Close the editor.
    if (gEditor.IsEnabled()) {
        gEditor.Disable();
    }

    // Set the debug menu track browsing index back to zero
    TrackBrowser::Instance->Reset();

    // Land on the same screen the gSkipIntro setting picks at boot.
    switch (CVarGetInteger("gSkipIntro", 0)) {
        case 0:
            gMenuSelection = HARBOUR_MASTERS_MENU;
            break;
        case 1:
            gMenuSelection = LOGO_INTRO_MENU;
            break;
        case 2:
            gMenuSelection = START_MENU;
            break;
        case 3:
            gMenuSelection = MAIN_MENU;
            break;
    }

    // Debug mode override gSkipIntro
    if (CVarGetInteger("gEnableDebugMode", 0) == true) {
        gMenuSelection = START_MENU;
    }
    // Re-enter through the intro's own transition protocol (see HM_TickIntro):
    // FADE_MODE_LOGO makes setup_menus rebuild the menu items and start a
    // fresh fade-in, replacing any in-flight transition that would otherwise
    // advance the stale screen right after the reset.
    gMenuFadeType = 0;
    gFadeModeSelection = FADE_MODE_LOGO;
}

#if defined(_WIN32)
extern "C" void PerformanceGpuSnapshot(unsigned long long*, unsigned long long*);
#endif
// Independent, low-volume performance sampling; works even with regular logs off.
extern "C" void PerfDiagMark(int section) {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point marks[8]{};
    static double sums[4]{};
    static int frames = 0;
    static Clock::time_point interval{};
    if (!CVarGetInteger("gDiagnostics.PerformanceCapture", 0) || gGamestate != RACING || gDemoMode) {
        frames = 0; interval = {}; for (double& sum : sums) sum = 0; return;
    }
    const auto now = Clock::now();
    if (section >= 0 && section < 4) marks[section] = now;
    else if (section >= 4 && section < 8 && marks[section - 4] != Clock::time_point{}) {
        sums[section - 4] += std::chrono::duration<double, std::milli>(now - marks[section - 4]).count();
    } else if (section == 8) {
        if (interval == Clock::time_point{}) interval = now;
        ++frames;
        double elapsed = std::chrono::duration<double>(now - interval).count();
        if (elapsed >= 5.0) {
            const auto file = Ship::Context::GetPathRelativeToAppDirectory("logs/performance-capture.csv");
            std::filesystem::create_directories(std::filesystem::path(file).parent_path());
            std::ofstream out(file, std::ios::app);
            unsigned long long gpuUs = 0, gpuCount = 0;
#if defined(_WIN32)
            static unsigned long long previousGpuUs = 0, previousGpuCount = 0;
            unsigned long long totalGpuUs = 0, totalGpuCount = 0;
            PerformanceGpuSnapshot(&totalGpuUs, &totalGpuCount);
            gpuUs = totalGpuUs - previousGpuUs; gpuCount = totalGpuCount - previousGpuCount;
            previousGpuUs = totalGpuUs; previousGpuCount = totalGpuCount;
#endif
            if (out) out << "unix_time=" << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() << ",racers=" << LocalMP_RacerCount() << ",fps=" << frames / elapsed
                << ",frame_ms=" << sums[0] / frames << ",game_state_ms=" << sums[1] / frames
                << ",cpu_path_ms=" << sums[2] / frames << ",graphics_submit_wait_ms=" << sums[3] / frames
                << ",gpu_frame_ms=" << (gpuCount ? (double)gpuUs / gpuCount / 1000.0 : -1.0) << ",gpu_samples=" << gpuCount << "\n";
            frames = 0; interval = now; for (double& sum : sums) sum = 0;
        }
    }
}

void push_frame() {
    PerfDiagMark(0);
#ifndef __SWITCH__
    static int previousLogLevel = -1;
    const int logLevel = CVarGetInteger(CVAR_DISABLE_DEBUG_LOGS, 0) ? spdlog::level::off :
                         CVarGetInteger("gDeveloperTools.LogLevel", 1);
    if (logLevel != previousLogLevel) {
        Ship::Context::GetRawInstance()->GetLogger()->set_level(static_cast<spdlog::level::level_enum>(logLevel));
        previousLogLevel = logLevel;
    }
#endif
    ApplyPendingReset();
    TickLightningSummary();
    TickAudioDiagnostics();
    // The audio mixer produces roughly one 30 Hz block per invocation. A
    // native-rate race can render many frames per audio block; driving audio
    // once per rendered frame would accelerate it and overflow the queue.
    static auto nextNativeAudio = std::chrono::steady_clock::time_point{};
    const auto audioNow = std::chrono::steady_clock::now();
    bool runAudio = true;
    if (GameEngine_IsExperimentalNativePresentation()) {
        if (nextNativeAudio == std::chrono::steady_clock::time_point{}) nextNativeAudio = audioNow;
        runAudio = audioNow >= nextNativeAudio;
        if (runAudio) {
            nextNativeAudio += std::chrono::microseconds(33333);
            if (audioNow - nextNativeAudio > std::chrono::milliseconds(200)) nextNativeAudio = audioNow;
        }
    } else {
        nextNativeAudio = {};
    }
#ifdef _WIN32
    sDiagnosticStage = "frame start";
    TickCrashDiagnostics();
    sDiagnosticStage = "audio start";
#endif
    if (runAudio) GameEngine::StartAudioFrame();
#ifdef _WIN32
    sDiagnosticStage = "engine start";
#endif
    GameEngine::Instance->StartFrame();
#ifdef _WIN32
    sDiagnosticStage = "game iteration";
#endif
    thread5_iteration();
#ifdef _WIN32
    sDiagnosticStage = "audio end";
#endif
    if (runAudio) GameEngine::EndAudioFrame();
#ifdef _WIN32
    sDiagnosticStage = "frame complete";
#endif
    PerfDiagMark(4);
    PerfDiagMark(8);
    // thread5_game_loop();
    // Graphics_ThreadUpdate();w
}

void CM_ThrowRuntimeError(const char* fmt, ...) {
    char error_mesg[2048];

    va_list args;
    va_start(args, fmt);
    vsnprintf(error_mesg, sizeof(error_mesg), fmt, args);
    va_end(args);

    const char* crash_desc = "\nSpaghettiKart has crashed! Please upload the logs to the support channel in Discord.";
    strncat(error_mesg, crash_desc, sizeof(error_mesg) - strlen(error_mesg) - 1);

    SPDLOG_ERROR(error_mesg);

    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR,
        "You dropped your plate of Spaghetti!",
        error_mesg,
        NULL
    );

    exit(EXIT_FAILURE);
}

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

extern "C" {
bool gQuickTimeTrial = false;
bool gRace200cc = false;
bool gRaceCustomCC = false;
}

#ifdef _WIN32
int SDL_main(int argc, char** argv) {
#else
#if defined(__cplusplus) && defined(PLATFORM_IOS)
extern "C"
#endif
    int
    main(int argc, char* argv[]) {
#endif
#ifdef _WIN32
    // Allow non-ascii characters for Windows
    setlocale(LC_ALL, ".UTF8");
    if (argc == 4 && strcmp(argv[1], "--crash-monitor") == 0) {
        const int result = CrashMonitor_Run(strtoul(argv[2], nullptr, 10), argv[3]);
        // This helper never initializes the game. Skip game-only static destructors.
        ExitProcess(static_cast<UINT>(result));
    }
#endif
#if defined(__APPLE__)
    // Disable the macOS "press and hold" accent/diacritic popup for this app. SDL keeps a Cocoa text
    // input context active, so holding a movement key is interpreted as holding a letter key in a text
    // field, and macOS shows the accent picker instead of repeating it. Per-app equivalent of
    // `defaults write -app <App> ApplePressAndHoldEnabled -bool false`; key repeat still works.
    CFPreferencesSetAppValue(CFSTR("ApplePressAndHoldEnabled"), kCFBooleanFalse, kCFPreferencesCurrentApplication);
    CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
#endif
#if defined(__APPLE__) && !defined(PLATFORM_IOS)
    // Default the writable data folder to ~/Library/Application Support/SpaghettiKart
    // (libultraship expands the ~ and creates the directory). Without this, a Finder
    // launch has no SHIP_HOME and libultraship falls back to the current working
    // directory, scattering config/saves/mods into the user's home folder.
    // overwrite=0 keeps any SHIP_HOME the user already set.
    setenv("SHIP_HOME", "~/Library/Application Support/SpaghettiKart", 0);
#endif
    // load_wasm();
    GameEngine::Create();
#ifdef _WIN32
    InitCrashDiagnostics();
#endif
    audio_init();
    sound_init();

    CustomEngineInit();

    switch(CVarGetInteger("gSkipIntro", 0)) {
        case 0:
            gMenuSelection = HARBOUR_MASTERS_MENU;
            break;
        case 1:
            gMenuSelection = LOGO_INTRO_MENU;
            break;
        case 2:
            gMenuSelection = START_MENU;
            break;
        case 3:
            gMenuSelection = MAIN_MENU;
            break;
    }

    // Debug mode override gSkipIntro
    if (CVarGetInteger("gEnableDebugMode", 0) == true) {
        gMenuSelection = START_MENU;
    }

    thread5_game_loop();
    gEditor.Load();
    if (argc >= 2 && strcmp(argv[1], "--time-trial") == 0) {
        const bool mirror = argc == 4 && strcmp(argv[3], "--mirror") == 0;
        const TrackInfo* track = (argc == 3 || mirror) ? gTrackRegistry.GetInfo(argv[2]) : nullptr;
        if (track) {
            TrackBrowser_SetTrack(argv[2]);
            gQuickTimeTrial = true;
            gModeSelection = TIME_TRIALS;
            gPlayerCount = 1;
            gScreenModeSelection = SCREEN_MODE_1P;
            gCharacterSelections[0] = MARIO;
            gCCSelection = CC_150;
            set_mirror_mode(mirror);
            gTourComplete = true;
            gMenuSelection = COURSE_SELECT_MENU;
            gGamestateNext = RACING;
            SPDLOG_INFO("Quick Time Trial: {} ({})", track->Name, track->ResourceName);
        } else {
            SPDLOG_ERROR("Usage: Spaghettify --time-trial <race course ID> [--mirror]");
            for (const TrackInfo* entry : gTrackRegistry.GetAllInfo()) {
                SPDLOG_INFO("Course: {} ({})", entry->Name, entry->ResourceName);
            }
        }
    }
    while (WindowIsRunning()) {
        push_frame();
    }
    LogLightningSummary("game shutdown");
    FlushAudioDiagnostics();
    CustomEngineDestroy();
#ifdef _WIN32
    if (sDiagnostics && !CVarGetInteger(CVAR_DISABLE_DEBUG_LOGS, 0)) {
        sDiagnostics->info("Normal game shutdown");
        sDiagnostics->flush();
    }
    CrashMonitor_Stop();
#endif
    // GameEngine::Instance->ProcessFrame(push_frame);
    GameEngine::Instance->Destroy();
    // Everything user-visible (config, saves) is persisted by Destroy(). Skip the
    // remaining static destructors: their order is unspecified and some log after
    // spdlog's own statics are gone, crashing on quit (same class as issue #689).
    // Mirrors the _Exit precedent on the extraction error paths.
    _Exit(0);
}

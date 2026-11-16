extern void PerfDiagMark(int section);
#include "port/LocalMultiplayer.h"
#include <libultraship.h>
#include <libultra/vi.h>
#include <libultra/os.h>
#include <libultraship/bridge/gfxdebuggerbridge.h>
#include <macros.h>
#include <decode.h>
#include <mk64.h>
#include <stubs.h>
#include <stdio.h>
#include "racing/framebuffer_effects.h"

#include "profiler.h"
#include "main.h"
#include "racing/memory.h"
#include "menus.h"
#include <segments.h>
#include <common_structs.h>
#include <defines.h>
#include "buffers.h"
#include "camera.h"
#include "racing/race_logic.h"
#include "racing/skybox_and_splitscreen.h"
#include "render_objects.h"
#include "effects.h"
#include "ending/code_80281780.h"
#include "audio/external.h"
#include "code_800029B0.h"
#include "ending/code_80280000.h"
#include "ending/podium_ceremony_actors.h"
#include "menu_items.h"
#include "code_80057C60.h"
#include "player_controller.h"
#include "render_player.h"
#include "racing/actors.h"
#include "racing/actors_extended.h"
#include "replays.h"
#include <debug.h>
#include "enhancements/freecam/freecam.h"
#include "engine/editor/Editor.h"
#include "port/interpolation/FrameInterpolation.h"
#include "engine/wasm.h"
#include "port/Game.h"
#include "port/Engine.h"
#include "port/NativeFrameClock.h"
#include "kart_attributes.h"
#include "engine/Matrix.h"
#include "engine/TrackBrowser.h"

// Declarations (not in this file)
void func_80091B78(void);

void audio_init();

void create_debug_thread(void);
void start_debug_thread(void);

struct SPTask* create_next_audio_frame_task(void);

struct VblankHandler* gVblankHandler1 = NULL;
struct VblankHandler* gVblankHandler2 = NULL;

struct SPTask* gActiveSPTask = NULL;
struct SPTask* sCurrentAudioSPTask = NULL;
struct SPTask* sCurrentDisplaySPTask = NULL;
struct SPTask* sNextAudioSPTask = NULL;
struct SPTask* sNextDisplaySPTask = NULL;

struct Controller gControllers[MAX_HUMAN_PLAYERS];
struct Controller* gControllerOne = &gControllers[0];
struct Controller* gControllerTwo = &gControllers[1];
struct Controller* gControllerThree = &gControllers[2];
struct Controller* gControllerFour = &gControllers[3];
static struct Controller sMenuController;
struct Controller* gControllerFive = &sMenuController; // All physical controllers combined.`
struct Controller* gControllerSix = &gControllers[5];
struct Controller* gControllerSeven = &gControllers[6];
struct Controller* gControllerEight = &gControllers[7];

Player gPlayers[NUM_RACERS];
Player* gPlayerOne = &gPlayers[0];
Player* gPlayerTwo = &gPlayers[1];
Player* gPlayerThree = &gPlayers[2];
Player* gPlayerFour = &gPlayers[3];
Player* gPlayerFive = &gPlayers[4];
Player* gPlayerSix = &gPlayers[5];
Player* gPlayerSeven = &gPlayers[6];
Player* gPlayerEight = &gPlayers[7];

UNUSED s32 D_800FD850[3];
struct GfxPool gGfxPools[2];
struct GfxPool* gGfxPool;

UNUSED s32 gfxPool_padding; // is this necessary?
struct VblankHandler gGameVblankHandler;
struct VblankHandler sSoundVblankHandler;
OSMesgQueue gDmaMesgQueue, gGameVblankQueue, gGfxVblankQueue, unused_gMsgQueue, gIntrMesgQueue, gSPTaskMesgQueue;
OSMesgQueue sSoundMesgQueue;
OSMesg sSoundMesgBuf[1];
OSMesg gDmaMesgBuf[1], gGameMesgBuf;
OSMesg gGfxMesgBuf[1];
UNUSED OSMesg D_8014F010, D_8014F014;
OSMesg gIntrMesgBuf[16], gSPTaskMesgBuf[16];
OSMesg gMainReceivedMesg;
OSIoMesg gDmaIoMesg;
OSMesgQueue gSIEventMesgQueue;
OSMesg gSIEventMesgBuf[3];

OSContStatus gControllerStatuses[MAX_HUMAN_PLAYERS];
OSContPad gControllerPads[MAX_HUMAN_PLAYERS];
u8 gControllerBits;
// Contains a 32x32 grid of indices into gCollisionIndices containing indices into gCollisionMesh
CollisionGrid gCollisionGrid[1024];
u16 gNumActors;
u16 gMatrixObjectCount;
s32 gTickLogic = 2;
s32 gTickVisuals = 1;
s32 gTickGame;
f32 D_80150118;

u16 wasSoftReset;
u16 D_8015011E;

s32 D_80150120;
s32 gGotoMode;
UNUSED s32 D_80150128;
UNUSED s32 D_8015012C;
UNUSED s32 D_80150140;
UNUSED s32 D_80150144;
f32 gScreenAspect;
f32 D_8015014C;
f32 D_80150150;
UNUSED f32 D_80150154;

struct D_80150158 gD_80150158[16];
Gfx* gDisplayListHead;

struct SPTask* gGfxSPTask;
s32 D_801502A0;
s32 D_801502A4;
u16* gPhysicalFramebuffers[3];
u16 gPortFramebuffers[3][SCREEN_WIDTH * SCREEN_HEIGHT];
uintptr_t gPhysicalZBuffer;
UNUSED u32 D_801502B8;
UNUSED u32 D_801502BC;
Mat4 sBillBoardMtx; // Faces 2D actors at the camera

s32 padding[2048];

u16 D_80152308;

UNUSED OSThread paddingThread;
OSThread gIdleThread;
ALIGNED8 u8 gIdleThreadStack[STACKSIZE]; // Based on sm64 and padding between bss symbols.
OSThread gVideoThread;
ALIGNED8 u8 gVideoThreadStack[STACKSIZE];
UNUSED OSThread D_80156820;
UNUSED ALIGNED8 u8 D_8015680_Stack[STACKSIZE];
OSThread gGameLoopThread;
ALIGNED8 u8 gGameLoopThreadStack[STACKSIZE];
OSThread gAudioThread;
ALIGNED8 u8 gAudioThreadStack[STACKSIZE];
UNUSED OSThread D_8015CD30;
UNUSED ALIGNED8 u8 D_8015CD30_Stack[STACKSIZE / 2];

ALIGNED8 u8 gGfxSPTaskYieldBuffer[4352];
ALIGNED8 u32 gGfxSPTaskStack[256];
OSMesg gPIMesgBuf[32];
OSMesgQueue gPIMesgQueue;

s32 gGamestate = 0xFFFF;
// gRaceState is externed as an s32 in other files. D_800DC514 is only used in main.c, likely a developer mistake.
s32 gRaceState = RACE_INIT;
u16 D_800DC514 = 0;
u16 gDemoMode = DEMO_MODE_INACTIVE;
u16 gEnableDebugMode = DEBUG_MODE;
s32 gGamestateNext = 7; // = COURSE_DATA_MENU?;
UNUSED s32 D_800DC528 = 1;
s32 gActiveScreenMode = SCREEN_MODE_1P;
s32 gScreenModeSelection = SCREEN_MODE_1P;
UNUSED s32 D_800DC534 = 0;
s32 gPlayerCountSelection1 = 2;
bool gTourComplete = false;

s32 gModeSelection = GRAND_PRIX;
s32 D_800DC540 = 0;
s32 D_800DC544 = 0;
s32 gCCSelection = CC_150;
s32 gGlobalTimer = 0;
// Graphics command pools must alternate on every presentation, even when
// the legacy visual timer advances less often than the render rate.
static u32 sNativePresentationCounter = 0;
static uint64_t sNativeVisualTicks = 0;
static int sWasNativeVisual = 0;
UNUSED s32 D_800DC550 = 0;
UNUSED s32 D_800DC554 = 0;
UNUSED s32 D_800DC558 = 0;
// Framebuffer rendering values (max 3)
u16 sRenderedFramebuffer = 0;
u16 sRenderingFramebuffer = 0;
UNUSED u16 D_800DC564 = 0;
s32 D_800DC568 = 0;
s32 D_800DC56C[8] = { 0 };
s16 sNumVBlanks = 0;
UNUSED s16 D_800DC590 = 0;
f32 gVBlankTimer = 0.0f;
f32 gCourseTimer = 0.0f;
size_t gTickCounter;

void create_thread(OSThread* thread, OSId id, void (*entry)(void*), void* arg, void* sp, OSPri pri) {
    thread->next = NULL;
    thread->queue = NULL;
    osCreateThread(thread, id, entry, arg, sp, pri);
}
void isPrintfInit(void);
void main_func(void) {
#ifdef VERSION_EU
    osTvType = OS_TV_PAL;
#endif
    osInitialize();
#ifdef DEBUG
    isPrintfInit(); // init osSyncPrintf
#endif
    create_thread(&gIdleThread, 1, &thread1_idle, NULL, gIdleThreadStack + ARRAY_COUNT(gIdleThreadStack), 100);
    osStartThread(&gIdleThread);
}
// extern OSViMode osViModeTable[];
/**
 * Initialize hardware, start main thread, then idle.
 */
void thread1_idle(void* arg) {
    osCreateViManager(OS_PRIORITY_VIMGR);
#ifdef VERSION_EU
    osViSetMode(&osViModeTable[OS_VI_PAL_LAN1]);
#else // VERSION_US
    if (osTvType == OS_TV_NTSC) {
        // osViSetMode(&osViModeTable[OS_VI_NTSC_LAN1]);
    } else {
        // osViSetMode(&osViModeTable[OS_VI_MPAL_LAN1]);
    }
#endif
    osViBlack(true);
    osViSetSpecialFeatures(OS_VI_GAMMA_OFF);
    osCreatePiManager(OS_PRIORITY_PIMGR, &gPIMesgQueue, gPIMesgBuf, ARRAY_COUNT(gPIMesgBuf));
    wasSoftReset = (s16) osResetType;
    create_debug_thread();
    start_debug_thread();
    create_thread(&gVideoThread, 3, &thread3_video, arg, gVideoThreadStack + ARRAY_COUNT(gVideoThreadStack), 100);
    osStartThread(&gVideoThread);
    osSetThreadPri(NULL, 0);

    // Halt
    while (true) {
        ;
    }
}

void setup_mesg_queues(void) {
    osCreateMesgQueue(&gDmaMesgQueue, gDmaMesgBuf, ARRAY_COUNT(gDmaMesgBuf));
    osCreateMesgQueue(&gSPTaskMesgQueue, gSPTaskMesgBuf, ARRAY_COUNT(gSPTaskMesgBuf));
    osCreateMesgQueue(&gIntrMesgQueue, gIntrMesgBuf, ARRAY_COUNT(gIntrMesgBuf));
    osViSetEvent(&gIntrMesgQueue, OS_MESG_32(MESG_VI_VBLANK), 1);
    osSetEventMesg(OS_EVENT_SP, &gIntrMesgQueue, OS_MESG_32(MESG_SP_COMPLETE));
    osSetEventMesg(OS_EVENT_DP, &gIntrMesgQueue, OS_MESG_32(MESG_DP_COMPLETE));
}

void start_sptask(s32 taskType) {
    if (taskType == M_AUDTASK) {
        gActiveSPTask = sCurrentAudioSPTask;
    } else {
        gActiveSPTask = sCurrentDisplaySPTask;
    }
    osSpTaskLoad(&gActiveSPTask->task);
    osSpTaskStartGo(&gActiveSPTask->task);
    gActiveSPTask->state = SPTASK_STATE_RUNNING;
}

/**
 * Initializes the Fast3D OSTask structure.
 * Loads F3DEX or F3DLX based on the number of players
 **/
void create_gfx_task_structure(void) {
    gGfxSPTask->msgqueue = &gGfxVblankQueue;
#ifdef TARGET_N64
    gGfxSPTask->msg = (OSMesg) 2;
    gGfxSPTask->task.t.type = M_GFXTASK;
    gGfxSPTask->task.t.flags = OS_TASK_DP_WAIT;
    gGfxSPTask->task.t.ucode_boot = rspF3DBootStart;
    gGfxSPTask->task.t.ucode_boot_size = ((u8*) rspF3DBootEnd - (u8*) rspF3DBootStart);
    // The split-screen multiplayer racing state uses F3DLX which has a simple subpixel calculation.
    // Singleplayer race mode and all other game states use F3DEX.
    // http://n64devkit.square7.ch/n64man/ucode/gspF3DEX.htm
    if (gGamestate != RACING || gPlayerCountSelection1 == 1) {
        gGfxSPTask->task.t.ucode = gspF3DEXTextStart;
        gGfxSPTask->task.t.ucode_data = gspF3DEXDataStart;
    } else {
        gGfxSPTask->task.t.ucode = gspF3DLXTextStart;
        gGfxSPTask->task.t.ucode_data = gspF3DLXDataStart;
    }
    gGfxSPTask->task.t.flags = 0;
    gGfxSPTask->task.t.flags = OS_TASK_DP_WAIT;
    gGfxSPTask->task.t.ucode_size = SP_UCODE_SIZE;
    gGfxSPTask->task.t.ucode_data_size = SP_UCODE_DATA_SIZE;
    gGfxSPTask->task.t.dram_stack = (u64*) &gGfxSPTaskStack;
    gGfxSPTask->task.t.dram_stack_size = SP_DRAM_STACK_SIZE8;
    gGfxSPTask->task.t.output_buff = (u64*) &gGfxSPTaskOutputBuffer;
    gGfxSPTask->task.t.output_buff_size = (u64*) ((u8*) gGfxSPTaskOutputBuffer + sizeof(gGfxSPTaskOutputBuffer));
    gGfxSPTask->task.t.data_ptr = (u64*) gGfxPool->gfxPool;
    gGfxSPTask->task.t.data_size = (gDisplayListHead - gGfxPool->gfxPool) * sizeof(Gfx);
#endif
    func_8008C214();
#ifdef TARGET_N64
    gGfxSPTask->task.t.yield_data_ptr = (u64*) &gGfxSPTaskYieldBuffer;
    gGfxSPTask->task.t.yield_data_size = OS_YIELD_DATA_SIZE;
#else
    if (GfxDebuggerIsDebuggingRequested()) {
        GfxDebuggerDebugDisplayList(gGfxPool->gfxPool);
    }
    profiler_log_thread5_time(BEFORE_DISPLAY_LISTS);
    Graphics_PushFrame(gGfxPool->gfxPool);
    profiler_log_thread5_time(AFTER_DISPLAY_LISTS);
#endif
}

f32 gDeltaTime = 0.0f;
// Unclamped elapsed time is used only by the experimental simulation clock.
static f32 sNativeElapsedTime = 0.0f;
f32 gNativeSimulationTickScale = 1.0f;
uint32_t gNativeSimulationLegacyTicks = 1;
uint32_t gNativeVisualSteps = 1;
float gNativeVisualPhase = 0.0f;
void calculate_delta_time(void) {
    static u32 prevtime = 0;
    u32 now = osGetCount();
    f32 deltaTime;

    if (now > prevtime) {
        deltaTime = (f32) (now - prevtime) / OS_CPU_COUNTER;
    } else {
        // Handle counter reset
        deltaTime = (f32) ((0xffffffff - prevtime) + 1 + now) / OS_CPU_COUNTER;
    }

    sNativeElapsedTime = prevtime == 0 ? 0.0f : deltaTime;
    prevtime = now;

    // Cap deltaTime to avoid large jumps (e.g., on pause/resume)
    if (deltaTime > 1.0f / 15.0f) { // Assume 15 FPS is the lowest acceptable rate
        deltaTime = 1.0f / 15.0f;
    }

    gDeltaTime = deltaTime;
}

void init_controllers(void) {
    osCreateMesgQueue(&gSIEventMesgQueue, &gSIEventMesgBuf[0], ARRAY_COUNT(gSIEventMesgBuf));
    osSetEventMesg(OS_EVENT_SI, &gSIEventMesgQueue, OS_MESG_32(0x33333333));
    osContInit(&gSIEventMesgQueue, &gControllerBits, gControllerStatuses);
    if ((gControllerBits & 1) == 0) {
        sIsController1Unplugged = true;
    } else {
        sIsController1Unplugged = false;
    }
}

void update_controller(s32 index) {
    struct Controller* controller = &gControllers[index];
    u16 stick;

    if (index == 0 && CVarGetInteger("gExperimentalMenuBenchmarkInput", 0) &&
        gMenuSelection == MAIN_MENU && gMainMenuSelection == MAIN_MENU_PLAYER_SELECT &&
        gFadeModeSelection == FADE_MODE_NONE) {
        static s32 menuTestStart = -1;
        if (menuTestStart < 0) menuTestStart = gGlobalTimer;
        const s32 elapsed = gGlobalTimer - menuTestStart;
        gControllerPads[index].stick_x = 0;
        gControllerPads[index].stick_y = 0;
        gControllerPads[index].button = elapsed == 30 ? R_JPAD : elapsed == 60 ? L_JPAD : 0;
    }

    // Reproducible, opt-in benchmark input: drive straight after the start.
    // Never read a real controller for this test player while it is active.
    if (index == 0 && CVarGetInteger("gExperimentalNativeBenchmark", 0) && gGamestate == RACING) {
        s32 benchSteer = CVarGetInteger("gExperimentalBenchmarkSteer", 0);
        if (benchSteer < -80) benchSteer = -80;
        if (benchSteer > 80) benchSteer = 80;
        gControllerPads[index].stick_x = gRaceState == RACE_IN_PROGRESS ? benchSteer : 0;
        gControllerPads[index].stick_y = 0;
        gControllerPads[index].right_stick_x = 0;
        gControllerPads[index].right_stick_y = 0;
        gControllerPads[index].button = gRaceState == RACE_IN_PROGRESS &&
            CVarGetInteger("gExperimentalAirBenchmark", 0) != 2 &&
            CVarGetInteger("gExperimentalHopBenchmark", 0) != 1 ? A_BUTTON : 0;
        if (CVarGetInteger("gExperimentalHopBenchmark", 0) && gRaceState == RACE_IN_PROGRESS &&
            gCourseTimer >= 1.5f && gCourseTimer < 1.6f) gControllerPads[index].button |= R_TRIG;
    }

    if (sIsController1Unplugged) {
        return;
    }

    // Prevents pause menu intereference while controlling flycam
    // Freecam only works with controller 1
    if ((CVarGetInteger("gFreecam", 0) == 1) && (gGamestate == RACING) && (index == 0)) {
        freecam_update_controller();
        return;
    }

    controller->rawStickX = gControllerPads[index].stick_x;
    controller->rawStickY = gControllerPads[index].stick_y;

    controller->rightRawStickX = gControllerPads[index].right_stick_x;
    controller->rightRawStickY = gControllerPads[index].right_stick_y;

    if ((gControllerPads[index].button & 4) != 0) {
        gControllerPads[index].button |= Z_TRIG;
    }
    controller->buttonPressed = gControllerPads[index].button & (gControllerPads[index].button ^ controller->button);
    controller->buttonDepressed = controller->button & (gControllerPads[index].button ^ controller->button);
    controller->button = gControllerPads[index].button;

    stick = 0;
    if (controller->rawStickX < -50) {
        stick |= L_JPAD;
    }
    if (controller->rawStickX > 50) {
        stick |= R_JPAD;
    }
    if (controller->rawStickY < -50) {
        stick |= D_JPAD;
    }
    if (controller->rawStickY > 50) {
        stick |= U_JPAD;
    }
    controller->stickPressed = stick & (stick ^ controller->stickDirection);
    controller->stickDepressed = controller->stickDirection & (stick ^ controller->stickDirection);
    controller->stickDirection = stick;
}

void read_controllers(void) {
    // OSMesg msg;

    osContStartReadData(&gSIEventMesgQueue);
    // osRecvMesg(&gSIEventMesgQueue, &msg, OS_MESG_BLOCK);
    osContGetReadData(gControllerPads);
    memset(gControllerFive, 0, sizeof(*gControllerFive));
    for (s32 i = 0; i < MAX_HUMAN_PLAYERS; i++) {
        update_controller(i);
        gControllerFive->button |= gControllers[i].button;
        gControllerFive->buttonPressed |= gControllers[i].buttonPressed;
        gControllerFive->buttonDepressed |= gControllers[i].buttonDepressed;
        gControllerFive->stickDirection |= gControllers[i].stickDirection;
        gControllerFive->stickPressed |= gControllers[i].stickPressed;
        gControllerFive->stickDepressed |= gControllers[i].stickDepressed;
    }
    LocalMP_OnInput();
}

void func_80000BEC(void) {
    gPhysicalZBuffer = (uintptr_t) VIRTUAL_TO_PHYSICAL(&gZBuffer);
}

void dispatch_audio_sptask(struct SPTask* spTask) {
    osWritebackDCacheAll();
    osSendMesg(&gSPTaskMesgQueue, OS_MESG_PTR(spTask), OS_MESG_NOBLOCK);
}

void exec_display_list(struct SPTask* spTask) {
    osWritebackDCacheAll();
    spTask->state = SPTASK_STATE_NOT_STARTED;
    if (sCurrentDisplaySPTask == NULL) {
        sCurrentDisplaySPTask = spTask;
        sNextDisplaySPTask = NULL;
        osSendMesg(&gIntrMesgQueue, OS_MESG_32(MESG_START_GFX_SPTASK), OS_MESG_NOBLOCK);
    } else {
        sNextDisplaySPTask = spTask;
    }
}

/**
 * Set default RCP (Reality Co-Processor) settings.
 */
void init_rcp(void) {
    init_rdp();
    set_viewport();
    select_framebuffer();
    init_z_buffer();
}

/**
 * End the master display list and initialize the graphics task structure for the next frame to be rendered.
 */
void end_master_display_list(void) {
    gDPFullSync(gDisplayListHead++);
    gSPEndDisplayList(gDisplayListHead++);
    create_gfx_task_structure();
}

// clear_frame_buffer from SM64, with a few edits
//! @todo Why did void* work for matching
void clear_framebuffer(s32 color) {
    gDPPipeSync(gDisplayListHead++);

    gDPSetRenderMode(gDisplayListHead++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetCycleType(gDisplayListHead++, G_CYC_FILL);

    gDPSetFillColor(gDisplayListHead++, color);
    gDPFillWideRectangle(gDisplayListHead++, OTRGetRectDimensionFromLeftEdge(0), 0, OTRGetGameRenderWidth(), SCREEN_HEIGHT);

    gDPPipeSync(gDisplayListHead++);

    gDPSetCycleType(gDisplayListHead++, G_CYC_1CYCLE);
}

void rendering_init(void) {
    gGfxPool = &gGfxPools[0];
    gGfxSPTask = &gGfxPool->spTask;
    gDisplayListHead = gGfxPool->gfxPool;
    init_rcp();
    clear_framebuffer(0);
    end_master_display_list();
    exec_display_list(&gGfxPool->spTask);
    sRenderingFramebuffer++;
    gGlobalTimer++;
    sNativePresentationCounter = (u32) gGlobalTimer;
}

void config_gfx_pool(void) {
    const u32 poolFrame = GameEngine_IsExperimentalNativePresentation() ? sNativePresentationCounter : (u32) gGlobalTimer;
    gGfxPool = &gGfxPools[poolFrame & 1];
    gDisplayListHead = gGfxPool->gfxPool;
    gGfxSPTask = &gGfxPool->spTask;
}

/**
 * Send current master display list for rendering.
 * Tell the VI which colour framebuffer to display.
 * Yields to the VI framerate twice, locking the game at 30 FPS.
 * Selects the next framebuffer to be rendered and displayed.
 */
void display_and_vsync(void) {
#ifdef TARGET_N64
    profiler_log_thread5_time(BEFORE_DISPLAY_LISTS);
#endif
    // osRecvMesg(&gGfxVblankQueue, &gMainReceivedMesg, OS_MESG_BLOCK);
    exec_display_list(&gGfxPool->spTask);
#ifdef TARGET_N64
    profiler_log_thread5_time(AFTER_DISPLAY_LISTS);
    osRecvMesg(&gGameVblankQueue, &gMainReceivedMesg, OS_MESG_BLOCK);
#endif
    osViSwapBuffer((void*) PHYSICAL_TO_VIRTUAL(gPhysicalFramebuffers[sRenderedFramebuffer]));
    profiler_log_thread5_time(THREAD5_END);
    // osRecvMesg(&gGameVblankQueue, &gMainReceivedMesg, OS_MESG_BLOCK);
    // crash_screen_set_framebuffer(gPhysicalFramebuffers[sRenderedFramebuffer]);

    if (++sRenderedFramebuffer == 3) {
        sRenderedFramebuffer = 0;
    }
    if (++sRenderingFramebuffer == 3) {
        sRenderingFramebuffer = 0;
    }
    if (GameEngine_IsExperimentalNativePresentation()) {
        ++sNativePresentationCounter;
        if (!sWasNativeVisual) sNativeVisualTicks = 0;
        for (uint32_t i = 0; i < gNativeSimulationLegacyTicks; ++i) {
            if (((++sNativeVisualTicks) & 1) && gTickVisuals) ++gGlobalTimer;
        }
        sWasNativeVisual = 1;
    } else {
        if (gTickVisuals) gGlobalTimer++;
        sNativePresentationCounter = (u32) gGlobalTimer;
        sWasNativeVisual = 0;
    }
}

void init_segment_ending_sequences(void) {
#ifdef TARGET_N64
    memset((void*) SEG_ENDING, 0, SEG_ENDING_SIZE);
    osWritebackDCacheAll();
    dma_copy((u8*) SEG_ENDING, (u8*) SEG_ENDING_ROM_START, SEG_ENDING_ROM_SIZE);
    osInvalICache((void*) SEG_ENDING, SEG_ENDING_SIZE);
    osInvalDCache((void*) SEG_ENDING, SEG_ENDING_SIZE);
#endif
}

void init_segment_racing(void) {
#ifdef TARGET_N64
    memset((void*) SEG_RACING, 0, SEG_RACING_SIZE);
    osWritebackDCacheAll();
    dma_copy((u8*) SEG_RACING, (u8*) SEG_RACING_ROM_START, SEG_RACING_ROM_SIZE);
    osInvalICache((void*) SEG_RACING, SEG_RACING_SIZE);
    osInvalDCache((void*) SEG_RACING, SEG_RACING_SIZE);
#endif
}

void dma_copy(u8* dest, u8* romAddr, size_t size) {

    osInvalDCache(dest, size);
    while (size > 0x100) {
        osPiStartDma(&gDmaIoMesg, 0, 0, (uintptr_t) romAddr, dest, 0x100, &gDmaMesgQueue);
        osRecvMesg(&gDmaMesgQueue, &gMainReceivedMesg, 1);
        size -= 0x100;
        romAddr += 0x100;
        dest += 0x100;
    }
    if (size != 0) {
        osPiStartDma(&gDmaIoMesg, 0, 0, (uintptr_t) romAddr, dest, size, &gDmaMesgQueue);
        osRecvMesg(&gDmaMesgQueue, &gMainReceivedMesg, 1);
    }
}

/**
 * Setup main segments and framebuffers.
 */
void setup_game_memory(void) {
    //     UNUSED u32 pad[2];
    //     ptrdiff_t commonCourseDataSize; // Compressed mio0 size
    //     uintptr_t textureSegSize;
    //     ptrdiff_t textureSegStart;
    //     uintptr_t allocatedMemory;
    //     UNUSED s32 unknown_padding;

    init_segment_racing();
    // gHeapEndPtr = SEG_RACING;
    //     set_segment_base_addr(0, (void *) SEG_START);

    //     // Memory pool size of 0xAB630
    initialize_memory_pool();

    func_80000BEC();

    //     // Initialize trig tables segment
    //     osInvalDCache((void *) TRIG_TABLES, TRIG_TABLES_SIZE);
    //     osPiStartDma(&gDmaIoMesg, 0, 0, TRIG_TABLES_ROM_START, (void *) TRIG_TABLES, TRIG_TABLES_SIZE,
    //     &gDmaMesgQueue); osRecvMesg(&gDmaMesgQueue, &gMainReceivedMesg, OS_MESG_BLOCK);

    //     set_segment_base_addr(2, (void *) load_data(SEG_DATA_START, SEG_DATA_END));

    //     commonCourseDataSize = COMMON_TEXTURES_SIZE;
    //     commonCourseDataSize = ALIGN16(commonCourseDataSize);

    // #ifdef AVOID_UB
    //     textureSegStart = (ptrdiff_t) SEG_RACING - commonCourseDataSize;
    // #else
    //     textureSegStart = SEG_RACING - commonCourseDataSize;
    // #endif
    //     osPiStartDma(&gDmaIoMesg, 0, 0, COMMON_TEXTURES_ROM_START, (void *) textureSegStart, commonCourseDataSize,
    //     &gDmaMesgQueue); osRecvMesg(&gDmaMesgQueue, &gMainReceivedMesg, OS_MESG_BLOCK);

    //     textureSegSize = *(uintptr_t *)(textureSegStart + 4);
    //     textureSegSize = ALIGN16(textureSegSize);
    //     allocatedMemory = gNextFreeMemoryAddress;
    //     mio0decode((u8 *) textureSegStart, (u8 *) allocatedMemory);
    //     set_segment_base_addr(0xD, (void *) allocatedMemory);

    //     gNextFreeMemoryAddress += textureSegSize;

    //     // Common track data does not get reloaded when the race state resets.
    //     // Therefore, only reset the memory ptr to after the common track data.
    gFreeMemoryResetAnchor = gNextFreeMemoryAddress;
}

/**
 * @brief
 *
 */
void game_init_clear_framebuffer(void) {
    gGamestateNext = 0; // = START_MENU_FROM_QUIT?
    clear_framebuffer(0);
}

void display_debug_info(void) {
    u16 rotY;
    if (!gEnableDebugMode) {
        D_800DC514 = false;
    } else if (D_800DC514) {
        if ((gControllerOne->buttonPressed & R_TRIG) && (gControllerOne->button & A_BUTTON) &&
            (gControllerOne->button & B_BUTTON)) {
            D_800DC514 = false;
        }
        rotY = camera1->rot[1];
        gDebugPathCount = gScreenOneCtx->pathCounter;

        if (rotY < 0x2000) {
            func_80057A50(40, 100, "SOUTH  ", gDebugPathCount);
        } else if (rotY < 0x6000) {
            func_80057A50(40, 100, "EAST   ", gDebugPathCount);
        } else if (rotY < 0xA000) {
            func_80057A50(40, 100, "NORTH  ", gDebugPathCount);
        } else if (rotY < 0xE000) {
            func_80057A50(40, 100, "WEST   ", gDebugPathCount);
        } else {
            func_80057A50(40, 100, "SOUTH  ", gDebugPathCount);
        }

    } else if ((gControllerOne->buttonPressed & L_TRIG) && (gControllerOne->button & A_BUTTON) &&
               (gControllerOne->button & B_BUTTON)) {
        D_800DC514 = true;
    }

    if (!gEnableDebugMode) {
        gEnableResourceMeters = 0;
    } else {
        if (gEnableResourceMeters) {
            resource_display();
            if (!(gControllerOne->button & L_TRIG) && (gControllerOne->button & R_TRIG) &&
                (gControllerOne->buttonPressed & B_BUTTON)) {
                gEnableResourceMeters = 0;
            }
        } else if (!(gControllerOne->button & L_TRIG) && (gControllerOne->button & R_TRIG) &&
                   (gControllerOne->buttonPressed & B_BUTTON)) {
            gEnableResourceMeters = 1;
        }
    }
}

void process_game_tick(void) {
    const int nativeFrame = GameEngine_IsExperimentalNativeRace();
    const uint32_t legacyTicks = nativeFrame ? gNativeSimulationLegacyTicks : 1;
    const size_t previousTickCounter = gTickCounter;
    gTickCounter += legacyTicks;

    if (Editor_IsPaused() == false) {
        LocalMP_UpdateAfk();
        if (D_8015011E) {
            // Default scale preserves the original fixed-step arithmetic.
            // The experimental fractional path measures elapsed race time.
            if (gNativeSimulationTickScale == 1.0f) {
                gCourseTimer += TRACK_TIMER_ITER;
            } else {
                gCourseTimer += TRACK_TIMER_ITER * gNativeSimulationTickScale;
            }
        }
        // Kart-to-kart collision applies an immediate impulse and overlap
        // correction. Reapplying those at every presentation frame changes
        // the contact response, so keep checks on their original tick rate.
        for (uint32_t tick = 0; tick < legacyTicks; ++tick) {
            func_802909F0();
            if (CVarGetInteger("gExperimentalCollisionBenchmark", 0) && gRaceState == RACE_IN_PROGRESS) {
                static unsigned checkedTicks = 0;
                if (++checkedTicks % 60 == 0) {
                    printf("COLLISION_GATE ticks=%u t=%.4f\n", checkedTicks, gCourseTimer);
                }
            }
        }
        evaluate_collision_for_players_and_actors();
        handle_a_press_for_all_players_during_race();
    }

    CM_TickCameras();

    // Editor requires this so the camera keeps moving while the game is paused.
    if (Editor_IsPaused() == true) {
        return;
    }
    
    func_80028F70(); // Player controller

    if (!nativeFrame) {
        PerfDiagMark(2);
        func_8028F474();
        PerfDiagMark(6);
    } else {
        // Path tracking, CPU decisions and traffic updates contain many
        // integer counters. Keep their original cadence until their own
        // time-based conversion is complete. gPreviousPlayerZ in path
        // tracking retains the position from its previous invocation.
        const f32 savedScale = gNativeSimulationTickScale;
        gNativeSimulationTickScale = 1.0f;
        for (uint32_t tick = 0; tick < legacyTicks; ++tick) func_8028F474();
        gNativeSimulationTickScale = savedScale;
    }
    if (!nativeFrame) {
        func_80059AC8();
        update_course_actors();
        CM_TickActors();
        if (gTickCounter & 1) { // Only run once per game loop
            CM_TickActors60fps();
        }
        CM_TickTrack();
    } else {
        // Legacy actor and effect state machines still count fixed 60 Hz
        // steps. Advance them at crossed boundaries while player movement is
        // being converted to the native simulation step. Keep ordering when
        // a long frame crosses more than one boundary.
        for (uint32_t tick = 0; tick < legacyTicks; ++tick) {
            gNativeSimulationLegacyTicks = 1;
            func_80059AC8();
            update_course_actors();
            CM_TickActors();
            if ((previousTickCounter + tick + 1) & 1) CM_TickActors60fps();
            CM_TickTrack();
        }
        gNativeSimulationLegacyTicks = legacyTicks;
        // Item-button edges must not disappear on frames with no 60 Hz tick.
        if (legacyTicks == 0) check_player_use_item();
    }
    if (CM_IsTourEnabled() == false) {
        func_8028FCBC();
    }
    LocalMP_RestoreAfkInput();
}

void race_logic_loop(void) {
    ClearMatrixPools();
    ClearObjectsMatrixPool();
    Editor_ClearMatrix();
    Editor_CleanWorld(); // Clears all actors
    gMatrixObjectCount = 0;
    gMatrixEffectCount = 0;

    if (gIsGamePaused != 0 && !LocalMP_IsActive()) {
        func_80290B14();
    }
    if (gIsInQuitToMenuTransition != 0) {
        func_802A38B4();
        return;
    }

    if (sNumVBlanks >= 6) {
        sNumVBlanks = 5;
    }
    else if (sNumVBlanks < 0) {
        sNumVBlanks = 1;
    }

    if (GameEngine_IsExperimentalNativeRace()) {
        // Camera effect state was updated once per 30 Hz game frame.
        for (uint32_t tick = 0; tick < gNativeSimulationLegacyTicks; ++tick)
            if ((gTickCounter + tick + 1) & 1) func_802A4EF4();
    } else {
        func_802A4EF4();
    }

    if (gModeSelection == TIME_TRIALS && !LocalMP_IsActive() && !gQuickTimeTrial) {
        replays_loop();
    }

    if (gIsGamePaused == false) {
        // The grid intro, setup and Lakitu countdown are logic-driven too.
        // Include them so Tab uses the same tick count before and after GO.
        const s32 logicTicks = GameEngine_IsExperimentalNativeRace() ? 1 :
            !Editor_IsPaused() && gRaceState >= RACE_INIT && gRaceState < RACE_FINISHED
            ? GameEngine_GetLogicTicksPerFrame(gTickLogic) : gTickLogic;
        u16 savedEdges[MAX_HUMAN_PLAYERS][4];
        if (logicTicks > gTickLogic) {
            for (s32 player = 0; player < MAX_HUMAN_PLAYERS; ++player) {
                savedEdges[player][0] = gControllers[player].buttonPressed;
                savedEdges[player][1] = gControllers[player].buttonDepressed;
                savedEdges[player][2] = gControllers[player].stickPressed;
                savedEdges[player][3] = gControllers[player].stickDepressed;
            }
        }
        for (s32 i = 0; i < logicTicks; i++) {
            // Extra ticks retain held buttons/sticks, but must not repeat a
            // physical press/release edge as though it were another input.
            if (i >= gTickLogic) {
                for (s32 player = 0; player < MAX_HUMAN_PLAYERS; ++player) {
                    gControllers[player].buttonPressed = gControllers[player].buttonDepressed = 0;
                    gControllers[player].stickPressed = gControllers[player].stickDepressed = 0;
                }
            }
            process_game_tick();
            if (gIsGamePaused || gGamestate != RACING || gIsInQuitToMenuTransition ||
                (logicTicks != gTickLogic && gRaceState == RACE_FINISHED)) break;
        }
        if (logicTicks > gTickLogic) {
            for (s32 player = 0; player < MAX_HUMAN_PLAYERS; ++player) {
                gControllers[player].buttonPressed = savedEdges[player][0];
                gControllers[player].buttonDepressed = savedEdges[player][1];
                gControllers[player].stickPressed = savedEdges[player][2];
                gControllers[player].stickDepressed = savedEdges[player][3];
            }
        }
        if (Editor_IsPaused() == false) {
            if (GameEngine_IsExperimentalNativeRace()) {
                // Kart animation, particles and random hop effects were
                // updated once per 30 Hz game frame, not per display frame.
                for (uint32_t step = 0; step < gNativeVisualSteps; ++step) func_80022744();
            } else {
                func_80022744();
            }
        }
    }
    func_8005A070();
    CM_TickEditor();
    profiler_log_thread5_time(LEVEL_SCRIPT_EXECUTE);
    sNumVBlanks = 0;
    gNumScreens = 0;
    init_rdp();
    if (D_800DC5B0 != 0) {
        select_framebuffer();
    }

    if (LocalMP_IsActive()) LocalMP_Layout();
    if (LocalMP_IsActive() && (LocalMP_Count() >= 3 || LocalMP_RaceResultsOpen())) {
        LocalMP_Layout();
        const s32 ranking = LocalMP_BattleResultsOpen() || LocalMP_RaceResultsOpen();
        const s32 firstScreen = ranking ? gPlayerWinningIndex : 0;
        const s32 lastScreen = ranking ? firstScreen + 1 : LocalMP_Count();
        for (s32 screen = firstScreen; screen < lastScreen; screen++) {
            // Set the projection for this viewport, including the wider last
            // player in 5P, 7P and 8P. Sky and game cameras use this same ratio.
            gScreenAspect = (f32) gScreenContexts[screen].screenWidth / gScreenContexts[screen].screenHeight;
            render_screens(&gScreenContexts[screen], RENDER_SCREEN_MODE_3P_4P_PLAYER_ONE, 8 + (screen % 4), screen);
        }
        gScreenAspect = (f32) gScreenContexts[0].screenWidth / gScreenContexts[0].screenHeight;
    } else switch (gActiveScreenMode) {
        case SCREEN_MODE_1P:
            render_screens(gScreenOneCtx, RENDER_SCREEN_MODE_1P_PLAYER_ONE, 0, 0);
            break;
        case SCREEN_MODE_2P_SPLITSCREEN_HORIZONTAL:
            if (gPlayerWinningIndex == 0) {
                // In VS Mode the winning player's viewport takes over the whole screen.
                // Rendering the winning player last places their screen above the other screens
                render_screens(gScreenTwoCtx, RENDER_SCREEN_MODE_2P_HORIZONTAL_PLAYER_TWO, 4, 1);
                render_screens(gScreenOneCtx, RENDER_SCREEN_MODE_2P_HORIZONTAL_PLAYER_ONE, 3, 0);
            } else {
                render_screens(gScreenOneCtx, RENDER_SCREEN_MODE_2P_HORIZONTAL_PLAYER_ONE, 3, 0);
                render_screens(gScreenTwoCtx, RENDER_SCREEN_MODE_2P_HORIZONTAL_PLAYER_TWO, 4, 1);
            }
            break;
        case SCREEN_MODE_2P_SPLITSCREEN_VERTICAL:
            if (gPlayerWinningIndex == 0) {
                render_screens(gScreenTwoCtx, RENDER_SCREEN_MODE_2P_VERTICAL_PLAYER_TWO, 2, 1);
                render_screens(gScreenOneCtx, RENDER_SCREEN_MODE_2P_VERTICAL_PLAYER_ONE, 1, 0);
            } else {
                render_screens(gScreenOneCtx, RENDER_SCREEN_MODE_2P_VERTICAL_PLAYER_ONE, 1, 0);
                render_screens(gScreenTwoCtx, RENDER_SCREEN_MODE_2P_VERTICAL_PLAYER_TWO, 2, 1);
            }
            break;
        case SCREEN_MODE_3P_4P_SPLITSCREEN:
            if (gPlayerWinningIndex == 0) {
                render_screens(gScreenTwoCtx, RENDER_SCREEN_MODE_3P_4P_PLAYER_TWO, 9, 1);
                render_screens(gScreenThreeCtx, RENDER_SCREEN_MODE_3P_4P_PLAYER_THREE, 10, 2);
                render_screens(gScreenFourCtx, RENDER_SCREEN_MODE_3P_4P_PLAYER_FOUR, 11, 3);
                render_screens(gScreenOneCtx, RENDER_SCREEN_MODE_3P_4P_PLAYER_ONE, 8, 0);
            } else if (gPlayerWinningIndex == 1) {
                render_screens(gScreenOneCtx, RENDER_SCREEN_MODE_3P_4P_PLAYER_ONE, 8, 0);
                render_screens(gScreenThreeCtx, RENDER_SCREEN_MODE_3P_4P_PLAYER_THREE, 10, 2);
                render_screens(gScreenFourCtx, RENDER_SCREEN_MODE_3P_4P_PLAYER_FOUR, 11, 3);
                render_screens(gScreenTwoCtx, RENDER_SCREEN_MODE_3P_4P_PLAYER_TWO, 9, 1);
            } else if (gPlayerWinningIndex == 2) {
                render_screens(gScreenOneCtx, RENDER_SCREEN_MODE_3P_4P_PLAYER_ONE, 8, 0);
                render_screens(gScreenTwoCtx, RENDER_SCREEN_MODE_3P_4P_PLAYER_TWO, 9, 1);
                render_screens(gScreenFourCtx, RENDER_SCREEN_MODE_3P_4P_PLAYER_FOUR, 11, 3);
                render_screens(gScreenThreeCtx, RENDER_SCREEN_MODE_3P_4P_PLAYER_THREE, 10, 2);
            } else {
                render_screens(gScreenOneCtx, RENDER_SCREEN_MODE_3P_4P_PLAYER_ONE, 8, 0);
                render_screens(gScreenTwoCtx, RENDER_SCREEN_MODE_3P_4P_PLAYER_TWO, 9, 1);
                render_screens(gScreenThreeCtx, RENDER_SCREEN_MODE_3P_4P_PLAYER_THREE, 10, 2);
                render_screens(gScreenFourCtx, RENDER_SCREEN_MODE_3P_4P_PLAYER_FOUR, 11, 3);
            }
            break;
    }

    display_debug_info();

    func_802A4300();
    if ((!LocalMP_IsActive() || (LocalMP_Count() != 3 && LocalMP_Count() <= 4 &&
        (LocalMP_Count() == 1 || MultiplayerOptions_DisplayCount() == 1))) && !LocalMP_RaceResultsOpen()) func_800591B4();
    if (!LocalMP_IsActive()) func_80093E20();
    else {
        LocalMP_DrawRacePositions();
        LocalMP_DrawItemWindows();
        LocalMP_DrawPauseMenu();
        LocalMP_DrawBattleResults();
        LocalMP_DrawRaceResults();
    }
#if DVDL
    display_dvdl();
#endif
    // Copies after all the main rendering is complete. This is accurate to hardware.
    // This means things like jumbotron will have the pause menu rendered in it.
    // To avoid that, this line can be moved above func_800591B4.
    FB_WriteFramebufferSliceToCPU(&gDisplayListHead, gPortFramebuffers[sRenderingFramebuffer], true);
    gDPFullSync(gDisplayListHead++);
    gSPEndDisplayList(gDisplayListHead++);

    // End of frame cleanup of actors, objects, etc.
    CM_RunGarbageCollector();
}

/**
 * mk64's game loop depends on a series of states.
 * It runs a wide branching series of code based on these states.
 * State 1) Clear framebuffer
 * State 2) Run menus
 * State 3) Process race related logic
 * State 4) Ending sequence
 * State 5) Credits
 *
 * Note that the state doesn't flip-flop at random but is permanent
 * until the state changes (ie. Exit menus and start a race).
 */

void game_state_handler(void) {
    FrameInterpolation_StartRecord();

#if DVDL
    if ((gControllerOne->button & L_TRIG) && (gControllerOne->button & R_TRIG) && (gControllerOne->button & Z_TRIG) &&
        (gControllerOne->button & A_BUTTON)) {
        gGamestateNext = CREDITS_SEQUENCE;
    } else if ((gControllerOne->button & L_TRIG) && (gControllerOne->button & R_TRIG) &&
               (gControllerOne->button & Z_TRIG) && (gControllerOne->button & B_BUTTON)) {
        gGamestateNext = ENDING;
    }
#endif
    switch (gGamestate) {
        case 7:
            game_init_clear_framebuffer();
            break;
        case START_MENU_FROM_QUIT:
        case MAIN_MENU_FROM_QUIT:
        case PLAYER_SELECT_MENU_FROM_QUIT:
        case COURSE_SELECT_MENU_FROM_QUIT:
            // Display black
            osViBlack(0);
            update_menus();
            init_rcp();
            func_80094A64(gGfxPool);
#if DVDL
            display_dvdl();
#endif
            break;
        case RACING:
            race_logic_loop();
            break;
        case ENDING:
            podium_ceremony_loop();
            break;
        case CREDITS_SEQUENCE:
            credits_loop();
            break;
    }
    FrameInterpolation_StopRecord();
}

void interrupt_gfx_sptask(void) {
    if (gActiveSPTask->task.t.type == M_GFXTASK) {
        gActiveSPTask->state = SPTASK_STATE_INTERRUPTED;
        osSpTaskYield();
    }
}

void receive_new_tasks(void) {
    UNUSED s32 pad;
    struct SPTask* spTask;

    while (osRecvMesg(&gSPTaskMesgQueue, (OSMesg*) &spTask, OS_MESG_NOBLOCK) != -1) {
        spTask->state = SPTASK_STATE_NOT_STARTED;
        switch (spTask->task.t.type) {
            case 2:
                sNextAudioSPTask = spTask;
                break;
            case 1:
                sNextDisplaySPTask = spTask;
                break;
        }
    }

    if (sCurrentAudioSPTask == NULL && sNextAudioSPTask != NULL) {
        sCurrentAudioSPTask = sNextAudioSPTask;
        sNextAudioSPTask = NULL;
    }
    if (sCurrentDisplaySPTask == NULL && sNextDisplaySPTask != NULL) {
        sCurrentDisplaySPTask = sNextDisplaySPTask;
        sNextDisplaySPTask = NULL;
    }
}

void set_vblank_handler(s32 index, struct VblankHandler* handler, OSMesgQueue* queue, OSMesg* msg) {
    handler->queue = queue;
    handler->msg = *msg;
    switch (index) {
        case 1:
            gVblankHandler1 = handler;
            break;
        case 2:
            gVblankHandler2 = handler;
            break;
    }
}

void start_gfx_sptask(void) {
    if (gActiveSPTask == NULL && sCurrentDisplaySPTask != NULL &&
        sCurrentDisplaySPTask->state == SPTASK_STATE_NOT_STARTED) {
        profiler_log_gfx_time(TASKS_QUEUED);
        start_sptask(M_GFXTASK);
    }
}

void handle_vblank(void) {
    gVBlankTimer += V_BlANK_TIMER_ITER;
    sNumVBlanks++;

    receive_new_tasks();

    // First try to kick off an audio task. If the gfx task is currently
    // running, we need to asynchronously interrupt it -- handle_sp_complete
    // will pick up on what we're doing and start the audio task for us.
    // If there is already an audio task running, there is nothing to do.
    // If there is no audio task available, try a gfx task instead.
    if (sCurrentAudioSPTask != NULL) {
        if (gActiveSPTask != NULL) {
            interrupt_gfx_sptask();
        } else {
            profiler_log_vblank_time();
            start_sptask(M_AUDTASK);
        }
    } else {
        if (gActiveSPTask == NULL && sCurrentDisplaySPTask != NULL &&
            sCurrentDisplaySPTask->state != SPTASK_STATE_FINISHED) {
            profiler_log_gfx_time(TASKS_QUEUED);
            start_sptask(M_GFXTASK);
        }
    }

/* This is where I would put my rumble code... If I had any. */
#if ENABLE_RUMBLE
#ifdef TARGET_N64
    rumble_thread_update_vi();
#endif
#endif

    if (gVblankHandler1 != NULL) {
        osSendMesg(gVblankHandler1->queue, gVblankHandler1->msg, OS_MESG_NOBLOCK);
    }
    if (gVblankHandler2 != NULL) {
        osSendMesg(gVblankHandler2->queue, gVblankHandler2->msg, OS_MESG_NOBLOCK);
    }
}

void handle_dp_complete(void) {
    // Gfx SP task is completely done.
    if (sCurrentDisplaySPTask->msgqueue != NULL) {
        osSendMesg(sCurrentDisplaySPTask->msgqueue, sCurrentDisplaySPTask->msg, OS_MESG_NOBLOCK);
    }
    profiler_log_gfx_time(RDP_COMPLETE);
    sCurrentDisplaySPTask->state = SPTASK_STATE_FINISHED_DP;
    sCurrentDisplaySPTask = NULL;
}

void handle_sp_complete(void) {
    struct SPTask* curSPTask = gActiveSPTask;

    gActiveSPTask = NULL;

    if (curSPTask->state == SPTASK_STATE_INTERRUPTED) {
        // handle_vblank tried to start an audio task while there was already a
        // gfx task running, so it had to interrupt the gfx task. That interruption
        // just finished.
        if (osSpTaskYielded((OSTask*) curSPTask) == 0) {
            // The gfx task completed before we had time to interrupt it.
            // Mark it finished, just like below.
            curSPTask->state = SPTASK_STATE_FINISHED;
            profiler_log_gfx_time(RSP_COMPLETE);
        }
        // Start the audio task, as expected by handle_vblank.
        profiler_log_vblank_time();
        start_sptask(M_AUDTASK);
    } else {
        curSPTask->state = SPTASK_STATE_FINISHED;
        if (curSPTask->task.t.type == M_AUDTASK) {
            // After audio tasks come gfx tasks.
            profiler_log_vblank_time();
            if (sCurrentDisplaySPTask != NULL) {
                if (sCurrentDisplaySPTask->state != SPTASK_STATE_FINISHED) {
                    if (sCurrentDisplaySPTask->state != SPTASK_STATE_INTERRUPTED) {
                        profiler_log_gfx_time(TASKS_QUEUED);
                    }
                    start_sptask(M_GFXTASK);
                }
            }
            sCurrentAudioSPTask = NULL;
            if (curSPTask->msgqueue != NULL) {
                osSendMesg(curSPTask->msgqueue, curSPTask->msg, OS_MESG_NOBLOCK);
            }
        } else {
            // The SP process is done, but there is still a Display Processor notification
            // that needs to arrive before we can consider the task completely finished and
            // null out sCurrentDisplaySPTask. That happens in handle_dp_complete.
            profiler_log_gfx_time(RSP_COMPLETE);
        }
    };
}

void thread3_video(UNUSED void* arg0) {
    s32 i;
    u64* framebuffer1;
    OSMesg msg;
    UNUSED s32 pad[4];

    gPhysicalFramebuffers[0] = (u16*) &gFramebuffer0;
    gPhysicalFramebuffers[1] = (u16*) &gFramebuffer1;
    gPhysicalFramebuffers[2] = (u16*) &gFramebuffer2;

    // Clear framebuffer.
    framebuffer1 = (u64*) &gFramebuffer1;
    for (i = 0; i < 19200; i++) {
        framebuffer1[i] = 0;
    }
    setup_mesg_queues();
    setup_game_memory();

    create_thread(&gAudioThread, 4, &thread4_audio, 0, gAudioThreadStack + ARRAY_COUNT(gAudioThreadStack), 20);
    osStartThread(&gAudioThread);

    create_thread(&gGameLoopThread, 5, &thread5_game_loop, 0, gGameLoopThreadStack + ARRAY_COUNT(gGameLoopThreadStack),
                  10);
    osStartThread(&gGameLoopThread);

    while (true) {
        osRecvMesg(&gIntrMesgQueue, &msg, OS_MESG_BLOCK);
        switch ((u32) msg.data32) {
            case MESG_VI_VBLANK:
                handle_vblank();
                break;
            case MESG_SP_COMPLETE:
                handle_sp_complete();
                break;
            case MESG_DP_COMPLETE:
                handle_dp_complete();
                break;
            case MESG_START_GFX_SPTASK:
                start_gfx_sptask();
                break;
        }
    }
}

void func_800025D4(void) {
    func_80091B78();
    gActiveScreenMode = SCREEN_MODE_1P;
    func_802A4D18();
}

void func_80002600(void) {
    func_80091B78();
    gActiveScreenMode = SCREEN_MODE_1P;
    func_802A4D18();
}

void func_8000262C(void) {
    func_80091B78();
    gActiveScreenMode = SCREEN_MODE_1P;
    func_802A4D18();
}

void func_80002658(void) {
    func_80091B78();
    gActiveScreenMode = SCREEN_MODE_1P;
    func_802A4D18();
}

/**
 * Sets trackId to NULL if
 *
 *
 */
void update_gamestate(void) {
    if (gGamestate != RACING) gQuickTimeTrial = false;
    switch (gGamestate) {
        case START_MENU_FROM_QUIT:
            func_80002658();
            gCurrentlyLoadedTrackAddr = 0;
            break;
        case MAIN_MENU_FROM_QUIT:
            func_800025D4();
            gCurrentlyLoadedTrackAddr = 0;
            break;
        case PLAYER_SELECT_MENU_FROM_QUIT:
            func_80002600();
            gCurrentlyLoadedTrackAddr = 0;
            break;
        case COURSE_SELECT_MENU_FROM_QUIT:
            func_8000262C();
            gCurrentlyLoadedTrackAddr = 0;
            break;
        case RACING:
            /**
             * @bug Reloading this segment makes random_u16() deterministic for player spawn order.
             * In laymens terms, random_u16() outputs the same value every time.
             */
            // init_segment_racing();
            setup_race();
            TrackBrowser_ResetSelectedTrack(); // Same function as gCurrentlyLoadedTrackAddr
            break;
        case ENDING:
            gCurrentlyLoadedTrackAddr = 0;
            init_segment_ending_sequences();
            setup_podium_ceremony();
            break;
        case CREDITS_SEQUENCE:
            gCurrentlyLoadedTrackAddr = 0;
            // init_segment_racing();
            init_segment_ending_sequences();
            load_credits();
            break;
    }
}

void thread5_game_loop(void) {
    setup_game_memory();
    osCreateMesgQueue(&gGfxVblankQueue, gGfxMesgBuf, 1);
    osCreateMesgQueue(&gGameVblankQueue, &gGameMesgBuf, 1);
    init_controllers();
    if (!wasSoftReset) {
        clear_nmi_buffer();
    }

    // set_vblank_handler(2, &gGameVblankHandler, &gGameVblankQueue, (OSMesg *)(OS_EVENT_SW2));
    // These variables track stats such as player wins.
    // In the event of a console reset, it remembers them.
    gNmiUnknown1 = &pAppNmiBuffer[0]; // 2  u8's, tracks number of times player 1/2 won a VS race
    gNmiUnknown2 =
        &pAppNmiBuffer[2]; // 9  u8's, 3x3, tracks number of times player 1/2/3   has placed in 1st/2nd/3rd in a VS race
    gNmiUnknown3 = &pAppNmiBuffer[11]; // 12 u8's, 4x3, tracks number of times player 1/2/3/4 has placed in 1st/2nd/3rd
                                       // in a VS race
    gNmiUnknown4 = &pAppNmiBuffer[23]; // 2  u8's, tracking number of Battle mode wins by player 1/2
    gNmiUnknown5 = &pAppNmiBuffer[25]; // 3  u8's, tracking number of Battle mode wins by player 1/2/3
    gNmiUnknown6 = &pAppNmiBuffer[28]; // 4  u8's, tracking number of Battle mode wins by player 1/2/3/4
    rendering_init();
    read_controllers();
    func_800C5CB8();
}

void thread5_iteration(void) {
    LocalMP_ProcessRequest();
    func_800CB2C4();
    calculate_delta_time();
#ifdef TARGET_N64
    while (true) {
        func_800CB2C4();
    }
#endif
    if (GfxDebuggerIsDebugging()) {
        Graphics_PushFrame(gGfxPool->gfxPool);
        return;
    }
    // Update the gamestate if it has changed (racing, menus, credits, etc.).
    if (gGamestateNext != gGamestate) {
        gGamestate = gGamestateNext;
        update_gamestate();
    }
    // Internal, disabled-by-default prototype. Only the race loop opts in;
    // menus, cutscenes and many race systems still require conversion.
    static NativeFrameClock nativeFrameClock = { 0 };
    static int wasNativeFrame = 0;
    const int nativeFrame = GameEngine_IsExperimentalNativePresentation();
    static uint64_t nativeElapsedLegacyTicks = 0;
    if (nativeFrame) {
        if (!wasNativeFrame) {
            NativeFrameClock_Reset(&nativeFrameClock);
            nativeElapsedLegacyTicks = 0;
        }
        const uint64_t rawElapsedNs = (uint64_t) (sNativeElapsedTime * 1000000000.0);
        const uint64_t elapsedNs = NativeFrameClock_BoundedElapsed(&nativeFrameClock, rawElapsedNs,
                                                                   UINT64_C(1000000000) / 15);
        NativeFrameStep step = NativeFrameClock_Advance(&nativeFrameClock, elapsedNs);
        gNativeSimulationTickScale = GameEngine_IsExperimentalNativeRace() ? (f32) step.legacy_tick_fraction : 1.0f;
        gNativeSimulationLegacyTicks = (uint32_t) step.legacy_ticks;
        gNativeVisualSteps = (uint32_t) ((nativeElapsedLegacyTicks + step.legacy_ticks + 1) / 2 -
                                        (nativeElapsedLegacyTicks + 1) / 2);
        nativeElapsedLegacyTicks += step.legacy_ticks;
        gNativeVisualPhase = (float) (((nativeElapsedLegacyTicks + 1) % 2 +
            (double) nativeFrameClock.legacy_phase / NATIVE_FRAME_NANOSECONDS_PER_SECOND) * 0.5);
    } else {
        gNativeSimulationTickScale = 1.0f;
        gNativeSimulationLegacyTicks = 1;
        gNativeVisualSteps = 1;
        gNativeVisualPhase = 0.0f;
    }
    wasNativeFrame = nativeFrame;
    if (CVarGetInteger("gExperimentalMenuBenchmark", 0) &&
        gGamestate >= START_MENU_FROM_QUIT && gGamestate <= COURSE_SELECT_MENU_FROM_QUIT) {
        static f64 menuBenchElapsed = 0.0;
        static s32 menuBenchFrames = 0;
        menuBenchElapsed += gDeltaTime;
        ++menuBenchFrames;
        if (menuBenchElapsed >= 0.5) {
            printf("MENU_BENCH fps=%.1f menu=%d visualTick=%d flashTick=%d selection=%d players=%d\n",
                   menuBenchFrames / menuBenchElapsed, gMenuSelection, gGlobalTimer, gCycleFlashMenu,
                   gMainMenuSelection, gPlayerCountSelection1);
            fflush(stdout);
            menuBenchElapsed = 0.0;
            menuBenchFrames = 0;
        }
    }
    if (CVarGetInteger("gExperimentalNativeBenchmark", 0) && gGamestate == RACING) {
        static f64 benchElapsed = 0.0;
        static s32 benchFrames = 0;
        static int benchSeeded = 0;
        static int effectTestState = 0;
        static f32 effectTestStart = 0.0f;
        static int effectTestSampled = 0;
        static int airTestState = 0;
        static int airTestMaxTicks = 0;
        static f32 airTestLandingTime = 0.0f;
        if (CVarGetInteger("gExperimentalHopBenchmark", 0) && gRaceState == RACE_IN_PROGRESS) {
            static int hopTestState = 0;
            static f32 initialY, peakY, peakHop;
            Player* p = &gPlayers[0];
            if (!hopTestState && gCourseTimer >= 1.4f) {
                initialY = peakY = p->pos[1]; peakHop = 0.0f; hopTestState = 1;
            }
            if (hopTestState == 1) {
                if (p->pos[1] > peakY) peakY = p->pos[1];
                if (p->kartHopVelocity > peakHop) peakHop = p->kartHopVelocity;
                if (gCourseTimer >= 2.4f) {
                    printf("HOP_BENCH initialY=%.4f peakY=%.4f peakHop=%.4f finalY=%.4f effects=%x\n",
                           initialY, peakY, peakHop, p->pos[1], p->effects);
                    hopTestState = 2;
                }
            }
        }
        if (CVarGetInteger("gExperimentalAirBenchmark", 0) && gRaceState == RACE_IN_PROGRESS) {
            Player* p = &gPlayers[0];
            if (!airTestState && gCourseTimer >= 1.5f) {
                if (CVarGetInteger("gExperimentalAirBenchmark", 0) == 2) {
                    // Fixed Choco Mountain coordinates make both isolated
                    // runs begin the probe at the same height and location.
                    p->pos[0] = -26.0f;
                    p->pos[1] = 55.35f + CVarGetInteger("gExperimentalAirDropHeight", 30);
                    p->pos[2] = -380.0f;
                    p->velocity[0] = p->velocity[2] = 0.0f;
                    p->currentSpeed = p->kartPropulsionStrength = 0.0f;
                } else {
                    p->pos[1] += 30.0f;
                }
                for (int axis = 0; axis < 3; ++axis) p->oldPos[axis] = p->pos[axis];
                p->velocity[1] = 0.0f;
                p->unk_0C2 = 0;
                airTestState = 1;
                printf("AIR_BENCH start=%.4f pos=(%.4f,%.4f,%.4f) velocity=(%.4f,%.4f,%.4f)\n",
                       gCourseTimer, p->pos[0], p->pos[1], p->pos[2], p->velocity[0], p->velocity[1], p->velocity[2]);
            } else if (airTestState == 1 && (p->effects & 8)) {
                airTestState = 2;
            }
            if (airTestState == 2) {
                if (p->unk_0C2 > airTestMaxTicks) airTestMaxTicks = p->unk_0C2;
                if (CVarGetInteger("gExperimentalAirBenchmark", 0) == 2 && gNativeSimulationLegacyTicks) {
                    printf("AIR_SAMPLE t=%.4f y=%.4f vy=%.4f groundDistance=%.4f airTicks=%d effects=%x\n",
                           gCourseTimer, p->pos[1], p->velocity[1], p->collision.surfaceDistance[2], p->unk_0C2, p->effects);
                }
                if (!(p->effects & 8)) {
                    airTestState = 3;
                    airTestLandingTime = gCourseTimer;
                    printf("AIR_BENCH land=%.4f y=%.4f vy=%.4f groundDistance=%.4f maxTicks=%d speed=%.4f\n",
                           gCourseTimer, p->pos[1], p->velocity[1], p->collision.surfaceDistance[2], airTestMaxTicks, p->speed);
                }
            }
            if (airTestState == 3 && gCourseTimer - airTestLandingTime < 0.5f && gNativeSimulationLegacyTicks) {
                printf("AIR_RECOVERY t=%.4f y=%.4f vy=%.4f groundDistance=%.4f effects=%x\n",
                       gCourseTimer, p->pos[1], p->velocity[1], p->collision.surfaceDistance[2], p->effects);
            }
        }
        Player* effectTestPlayer = &gPlayers[NUM_RACERS - 1];
        if (CVarGetInteger("gExperimentalAccelerationBoundaryBenchmark", 0) && gRaceState == RACE_IN_PROGRESS) {
            static int boundaryDone = 0;
            if (!boundaryDone && gCourseTimer >= 1.0f) {
                const f32 savedScale = gNativeSimulationTickScale;
                *effectTestPlayer = gPlayers[0];
                effectTestPlayer->type = 0;
                effectTestPlayer->topSpeed = 320.0f;
                const int benchmarkMode = CVarGetInteger("gExperimentalAccelerationBoundaryBenchmark", 0);
                const int alternate = benchmarkMode >= 2;
                const int characterCount = benchmarkMode >= 3 ? 8 : 1;
                const int firstCharacter = effectTestPlayer->characterId;
                for (int character = 0; character < characterCount; ++character) {
                effectTestPlayer->characterId = benchmarkMode >= 3 ? character : firstCharacter;
                if (benchmarkMode >= 4) effectTestPlayer->topSpeed = gTopSpeedTable[gCCSelection][character];
                for (int band = 1; band <= 10; ++band) {
                    const f32 initial = effectTestPlayer->topSpeed * band / 10.0f - (band == 10 ? 0.1f : 0.5f);
                    effectTestPlayer->currentSpeed = initial;
                    gNativeSimulationTickScale = 1.0f;
                    if (alternate) player_accelerate_alternative(effectTestPlayer);
                    else player_accelerate(effectTestPlayer);
                    const f32 legacy = effectTestPlayer->currentSpeed;
                    effectTestPlayer->currentSpeed = initial;
                    gNativeSimulationTickScale = 0.25f;
                    for (int i = 0; i < 4; ++i) {
                        if (alternate) player_accelerate_alternative(effectTestPlayer);
                        else player_accelerate(effectTestPlayer);
                    }
                    printf("ACCEL_BOUNDARY band=%d start=%.2f legacy=%.5f fractional=%.5f character=%d\n",
                           band, initial, legacy, effectTestPlayer->currentSpeed, effectTestPlayer->characterId);
                }
                }
                gNativeSimulationTickScale = savedScale;
                boundaryDone = 1;
            }
        }
        if (CVarGetInteger("gExperimentalTreeContactBenchmark", 0) && gRaceState == RACE_IN_PROGRESS) {
            static int treeTestState = 0;
            static f32 treeTestStart = 0.0f;
            static struct Actor* treeTestActor = NULL;
            const int contactTestMode = CVarGetInteger("gExperimentalTreeContactBenchmark", 0);
            if (!treeTestState && gCourseTimer >= 1.0f) {
                for (size_t i = 0; i < CM_GetActorSize(); ++i) {
                    struct Actor* actor = CM_GetActor(i);
                    if (actor->type == ACTOR_TREE_MARIO_RACEWAY || actor->type == ACTOR_TREE_YOSHI_VALLEY ||
                        actor->type == ACTOR_TREE_ROYAL_RACEWAY || actor->type == ACTOR_TREE_MOO_MOO_FARM ||
                        actor->type == ACTOR_PALM_TREE || actor->type == ACTOR_TREE_FRAPPE_SNOWLAND) {
                        treeTestActor = actor;
                        break;
                    }
                }
                if (!treeTestActor) { printf("TREE_BENCH no requested actor found (mode=%d)\n", contactTestMode); treeTestState = 2; }
                else {
                    *effectTestPlayer = gPlayers[0];
                    effectTestPlayer->type = contactTestMode >= 2 ? PLAYER_HUMAN : 0;
                    effectTestPlayer->speed = 0;
                    treeTestStart = gCourseTimer;
                    treeTestState = 1;
                }
            }
            if (treeTestState == 1 && gCourseTimer - treeTestStart < 1.0f) {
                effectTestPlayer->pos[0] = treeTestActor->pos[0] + 1.0f;
                effectTestPlayer->pos[1] = treeTestActor->pos[1] + 1.0f;
                effectTestPlayer->pos[2] = treeTestActor->pos[2];
                effectTestPlayer->velocity[0] = effectTestPlayer->velocity[2] = 0;
                effectTestPlayer->effects = 0;
                if (contactTestMode == 2) collision_mario_sign(effectTestPlayer, treeTestActor);
                else if (contactTestMode == 3) collision_piranha_plant(effectTestPlayer, (struct PiranhaPlant*) treeTestActor);
                else collision_tree(effectTestPlayer, treeTestActor);
            } else if (treeTestState == 1) {
                printf("TREE_BENCH done elapsed=%.4f\n", gCourseTimer - treeTestStart);
                treeTestState = 2;
            }
        }
        if (CVarGetInteger("gExperimentalActorContactBenchmark", 0) && gRaceState == RACE_IN_PROGRESS) {
            static int state = 0;
            static f32 startTime = 0.0f;
            static unsigned contacts = 0;
            static unsigned boundaryContacts = 0;
            const int contactMode = CVarGetInteger("gExperimentalActorContactBenchmark", 0);
            Player* testPlayer = &gPlayers[NUM_RACERS - 1];
            if (!state && gCourseTimer >= 1.0f) {
                *testPlayer = gPlayers[0];
                testPlayer->type = 0;
                testPlayer->boundingBoxSize = 10.0f;
                testPlayer->velocity[0] = 1.0f;
                testPlayer->velocity[1] = testPlayer->velocity[2] = 0.0f;
                startTime = gCourseTimer;
                state = 1;
            }
            if (state == 1 && gCourseTimer - startTime < 1.0f) {
                const int repeats = GameEngine_IsExperimentalNativeRace() ? 1 : gTickLogic;
                const f32 elapsed = gCourseTimer - startTime;
                const f32 testSpeed = contactMode == 3 ? 600.0f : contactMode == 4 ? 2400.0f : 12500.0f;
                Vec3f obstacle = { contactMode >= 2 ? (elapsed - 0.1f) * testSpeed : 5.0f, 0.0f, 0.0f };
                for (int i = 0; i < repeats; ++i) {
                    testPlayer->pos[0] = testPlayer->pos[1] = testPlayer->pos[2] = 0.0f;
                    testPlayer->effects = 0;
                    const int hit = query_and_resolve_collision_player_actor(testPlayer, obstacle, 10.0f, 10.0f, 0.8f) != 0;
                    contacts += hit;
                    if (!GameEngine_IsExperimentalNativeRace() || gNativeSimulationLegacyTicks) boundaryContacts += hit;
                }
            } else if (state == 1) {
                printf("ACTOR_CONTACT_BENCH mode=%d duration=%.4f contacts=%u boundaryContacts=%u velocity=%.5f position=%.5f\n",
                       contactMode, gCourseTimer - startTime, contacts, boundaryContacts, testPlayer->velocity[0], testPlayer->pos[0]);
                state = 2;
            }
        }
        if (CVarGetInteger("gExperimentalCollisionBenchmark", 0) && gRaceState == RACE_IN_PROGRESS) {
            static int initialized = 0;
            static unsigned contactTicks = 0;
            Player* a = &gPlayers[NUM_RACERS - 2];
            Player* b = &gPlayers[NUM_RACERS - 1];
            if (!initialized && gCourseTimer >= 1.0f) {
                *a = *b = gPlayers[0];
                a->type = b->type = 0;
                a->characterId = b->characterId = 0;
                a->boundingBoxSize = b->boundingBoxSize = 10.0f;
                a->velocity[0] = 1.0f; b->velocity[0] = -1.0f;
                a->velocity[1] = a->velocity[2] = b->velocity[1] = b->velocity[2] = 0.0f;
                initialized = 1;
            }
            if (initialized && contactTicks < 60) {
                const uint32_t repeats = GameEngine_IsExperimentalNativeRace() ? gNativeSimulationLegacyTicks : gTickLogic;
                for (uint32_t step = 0; step < repeats && contactTicks < 60; ++step) {
                    // Recreate a persistent overlap to make repeated impulses
                    // visible while leaving all active race karts untouched.
                    a->pos[0] = 0.0f; b->pos[0] = 5.0f;
                    a->pos[1] = b->pos[1] = a->pos[2] = b->pos[2] = 0.0f;
                    a->effects = b->effects = 0;
                    func_802903D8(a, b);
                    if (++contactTicks == 60) {
                        printf("COLLISION_BENCH ticks=%u t=%.4f velocity=(%.6f,%.6f) pos=(%.6f,%.6f)\n",
                               contactTicks, gCourseTimer, a->velocity[0], b->velocity[0], a->pos[0], b->pos[0]);
                    }
                }
            }
        }
        if (CVarGetInteger("gExperimentalEffectBenchmark", 0) && gRaceState == RACE_IN_PROGRESS) {
            const int effectTest = CVarGetInteger("gExperimentalEffectBenchmark", 0);
            const u32 effectMask = effectTest == 2 ? 0x80000 : effectTest == 3 ? 0x800000 :
                                   (effectTest == 4 || effectTest == 18) ? LIGHTNING_EFFECT : effectTest == 5 ? BOOST_EFFECT :
                                   effectTest == 6 ? BOOST_RAMP_ASPHALT_EFFECT :
                                   effectTest == 7 ? BOOST_RAMP_WOOD_EFFECT :
                                   effectTest == 8 ? HIT_BY_ITEM_EFFECT : effectTest == 9 ? 0x01000000 :
                                   effectTest == 10 ? HIT_EFFECT : effectTest == 11 ? 0x40 :
                                   effectTest == 12 ? 0x80 : effectTest == 13 ? 0x400 :
                                   effectTest == 14 ? STAR_EFFECT : effectTest == 15 ? BOO_EFFECT :
                                   (effectTest == 16 || effectTest == 17) ? 0 : 0x10000;
            if (!effectTestState && gCourseTimer >= 1.0f) {
                *effectTestPlayer = gPlayers[0];
                effectTestPlayer->type &= ~PLAYER_EXISTS;
                effectTestPlayer->effects = effectMask | ((effectTest == 8 || effectTest == 9) ? 8 : 0);
                if (effectTest == 18) effectTestPlayer->effects |= 0x20000;
                effectTestPlayer->triggers = 0;
                if (effectTest == 17) {
                    effectTestPlayer->triggers = LOSE_BATTLE_EFFECT;
                    effectTestPlayer->alpha = 255;
                }
                effectTestPlayer->kartProps = 0;
                effectTestPlayer->lakituProps = 0;
                if (effectTest == 16) {
                    effectTestPlayer->lakituProps = HELD_BY_LAKITU | LAKITU_SCENE;
                    effectTestPlayer->unk_222 = 4;
                    effectTestPlayer->unk_0C8 = 0;
                    effectTestPlayer->unk_074 = effectTestPlayer->pos[1] - effectTestPlayer->boundingBoxSize + 2.0f;
                }
                effectTestPlayer->unk_0A8 = 0;
                effectTestPlayer->unk_236 = 1;
                effectTestPlayer->unk_0E0 = 0;
                effectTestPlayer->unk_0AE = effectTestPlayer->rotation[1];
                effectTestPlayer->unk_0B2 = effectTest == 3 ? 4 :
                                             (effectTest == 11 || effectTest == 12 || effectTest == 18) ? 2 : 1;
                effectTestPlayer->unk_0B0 = effectTest == 4 ? 0x190 : 0;
                if (effectTest >= 11) effectTestPlayer->currentSpeed = 150.0f;
                if (effectTest == 14 || effectTest == 15) {
                    effectTestPlayer->type &= ~PLAYER_HUMAN;
                    effectTestPlayer->alpha = 255;
                    gPlayerOtherScreensAlpha[NUM_RACERS - 1] = 255;
                    gPlayerStarEffectStartTime[NUM_RACERS - 1] = (s32) gCourseTimer - 8;
                    gPlayerBooEffectStartTime[NUM_RACERS - 1] = (s32) gCourseTimer - 5;
                }
                if (effectTest == 10) {
                    effectTestPlayer->unk_238 = 0;
                    effectTestPlayer->unk_046 &= ~0x80;
                    effectTestPlayer->unk_D98 = 1;
                    effectTestPlayer->unk_D9C = 0.0f;
                    effectTestPlayer->unk_DA0 = 65.0f;
                    effectTestPlayer->collision.surfaceDistance[2] = 0.0f;
                    D_8018D990[NUM_RACERS - 1] = 0;
                }
                if (effectTest >= 5) {
                    effectTestPlayer->boostPower = 100.0f;
                    effectTestPlayer->boostTimer = 12;
                    effectTestPlayer->surfaceType = effectTest == 6 ? BOOST_RAMP_ASPHALT :
                                                    effectTest == 7 ? BOOST_RAMP_WOOD : 0;
                    effectTestPlayer->unk_256 = 0;
                }
                D_8018D920[NUM_RACERS - 1] = 0;
                effectTestState = 1;
                effectTestStart = gCourseTimer;
                printf("EFFECT_BENCH effect=%x start=%.4f\n", effectMask, gCourseTimer);
            }
            if (effectTestState == 1) {
                if (effectTest == 18 && !effectTestSampled && !(effectTestPlayer->effects & 0x20000)) {
                    effectTestSampled = 1;
                    printf("EFFECT_BENCH effect=%x spinEnd=%.4f speed=%.3f yaw=%d\n", effectMask,
                           gCourseTimer, effectTestPlayer->currentSpeed, effectTestPlayer->rotation[1]);
                }
                if (effectTest == 15 && !effectTestSampled && gCourseTimer - effectTestStart >= 0.5f) {
                    effectTestSampled = 1;
                    printf("EFFECT_BENCH effect=%x sample=%.4f alpha=%d otherAlpha=%d\n", effectMask,
                           gCourseTimer - effectTestStart, effectTestPlayer->alpha,
                           gPlayerOtherScreensAlpha[NUM_RACERS - 1]);
                }
                if (effectTest >= 6 && effectTest <= 7) {
                    if (!effectTestSampled && gCourseTimer - effectTestStart >= 0.5f) {
                        effectTestSampled = 1;
                        printf("EFFECT_BENCH effect=%x sample=%.4f power=%.4f\n", effectMask,
                               gCourseTimer - effectTestStart, effectTestPlayer->boostPower);
                    }
                    if (gCourseTimer - effectTestStart >= 1.0f) effectTestPlayer->surfaceType = 0;
                }
                const int repeats = GameEngine_IsExperimentalNativeRace() ? 1 : gTickLogic;
                for (int step = 0; step < repeats; ++step) apply_effect(effectTestPlayer, NUM_RACERS - 1, 0);
                if (effectTest == 17 ? !(effectTestPlayer->triggers & (LOSE_BATTLE_EFFECT | BECOME_BOMB_EFFECT)) :
                    effectTest == 16 ? !(effectTestPlayer->lakituProps & HELD_BY_LAKITU) :
                                       !(effectTestPlayer->effects & effectMask)) {
                    effectTestState = 2;
                    printf("EFFECT_BENCH effect=%x end=%.4f yaw=%d speed=%.3f gravity=%.3f size=%.3f y=%.3f alpha=%d otherAlpha=%d\n", effectMask,
                           gCourseTimer, effectTestPlayer->rotation[1], effectTestPlayer->currentSpeed,
                           effectTestPlayer->kartGravity, effectTestPlayer->size, effectTestPlayer->pos[1],
                           effectTestPlayer->alpha, gPlayerOtherScreensAlpha[NUM_RACERS - 1]);
                }
            }
        }
        if (!benchSeeded && gRaceState == RACE_IN_PROGRESS) {
            gRandomSeed16 = 0x1234;
            if (CVarGetInteger("gExperimentalBenchmarkSolo", 0)) {
                // Isolated physics comparison: remove competitors after GO so
                // player contact cannot affect the measured trajectory.
                for (s32 i = 1; i < LocalMP_RacerCount(); ++i) gPlayers[i].type &= ~PLAYER_EXISTS;
            }
            benchSeeded = 1;
        }
        benchElapsed += gDeltaTime;
        ++benchFrames;
        if (benchElapsed >= 0.25) {
            printf("NATIVE_BENCH fps=%.1f raceState=%d courseTime=%.3f player=(%.2f,%.2f,%.2f) speed=%.3f rng=%u visualTick=%d objectTick=%d\n",
                   benchFrames / benchElapsed, gRaceState, gCourseTimer,
                   gPlayers[0].pos[0], gPlayers[0].pos[1], gPlayers[0].pos[2], gPlayers[0].speed, gRandomSeed16, gGlobalTimer, D_8018D400);
            if (CVarGetInteger("gExperimentalReplayGhostTest", 0) == 3) {
                const Player* ghost = &gPlayers[1];
                printf("REPLAY_GHOST_MOTION t=%.3f type=%04x pos=(%.3f,%.3f,%.3f) speed=%.4f\n",
                       gCourseTimer, (unsigned) ghost->type, ghost->pos[0], ghost->pos[1], ghost->pos[2], ghost->speed);
            }
            if (CVarGetInteger("gExperimentalBenchmarkSolo", 0)) {
                const Player* p = &gPlayers[0];
                printf("NATIVE_WALL t=%.3f v=(%.4f,%.4f,%.4f) current=%.4f propulsion=%.4f yaw=%d steer=%d turn=%d turnRate=%u turnDamp=%.3f wall=(%.4f,%.4f) recovery=(%d,%u) friction=%.3f airTicks=%d effects=%x\n",
                       gCourseTimer, p->velocity[0], p->velocity[1], p->velocity[2], p->currentSpeed,
                       p->kartPropulsionStrength, p->rotation[1], p->unk_07C >> 16, p->unk_078, p->unk_200, p->unk_090,
                       p->collision.surfaceDistance[0],
                       p->collision.surfaceDistance[1], p->unk_10C, p->unk_256, p->kartFriction, p->unk_0C2, p->effects);
            }
            fflush(stdout);
            benchElapsed = 0.0;
            benchFrames = 0;
        }
    }
    if (CVarGetInteger("gExperimentalCreditsBenchmark", 0) && gGamestate == CREDITS_SEQUENCE) {
        static f64 elapsed = 0.0;
        static s32 frames = 0;
        elapsed += gDeltaTime;
        ++frames;
        if (elapsed >= 0.5) {
            Camera* camera = gScreenOneCtx->camera;
            printf("CREDITS_BENCH fps=%.1f visualTick=%d camera=(%.2f,%.2f,%.2f) transition=%d\n",
                   frames / elapsed, gGlobalTimer, camera->pos[0], camera->pos[1], camera->pos[2],
                   gIsInQuitToMenuTransition);
            fflush(stdout);
            frames = 0;
            elapsed = 0.0;
        }
    }
    profiler_log_thread5_time(THREAD5_START);
    config_gfx_pool();
    read_controllers();
    FB_CreateFramebuffers();
    clear_framebuffer(0); // Clear the framebuffer
    PerfDiagMark(1);
    game_state_handler();
    PerfDiagMark(5);

    // call_render_hook();

    end_master_display_list();
    PerfDiagMark(3);
    display_and_vsync();
    PerfDiagMark(7);
}

/**
 * Sound processing thread. Runs at 50 or 60 FPS according to osTvType.
 */
void thread4_audio(UNUSED void* arg) {
    UNUSED u32 unused[3];
    audio_init();
    osCreateMesgQueue(&sSoundMesgQueue, sSoundMesgBuf, ARRAY_COUNT(sSoundMesgBuf));
    set_vblank_handler(1, &sSoundVblankHandler, &sSoundMesgQueue, (OSMesg*) 512);

    while (true) {
        OSMesg msg;
        struct SPTask* spTask;

        osRecvMesg(&sSoundMesgQueue, &msg, OS_MESG_BLOCK);

        profiler_log_thread4_time();

        spTask = create_next_audio_frame_task();
        if (spTask != NULL) {
            dispatch_audio_sptask(spTask);
        }
        profiler_log_thread4_time();
    }
}

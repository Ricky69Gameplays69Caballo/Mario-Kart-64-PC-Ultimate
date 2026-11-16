#include "CustomTrack.h"
#include "Track.h"

#include <libultraship/libultraship.h>
#include "libultraship/bridge/resourcebridge.h"
#include "align_asset_macro.h"
#include <unordered_set>
#include <string>
#include <cstring>
#include <algorithm>

#include "port/Engine.h"
#include "port/interpolation/FrameInterpolation.h"
#include "engine/Matrix.h"
#include "engine/editor/SceneManager.h"

extern "C" {
#include "racing/collision.h"
#include "racing/render_courses.h"
#include "racing/actors.h"
#include "racing/math_util.h"
#include "code_80005FD0.h"
}
#include "assets/textures/other_textures.h"
#include "assets/models/data_segment2.h"

CustomTrack::CustomTrack() {
    Props.SetText(Props.Name, "Blank Track", sizeof(Props.Name));
    Props.SetText(Props.DebugName, "blnktrck", sizeof(Props.DebugName));
    Props.SetText(Props.TrackLength, "100m", sizeof(Props.TrackLength));
    ResourceName = "mod:blank_track";
    Props.Minimap.Texture = minimap_mario_raceway;
    Props.Minimap.Width = ResourceGetTexWidthByName(Props.Minimap.Texture);
    Props.Minimap.Height = ResourceGetTexHeightByName(Props.Minimap.Texture);
    Props.Minimap.Pos[0].X = 257;
    Props.Minimap.Pos[0].Y = 170;
    Props.Minimap.PlayerX = 0;
    Props.Minimap.PlayerY = 0;
    Props.Minimap.PlayerScaleFactor = 0.22f;
    Props.Minimap.FinishlineX = 0;
    Props.Minimap.FinishlineY = 0;
    Props.Minimap.Colour = { 255, 255, 255 };

    Props.WaterLevel = FLT_MAX;

    Props.LakituTowType = (s32) OLakitu::LakituTowType::NORMAL;
    Props.AIBehaviour = D_0D008F28;
    Props.AIMaximumSeparation = 50.0f;
    Props.AIMinimumSeparation = 0.3f;
    Props.AIDistance = gMarioRacewayAIDistances;
    Props.AISteeringSensitivity = 48;

    Props.NearPersp = 3.0f;
    Props.FarPersp = 6800.0f;

    Props.PathSizes = { 600, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 };

    Props.CurveTargetSpeed[0] = 4.1666665f;
    Props.CurveTargetSpeed[1] = 5.5833334f;
    Props.CurveTargetSpeed[2] = 6.1666665f;
    Props.CurveTargetSpeed[3] = 6.75f;

    Props.NormalTargetSpeed[0] = 3.75f;
    Props.NormalTargetSpeed[1] = 5.1666665f;
    Props.NormalTargetSpeed[2] = 5.75f;
    Props.NormalTargetSpeed[3] = 6.3333334f;

    Props.D_0D0096B8[0] = 3.3333332f;
    Props.D_0D0096B8[1] = 3.9166667f;
    Props.D_0D0096B8[2] = 4.5f;
    Props.D_0D0096B8[3] = 5.0833334f;

    Props.OffTrackTargetSpeed[0] = 3.75f;
    Props.OffTrackTargetSpeed[1] = 5.1666665f;
    Props.OffTrackTargetSpeed[2] = 5.75f;
    Props.OffTrackTargetSpeed[3] = 6.3333334f;

    Props.PathTable[0] = NULL;
    Props.PathTable[1] = NULL;
    Props.PathTable[2] = NULL;
    Props.PathTable[3] = NULL;

    Props.PathTable2[0] = NULL;
    Props.PathTable2[1] = NULL;
    Props.PathTable2[2] = NULL;
    Props.PathTable2[3] = NULL;
    Props.PathTable2[4] = NULL;

    Props.Clouds = NULL;
    Props.CloudList = NULL;
    Props.Sequence = MusicSeq::MUSIC_SEQ_UNKNOWN;

    bFog = false;
    gFogColour.r = 0;
    gFogColour.g = 0;
    gFogColour.b = 0;
    gFogColour.a = 255;
    gFogMin = 995;
    gFogMax = 1000;
}

void CustomTrack::Load() {
    /** Get TrackInfo **/
    printf("[CustomTrack] Loading... %s\n", ResourceName.c_str());
    const TrackInfo* info = gTrackRegistry.GetInfo(ResourceName);
    if (nullptr == info) {
        printf("[CustomTrack] Could not find TrackInfo for %s\n", ResourceName.c_str());
        return;
    }

    /** Get Directory to Track files **/
    const std::string& path = info->Path;
    if (path.empty()) {
        printf("[CustomTrack] No track path\n");
        return;
    }

    /** Get Props, Minimap, Actors, Environment settings from scene.json **/
    TrackEditor::LoadTrackDataFromJson(this, path);
    ApplyCloudSet();
    for (ScrollMaterial& m : ScrollMaterials) {
        m.Path = path + "/" + m.Name;
        m.OffS = 0;
        m.OffT = 0;
    }
    if (!ScrollMaterials.empty()) {
        printf("[CustomTrack] %zu scrolling materials\n", ScrollMaterials.size());
    }

    /** Get Track Mesh **/
    const std::string meshPath = (path + "/data_track_sections");

    TrackSections* meshData = (TrackSections*) LOAD_ASSET_RAW(meshPath.c_str());
    if (!meshData) {
        printf("[CustomTrack] No valid mesh found!\n");
        return;
    }

    size_t numMesh = ( ResourceGetSizeByName(meshPath.c_str()) / sizeof(TrackSections) );

    /** Iterate over meshes **/
    for (size_t i = 0; i < numMesh; i++) {
        if (gIsMirrorMode != 0) {
            CustomTrack::InvertTriangleWindingModdedByName(ResourceGetNameByCrc(meshData[i].crc));
        }

        /** Sort meshes into DrawLayer queues **/
        switch (static_cast<DrawLayer>(meshData[i].layer)) {
            case DrawLayer::DRAW_INVISIBLE:
                break;
            default:
            case DrawLayer::DRAW_OPAQUE:
                mOpaqueItems.push_back(meshData[i].crc);
                break;
            case DrawLayer::DRAW_TRANSLUCENT:
                mTranslucentItems.push_back(meshData[i]);
                break;
            case DrawLayer::DRAW_TRANSLUCENT_NO_ZBUFFER:
                mTranslucentNoZBufferItems.push_back(meshData[i]);
                break;
        }
    }

    /** Per-section view lists: only draw what is visible from the camera's track section **/
    mSectionViewNames.clear();
    mSectionViews.clear();
    auto archives = GameEngine::Instance->context->GetResourceManager()->GetArchiveManager();
    for (int i = 1; i < 100; i++) {
        char name[32];
        snprintf(name, sizeof(name), "/section_view_%02d", i);
        if (!archives->HasFile(path + name)) {
            break;
        }
        // gSPDisplayList only resolves resource names carrying the OTR signature
        mSectionViewNames.push_back("__OTR__" + path + name);
    }
    for (const std::string& name : mSectionViewNames) {
        if (gIsMirrorMode != 0) {
            CustomTrack::InvertTriangleWindingModdedByName(name.c_str() + strlen("__OTR__"));
        }
        // One list per camera direction in the section table; use the same list for all four
        for (int direction = 0; direction < 4; direction++) {
            mSectionViews.push_back(name.c_str());
        }
    }
    if (!mSectionViews.empty()) {
        printf("[CustomTrack] Using %zu section view lists\n", mSectionViewNames.size());
    }
    mSectionViewXluNames.clear();
    mSectionViewsXlu.clear();
    for (int i = 1; i < 100; i++) {
        char name[40];
        snprintf(name, sizeof(name), "/section_view_xlu_%02d", i);
        if (!archives->HasFile(path + name)) {
            break;
        }
        mSectionViewXluNames.push_back("__OTR__" + path + name);
    }
    for (const std::string& name : mSectionViewXluNames) {
        if (gIsMirrorMode != 0) {
            CustomTrack::InvertTriangleWindingModdedByName(name.c_str() + strlen("__OTR__"));
        }
        for (int direction = 0; direction < 4; direction++) {
            mSectionViewsXlu.push_back(name.c_str());
        }
    }
    if (mSectionViewsXlu.size() != mSectionViews.size()) {
        mSectionViewsXlu.clear(); // translucent lists only make sense next to the opaque ones
    }

    Track::Init(); // Base class

    /** Generate Collision Mesh **/
    CustomTrack::ParseMeshForCollision(meshData, numMesh);
    func_80295C6C(); // generate collision grid

    /** Dumb hack that needs to be removed **/
    if (Props.WaterLevel == FLT_MAX) {
        Props.WaterLevel = gTrackMinY - 10.0f;
    }
}

void CustomTrack::InvertTriangleWindingModdedByName(const char* name) {
    if (name == nullptr) {
        return;
    }
    Gfx* gfx = (Gfx*)ResourceGetDataByName(name);
    InvertTriangleWindingModdedInternal(gfx, name);
}

// C++ version of parse_track_displaylists()
void CustomTrack::ParseMeshForCollision(TrackSections* sections, size_t numMesh) {
    printf("\n[Track] Generating Collision Meshes...\n");

    // Set configurations to zero
    D_8015F59C = 0;
    D_8015F5A0 = 0;
    D_8015F5A4 = 0;

    for (size_t i = 0; i < numMesh; i++) {
        switch(static_cast<SurfaceClip>(sections[i].clip)) {
            case SurfaceClip::CLIP_NONE:
                continue;
            case SurfaceClip::CLIP_SINGLE_SIDED_WALL:
                D_8015F59C = 1;
                break;
            case SurfaceClip::CLIP_SURFACE:
                D_8015F5A0 = 1;
                break;
            case SurfaceClip::CLIP_DOUBLE_SIDED_WALL:
                D_8015F5A4 = 1;
                break;
        }

        const char* name = ResourceGetNameByCrc(sections[i].crc);
        printf("  %s\n", name);
        generate_collision_mesh((Gfx*) ResourceGetDataByCrc(sections[i].crc), sections[i].surfaceType,
                                sections[i].sectionId);
    }
    printf("[Track] Collision Mesh Generation Complete!\n\n");
}

void CustomTrack::TestPath() {
    // DEBUG ONLY TO VISUALIZE PATH
    return;
    s16 x;
    s16 y;
    s16 z;
    Vec3s rot = { 0, 0, 0 };
    Vec3f vel = { 0, 0, 0 };

    for (size_t i = 0; i < gPathCountByPathIndex[0]; i++) {
        x = gTrackPaths[0][i].x;
        y = gTrackPaths[0][i].y;
        z = gTrackPaths[0][i].z;

        if (((x & 0xFFFF) == 0x8000) && ((y & 0xFFFF) == 0x8000) && ((z & 0xFFFF) == 0x8000)) {
            break;
        }

        f32 height = spawn_actor_on_surface(x, 2000.0f, z);
        Vec3f itemPos = { (f32) x, height, (f32) z };
        add_actor_to_empty_slot(itemPos, rot, vel, ACTOR_ITEM_BOX);
    }
}

void CustomTrack::BeginPlay() {
    mInitialSky = Props.Skybox;
    mWeatherTick = 0;
    mRaceScreenFrame = UINT32_MAX;
    printf("[Track] BeginPlay\n");
    CustomTrack::TestPath();
    this->SpawnActors();
}

void CustomTrack::Draw(ScreenContext* arg0) {
    UpdateRaceScreens();
    gSPSetGeometryMode(gDisplayListHead++, G_SHADING_SMOOTH);
    gSPClearGeometryMode(gDisplayListHead++, G_LIGHTING);

    if (bFog) {
        gDPSetCycleType(gDisplayListHead++, G_CYC_2CYCLE);
        gDPSetRenderMode(gDisplayListHead++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2);
        gSPSetGeometryMode(gDisplayListHead++, G_FOG);

        gDPSetFogColor(gDisplayListHead++, gFogColour.r, gFogColour.g, gFogColour.b, gFogColour.a);
        gSPFogPosition(gDisplayListHead++, gFogMin, gFogMax);
        gDPPipeSync(gDisplayListHead++);
    } else {
        gSPClearGeometryMode(gDisplayListHead++, G_FOG);
    }

    set_track_light_direction(D_800DC610, D_802B87D4, 0, 1);
    gSPTexture(gDisplayListHead++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gSPSetGeometryMode(gDisplayListHead++, G_SHADING_SMOOTH);

    if (func_80290C20(arg0->camera) == 1) {
        gDPSetCombineMode(gDisplayListHead++, G_CC_SHADE, G_CC_SHADE);
        gDPSetRenderMode(gDisplayListHead++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    }

    const TrackInfo* info = gTrackRegistry.GetInfo(ResourceName);
    if (nullptr == info) {
        printf("[Track] [Draw] Resource name did not return a valid TrackInfo %s\n", ResourceName.c_str());
        return;
    }
    std::string res = info->Path + "/data_track_sections";

    TrackSections* sections = (TrackSections*) LOAD_ASSET_RAW(res.c_str());
    size_t size = ResourceGetSizeByName(res.c_str());
    size_t totalSections = size / sizeof(TrackSections);
    if (!mSectionViews.empty()) {
        render_track_sections(mSectionViews.data(), arg0, mSectionViews.size());
    } else {
        for (uint64_t item : mOpaqueItems) {
            gSPDisplayList(gDisplayListHead++, (Gfx*) ResourceGetDataByCrc(item));
        }
    }
    Objects.Draw(arg0, false);
}

void CustomTrack::ApplyCloudSet() {
    Props.Clouds = NULL;
    Props.CloudList = NULL;
    if (CloudSet.empty()) {
        return;
    }
    mCloudType = CloudType::CLOUDS;
    if (CloudSet == "koopa_troopa_beach") {
        Props.CloudTexture = (u8*) gTextureExhaust3;
        Props.Clouds = gKoopaTroopaBeachClouds;
    } else if (CloudSet == "sherbet_land") {
        Props.CloudTexture = (u8*) gTextureExhaust1;
        Props.Clouds = gSherbetLandClouds;
    } else if (CloudSet == "moo_moo_farm") {
        Props.CloudTexture = (u8*) gTextureExhaust0;
        Props.Clouds = gYoshiValleyMooMooFarmClouds;
    } else if (CloudSet == "royal_raceway") {
        Props.CloudTexture = (u8*) gTextureExhaust4;
        Props.Clouds = gRoyalRacewayClouds;
    } else if (CloudSet == "luigi_raceway") {
        Props.CloudTexture = (u8*) gTextureExhaust2;
        Props.Clouds = gLuigiRacewayClouds;
    } else if (CloudSet == "kalimari_desert") {
        Props.CloudTexture = (u8*) gTextureExhaust5;
        Props.Clouds = gKalimariDesertClouds;
    } else {
        printf("[CustomTrack] Unknown CloudSet %s\n", CloudSet.c_str());
        return;
    }
    Props.CloudList = Props.Clouds;
    printf("[CustomTrack] Using cloud set %s\n", CloudSet.c_str());
}

static void StepScrollOffset(int32_t& offset, int16_t speed) {
    if (speed == 0) {
        return;
    }
    offset += speed;
    if (speed > 0 && offset > 255) {
        offset = 0;
    } else if (speed < 0 && offset < 0) {
        offset = 255;
    }
}

void CustomTrack::Tick() {
    ++mWeatherTick;
    if (LapSkyTransition) {
        UpdateLapSky();
        bool storm = false;
        for (int i = 0; i < NUM_RACERS; ++i) {
            if ((gPlayers[i].type & (PLAYER_EXISTS | PLAYER_HUMAN)) == (PLAYER_EXISTS | PLAYER_HUMAN) &&
                gLapCountByPlayerId[i] == 1) { storm = true; break; }
        }
        if (!storm) { mLightningCountdown = 0; mLightningRemaining = 0; }
        else if (mLightningRemaining > 0) --mLightningRemaining;
        else if (mLightningCountdown > 0) --mLightningCountdown;
        else {
            // The ROM uses 45 30 Hz updates. Trigger intervals are an approximation;
            // the ROM's trigger is coupled to its music-channel state.
            mLightningRemaining = 90;
            mLightningCountdown = 480 + (int)((mWeatherTick * 1103515245u) % 480u);
        }
    }
    Objects.Tick();
    for (ScrollMaterial& m : ScrollMaterials) {
        StepScrollOffset(m.OffS, m.S);
        StepScrollOffset(m.OffT, m.T);
        Gfx* gfx = (Gfx*) ResourceGetDataByName(m.Path.c_str());
        if (gfx != nullptr) {
            if (ResourceName == "amped:course_09" || ResourceName == "amped:course_16" ||
                (ResourceName == "amped:course_12" &&
                 (m.Name == "material_4be8" || m.Name == "material_4f18" || m.Name == "material_54f0"))) {
                // Scrolling neon arrows need a fixed tile window: moving only
                // its upper corner shrinks the texture and distorts the rails.
                for (Gfx* cmd = gfx; GFX_GET_OPCODE(cmd->words.w0) != (G_ENDDL << 24); ++cmd) {
                    if (GFX_GET_OPCODE(cmd->words.w0) != (G_SETTILESIZE << 24)) continue;
                    const uint32_t uls = (cmd->words.w0 >> 12) & 0xFFF;
                    const uint32_t ult = cmd->words.w0 & 0xFFF;
                    const uint32_t width = ((cmd->words.w1 >> 12) & 0xFFF) - uls;
                    const uint32_t height = (cmd->words.w1 & 0xFFF) - ult;
                    cmd->words.w0 = (G_SETTILESIZE << 24) | (m.OffS << 12) | m.OffT;
                    cmd->words.w1 = (cmd->words.w1 & 0xFF000000) |
                                    ((m.OffS + width) << 12) | (m.OffT + height);
                    break;
                }
            } else {
                find_and_set_tile_size((uintptr_t) gfx, m.OffS, m.OffT);
            }
        }
    }
}

void CustomTrack::UpdateLapSky() {
    int lap = 0;
    for (int i = 0; i < NUM_RACERS; ++i) {
        if ((gPlayers[i].type & (PLAYER_EXISTS | PLAYER_HUMAN)) == (PLAYER_EXISTS | PLAYER_HUMAN)) {
            lap = std::max(lap, gLapCountByPlayerId[i]);
        }
    }
    const bool night = lap == 1;
    const auto blend = [](RGB8& colour, RGB8 target) {
        const auto step = [](int value, int goal) -> uint8_t {
            return static_cast<uint8_t>(value + std::clamp(goal - value, -3, 3));
        };
        colour = { step(colour.r, target.r), step(colour.g, target.g), step(colour.b, target.b) };
    };
    auto& sky = Props.Skybox;
    blend(sky.TopLeft, night ? RGB8{ 9, 32, 112 } : mInitialSky.TopLeft);
    blend(sky.TopRight, night ? RGB8{ 9, 32, 112 } : mInitialSky.TopRight);
    blend(sky.BottomLeft, night ? RGB8{ 0, 48, 32 } : mInitialSky.BottomLeft);
    blend(sky.BottomRight, night ? RGB8{ 0, 48, 32 } : mInitialSky.BottomRight);
}

void CustomTrack::UpdateRaceScreens() {
    if (RaceScreenTextures[0].empty() || mRaceScreenFrame == gGlobalTimer ||
        (gActiveScreenMode != SCREEN_MODE_1P && !CVarGetInteger("gMultiplayer.LuigiRacewayScreen", 1))) {
        return;
    }
    mRaceScreenFrame = gGlobalTimer;
    const auto* source = gPortFramebuffers[(sRenderedFramebuffer + 2) % 3];
    for (size_t i = 0; i < RaceScreenTextures.size(); ++i) {
        const char* texture = RaceScreenTextures[i].c_str();
        if (ResourceGetTexWidthByName(texture) != 64 || ResourceGetTexHeightByName(texture) != 32) continue;
        auto* target = static_cast<u16*>(LOAD_ASSET_RAW(texture));
        if (!target) continue;
        const int x = 88 + (i % 2) * 64;
        const int y = 72 + (i / 2) * 32;
        for (int row = 0; row < 32; ++row) {
            std::copy_n(source + (y + row) * SCREEN_WIDTH + x, 64, target + row * 64);
        }
        gSPInvalidateTexCache(gDisplayListHead++, (uintptr_t) texture);
    }
}

// Detect a collision roof above this camera using the track's existing spatial grid.
static bool ShrineHasRoof(const Camera* camera, const std::vector<std::array<float, 9>>& roofs) {
    const float x=camera->pos[0], y=camera->pos[1], z=camera->pos[2];
    const float localX = gIsMirrorMode ? -x : x;
    for (const auto& t : roofs) {
        const float den=(t[5]-t[8])*(t[0]-t[6])+(t[6]-t[3])*(t[2]-t[8]);
        if (fabsf(den)<0.001f) continue;
        const float u=((t[5]-t[8])*(localX-t[6])+(t[6]-t[3])*(z-t[8]))/den;
        const float v=((t[8]-t[2])*(localX-t[6])+(t[0]-t[6])*(z-t[8]))/den;
        if (u<0 || v<0 || u+v>1) continue;
        const float height=u*t[1]+v*t[4]+(1-u-v)*t[7];
        if (height>y+2 && height<y+400) return true;
    }
    const int cellX=(gTrackMaxX-gTrackMinX)/GRID_SIZE, cellZ=(gTrackMaxZ-gTrackMinZ)/GRID_SIZE;
    if (!gCollisionIndices || !gCollisionMesh || cellX<=0 || cellZ<=0 || x<gTrackMinX || z<gTrackMinZ) return false;
    const int ix=(int)((x-gTrackMinX)/cellX), iz=(int)((z-gTrackMinZ)/cellZ);
    if (ix<0 || iz<0 || ix>=GRID_SIZE || iz>=GRID_SIZE) return false;
    const auto& cell=gCollisionGrid[iz*GRID_SIZE+ix];
    for (u32 i=0; i<cell.numTriangles; ++i) {
        const u16 index=gCollisionIndices[cell.triangle+i];
        if (index>=gCollisionMeshCount) continue;
        const auto& t=gCollisionMesh[index];
        if (!t.vtx1 || !t.vtx2 || !t.vtx3 || fabsf(t.normalY)<0.35f || x<t.minX || x>t.maxX || z<t.minZ || z>t.maxZ) continue;
        const auto* a=t.vtx1->v.ob; const auto* b=t.vtx2->v.ob; const auto* c=t.vtx3->v.ob;
        const float den=(b[2]-c[2])*(a[0]-c[0])+(c[0]-b[0])*(a[2]-c[2]);
        if (fabsf(den)<0.001f) continue;
        const float u=((b[2]-c[2])*(x-c[0])+(c[0]-b[0])*(z-c[2]))/den;
        const float v=((c[2]-a[2])*(x-c[0])+(a[0]-c[0])*(z-c[2]))/den;
        if (u<0 || v<0 || u+v>1) continue;
        const float h=u*a[1]+v*b[1]+(1-u-v)*c[1];
        if (h>y+2 && h<y+400) return true;
    }
    return false;
}

void CustomTrack::DrawWeather(const ScreenContext* screen) {
    const bool submerged = UnderwaterTint && screen->camera->pos[1] < Props.WaterLevel;
    bool shrineNight = false;
    if (LapSkyTransition) {
        for (int i = 0; i < NUM_RACERS; ++i) {
            if ((gPlayers[i].type & (PLAYER_EXISTS | PLAYER_HUMAN)) == (PLAYER_EXISTS | PLAYER_HUMAN) &&
                gLapCountByPlayerId[i] == 1) { shrineNight = true; break; }
        }
    }
    if (!Rain && !submerged && !shrineNight) return;

    const int left = std::max(0, screen->screenStartX - screen->screenWidth / 2);
    const int top = std::max(0, screen->screenStartY - screen->screenHeight / 2);
    const int right = std::min(SCREEN_WIDTH, screen->screenStartX + screen->screenWidth / 2);
    const int bottom = std::min(SCREEN_HEIGHT, screen->screenStartY + screen->screenHeight / 2);
    const int width = right - left;
    const int height = bottom - top;
    if (width < 2 || height < 2) return;

    gDPPipeSync(gDisplayListHead++);
    gSPDisplayList(gDisplayListHead++, (Gfx*) D_02008008);
    if (shrineNight) {
        // Match the lap-two darkening while retaining the existing track colors.
        const bool flash = mLightningRemaining > 0 && ((mLightningRemaining / 4) % 3 == 0);
        if (flash) gDPSetPrimColor(gDisplayListHead++, 0, 0, 205, 218, 255, 76);
        else gDPSetPrimColor(gDisplayListHead++, 0, 0, 8, 16, 42, 100);
        const int tintLeft = left == 0 ? OTRGetRectDimensionFromLeftEdge(0) : left;
        const int tintRight = right == SCREEN_WIDTH ? OTRGetRectDimensionFromRightEdge(SCREEN_WIDTH) : right;
        gDPFillWideRectangle(gDisplayListHead++, tintLeft, top, tintRight, bottom);
    }
    if (submerged) {
        gDPSetPrimColor(gDisplayListHead++, 0, 0, 20, 76, 175, 96);
        const int tintLeft = left == 0 ? OTRGetRectDimensionFromLeftEdge(0) : left;
        const int tintRight = right == SCREEN_WIDTH ? OTRGetRectDimensionFromRightEdge(SCREEN_WIDTH) : right;
        gDPFillWideRectangle(gDisplayListHead++, tintLeft, top, tintRight, bottom);
    }
    if ((Rain || shrineNight) && !((LapSkyTransition || !RainRoofs.empty()) && ShrineHasRoof(screen->camera, RainRoofs))) {
        gDPSetPrimColor(gDisplayListHead++, 0, 0, 180, 205, 225, 86);
        const int drops = std::max(4, width * height / 1200);
        for (int i = 0; i < drops; ++i) {
            const uint32_t seed = (i + 1u) * 0x9E3779B9u;
            const int x = left + (seed ^ (mWeatherTick / 24 + i) * 0x45D9F3Bu) % (width - 1);
            const int y = top + ((seed >> 16) + mWeatherTick * (4u + (seed & 3u))) % height;
            const int end = std::min(bottom, y + 4 + static_cast<int>(seed & 3u));
            gDPFillRectangle(gDisplayListHead++, x, y, x + 1, end);
        }
    }
    gDPPipeSync(gDisplayListHead++);
}

void CustomTrack::DrawTransparency(ScreenContext* screen, uint16_t pathCounter, uint16_t cameraRot, uint16_t playerDirection) {
    // Original translucent table (lake water): drawn after the karts so it blends over anything submerged.
    if (!mSectionViewsXlu.empty()) {
        gSPSetGeometryMode(gDisplayListHead++, G_ZBUFFER | G_SHADING_SMOOTH);
        gSPClearGeometryMode(gDisplayListHead++, G_LIGHTING);
        if (bFog) {
            gDPSetCycleType(gDisplayListHead++, G_CYC_2CYCLE);
            gSPSetGeometryMode(gDisplayListHead++, G_FOG);
            gDPSetFogColor(gDisplayListHead++, gFogColour.r, gFogColour.g, gFogColour.b, gFogColour.a);
            gSPFogPosition(gDisplayListHead++, gFogMin, gFogMax);
        } else {
            gSPClearGeometryMode(gDisplayListHead++, G_FOG);
        }
        gSPTexture(gDisplayListHead++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        render_track_sections(mSectionViewsXlu.data(), screen, mSectionViewsXlu.size());
    }

    FVector cam = { screen->camera->pos[0], screen->camera->pos[1], screen->camera->pos[2] };

    std::sort(mTranslucentItems.begin(), mTranslucentItems.end(),
            [&](TrackSections a, TrackSections b) {
        FVector posA = FVector(a.location[0], a.location[1], a.location[2]);
        FVector posB = FVector(b.location[0], b.location[1], b.location[2]);;

        float distA = (posA - cam).Square();
        float distB = (posB - cam).Square();

        return distA > distB; // farthest first
    });

    for (TrackSections& item : mTranslucentItems) {
        Mat4 matrix;
        FVector pos = {item.location[0], item.location[1], item.location[2]};
        FrameInterpolation_RecordOpenChild("translucent_obj", (item.crc << 4) | screen - gScreenContexts);
        ApplyMatrixTransformations(matrix, pos, IRotator(0, 0, 0), FVector(1, 1, 1));
        AddObjectMatrix(matrix, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(gDisplayListHead++, (Gfx*) ResourceGetDataByCrc(item.crc));
        FrameInterpolation_RecordCloseChild();
    }

    gSPClearGeometryMode(gDisplayListHead++, G_ZBUFFER);
    for (TrackSections& item : mTranslucentNoZBufferItems) {
        Mat4 matrix;
        FVector pos = {item.location[0], item.location[1], item.location[2]};
        FrameInterpolation_RecordOpenChild("translucent_no_z", (item.crc << 4) | screen - gScreenContexts);
        ApplyMatrixTransformations(matrix, pos, IRotator(0, 0, 0), FVector(1, 1, 1));
        AddObjectMatrix(matrix, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(gDisplayListHead++, (Gfx*) ResourceGetDataByCrc(item.crc));
        FrameInterpolation_RecordCloseChild();
    }
    gSPSetGeometryMode(gDisplayListHead++, G_ZBUFFER);
    Objects.Draw(screen, true);
    DrawWeather(screen);
}

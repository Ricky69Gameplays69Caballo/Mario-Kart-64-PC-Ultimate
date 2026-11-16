#pragma once
#include <libultraship/libultraship.h>
#include <string>
#include <array>
#include <vector>
#include "Track.h"
#include "AmpedObjects.h"

class CustomTrack : public Track {
public:
    // Texture scrolling read from scene.json "ScrollMaterials" (OverKart64 / Tarmac64 scroll data).
    // Every tick the material's tile offset moves by S/T (1/4 texel units) and wraps at 255,
    // like the stock Koopa Troopa Beach water.
    struct ScrollMaterial {
        std::string Name; // material resource name inside the track folder, e.g. "material_36d0"
        int16_t S = 0;
        int16_t T = 0;
        int32_t OffS = 0;
        int32_t OffT = 0;
        std::string Path; // resolved resource path
    };
    std::vector<ScrollMaterial> ScrollMaterials;
    // Stock cloud layout + image to borrow, from scene.json "CloudSet" (Amped Up picks one per course)
    std::string CloudSet;
    bool Rain = false;
    bool UnderwaterTint = false;
    bool LapSkyTransition = false;
    std::vector<std::array<float, 9>> RainRoofs;
    std::array<std::string, 6> RaceScreenTextures;
    // Course objects from scene.json "AmpedObjects"
    AmpedObjectSet Objects;

    explicit CustomTrack();

    virtual void Load() override;
    virtual void BeginPlay() override;
    virtual void Draw(ScreenContext*) override;
    virtual void DrawTransparency(ScreenContext* screen, uint16_t pathCounter, uint16_t cameraRot, uint16_t playerDirection) override;
    virtual void Tick() override;
    virtual bool IsMod() override { return true; }
    void InvertTriangleWindingModdedByName(const char* name);
    void ParseMeshForCollision(TrackSections* sections, size_t numMesh);
    void TestPath();
    void ApplyCloudSet();

private:
    void UpdateRaceScreens();
    void DrawWeather(const ScreenContext* screen);
    void UpdateLapSky();
    SkyboxColours mInitialSky{};
    uint32_t mWeatherTick = 0;
    int mLightningCountdown = 0;
    int mLightningRemaining = 0;
    uint32_t mRaceScreenFrame = UINT32_MAX;

protected:

enum class SurfaceClip : uint16_t {
    CLIP_NONE,
    CLIP_DEFAULT,
    CLIP_SINGLE_SIDED_WALL,
    CLIP_SURFACE,
    CLIP_DOUBLE_SIDED_WALL
};

enum class DrawLayer : uint16_t {
    DRAW_INVISIBLE,
    DRAW_OPAQUE,
    DRAW_TRANSLUCENT,
    DRAW_TRANSLUCENT_NO_ZBUFFER
};
    // Containers that hold meshes that get drawn
    std::vector<uint64_t> mOpaqueItems;
    std::vector<TrackSections> mTranslucentItems;
    std::vector<TrackSections> mTranslucentNoZBufferItems;
    // Optional per-section view lists (section_view_NN), drawn like stock tracks' section tables
    std::vector<std::string> mSectionViewNames;
    std::vector<const char*> mSectionViews;
    // Translucent section chunks (section_view_xlu_NN, e.g. lake water): drawn after the karts, in DrawTransparency
    std::vector<std::string> mSectionViewXluNames;
    std::vector<const char*> mSectionViewsXlu;
};

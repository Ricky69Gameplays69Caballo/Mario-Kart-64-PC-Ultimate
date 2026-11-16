#pragma once
// Course objects for imported Amped Up tracks, read from scene.json "AmpedObjects" (written by
// work/build-amped-objects.py). Draws baked models / camera-facing sprites and runs the behaviours
// recovered from the original game: traffic on the racing path, walkers, steam vents, fire jets,
// bouncing apples and contact hazards.
#include <libultraship/libultraship.h>
#include <nlohmann/json.hpp>
#include <array>
#include <string>
#include <vector>
#include "Track.h" // ScreenContext
#include "common_structs.h" // struct Collision
#include "defines.h"

class AmpedObjectSet {
public:
    void Load(const nlohmann::json& list, const std::string& trackPath);
    void Clear();
    void Tick();
    void Draw(ScreenContext* screen, bool translucent);
    bool Empty() const { return mObjects.empty(); }

private:
    enum class Behaviour { NONE, WALKER, PATROL, POKEY, SANDY_BALL, CARNIVAL_FAN, CARNIVAL_TEETH, MINECART, CAR, APPLE, GUST, GORDO, SPINNING, VENT, FIREJET, CHOMP, BOULDER, SCUTTLEBUG, MIST, GATE, SHRINE_ENEMY, SHRINE_GHOST, SHRINE_FISH, NEO_BOWSER, NEO_ROTOR, NEO_SWIRL };
    enum class Hit { NONE, SPIN, TUMBLE, HIT, SOLID, SQUISH };

    struct Obj {
        std::vector<std::string> frames; // "__OTR__tracks/amped_NN/obj_<model>[_fNN]"
        int frameTicks = 2;
        float pos[3] = { 0, 0, 0 };
        float home[3] = { 0, 0, 0 };
        uint16_t yaw = 0;
        uint16_t roll = 0, rollSpeed = 0; // spin about the model's own z axis
        uint16_t pitch = 0;
        uint16_t yawOffset = 0; // added to the logic yaw when drawing
        uint16_t homeYaw = 0;
        int countdown = 0;
        int splashTicks = 0;
        std::array<std::string, 4> splashModels;
        float splashPos[3] = {};
        int targetPlayer = -1;
        struct ShrineShot { float pos[3]{}; float vel[3]{}; int life = 0; };
        std::array<ShrineShot, 7> shrineShots{};
        std::vector<std::string> shrineAttackFrames;
        std::string shrineShotModel;
        std::array<std::string, 2> minecartRider;
        std::string minecartDebrisModel;
        std::string scuttleBody;
        struct CartDebris { float pos[3]{}; float vx = 0.0f, vy = 0.0f, vz = 0.0f, scale = 0.2f; int life = 0, impactTicks = 0; uint16_t roll = 0; uint8_t green = 180; };
        uint32_t cartRandom = 0x91e10da5u;
        std::array<CartDebris, 64> minecartDebris{};

        bool nightOnly = false;
        bool noCull = false;       // draw at any distance (sky dome)
        int ccMask = 0xF;          // engine classes (bit = gCCSelection) the object exists in
        float vel[3] = { 0, 0, 0 }; // boulders
        struct Collision col {};
        bool sway = false; // GCN trees: shared roll sway + vertical stretch (0x80454c60)
        bool ground = false; // height follows the track surface
        float scale = 1.0f;
        float drawYOffset = 0.0f;
        float stretch = 1.0f;
        bool billboard = false;
        bool reflectModel = false;
        bool translucent = false;
        bool hidden = false;
        uint8_t colour[4] = { 255, 255, 255, 255 };
        Behaviour behaviour = Behaviour::NONE;
        Hit hit = Hit::NONE;
        float radius = 0.0f;
        float hitBelow = 0.0f;
        float hitPush = 0.0f;
        float orbitRadius = 0.0f;
        uint16_t orbitYaw = 0;
        uint16_t orbitRate = 270;
        float billboardOffset = 0.0f;
        float speed = 1.0f;
        std::array<float, 3> speedCC{}; // per engine class (50cc / 100cc+ / 150cc), when hasSpeedCC
        bool hasSpeedCC = false;
        std::vector<std::array<float, 3>> path;
        std::vector<std::array<float, 3>> contactVertices;
        size_t target = 1;
        int dir = 1;
        bool tree = false; // a Star kart knocks it down
        bool loop = false; // walk the path as a closed loop instead of back and forth
        float lift = 0.0f;
        int size = 10;
        float lane = 0.0f;
        int pathPoint = 0;
        int16_t waypoint = 0;
        bool started = false;
        int timer = 0;
        int state = 0;
        int counter = 0;
        float vy = 0.0f;
        int knocked = 0; // ticks until a knocked-away object comes back
        std::array<uint8_t, NUM_RACERS> cooldown{};
        const char* smoke = nullptr; // particle model used by vents / fire jets
        int smokeFrames = 1;
    };
    struct Particle {
        const Obj* source;
        float pos[3];
        float vy;
        float scale;
        int age;
        int life;
    };

    void Contact(Obj& o, float heightRange);
    void Emit(const Obj& o, float vy, float scale, int life);

    std::vector<Obj> mObjects;
    std::vector<Particle> mParticles;
    uint32_t mTick = 0;
    uint16_t mSway = 0;
};

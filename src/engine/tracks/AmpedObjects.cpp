#include "AmpedObjects.h"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>
#include <libultra/gbi.h>
#include "engine/Matrix.h"
#include "engine/CoreMath.h"
#include "port/interpolation/FrameInterpolation.h"
#include "port/Game.h"

extern "C" {
#include "macros.h"
#include "sounds.h"
#include "audio/external.h"
#include "racing/actors.h"
#include "main.h"
#include "defines.h"
#include "camera.h"
#include "code_80005FD0.h"
#include "waypoints.h"
#include "effects.h"
#include "racing/collision.h"
#include "racing/math_util.h"
#include "code_800029B0.h" // gWaterLevel
}

namespace {
std::array<int, NUM_RACERS> sHazyScorch{};
uint16_t YawTowards(float dx, float dz) {
    return (uint16_t) (int32_t) (std::atan2(dx, dz) * (32768.0 / M_PI));
}

// The original type-27 actor starts at 270, then reloads by engine class.
int WhispyCountdown(int ticks) {
    const int age = ticks / 2;
    if (age < 270) return 270 - age;
    const int cycle = gCCSelection == CC_50 ? 450 : gCCSelection == CC_150 ? 350 : 400;
    return cycle - (age - 270) % cycle;
}
} // namespace

extern "C" int AmpedIsScorched(int playerIndex) {
    return playerIndex >= 0 && playerIndex < NUM_RACERS && sHazyScorch[playerIndex] > 0;
}

void AmpedObjectSet::Clear() {
    sHazyScorch.fill(0);
    mObjects.clear();
    mParticles.clear();
    mTick = 0;
}

void AmpedObjectSet::Load(const nlohmann::json& list, const std::string& trackPath) {
    Clear();
    if (!list.is_array()) {
        return;
    }
    for (const auto& j : list) {
        try {
            Obj o;
            const std::string model = j.at("Model").get<std::string>();
            const int frames = j.value("Frames", 1);
            const std::string base = "__OTR__" + trackPath + "/obj_" + model;
            if (frames <= 1) {
                o.frames.push_back(base);
            } else {
                for (int i = 0; i < frames; i++) {
                    char suffix[8];
                    snprintf(suffix, sizeof(suffix), "_f%02d", i);
                    o.frames.push_back(base + suffix);
                }
            }
            for (int i = 0; i < 3; i++) {
                o.pos[i] = o.home[i] = j.at("Pos")[i].get<float>();
            }
            o.frameTicks = std::max(1, j.value("FrameTicks", 2));
            o.yaw = (uint16_t) j.value("Yaw", 0);
            o.rollSpeed = (uint16_t) j.value("RollSpeed", 0);
            o.pitch = (uint16_t) j.value("Pitch", 0);
            o.roll = (uint16_t) j.value("Roll", 0);
            o.sway = j.value("Sway", false);
            // This aligns the baked model with its heading in either layout.
            o.yawOffset = (uint16_t) j.value("YawOffset", 0);
            o.homeYaw = o.yaw;
            o.ccMask = j.value("CCMask", 0xF);
            o.nightOnly = j.value("NightOnly", false);
            o.noCull = j.value("NoCull", false);
            o.ground = j.value("Ground", false);
            o.scale = j.value("Scale", 1.0f);
            o.drawYOffset = j.value("DrawYOffset", 0.0f);
            o.stretch = j.value("DrawScaleY", 1.0f);
            o.billboard = j.value("Billboard", false);
            o.billboardOffset = j.value("BillboardOffset", 0.0f);
            o.reflectModel = gIsMirrorMode && j.value("MirrorModel", false);
            o.translucent = j.value("Translucent", false);
            o.hidden = j.value("Hidden", false);
            if (j.contains("Colour")) {
                for (int i = 0; i < 4; i++) {
                    o.colour[i] = (uint8_t) j["Colour"][i].get<int>();
                }
            }
            const std::string behaviour = j.value("Behaviour", "");
            if (behaviour == "walker") o.behaviour = Behaviour::WALKER;
            else if (behaviour == "patrol") o.behaviour = Behaviour::PATROL;
            else if (behaviour == "pokey") { o.behaviour = Behaviour::POKEY; o.counter = std::max(1, j.value("Size", 4)); }
            else if (behaviour == "sandy_ball") { o.behaviour = Behaviour::SANDY_BALL; func_802AAAAC(&o.col); }
            else if (behaviour == "carnival_fan") o.behaviour = Behaviour::CARNIVAL_FAN;
            else if (behaviour == "carnival_teeth") { o.behaviour = Behaviour::CARNIVAL_TEETH; o.countdown = std::max(0, j.value("InitialDelay", 0)); }
            else if (behaviour == "minecart") {
                o.behaviour = Behaviour::MINECART;
                o.minecartDebrisModel = "__OTR__" + trackPath + "/obj_cart_debris";
                // Type 19 draw 0x80455f64 uses miner sprite 0x80711ba0.
                for (int i = 0; i < 2; ++i)
                    o.minecartRider[i] = "__OTR__" + trackPath + "/obj_miner17_f0" + std::to_string(i);
            }
            else if (behaviour == "car") o.behaviour = Behaviour::CAR;
            else if (behaviour == "apple") o.behaviour = Behaviour::APPLE;
            else if (behaviour == "gust") o.behaviour = Behaviour::GUST;
            else if (behaviour == "gordo") o.behaviour = Behaviour::GORDO;
            else if (behaviour == "spinning") o.behaviour = Behaviour::SPINNING;
            else if (behaviour == "neo_bowser") o.behaviour = Behaviour::NEO_BOWSER;
            else if (behaviour == "neo_rotor") o.behaviour = Behaviour::NEO_ROTOR;
            else if (behaviour == "neo_swirl") o.behaviour = Behaviour::NEO_SWIRL;
            else if (behaviour == "vent") o.behaviour = Behaviour::VENT;
            else if (behaviour == "firejet") o.behaviour = Behaviour::FIREJET;
            else if (behaviour == "chomp") o.behaviour = Behaviour::CHOMP;
            else if (behaviour == "boulder") o.behaviour = Behaviour::BOULDER;
            else if (behaviour == "scuttlebug") {
                o.behaviour = Behaviour::SCUTTLEBUG;
                o.scuttleBody = "__OTR__" + trackPath + "/obj_scuttle_body";
            }
            else if (behaviour == "mist") o.behaviour = Behaviour::MIST;
            else if (behaviour == "gate") o.behaviour = Behaviour::GATE;
            else if (behaviour == "shrine_fish") {
                o.behaviour = Behaviour::SHRINE_FISH;
                for (int i=0;i<4;++i) {
                    char suffix[12]; snprintf(suffix,sizeof(suffix),"_f%02d",i);
                    o.splashModels[i] = "__OTR__" + trackPath + "/obj_shrine_fish_splash" + suffix;
                }
                o.countdown = j.value("InitialDelay", 0);
                o.size = o.countdown;
            }
            else if (behaviour == "shrine_ghost") o.behaviour = Behaviour::SHRINE_GHOST;
            else if (behaviour == "shrine_enemy") {
                o.behaviour = Behaviour::SHRINE_ENEMY;
                o.shrineShotModel = "__OTR__" + trackPath + "/obj_shrine_enemy_projectile";
                for (int i = 0; i < 120; ++i) {
                    char suffix[16]; snprintf(suffix, sizeof(suffix), "_f%02d", i);
                    o.shrineAttackFrames.push_back("__OTR__" + trackPath + "/obj_shrine_enemy_anim2" + suffix);
                }
            }
            const std::string hit = j.value("Hit", "");
            if (hit == "spin") o.hit = Hit::SPIN;
            else if (hit == "tumble") o.hit = Hit::TUMBLE;
            else if (hit == "squish") o.hit = Hit::SQUISH;
            else if (hit == "hit") o.hit = Hit::HIT;
            else if (hit == "solid") o.hit = Hit::SOLID;
            o.radius = j.value("Radius", 0.0f);
            o.hitBelow = j.value("HitBelow", 0.0f);
            o.hitPush = j.value("HitPush", 0.0f);
            o.orbitRadius = j.value("OrbitRadius", 0.0f);
            o.orbitYaw = (uint16_t) j.value("OrbitYaw", 0);
            o.orbitRate = (uint16_t) j.value("OrbitRate", 270);
            o.speed = j.value("Speed", 1.0f);
            o.lift = j.value("Lift", 0.0f);
            o.size = o.behaviour == Behaviour::SHRINE_FISH ? j.value("InitialDelay", 0) : j.value("Size", 10);
            o.lane = j.value("Lane", 0.0f);
            o.pathPoint = j.value("PathPoint", 0);
            o.loop = j.value("Loop", false);
            o.tree = j.value("Tree", false);
            if (j.contains("SpeedCC") && j["SpeedCC"].size() == 3) {
                for (int i = 0; i < 3; i++) o.speedCC[i] = j["SpeedCC"][i].get<float>();
                o.hasSpeedCC = true;
            }
            if (j.contains("Path")) {
                for (const auto& p : j["Path"]) {
                    o.path.push_back({ p[0].get<float>(), p[1].get<float>(), p[2].get<float>() });
                }
            }
            if (gIsMirrorMode) {
                // Scene coordinates are authored for the unmirrored track.
                // Reflect the runtime spawn and private paths before movement and collision.
                o.pos[0] = o.home[0] = -o.home[0];
                for (auto& point : o.path) {
                    point[0] = -point[0];
                }
                o.yaw = o.homeYaw = -o.yaw;
                o.orbitYaw = -o.orbitYaw;
                o.roll = -o.roll;
                o.rollSpeed = -o.rollSpeed;
            }
            o.timer = (int) (mObjects.size() * 37 % 200); // desynchronise identical objects
            if (o.behaviour == Behaviour::APPLE) {
                if (j.contains("ContactVertices")) {
                    o.contactVertices = j.at("ContactVertices").get<decltype(o.contactVertices)>();
                }
                o.counter = 2 + (int) std::count_if(mObjects.begin(), mObjects.end(), [](const Obj& existing) {
                    return existing.behaviour == Behaviour::APPLE;
                });
                o.size = 364 + (o.timer * 13) % 364;
                func_802AAAAC(&o.col);
            }
            mObjects.push_back(std::move(o));
        } catch (const std::exception& e) {
            printf("[AmpedObjects] bad object entry: %s\n", e.what());
        }
    }
    mParticles.reserve(256);
    printf("[AmpedObjects] %zu objects\n", mObjects.size());
}

void AmpedObjectSet::Emit(const Obj& o, float vy, float scale, int life) {
    if (mParticles.size() >= 400) {
        return;
    }
    Particle p{ &o, { o.pos[0], o.pos[1], o.pos[2] }, vy, scale, 0, life };
    mParticles.push_back(p);
}

// Player contact. heightRange: vertical half-size of the hit volume.
void AmpedObjectSet::Contact(Obj& o, float heightRange) {
    if ((o.behaviour == Behaviour::POKEY || o.behaviour == Behaviour::SANDY_BALL) && o.hidden) return;
    if (o.hit == Hit::NONE || o.radius <= 0.0f || o.knocked > 0) {
        return;
    }
    if (o.hit == Hit::SOLID) {
        // Trees, palms and pipes: same push-out and slow-down as the game's own palm trees
        // (query_and_resolve_collision_player_actor: radius, vertical range, bounce 0.8).
        Vec3f pos = { o.pos[0], o.pos[1], o.pos[2] };
        for (int i = 0; i < NUM_RACERS; i++) {
            Player* player = &gPlayers[i];
            if (!(player->type & PLAYER_EXISTS) || (player->effects & BOO_EFFECT)) {
                continue;
            }
            if (o.cooldown[i] > 0) {
                o.cooldown[i]--;
            }
            if (o.tree && (player->effects & STAR_EFFECT)) {
                // a Star kart flattens the tree (like the original: it falls and disappears)
                float dx = player->pos[0] - o.pos[0], dz = player->pos[2] - o.pos[2];
                if (dx * dx + dz * dz < (o.radius + 12.0f) * (o.radius + 12.0f) && fabsf(player->pos[1] - o.pos[1]) < 60.0f) {
                    o.knocked = 900;
                    return;
                }
                continue;
            }
            if (query_and_resolve_collision_player_actor(player, pos, o.radius, 50.0f, 0.8f)) {
                if (o.cooldown[i] == 0 && (player->type & PLAYER_HUMAN) && !(player->type & PLAYER_INVISIBLE_OR_BOMB)) {
                    func_800C9060(i, SOUND_ARG_LOAD(0x19, 0x00, 0x70, 0x18));
                    o.cooldown[i] = 20;
                }
            }
        }
        return;
    }
    for (int i = 0; i < NUM_RACERS; i++) {
        Player* player = &gPlayers[i];
        if (!(player->type & PLAYER_EXISTS)) {
            continue;
        }
        if (o.cooldown[i] > 0) {
            o.cooldown[i]--;
            continue;
        }
        float dx = player->pos[0] - o.pos[0];
        float dz = player->pos[2] - o.pos[2];
        float dy = player->pos[1] - o.pos[1];
        if (dy < -std::max(o.hitBelow, heightRange * 0.25f) || dy > heightRange) {
            continue;
        }
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist >= o.radius) {
            continue;
        }
        bool star = (player->effects & STAR_EFFECT) != 0;
        if (o.behaviour == Behaviour::SANDY_BALL) {
            SPDLOG_INFO("Sandy ball contact: player={} horizontal={:.2f} vertical={:.2f} radius={:.2f} star={}", i + 1, dist, dy, o.radius, star);
        }
        if (player->effects & BOO_EFFECT) {
            continue;
        }
        switch (o.hit) {
            case Hit::SPIN:
                if (!star) player->triggers |= SPINOUT_TRIGGER;
                break;
            case Hit::TUMBLE:
                if (!star) {
                    player->triggers |= VERTICAL_TUMBLE_TRIGGER;
                    if (o.hitPush > 0.0f) {
                        const float awayX = dist > 0.01f ? dx / dist : -sins(-player->rotation[1]);
                        const float awayZ = dist > 0.01f ? dz / dist : -coss(-player->rotation[1]);
                        const float outwardSpeed = player->velocity[0] * awayX + player->velocity[2] * awayZ;
                        const float impulse = std::max(0.0f, o.hitPush - outwardSpeed);
                        player->velocity[0] += awayX * impulse;
                        player->velocity[2] += awayZ * impulse;
                        // Vertical tumble zeroes horizontal velocity. Separate the kart on
                        // contact as well so the tumble cannot cancel the entire push.
                        constexpr float separation = 30.0f;
                        player->pos[0] += awayX * separation;
                        player->pos[2] += awayZ * separation;
                        if (player->type & PLAYER_HUMAN) {
                            SPDLOG_INFO("Gordo hit: player={} distance={:.2f} separation={:.2f} impulse={:.2f}",
                                        i + 1, dist, separation, impulse);
                        }
                    }
                }
                break;
            case Hit::SQUISH:
                // Flattened like under a Thwomp (stock squish effect: timed, then the kart pops back up)
                if (!star && !(player->effects & HIT_EFFECT)) trigger_squish(player, (s16) (player - gPlayerOne));
                break;
            case Hit::HIT:
                // Amped: touching an enemy damages you unless you have a Star; either way it is defeated
                if (!star) player->triggers |= LOW_TUMBLE_TRIGGER;
                o.knocked = 600;
                break;
            default:
                break;
        }
        if (o.behaviour == Behaviour::POKEY && --o.counter <= 0) { o.hidden = true; return; }
        o.cooldown[i] = 60;
    }
}

void AmpedObjectSet::Tick() {
    mTick++;
    for (auto& timer : sHazyScorch) if (timer > 0) --timer;
    mSway += 0x16C; // original: +0x2D8 per 30 Hz update
    for (Obj& o : mObjects) {
        if (!((o.ccMask >> gCCSelection) & 1)) {
            o.hidden = true;
            continue;
        }
        o.timer++;
        o.roll += o.rollSpeed;
        if (o.orbitRadius > 0.0f) {
            const uint16_t angle = o.yaw + (gIsMirrorMode ? -1 : 1) * static_cast<uint16_t>(mTick * o.orbitRate);
            const float lateral = sins(angle) * o.orbitRadius;
            o.pos[0] = o.home[0] + lateral * coss(o.orbitYaw);
            o.pos[1] = o.home[1] + coss(angle) * o.orbitRadius;
            o.pos[2] = o.home[2] - lateral * sins(o.orbitYaw);
        }
        if (o.knocked > 0) {
            o.knocked--;
            continue;
        }
        switch (o.behaviour) {
            case Behaviour::SHRINE_FISH: {
                bool storm=false;
                for (int i=0;i<NUM_RACERS;++i) if ((gPlayers[i].type & (PLAYER_EXISTS | PLAYER_HUMAN)) ==
                    (PLAYER_EXISTS | PLAYER_HUMAN) && gLapCountByPlayerId[i] == 1) { storm=true; break; }
                o.hidden = !storm;
                if (!storm) { o.state=0; o.splashTicks=0; break; }
                if ((mTick & 1) == 0) {
                    if (o.splashTicks>0) --o.splashTicks;
                    if (o.state==0) {
                        if (o.countdown>0) --o.countdown;
                        else { o.state=1; o.vy=7.0f; o.counter=0; o.pos[0]=o.home[0]; o.pos[1]=o.home[1]; o.pos[2]=o.home[2]; }
                    } else if (o.state==2) {
                        if (o.countdown>0) --o.countdown;
                        else { o.state=1; o.vy=5.0f; o.counter=0; }
                    } else {
                        // The ROM starts with velocity 7, later hops with 5, and gravity -0.25.
                        const float previousY=o.pos[1];
                        o.pos[0] += sins(o.homeYaw);
                        o.pos[2] += coss(o.homeYaw);
                        o.vy -= 0.25f;
                        o.pos[1] += o.vy;
                        o.counter = std::min(9, o.counter + 1); // ROM 0x8045E918 approaches frame 9.
                        const float water=o.home[1]+22.0f;
                        if ((previousY < water && o.pos[1] >= water) ||
                            (previousY > water && o.pos[1] <= water)) {
                            o.splashTicks=16; o.splashPos[0]=o.pos[0]; o.splashPos[1]=water; o.splashPos[2]=o.pos[2];
                        }
                        if (o.vy<0) {
                            static Collision fishFloor{};
                            // Only accept a floor actually hit this frame: meshIndexZX can otherwise
                            // retain a previous road triangle while the fish is over water.
                            const u16 floorHit=check_bounding_collision(&fishFloor, 10.0f, o.pos[0], o.pos[1]+10.0f, o.pos[2]);
                            const float floor=(floorHit & FACING_Y_AXIS) ?
                                calculate_surface_height(o.pos[0], o.pos[1]+40.0f, o.pos[2], fishFloor.meshIndexZX) : -3000.0f;
                            if (floor > water+5.0f && floor < 3000.0f && previousY >= floor && o.pos[1] <= floor) {
                                o.pos[1]=floor; o.state=2; o.countdown=o.size; o.counter=0;
                            } else if (o.pos[1] < water-50.0f) {
                                o.pos[0]=o.home[0]; o.pos[1]=o.home[1]; o.pos[2]=o.home[2];
                                o.state=0; o.countdown=39;
                            }
                        }
                    }
                }
                o.hidden = o.state==0;
                if (!o.hidden) {
                    // Original type-42 contact at 0x8045EA90 uses a 10-unit
                    // axis-aligned box and immediately applies the spin effect.
                    for (int i=0; i<NUM_RACERS; ++i) {
                        Player* player=&gPlayers[i];
                        if (o.cooldown[i]>0) { --o.cooldown[i]; continue; }
                        if (!(player->type & PLAYER_EXISTS) || (player->effects & (STAR_EFFECT | BOO_EFFECT))) continue;
                        if (fabsf(player->pos[0]-o.pos[0]) <= 10.0f &&
                            fabsf(player->pos[1]-o.pos[1]) <= 10.0f &&
                            fabsf(player->pos[2]-o.pos[2]) <= 10.0f) {
                            player->triggers |= SPINOUT_TRIGGER;
                            o.counter=0; o.cooldown[i]=60;
                        }
                    }
                }
                continue;
            }
            case Behaviour::SHRINE_GHOST: {
                // Original type 43 orbits 290 units and oscillates vertically by 6.
                const uint16_t phase = o.homeYaw + (gIsMirrorMode ? 1 : -1) * (uint16_t)((mTick / 2) * 91);
                o.pos[0] = o.home[0] + coss(phase) * 290.0f;
                o.pos[2] = o.home[2] - sins(phase) * 290.0f;
                o.pos[1] = o.home[1] + sins((uint16_t)(phase * 3 + o.timer * 1183)) * 6.0f;
                o.yaw = phase - 0x4000;
                continue;
            }
            case Behaviour::SHRINE_ENEMY: {
                // Type 38: one unit per 30 Hz update, 50 updates at each endpoint.
                if ((mTick & 1) == 0) {
                    if (o.state == 0) {
                        if (o.countdown > 0) --o.countdown;
                        else if (o.path.size() >= 2) {
                            const auto& goal = o.path[o.target % o.path.size()];
                            auto step = [](float& p, float dest) { p += std::clamp(dest - p, -1.0f, 1.0f); };
                            step(o.pos[0], goal[0]); step(o.pos[2], goal[2]);
                            if (o.pos[0] == goal[0] && o.pos[2] == goal[2]) {
                                o.target = (o.target + 1) % o.path.size(); o.countdown = 50;
                            }
                        }
                        float nearest = 360000.0f;
                        for (int i = 0; i < NUM_RACERS; ++i) {
                            Player* p = &gPlayers[i];
                            if (!(p->type & PLAYER_EXISTS) || (p->effects & BOO_EFFECT)) continue;
                            float dx=p->pos[0]-o.pos[0], dz=p->pos[2]-o.pos[2];
                            float d=dx*dx+dz*dz;
                            if (d < nearest) { nearest=d; o.targetPlayer=i; }
                        }
                        if (nearest < 360000.0f) { o.state=1; o.counter=0; }
                    } else {
                        o.counter += 2; // original frame counter advances by gLogicTicksPerFrame
                        if (o.targetPlayer >= 0 && (gPlayers[o.targetPlayer].type & PLAYER_EXISTS)) {
                            Player* p=&gPlayers[o.targetPlayer];
                            float dx=p->pos[0]-o.pos[0], dz=p->pos[2]-o.pos[2];
                            o.yaw=YawTowards(dx,dz);
                            // Animation 2 releases one of seven type-39 shots in each 10-update window.
                            if (o.counter >= 140 && o.counter < 210 && (o.counter-140)%10 == 0) {
                                auto& shot=o.shrineShots[(o.counter-140)/10];
                                shot.pos[0]=o.pos[0]; shot.pos[1]=o.pos[1]+27.0f; shot.pos[2]=o.pos[2];
                                float dy=p->pos[1]-(o.pos[1]+30.0f);
                                float length=std::max(1.0f,sqrtf(dx*dx+dy*dy+dz*dz));
                                // Original look-at direction, then 15 horizontal / 12 vertical per update.
                                shot.vel[0]=dx/length*15.0f; shot.vel[1]=dy/length*12.0f; shot.vel[2]=dz/length*15.0f;
                                shot.life=90;
                            }
                        }
                        if (o.counter >= 240) { o.state=0; o.counter=0; o.targetPlayer=-1; }
                    }
                    for (auto& shot:o.shrineShots) {
                        if (shot.life <= 0) continue;
                        --shot.life;
                        for (int axis=0; axis<3; ++axis) shot.pos[axis]+=shot.vel[axis];
                        for (int i=0; i<NUM_RACERS; ++i) {
                            Player* p=&gPlayers[i];
                            if (!(p->type & PLAYER_EXISTS) || (p->effects & (BOO_EFFECT | STAR_EFFECT))) continue;
                            float dx=p->pos[0]-shot.pos[0], dy=p->pos[1]-shot.pos[1], dz=p->pos[2]-shot.pos[2];
                            if (dx*dx+dy*dy+dz*dz < 15.0f*15.0f) {
                                func_8008C73C(p, (s16)i); shot.life=0; break;
                            }
                        }
                    }
                }
                continue;
            }
            case Behaviour::MINECART: {
                // Type 19: 7 units per 30 Hz update, advancing within 250 units of a waypoint.
                // At the end the original resets to the first point and waits 120 updates.
                if ((mTick & 1) || o.path.size() < 2) break;
                auto cartRand = [&o](uint32_t limit) -> uint32_t {
                    o.cartRandom = o.cartRandom * 1664525u + 1013904223u;
                    return (uint32_t)(((uint64_t)(o.cartRandom >> 16) * limit) >> 16);
                };
                for (auto& debris : o.minecartDebris) {
                    if (debris.impactTicks > 0) { --debris.impactTicks; continue; }
                    if (debris.life > 0) {
                        const float oldX = debris.pos[0], oldY = debris.pos[1], oldZ = debris.pos[2];
                        debris.pos[0] += debris.vx;
                        debris.pos[1] += debris.vy;
                        debris.pos[2] += debris.vz;
                        debris.roll += 1820 + cartRand(5461);
                        Collision ground{};
                        func_802AAAAC(&ground);
                        if (debris.vy < 0.0f &&
                            (actor_terrain_collision(&ground, 4.0f, debris.pos[0], debris.pos[1] + 4.0f, debris.pos[2],
                                                     oldX, oldY, oldZ) & FACING_Y_AXIS)) {
                            const float floor = calculate_surface_height(debris.pos[0], debris.pos[1], debris.pos[2], ground.meshIndexZX);
                            if (floor > -3000.0f && floor <= oldY && debris.pos[1] <= floor + 4.0f) {
                                debris.pos[1] = floor + 1.0f;
                                debris.life = 0;
                                debris.impactTicks = 8;
                                continue;
                            }
                        }
                        --debris.life;
                    }
                }
                if (o.countdown > 0) { --o.countdown; break; }
                const auto& goal = o.path[o.target];
                float dx = goal[0] - o.pos[0], dz = goal[2] - o.pos[2];
                float distance = sqrtf(dx * dx + dz * dz);
                if (distance <= 250.0f) {
                    if (++o.target >= o.path.size()) {
                        for (int axis = 0; axis < 3; ++axis) o.pos[axis] = o.path[0][axis];
                        o.target = 1;
                        o.countdown = 120;
                    }
                } else {
                    if (fabsf(dx) + fabsf(dz) > 0.001f) o.yaw = YawTowards(dx, dz);
                    o.pos[0] += dx / distance * 7.0f;
                    o.pos[2] += dz / distance * 7.0f;
                }
                // Reduce the visual emission to one quarter of the previous port rate.
                if (o.path.size() == 5 && ((mTick / 2) % 4) == 0) {
                    int emitted = 0;
                    const uint8_t green = (uint8_t)(125 + cartRand(125));
                    for (auto& debris : o.minecartDebris) {
                        if (debris.life != 0 || debris.impactTicks != 0) continue;
                        for (int axis = 0; axis < 3; ++axis) debris.pos[axis] = o.pos[axis];
                        // 0x80468fc0's scatter parameter is 30: independent
                        // random horizontal angles and a signed vertical offset.
                        debris.pos[0] += 30.0f * coss((uint16_t)cartRand(65535));
                        // Keep the source below the cart body while retaining scattered positions.
                        debris.pos[1] -= 6.0f + cartRand(20);
                        debris.pos[2] += 30.0f * sins((uint16_t)cartRand(65535));
                        debris.green = green;
                        debris.roll = 0;
                        debris.vy = -18.0f;
                        debris.vx = sins(o.yaw) * 4.0f;
                        debris.vz = coss(o.yaw) * 4.0f;
                        debris.scale = emitted == 0 ? 0.2f : 0.5f;
                        debris.life = 20;
                        if (++emitted == 2) break;
                    }
                }
                break;
            }
            case Behaviour::WALKER:
            case Behaviour::POKEY:
            case Behaviour::CARNIVAL_TEETH:
            case Behaviour::PATROL: {
                if (o.path.size() < 2) break;
                if (o.behaviour == Behaviour::CARNIVAL_TEETH && o.countdown > 0) { --o.countdown; break; }
                if (o.hasSpeedCC) {
                    o.speed = o.speedCC[gCCSelection == 0 ? 0 : (gCCSelection == 2 ? 2 : 1)];
                }
                const auto& t = o.path[o.target];
                float dx = t[0] - o.pos[0], dy = t[1] - o.pos[1], dz = t[2] - o.pos[2];
                if (o.ground) {
                    dy = 0.0f; // height comes from the floor, so waypoints are reached horizontally
                }
                float dist = sqrtf(dx * dx + dy * dy + dz * dz);
                if (dist <= o.speed && o.loop) {
                    o.target = (o.target + 1) % o.path.size(); // Amped wraps to the first waypoint at the end marker
                } else if (dist <= o.speed) {
                    if ((o.target == o.path.size() - 1 && o.dir > 0) || (o.target == 0 && o.dir < 0)) {
                        o.dir = -o.dir; // walk back along the path
                    }
                    o.target += o.dir;
                    if (o.behaviour == Behaviour::CARNIVAL_TEETH) o.countdown = 30;
                } else {
                    o.pos[0] += dx / dist * o.speed;
                    o.pos[1] += dy / dist * o.speed;
                    o.pos[2] += dz / dist * o.speed;
                    o.yaw = YawTowards(dx, dz);
                }
                if (o.ground) {
                    // The original applies gravity (1.5 per 30 Hz update) and the stock floor collision to the walker, so its
                    // height follows the track surface instead of the waypoint heights.
                    static Collision scratch{};
                    check_bounding_collision(&scratch, 10.0f, o.pos[0], o.pos[1], o.pos[2]);
                    float h = calculate_surface_height(o.pos[0], o.pos[1] + 60.0f, o.pos[2], scratch.meshIndexZX);
                    if (h > -3000.0f && h < 3000.0f) {
                        o.pos[1] = h; // stand exactly on the surface: no falling lag when the ground drops away
                    }
                }
                break;
            }
            case Behaviour::CAR: {
                if (gTrackPaths[0] == nullptr) break;
                if (!o.started) {
                    // Spawned on a racing-path point, then driven like Toad's Turnpike traffic with a fixed lane
                    TrackPathPoint* p = &gTrackPaths[0][o.pathPoint];
                    o.pos[0] = p->x;
                    o.pos[1] = p->y;
                    o.pos[2] = p->z;
                    o.waypoint = (int16_t) o.pathPoint;
                    o.speed = (float) ((gCCSelection + 12) / 2);
                    o.started = true;
                }
                s16 yaw;
                if (gIsInExtra == 0) {
                    yaw = func_8000D6D0(o.pos, &o.waypoint, o.speed, o.lane, 0, 3);
                } else {
                    yaw = func_8000D940(o.pos, &o.waypoint, o.speed, o.lane, 0);
                }
                o.yaw = (uint16_t) yaw;
                break;
            }
            case Behaviour::APPLE: {
                // Type 25 (0x80458e30, 0x80458ff0): each apple approaches terminal speed -4.5
                // by its spawn ordinal / 10, then rebounds at 2.2 times the incoming speed.
                if (mTick & 1) continue;
                const float step = o.counter / 10.0f;
                o.vy += std::clamp(-4.5f - o.vy, -step, step);
                const float oldY = o.pos[1];
                o.pos[1] += o.vy;
                // Amped's type-25 contact test uses a 20-unit horizontal radius,
                // with a 10-unit lower allowance and a 30-unit upper allowance.
                // Include the visible apple radius below the center so the
                // mesh-based ground bounce cannot preempt a hit on a low kart.
                if (o.vy < 0.0f) {
                    for (int i = 0; i < NUM_RACERS; ++i) {
                        Player& player = gPlayers[i];
                        if (!(player.type & PLAYER_EXISTS) || (player.lakituProps & HELD_BY_LAKITU) ||
                            (player.effects & (STAR_EFFECT | BOO_EFFECT | HIT_EFFECT))) continue;
                        const float dx = player.pos[0] - o.pos[0];
                        const float dz = player.pos[2] - o.pos[2];
                        if (dx * dx + dz * dz > 20.0f * 20.0f ||
                            player.pos[1] < o.pos[1] - 35.0f || player.pos[1] >= o.pos[1] + 30.0f) continue;
                        trigger_apple_squish(&player, i);
                        // 0x80459528 resets height and velocity and waits 100 original updates.
                        o.knocked = 200;
                        o.pos[1] = o.home[1];
                        o.vy = 0.0f;
                        break;
                    }
                    if (o.knocked > 0) continue;
                }
                const int turn = gIsMirrorMode ? -o.size : o.size;
                o.yaw += turn;
                o.roll += turn;
                o.pitch += o.size / 2;
                // The ROM's 25-unit test radius is close to the upright mesh radius.
                // Use the lowest rotated mesh vertex so the tumbling apple lands on its visible surface.
                float bottom = -25.0f;
                if (!o.contactVertices.empty()) {
                    Mat4 matrix;
                    ApplyMatrixTransformations(matrix, FVector(0, 0, 0), IRotator(o.pitch, o.yaw, o.roll),
                                               FVector(o.scale, o.scale, o.scale));
                    bottom = 0.0f;
                    for (const auto& vertex : o.contactVertices) {
                        const float y = vertex[0] * matrix[0][1] + vertex[1] * matrix[1][1] + vertex[2] * matrix[2][1];
                        bottom = std::min(bottom, y);
                    }
                }
                constexpr float probeRadius = 8.0f;
                if (o.vy < 0.0f &&
                    (actor_terrain_collision(&o.col, probeRadius, o.pos[0], o.pos[1] + bottom + probeRadius, o.pos[2],
                                             o.pos[0], oldY, o.pos[2]) & FACING_Y_AXIS)) {
                    const float contactY = calculate_surface_height(o.pos[0], o.pos[1], o.pos[2], o.col.meshIndexZX) - bottom;
                    if (o.pos[1] <= contactY) {
                        o.pos[1] = contactY;
                        o.vy *= -2.2f;
                    }
                }
                continue;
            }
            case Behaviour::GUST: {
                if (!o.started) {
                    o.timer = 0;
                    if (gRaceState < RACE_IN_PROGRESS) continue;
                    o.started = true;
                }
                const int countdown = WhispyCountdown(o.timer);
                if (countdown < 30 || countdown > 58) continue;
                const float x = sins(o.yaw), z = coss(o.yaw);
                // 0x8045ad80 uses three moving boxes and adds the emitter velocity / 10.
                const float distance = 5.0f * (59 - countdown);
                const float originX = o.pos[0] + x * 138.0f;
                const float originZ = o.pos[2] + z * 138.0f;
                for (int i = 0; i < NUM_RACERS; ++i) {
                    Player& player = gPlayers[i];
                    if (!(player.type & PLAYER_EXISTS) || (player.lakituProps & HELD_BY_LAKITU) ||
                        (player.effects & (STAR_EFFECT | BOO_EFFECT))) continue;
                    const auto inBox = [&](float advance, float radius) {
                        return std::abs(player.pos[0] - (originX + x * advance)) <= radius &&
                               std::abs(player.pos[1] - (o.pos[1] - 100.0f)) <= radius &&
                               std::abs(player.pos[2] - (originZ + z * advance)) <= radius;
                    };
                    if (inBox(distance, 100.0f) || inBox(distance * 0.5f, 50.0f) ||
                        inBox(distance / 3.0f, 75.0f)) {
                        player.velocity[0] += x * 0.25f;
                        player.velocity[2] += z * 0.25f;
                    }
                }
                continue;
            }
            case Behaviour::GATE: {
                // The type-36 controller (0x8045b910) waits 500 original updates,
                // then drives the tape between 0 and -100 at one unit per 30 Hz step.
                if (gRaceState >= RACE_IN_PROGRESS) ++o.counter;
                const int travel = std::max(0, o.counter - 1000) % 400;
                const float height = -0.5f * std::min(travel, 400 - travel);
                // Type 35 (0x8045f4d0) adds a 10-unit sine to its base height.
                o.pos[1] = o.home[1] + height + 10.0f * sins((uint16_t) (mTick * 366));
                break;
            }
            case Behaviour::NEO_BOWSER:
            case Behaviour::NEO_ROTOR:
            case Behaviour::NEO_SWIRL:
                // Type 67: the ROM bobs by 10 units and advances its sine
                // phase by 91 per 60 Hz tick; the rotor turns -1820 per tick.
                o.pos[1] = o.home[1] + 10.0f * sins((uint16_t)(mTick * 91));
                if (o.behaviour == Behaviour::NEO_ROTOR) o.yaw -= 1820;
                if (o.behaviour == Behaviour::NEO_SWIRL) {
                    // Original 0x8045f4d0 scrolls the ring vertices' T coordinate.
                    Vtx* vertices = (Vtx*) ResourceGetDataByName("tracks/amped_12/obj_bowser_flyer_swirl_vtx");
                    if (vertices) {
                        for (int i = 0; i < 48; ++i)
                            vertices[i].v.tc[1] = (int16_t)((uint16_t)vertices[i].v.tc[1] + 20u);
                    }
                }
                break;
            case Behaviour::SPINNING:
                o.yaw += 0x200;
                break;
            case Behaviour::MIST:
                if (o.timer % 40 == 0) Emit(o, o.speed, 1.0f, 36);
                continue;
            case Behaviour::VENT: {
                if (o.timer % 6 == 0) Emit(o, 1.6f, 1.0f, (int) o.frames.size() * 3);
                for (int i = 0; i < NUM_RACERS; i++) {
                    Player* player = &gPlayers[i];
                    if (!(player->type & PLAYER_EXISTS)) { o.cooldown[i] = 0; continue; }
                    const float dx = player->pos[0] - o.pos[0], dz = player->pos[2] - o.pos[2];
                    const float dy = player->pos[1] - o.pos[1];
                    const bool inColumn = dx * dx + dz * dz <= o.radius * o.radius;
                    // Each racer is lifted once per entry. Keep the release latched
                    // until they move clear, so falling back does not trap them.
                    if (!inColumn || (player->lakituProps & (HELD_BY_LAKITU | LAKITU_RETRIEVAL))) {
                        o.cooldown[i] = 0;
                        continue;
                    }
                    if (o.cooldown[i] == 2) continue;
                    if (o.cooldown[i] == 0) {
                        // Original entry uses the vent's 12-unit radius and a
                        // 20-unit vertical range, not the whole lift column.
                        if (dy < -10.0f || dy > 20.0f) continue;
                        o.cooldown[i] = 1;
                        if (!(player->effects & 0x800000)) func_8008D7B0(player, i);
                    }
                    if (dy >= 100.0f) {
                        o.cooldown[i] = 2;
                        // The stock airborne spin prevents steering until landing.
                        // Release it at the top so the kart can leave the column.
                        if (player->effects & 0x800000) func_8008D97C(player);
                        continue;
                    }
                    player->velocity[0] = 0.0f;
                    player->velocity[1] = o.lift;
                    player->velocity[2] = 0.0f;
                    player->currentSpeed = 0.0f;
                    player->boostPower = 0.0f;
                    player->kartPropulsionStrength = 0.0f;
                }
                break;
            }
            case Behaviour::FIREJET: {
                // Original state and particle emission run at 30 Hz.
                if (mTick & 1) continue;
                enum { DORMANT, RISING, BURNING, OFF };
                if (o.state == DORMANT) {
                    for (int i = 0; i < NUM_RACERS; i++) {
                        Player* player = &gPlayers[i];
                        if (!(player->type & PLAYER_EXISTS)) continue;
                        float dx = player->pos[0] - o.pos[0], dz = player->pos[2] - o.pos[2];
                        if (dx * dx + dz * dz < 2200.0f * 2200.0f) {
                            o.state = RISING;
                            o.counter = 1;
                            break;
                        }
                    }
                } else if (o.state == RISING) {
                    if (++o.counter > o.size * 2) {
                        o.state = BURNING;
                        o.counter = 100;
                    }
                } else if (o.state == BURNING) {
                    if (--o.counter <= 0) {
                        o.state = OFF;
                        o.counter = 100;
                    }
                } else if (--o.counter <= 0) {
                    o.state = DORMANT;
                }
                if (o.state == RISING || o.state == BURNING) {
                    int height = (o.state == RISING) ? o.counter : o.size * 2;
                    // 0x80460590 positions successive puffs at y + 3.5 * height^2 / 10.
                    const int plumeStep = o.state == RISING ? height : (o.timer / 2) % (o.size * 2 + 1);
                    const size_t oldCount = mParticles.size();
                    Emit(o, 0.0f, std::max(0.1f, plumeStep / 10.0f), 60);
                    if (mParticles.size() > oldCount)
                        mParticles.back().pos[1] += 0.35f * plumeStep * plumeStep;
                    // Type 47 calls the stock spin and marks the racer as burned.
                    for (int i = 0; i < NUM_RACERS; ++i) {
                        Player* player = &gPlayers[i];
                        if (o.cooldown[i] > 0) --o.cooldown[i];
                        if (!(player->type & PLAYER_EXISTS) || o.cooldown[i] > 0 ||
                            (player->effects & (STAR_EFFECT | BOO_EFFECT))) continue;
                        const float dx = player->pos[0] - o.pos[0], dz = player->pos[2] - o.pos[2];
                        const float dy = player->pos[1] - o.pos[1];
                        if (dx * dx + dz * dz > o.radius * o.radius || dy < -10.0f ||
                            dy > 20.0f + 0.35f * height * height) continue;
                        func_8008C73C(player, (s16)i);
                        sHazyScorch[i] = 120;
                        o.cooldown[i] = 60;
                    }
                }
                continue;
            }
            case Behaviour::BOULDER: {
                // HMC rolling boulder (spawn 0x80460AE0, tick 0x80460C20). Original 30 Hz with step D_80150214 (2) * 1.05;
                // here 60 Hz with step 1 * 1.05, which gives the same motion per second.
                const float dt = 1.05f;
                if (o.countdown > 0) {
                    o.countdown--;
                    o.hidden = true;
                    continue;
                }
                if (!o.started) {
                    bool playerInRange = false, anotherRolling = false;
                    for (int i = 0; i < NUM_RACERS; ++i) {
                        if ((gPlayers[i].type & PLAYER_EXISTS) && gNearestPathPointByPlayerId[i] >= o.pathPoint &&
                            gNearestPathPointByPlayerId[i] <= 95) { playerInRange = true; break; }
                    }
                    for (const auto& other : mObjects) {
                        if (&other != &o && other.behaviour == Behaviour::BOULDER && other.started && !other.hidden &&
                            ((other.ccMask >> gCCSelection) & 1)) { anotherRolling = true; break; }
                    }
                    if (!playerInRange || anotherRolling) { o.hidden = true; continue; }
                    o.started = true;
                }
                o.hidden = false;
                o.pitch = (uint16_t) (o.pitch + (int) (o.vel[2] * 410.0f)); // obj+0x90 += D * (s32) (vz * 410)
                o.roll = (uint16_t) (o.roll + (int) (o.vel[0] * 410.0f));   // obj+0x94 += D * (s32) (vx * 410)
                float vy = o.vel[1] - dt * 0.1f;                           // 0x80466B1C
                if (vy < -8.0f) vy = -8.0f;                                // 0x80466B58
                o.vel[1] = vy;
                o.pos[0] += o.vel[0] * dt;
                o.pos[1] += vy;
                o.pos[2] += o.vel[2] * dt;
                check_bounding_collision(&o.col, o.radius, o.pos[0], o.pos[1] + 5.0f, o.pos[2]);
                auto push = [&o, dt](float dist, const Vec3f n) {
                    Vec3f neg = { -n[0], -n[1], -n[2] };
                    o.pos[0] += neg[0] * dist;
                    o.pos[1] += neg[1] * dist;
                    o.pos[2] += neg[2] * dist;
                    adjust_pos_orthogonally(neg, dist, o.vel, dt);
                };
                if (o.col.surfaceDistance[2] < 0.0f) { // floor
                    push(o.col.surfaceDistance[2], o.col.orientationVector);
                    o.vel[1] = 2.0f * (vy * -0.08f); // D * (vy * -0.08), 0x80466B64
                }
                if (o.col.surfaceDistance[0] < 0.0f) push(o.col.surfaceDistance[0], o.col.unk48);
                if (o.col.surfaceDistance[1] < 0.0f) push(o.col.surfaceDistance[1], o.col.unk54);
                if (o.pos[1] < CM_GetWaterLevel(o.pos, nullptr) - o.radius || o.pos[1] < o.home[1] - 3000.0f) { // fell into the water: back to the start
                    for (int k = 0; k < 3; k++) {
                        o.pos[k] = o.home[k];
                        o.vel[k] = 0.0f;
                    }
                    o.countdown = 16;
                    o.started = false;
                }
                break;
            }
            case Behaviour::SANDY_BALL: {
                // Spawn heading and velocity come from the Sandy Slide type-58 setup.
                if (o.countdown > 0) { --o.countdown; o.hidden = true; continue; }
                o.hidden = false;
                o.pos[0] -= sins(o.homeYaw) * o.speed;
                o.pos[2] -= coss(o.homeYaw) * o.speed;
                o.vel[1] = std::max(o.vel[1] - 0.075f, -5.0f);
                o.pos[1] += o.vel[1];
                check_bounding_collision(&o.col, o.radius, o.pos[0], o.pos[1], o.pos[2]);
                // The launch point can be far above the floor, outside the small
                // sphere used for wall and racer collisions.
                float floor = -100000.0f;
                const int cellX = (gTrackMaxX - gTrackMinX) / GRID_SIZE;
                const int cellZ = (gTrackMaxZ - gTrackMinZ) / GRID_SIZE;
                if (gCollisionIndices && gCollisionMesh && cellX > 0 && cellZ > 0 &&
                    o.pos[0] >= gTrackMinX && o.pos[2] >= gTrackMinZ) {
                    const int ix = (int)((o.pos[0] - gTrackMinX) / cellX);
                    const int iz = (int)((o.pos[2] - gTrackMinZ) / cellZ);
                    if (ix >= 0 && iz >= 0 && ix < GRID_SIZE && iz < GRID_SIZE) {
                        const auto& cell = gCollisionGrid[iz * GRID_SIZE + ix];
                        for (u32 j = 0; j < cell.numTriangles; ++j) {
                            const u16 index = gCollisionIndices[cell.triangle + j];
                            if (index >= gCollisionMeshCount) continue;
                            const auto& tri = gCollisionMesh[index];
                            if (!tri.vtx1 || !tri.vtx2 || !tri.vtx3 || fabsf(tri.normalY) < 0.35f ||
                                o.pos[0] < tri.minX || o.pos[0] > tri.maxX || o.pos[2] < tri.minZ || o.pos[2] > tri.maxZ) continue;
                            const auto* a = tri.vtx1->v.ob; const auto* b = tri.vtx2->v.ob; const auto* c = tri.vtx3->v.ob;
                            const float den = (b[2]-c[2])*(a[0]-c[0]) + (c[0]-b[0])*(a[2]-c[2]);
                            if (fabsf(den) < 0.001f) continue;
                            const float u = ((b[2]-c[2])*(o.pos[0]-c[0]) + (c[0]-b[0])*(o.pos[2]-c[2])) / den;
                            const float v = ((c[2]-a[2])*(o.pos[0]-c[0]) + (a[0]-c[0])*(o.pos[2]-c[2])) / den;
                            if (u < 0 || v < 0 || u + v > 1) continue;
                            const float h = u*a[1] + v*b[1] + (1-u-v)*c[1];
                            if (h <= o.pos[1] + 30.0f && h > floor) floor = h;
                        }
                    }
                }
                if (floor > -3000.0f && floor < 3000.0f && floor <= o.pos[1] + 30.0f && floor >= o.pos[1] - 250.0f) {
                    // The extracted triangle starts at Y=0: its base is the contact point.
                    o.pos[1] = floor;
                    o.vel[1] = 0.0f;
                }
                if (mTick % 60 == 0) {
                    const Player& player = gPlayers[0];
                    SPDLOG_INFO("Sandy ball: spawn=({:.2f},{:.2f},{:.2f}) pos=({:.2f},{:.2f},{:.2f}) floor={:.2f} vy={:.2f} radius={:.2f} wall=({:.2f},{:.2f}) player=({:.2f},{:.2f},{:.2f}) hidden={} age={}",
                                o.home[0], o.home[1], o.home[2], o.pos[0], o.pos[1], o.pos[2], floor, o.vel[1], o.radius,
                                o.col.surfaceDistance[0], o.col.surfaceDistance[1], player.pos[0], player.pos[1], player.pos[2], o.hidden, o.counter);
                }
                ++o.counter;
                // The launch position overlaps the cannon wall. Ignore wall hits
                // until the ball has had time to clear the launcher.
                if (o.counter >= 600 || (o.counter > 30 &&
                    (o.col.surfaceDistance[0] < -2.0f || o.col.surfaceDistance[1] < -2.0f))) {
                    SPDLOG_INFO("Sandy ball reset: pos=({:.2f},{:.2f},{:.2f}) age={} walls=({:.2f},{:.2f})", o.pos[0], o.pos[1], o.pos[2], o.counter, o.col.surfaceDistance[0], o.col.surfaceDistance[1]);
                    for (int k = 0; k < 3; ++k) o.pos[k] = o.home[k];
                    o.vel[1] = 0.0f; o.counter = 0; o.countdown = 60; o.hidden = true;
                }
                break;
            }
            case Behaviour::CARNIVAL_FAN: {
                const float plateX = gIsMirrorMode ? -1767.52f : 1767.52f;
                for (int i = 0; i < NUM_RACERS; ++i) {
                    const Player& p = gPlayers[i];
                    const float dx = p.pos[0] - plateX, dz = p.pos[2] + 1289.5f;
                    if ((p.type & PLAYER_EXISTS) && dx*dx + dz*dz < 50.0f*50.0f && fabsf(p.pos[1] - 340.7f) < 30.0f) o.countdown = 600;
                }
                if (o.countdown > 0) {
                    --o.countdown;
                    o.roll += 0x0800;
                    // Original type-62 contact (0x80465ab0): a fixed world-space
                    // wind box extending toward negative Z from the fan. The port
                    // updates twice per original tick, so halve its 0.5 impulse.
                    for (int i = 0; i < NUM_RACERS; ++i) {
                        Player& p = gPlayers[i];
                        if (!(p.type & PLAYER_EXISTS) || (p.effects & (BOO_EFFECT | 0x200))) continue;
                        if (fabsf(p.pos[0] - o.home[0]) <= 100.0f &&
                            p.pos[1] >= o.home[1] - 140.0f && p.pos[1] <= o.home[1] + 100.0f &&
                            p.pos[2] >= o.home[2] - 1050.0f && p.pos[2] <= o.home[2] - 15.0f) {
                            p.velocity[2] -= 0.25f;
                        }
                    }
                }
                break;
            }
            case Behaviour::SCUTTLEBUG: {
                // Original 0x80461c4c: forward 0.7 and yaw 182 per 30 Hz update.
                // D_8016F784 is the mirror sign, not a frame-rate multiplier.
                const float heading = o.yaw * (float)(M_PI / 32768.0);
                o.pos[0] += sinf(heading) * 0.35f;
                o.pos[2] += cosf(heading) * 0.35f;
                o.yaw += gIsMirrorMode ? -91 : 91;
                break;
            }
            case Behaviour::CHOMP: {
                // GCN Chain Chomp (update 0x804545c0, tick 0x80454070). Original runs at 30 Hz: speeds are halved and
                // timers doubled here. 0 idle at home, 1 alerted (turns to the racer, barks), 2 lunge, 3 pause, 4 return.
                enum { IDLE, ALERT, LUNGE, PAUSE, RETURN };
                auto turnTo = [](uint16_t cur, uint16_t target, int rate) {
                    int d = (int16_t) (uint16_t) (target - cur);
                    d = std::clamp(d, -rate, rate);
                    return (uint16_t) (cur + d);
                };
                auto forward = [&o](float speed) {
                    float a = o.yaw * (float) (M_PI / 32768.0);
                    o.pos[0] += sinf(a) * speed;
                    o.pos[2] += cosf(a) * speed;
                };
                auto snapGround = [&o]() {
                    static Collision scratch{};
                    check_bounding_collision(&scratch, 10.0f, o.pos[0], o.pos[1], o.pos[2]);
                    float h = calculate_surface_height(o.pos[0], o.pos[1] + 60.0f, o.pos[2], scratch.meshIndexZX);
                    if (h > -3000.0f && h < 3000.0f) o.pos[1] = h + 5.0f; // spawn height = table y + 5 (0x8045756c)
                };
                if (o.countdown > 0) o.countdown--;
                if (o.state == IDLE) {
                    o.yaw = turnTo(o.yaw, o.homeYaw, 0x1F4 / 2);
                    for (int i = 0; i < NUM_RACERS; i++) {
                        Player* player = &gPlayers[i];
                        if (!(player->type & PLAYER_EXISTS) || (player->effects & BOO_EFFECT)) continue;
                        float dx = player->pos[0] - o.pos[0], dz = player->pos[2] - o.pos[2];
                        if (dx * dx + dz * dz <= 280.0f * 280.0f) { // 0x80457634
                            o.targetPlayer = i;
                            o.state = ALERT;
                            o.countdown = 40; // 20 updates
                            break;
                        }
                    }
                } else if (o.state == ALERT) {
                    if (o.countdown == 2) {
                        static Vec3f still = { 0.0f, 0.0f, 0.0f };
                        Vec3f p = { o.pos[0], o.pos[1], o.pos[2] };
                        func_800C98B8(p, still, SOUND_ARG_LOAD(0x19, 0x01, 0x80, 0x57)); // bark
                    }
                    if (o.countdown == 0) {
                        o.state = LUNGE;
                        o.countdown = 36; // 18 updates
                    }
                } else if (o.state == LUNGE) {
                    forward(11.5f / 2.0f); // 0x80457628 per update
                    snapGround();
                    if (o.countdown == 0) {
                        o.state = PAUSE;
                        o.countdown = 50; // 25 updates
                        o.targetPlayer = -1;
                    }
                } else if (o.state == PAUSE) {
                    if (o.countdown == 0) o.state = RETURN;
                } else {
                    float dx = o.home[0] - o.pos[0], dz = o.home[2] - o.pos[2];
                    if (dx * dx + dz * dz <= 20.0f * 20.0f) { // 0x80457568
                        o.state = IDLE;
                    } else {
                        o.yaw = turnTo(o.yaw, YawTowards(dx, dz), 0x3E8 / 2);
                        forward(2.0f / 2.0f); // 0x80457554 per update
                        snapGround();
                    }
                }
                if ((o.state == ALERT || o.state == LUNGE) && o.targetPlayer >= 0) {
                    // obj+0x9C: keeps turning toward the chosen racer (0x5DC per update), also while lunging
                    Player* player = &gPlayers[o.targetPlayer];
                    o.yaw = turnTo(o.yaw, YawTowards(player->pos[0] - o.pos[0], player->pos[2] - o.pos[2]), 0x5DC / 2);
                }
                Contact(o, 150.0f); // 0x80457638
                continue;
            }
            default:
                break;
        }
        // Carnival tooth panels are rectangular, with their origin at the bottom.
        // Resolve against the visible box instead of the generic round tree collider.
        if (!o.frames.empty() && o.frames[0].find("tracks/amped_16/obj_panel") != std::string::npos) {
            const float halfX = 2304.0f * o.scale;
            const float halfZ = 1088.0f * o.scale;
            const float height = 4480.0f * o.scale;
            for (int i = 0; i < NUM_RACERS; ++i) {
                Player& p = gPlayers[i];
                if (!(p.type & PLAYER_EXISTS) || (p.effects & BOO_EFFECT)) continue;
                const float radius = p.boundingBoxSize;
                if (fabsf(p.pos[0] - o.pos[0]) > halfX + radius ||
                    p.pos[1] + radius < o.pos[1] || p.pos[1] - radius > o.pos[1] + height) continue;
                const float dz = p.pos[2] - o.pos[2];
                const float oldDz = p.oldPos[2] - o.pos[2];
                const float edge = halfZ + radius;
                if (fabsf(dz) < edge || (oldDz < -edge && dz > edge) || (oldDz > edge && dz < -edge)) {
                    const float side = oldDz < 0.0f ? -1.0f : 1.0f;
                    p.pos[2] = o.pos[2] + side * edge;
                    if (p.velocity[2] * side < 0.0f) p.velocity[2] = 0.0f;
                }
            }
            continue;
        }
        float range = (o.behaviour == Behaviour::POKEY) ? (20.0f * o.scale * 64.0f * o.counter) :
                      (o.behaviour == Behaviour::CAR) ? 25.0f : o.radius * 2.0f + 10.0f;
        Contact(o, range);
    }

    for (size_t i = 0; i < mParticles.size();) {
        Particle& p = mParticles[i];
        p.pos[1] += p.vy;
        if (++p.age >= p.life) {
            mParticles[i] = mParticles.back();
            mParticles.pop_back();
        } else {
            i++;
        }
    }
}

void AmpedObjectSet::Draw(ScreenContext* screen, bool translucent) {
    if (mObjects.empty() || screen == nullptr || screen->camera == nullptr) {
        return;
    }
    Camera* camera = screen->camera;
    const int screenIndex = (int) (screen - gScreenContexts);
    gSPSetGeometryMode(gDisplayListHead++, G_SHADING_SMOOTH);
    gSPClearGeometryMode(gDisplayListHead++, G_LIGHTING);

    auto drawOne = [&](const char* dl, const float* pos, uint16_t yaw, bool billboard, float scale, const uint8_t* colour,
                       uint32_t tag, uint16_t roll, uint16_t pitch = 0, float stretchY = 1.0f, bool cull = true,
                       bool reflectModel = false) {
        float dx = camera->pos[0] - pos[0], dz = camera->pos[2] - pos[2];
        if (cull && dx * dx + dz * dz > 7000.0f * 7000.0f) {
            return;
        }
        Mat4 mtx;
        IRotator rot;
        rot.Set(pitch, billboard ? YawTowards(dx, dz) : yaw, roll);
        FrameInterpolation_RecordOpenChild("amped_object", tag);
        ApplyMatrixTransformations(mtx, FVector(pos[0], pos[1], pos[2]), rot,
                                   FVector(reflectModel ? -scale : scale, scale * stretchY, scale));
        if (render_set_position(mtx, 0) != 0) {
            if (translucent) {
                gDPSetPrimColor(gDisplayListHead++, 0, 0, colour[0], colour[1], colour[2], colour[3]);
            }
            gSPDisplayList(gDisplayListHead++, (Gfx*) dl);
        }
        FrameInterpolation_RecordCloseChild();
    };

    for (size_t i = 0; i < mObjects.size(); i++) {
        const Obj& o = mObjects[i];
        if (translucent && o.behaviour == Behaviour::SHRINE_FISH && o.splashTicks > 0) {
            const float* splashPos = o.splashPos;
            const uint8_t splashColour[4] = { 163, 163, 255, 200 };
            drawOne(o.splashModels[std::min(3, (16 - o.splashTicks) / 4)].c_str(), splashPos, 0, true, 3.5f, splashColour,
                    (uint32_t)(0x600000 | (i << 4) | (screenIndex & 15)), 0);
        }
        if (translucent && o.behaviour == Behaviour::MINECART) {
            for (size_t j = 0; j < o.minecartDebris.size(); ++j) {
                const auto& debris = o.minecartDebris[j];
                const uint8_t goldColour[4] = {255, debris.green, 5, 255};
                const uint32_t debrisTag = (uint32_t)(0x800000 | (i << 14) | (j << 6) | (screenIndex & 15));
                if (debris.life > 0) drawOne(o.minecartDebrisModel.c_str(), debris.pos, 0, true, debris.scale,
                    goldColour, debrisTag, debris.roll);
                if (debris.impactTicks > 0) {
                    // Gold fragments fan out and fade after crossing a track surface.
                    const float age = (float)(8 - debris.impactTicks);
                    const uint8_t impactColour[4] = {255, debris.green, 5, (uint8_t)(debris.impactTicks * 28)};
                    for (int piece = 0; piece < 3; ++piece) {
                        const uint16_t angle = (uint16_t)(debris.roll + piece * 21845);
                        Vec3f fragment = {debris.pos[0] + sins(angle) * age * 1.3f,
                                          debris.pos[1] + age * (8.0f - age) * 0.16f,
                                          debris.pos[2] + coss(angle) * age * 1.3f};
                        drawOne(o.minecartDebrisModel.c_str(), fragment, 0, true, debris.scale * 0.35f,
                                impactColour, debrisTag | ((piece + 1) << 4), angle);
                    }
                }
            }
        }
        if (o.nightOnly) {
            bool active = false;
            for (int player = 0; player < NUM_RACERS; ++player) {
                if ((gPlayers[player].type & (PLAYER_EXISTS | PLAYER_HUMAN)) == (PLAYER_EXISTS | PLAYER_HUMAN) &&
                    gLapCountByPlayerId[player] == 1) { active = true; break; }
            }
            if (!active) continue;
        }
        if (o.translucent != translucent || o.hidden || o.knocked > 0 || o.frames.empty()) {
            continue;
        }
        size_t frame = (o.timer / o.frameTicks) % o.frames.size();
        if (o.behaviour == Behaviour::SHRINE_FISH) frame = std::min((size_t)o.counter, o.frames.size() - 1);
        const uint32_t tag = (uint32_t) ((i << 4) | (screenIndex & 0xF));
        if (o.behaviour == Behaviour::GUST) {
            if (!o.started) continue;
            const int countdown = WhispyCountdown(o.timer);
            // 0x8045abdc emits type-4 IA16 puffs at countdown 149..60. Each puff
            // starts at the mouth, travels forward at 8..27, and uses the seven ROM frames.
            constexpr int firstPuff = 149;
            constexpr int lastPuff = 60;
            constexpr int ticksPerFrame = 5;
            const int lifetime = (int) o.frames.size() * ticksPerFrame;
            for (int spawn = std::min(countdown + lifetime - 1, firstPuff);
                 spawn >= std::max(countdown, lastPuff); --spawn) {
                const int age = spawn - countdown;
                const uint32_t seed = (uint32_t) (o.timer / 2 - age) * 0x9E3779B9u;
                const float elapsed = age + (o.timer & 1) * 0.5f;
                const float distance = (8 + ((seed >> 16) % 20)) * elapsed;
                const float rise = (int) ((seed >> 9) % 5) - 1;
                const float spread = ((int) ((seed >> 24) % 20) - 10) * 0.025f * elapsed;
                const float x = sins(o.yaw), z = coss(o.yaw);
                Vec3f pos = { o.pos[0] + x * distance + z * spread, o.pos[1] + rise * elapsed,
                              o.pos[2] + z * distance - x * spread };
                const uint8_t colour[] = { 255, 255, 255, 79 };
                drawOne(o.frames[age / ticksPerFrame].c_str(), pos, 0, true, o.scale * (2 + (seed & 1)),
                        colour, tag | ((uint32_t) (spawn - lastPuff + 1) << 20), 0);
            }
            continue;
        }
        uint16_t roll = o.roll;
        float stretchY = o.stretch;
        if (o.sway) {
            // 0x80454c60: roll += sin(a) * 1024; Y row scale = 0.15 + cos(a) * 0.01 (draw scale 0.15)
            roll = (uint16_t) (roll + (int16_t) (sins(mSway) * 1024.0f));
            stretchY = (0.15f + coss(mSway) * 0.01f) / 0.15f;
        }
        Vec3f pos = { o.pos[0], o.pos[1] + o.drawYOffset, o.pos[2] };
        if (o.billboardOffset > 0.0f) {
            const uint16_t facing = YawTowards(camera->pos[0] - pos[0], camera->pos[2] - pos[2]);
            pos[0] += sins(facing) * o.billboardOffset;
            pos[2] += coss(facing) * o.billboardOffset;
        }
        const char* model = o.frames[frame].c_str();
        if (o.behaviour == Behaviour::SHRINE_ENEMY && o.state == 1 && !o.shrineAttackFrames.empty())
            model = o.shrineAttackFrames[std::min(119, o.counter / 2)].c_str();
        if (o.behaviour == Behaviour::POKEY) {
            for (int part = 0; part < o.counter; ++part) {
                Vec3f segment = {pos[0], pos[1] + part * 20.0f * o.scale * 64.0f, pos[2]};
                const char* partModel = model;
                if (part == o.counter - 1) {
                    partModel = ((o.timer / 20) & 1)
                        ? "__OTR__tracks/amped_15/obj_pokey_face_closed"
                        : "__OTR__tracks/amped_15/obj_pokey_face_open";
                }
                drawOne(partModel, segment, 0, true, o.scale, o.colour,
                        tag | ((uint32_t)(part + 1) << 20), 0);
            }
        } else {
            drawOne(model, pos, (uint16_t) (o.yaw + o.yawOffset), o.billboard,
                    o.scale, o.colour, tag, roll, o.pitch, stretchY, !o.noCull, o.reflectModel);
        }
        if (o.behaviour == Behaviour::SCUTTLEBUG) {
            // Limb 0x80563290 is a camera-facing two-texture body.
            Vec3f bodyPos = {o.pos[0], o.pos[1] + 7.5f, o.pos[2]};
            drawOne(o.scuttleBody.c_str(), bodyPos, 0, true, 0.5f, o.colour, tag | 0x900000, 0);
        }
        if (o.behaviour == Behaviour::MINECART) {
            // Original draw places the camera-facing miner 45 units above the cart.
            Vec3f riderPos = {o.pos[0], o.pos[1] + 45.0f, o.pos[2]};
            drawOne(o.minecartRider[(o.timer / 8) % 2].c_str(), riderPos, 0, true, 0.6f,
                    o.colour, tag | 0x700000, 0);
        }
        if (o.behaviour == Behaviour::SHRINE_ENEMY) {
            for (size_t j=0; j<o.shrineShots.size(); ++j) {
                const auto& shot=o.shrineShots[j];
                if (shot.life > 0) drawOne(o.shrineShotModel.c_str(), shot.pos, 0, true, 0.19f, o.colour,
                    tag + (uint32_t)(0x400000 + (j << 4)), 0);
            }
        }
    }
    if (translucent) {
        for (size_t i = 0; i < mParticles.size(); i++) {
            const Particle& p = mParticles[i];
            const Obj& o = *p.source;
            size_t frame = (size_t) p.age * o.frames.size() / std::max(1, p.life);
            if (frame >= o.frames.size()) frame = o.frames.size() - 1;
            drawOne(o.frames[frame].c_str(), p.pos, 0, true, o.scale * p.scale, o.colour,
                    (uint32_t) (0x100000 | (i << 4) | (screenIndex & 0xF)), 0);
        }
    }
    gSPSetGeometryMode(gDisplayListHead++, G_ZBUFFER);
}

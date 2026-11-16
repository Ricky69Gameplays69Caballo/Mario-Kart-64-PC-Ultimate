#include <libultraship.h>
#include "World.h"
#include "Cup.h"
#include "tracks/Track.h"
#include "TrainCrossing.h"
#include <memory>
#include "objects/Object.h"
#include "port/Game.h"
#include "port/LocalMultiplayer.h"
#include "engine/sky/Sky.h"

extern "C" {
#include "objects.h"
#include "main.h"
#include "defines.h"
#include "audio/external.h"
#include "menus.h"
#include "code_800029B0.h"
}

#include "engine/cameras/GameCamera.h"

World* World::Instance;
std::unique_ptr<Track> mTrack;
Cup* CurrentCup;

World::World() {
    Instance = this;
    RaceManagerInstance = std::make_unique<RaceManager>(*this);
}

World::~World() {
    CleanWorld();
}

void World::AddCup(Cup* cup) {
    Cups.push_back(cup);
}

void World::InitExtraCups() {
    static const char* names[] = { "AMPED CUP 1", "AMPED CUP 2", "AMPED CUP 3", "AMPED CUP 4" };
    for (int i = 0; i < 16; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "amped:course_%02d", i + 1);
        if (!gTrackRegistry.Find(name)) return;
    }
    for (int cup = 0; cup < 4; ++cup) {
        std::vector<std::string> tracks;
        for (int row = 0; row < 4; ++row) {
            char name[32];
            snprintf(name, sizeof(name), "amped:course_%02d", cup * 4 + row + 1);
            tracks.emplace_back(name);
        }
        ExtraCups[cup] = std::make_unique<Cup>("amped:cup_" + std::to_string(cup + 1), names[cup], tracks);
    }
}

bool World::ExtraCupsAvailable() const {
    return CVarGetInteger(CVAR_EXTRA_MAPS, 1) && ExtraCups[0] && ExtraCups[1] && ExtraCups[2] && ExtraCups[3];
}

void World::SetExtraCupPage(bool enabled) {
    ExtraCupPage = enabled && ExtraCupsAvailable();
}

Cup* World::GetRaceCup(size_t index) {
    if (ExtraCupPage && index < 4 && ExtraCupsAvailable()) return ExtraCups[index].get();
    return index < Cups.size() ? Cups[index] : nullptr;
}

void World::SetCurrentTrack(std::unique_ptr<Track> track) {
    if (mTrack == track) {
        return;
    }
    mTrack = std::move(track);
}

TrainCrossing* World::AddCrossing(Vec3f position, u32 waypointMin, u32 waypointMax, f32 approachRadius,
                                  f32 exitRadius) {
    auto crossing = std::make_shared<TrainCrossing>(position, waypointMin, waypointMax, approachRadius, exitRadius);
    Crossings.push_back(crossing);
    return crossing.get();
}

u32 World::GetCupIndex() {
    return this->CupIndex;
}

u32 World::NextCup() {
    const bool race = gModeSelection == GRAND_PRIX || gModeSelection == VERSUS || gModeSelection == TIME_TRIALS;
    const size_t last = gModeSelection == BATTLE ? Cups.size() - 1 : Cups.size() - 2;
    if (race && ExtraCupsAvailable() && CupIndex == 3 && !ExtraCupPage) {
        ExtraCupPage = true;
        CupIndex = 0;
    } else if (race && ExtraCupPage && CupIndex == 3) {
        ExtraCupPage = false;
        CupIndex = 0;
    } else if (CupIndex < last) {
        ++CupIndex;
    } else return CupIndex;
    CurrentCup = GetRaceCup(CupIndex);
    CurrentCup->CursorPosition = 0;
    reset_cycle_flash_menu();
    play_sound2(SOUND_MENU_CURSOR_MOVE);
    return CupIndex;
}

u32 World::PreviousCup() {
    const bool race = gModeSelection == GRAND_PRIX || gModeSelection == VERSUS || gModeSelection == TIME_TRIALS;
    if (race && ExtraCupsAvailable() && CupIndex == 0) {
        ExtraCupPage = !ExtraCupPage;
        CupIndex = 3;
    } else if (CupIndex > 0) {
        --CupIndex;
    } else return 0;
    CurrentCup = GetRaceCup(CupIndex);
    CurrentCup->CursorPosition = 0;
    reset_cycle_flash_menu();
    play_sound2(SOUND_MENU_CURSOR_MOVE);
    return CupIndex;
}

void World::SetCupIndex(size_t index) {
    CupIndex = index;
}

void World::SetCurrentCup(Cup* cup) {
    if (cup) {
        ExtraCupPage = false;
        for (const auto& extra : ExtraCups) if (extra && extra.get() == cup) ExtraCupPage = true;
        CurrentCup = cup;
        CurrentCup->CursorPosition = 0;
    }
}

void World::TickCameras() {

    for (size_t i = 0; i < 8; i++) {
        ScreenContext* screen = &gScreenContexts[i];
        if (NULL == screen->pendingCamera) { continue; }
        if (screen->pendingCamera != screen->camera) {
            screen->camera = screen->pendingCamera;
            screen->pendingCamera = nullptr;
        }
    }

    for (auto& camera : Cameras) {
        if (camera->IsActive()) {
            camera->Tick();
        }
    }
}

AActor* World::AddActor(std::unique_ptr<AActor> actor) {
    Actors.push_back(std::move(actor));
    Actors.back()->BeginPlay();
    return Actors.back().get();
}

struct Actor* World::AddBaseActor() {
    Actors.push_back(std::make_unique<AActor>());

    AActor* actor = Actors.back().get();

    // Skip C++ vtable to access variables in C
    return reinterpret_cast<struct Actor*>(reinterpret_cast<char*>(actor) + sizeof(void*));
}

void World::ActorBeginPlay(Actor* actor) {
    AActor* act = ConvertActorToAActor(actor);
    act->BeginPlay();
}

/**
 * Converts a C struct Actor* to its C++ AActor class
 */
AActor* World::ConvertActorToAActor(Actor* actor) {
    // Move the ptr back so that it points at the vtable.
    // Which is the initial item in the class, or in other words
    // Point to the class.
    return reinterpret_cast<AActor*>((char*) actor - sizeof(void*));
}

/**
 * Converts a C++ AActor class to a C Actor* struct.
 */
Actor* World::ConvertAActorToActor(AActor* actor) {
    // Move the ptr forward past the vtable.
    // This allows C to access the class variables like a normal Actor* struct.
    return reinterpret_cast<Actor*>((char*) actor + sizeof(void*));
}

AActor* World::GetActor(size_t index) {
    return Actors[index].get();
}

void World::TickActors() {
    // This only ticks modded actors
    for (auto& actor : Actors) {
        if (actor->IsMod()) { // Do not tick unported C actors
            actor->Tick();
        }
    }
}

void World::TickActors60fps() {
    for (auto& actor: Actors) {
        if (actor->IsMod()) {
            actor->Tick60fps();
        }
    }
}

StaticMeshActor* World::AddStaticMeshActor(const std::string& name, FVector pos, IRotator rot, FVector scale, const std::string& model, int32_t* collision) {
    StaticMeshActors.push_back(std::make_unique<StaticMeshActor>(name, pos, rot, scale, model, collision));
    auto* actor = StaticMeshActors.back().get();
    return actor;
}

void World::DrawStaticMeshActors() {
    for (const auto& actor : StaticMeshActors) {
        actor->Draw();
    }
}

// OObject* World::AddObject(OObject object) {
//     Objects.push_back(std::make_unique<OObject>(object));

//     // This is an example of how to get the C object.
//     // However, nothing is being done with it, so it's been commented out.
//     // if (object->_objectIndex != -1) {
//     //     Object* cObj = &gObjectList[object->_objectIndex];
//     // }

//     return Objects.back().get();
// }

OObject* World::AddObject(std::unique_ptr<OObject> object) {
    Objects.push_back(std::move(object));

    // This is an example of how to get the C object.
    // However, nothing is being done with it, so it's been commented out.
    // if (object->_objectIndex != -1) {
    //     Object* cObj = &gObjectList[object->_objectIndex];
    // }

    return Objects.back().get();
}

void World::TickObjects() {
    for (auto& object : Objects) {
        object->Tick();
    }
}

// Some objects such as lakitu are ticked in process_game_tick.
// This is a fallback to support those objects. Probably don't use this.
void World::TickObjects60fps() {
    for (auto& object : Objects) {
        object->Tick60fps();
    }
}

ParticleEmitter* World::AddEmitter(std::unique_ptr<ParticleEmitter> emitter) {
    Emitters.push_back(std::move(emitter));
    return Emitters.back().get();
}

void World::DrawObjects(s32 cameraId) {
    for (auto& object : Objects) {
        object->Draw(cameraId);
    }
}

void World::TickParticles() {
    for (const auto& emitter : Emitters) {
        emitter->Tick();
    }
}

void World::DrawParticles(s32 cameraId) {
    for (const auto& emitter : Emitters) {
        emitter->Draw(cameraId);
    }
}

// Sets OObjects or AActors static member variables back to default values
void World::Reset() {
    for (auto& object : Objects) {
        object->Reset(); // Used for OPenguin
    }
}

Object* World::GetObjectByIndex(size_t index) {
    // if (index < this->Objects.size()) {
    //  Assuming GameActor::a is accessible, use reinterpret_cast if needed
    //    return reinterpret_cast<Object*>(&this->Objects[index]->o);
    //}
    return nullptr; // Or handle the error as needed
}

// Deletes all objects from the world
void World::CleanWorld(void) {
    printf("[Game.cpp] Clean World\n");

    World::Reset(); // Reset OObjects

    for (size_t i = 0; i < ARRAY_COUNT(mPlayerBombKart); i++) {
        mPlayerBombKart[i].state = PlayerBombKart::PlayerBombKartState::DISABLED;
        mPlayerBombKart[i]._primAlpha = 0;
    }

    gEditor.ClearObjects();
    Actors.clear();
    StaticMeshActors.clear();
    Objects.clear();
    Emitters.clear();
    Lakitus.clear();
    Sky::Instance->GetSkyActors().clear();
}

void World::CleanActors(void) {
    World::Reset();
    Actors.clear();
    Lakitus.clear();
    Objects.clear();
}

#include <libultraship.h>
#include <libultra/gbi.h>
#include "engine/CoreMath.h"

#include "Finishline.h"
#include "engine/Actor.h"
#include "engine/World.h"
#include "engine/tracks/Track.h"
#include "assets/models/common_data.h"
#include "src/port/Game.h"
#include "port/interpolation/FrameInterpolation.h"

extern "C" {
#include "macros.h"
#include "common_structs.h"
#include "racing/math_util.h"
#include "actor_types.h"
#include "racing/actors.h"
extern f32 gKartHopInitialVelocityTable[];
extern f32 gKartGravityTable[];
}

size_t AFinishline::_count = 0;

AFinishline::AFinishline(const SpawnParams& params) : AActor(params) {
    Name = "Finishline";
    ResourceName = "mk:finishline";
    _idx = _count;

    if (params.Location.has_value()) {
        FVector pos = params.Location.value_or(FVector(0, 0, 0));
        // Set spawn point to the provided position
        Pos[0] = D_8015F8D0[0] = pos.x;
        Pos[1] = D_8015F8D0[1] = pos.y - 15;
        Pos[2] = D_8015F8D0[2] = pos.z;
    } else {
        // Set spawn point to the tracks first path point.
        Pos[0] = D_8015F8D0[0] = gCurrentTrackPath->x;
        Pos[1] = D_8015F8D0[1] = (f32) (gCurrentTrackPath->y - 15);
        Pos[2] = D_8015F8D0[2] = gCurrentTrackPath->z;
    }

    IRotator rot = params.Rotation.value_or(IRotator(0, 0, 0));

    Rot[0] = rot.pitch;
    Rot[1] = rot.yaw;
    Rot[2] = rot.roll;

    Flags = -0x8000 | 0x4000;

    BoundingBoxSize = 0.0f;

    _count +=1;
}

void AFinishline::BeginPlay() {
    // Prevent collision mesh from being generated extra times.
    if (Editor_IsEnabled()) {
        if (Triangles.size() == 0) {
            TrackEditor::GenerateCollisionMesh(this, (Gfx*)LOAD_ASSET_RAW(D_0D001B90), 1.0f);
        }
    }
}

void AFinishline::Tick() {
}

void AFinishline::Draw(Camera *camera) {
    Mat4 mtx;
    s16 temp = Pos[2];
    s32 maxObjectsReached;

    if (gGamestate == CREDITS_SEQUENCE) {
        return;
    }

    FrameInterpolation_RecordOpenChild("finishline", (_idx << 4) | (camera->cameraId));

    mtxf_pos_rotation_xyz(mtx, Pos, Rot);

    maxObjectsReached = render_set_position(mtx, 0) == 0;
    if (maxObjectsReached) {
        FrameInterpolation_RecordCloseChild();
        return;
    }

    // The mod supplies its own banner palette and eight tiles. Keep the
    // stock geometry and posts, except for Secret Slide's wall-hung banner.
    Track* track = GetWorld()->GetTrack();
    const bool amped = track && track->ResourceName.rfind("amped:course_", 0) == 0 &&
                       track->ResourceName != "amped:course_08";
    const bool wallHung = amped && track->ResourceName == "amped:course_11";
    if (amped) {
        const bool front = temp < camera->pos[2];
        const char* model = front ? (bFog ? D_0D001C20 : D_0D001B90)
                                  : (bFog ? D_0D001C88 : D_0D001BD8);
        if (bFog) {
            gDPSetFogColor(gDisplayListHead++, gFogColour.r, gFogColour.g, gFogColour.b, 0xFF);
        }
        const Gfx* commands = (const Gfx*)LOAD_ASSET_RAW(model);
        if (commands) {
            const int post = bFog ? (front ? 8 : 5) : (front ? 6 : 3);
            const int banner = front ? (bFog ? 5 : 3) : -1;
            const int count = bFog ? (front ? 12 : 11) : 8;
            for (int i = 0; i < count; ++i) {
                if (i == post) {
                    if (!wallHung) {
                        // The stock post geometry is single-sided; Amped's banner
                        // can be approached from either side of the start line.
                        gSPClearGeometryMode(gDisplayListHead++, G_CULL_BOTH);
                        gSPDisplayList(gDisplayListHead++, (Gfx*) common_model_finish_post);
                        gSPSetGeometryMode(gDisplayListHead++, G_CULL_BACK);
                    }
                    continue;
                }
                if (i == banner) {
                    gSPDisplayList(gDisplayListHead++, (Gfx*) "__OTR__tracks/amped_11/obj_amped_start_banner");
                } else {
                    *gDisplayListHead++ = commands[i];
                }
            }
        }
        FrameInterpolation_RecordCloseChild();
        return;
    }

    if (temp < camera->pos[2]) {
        if (bFog) {

            gDPSetFogColor(gDisplayListHead++, gFogColour.r, gFogColour.g, gFogColour.b, 0xFF);
            gSPDisplayList(gDisplayListHead++, (Gfx*) D_0D001C20);
        } else {
            gSPDisplayList(gDisplayListHead++, (Gfx*) D_0D001B90);
        }
    } else if (bFog) {
        gDPSetFogColor(gDisplayListHead++, gFogColour.r, gFogColour.g, gFogColour.b, 0xFF);
        gSPDisplayList(gDisplayListHead++, (Gfx*) D_0D001C88);
    } else {
        gSPDisplayList(gDisplayListHead++, (Gfx*) D_0D001BD8);
    }

    FrameInterpolation_RecordCloseChild();
}

void AFinishline::Collision(Player* player, AActor* actor) {
}

bool AFinishline::IsMod() {
    return true;
}

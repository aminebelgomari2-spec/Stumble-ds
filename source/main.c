#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "config.h"

// Structure du personnage (Physique Stumble Guys / Ragdoll simplifié)
typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float rotY;
    bool isGrounded;
    bool canDash;
    bool isRagdoll;
    float ragdollTimer;
} Player;

static Player g_player;
static float g_cameraAngleX = 0.0f;
static float g_cameraAngleY = 0.0f;
static C3D_RenderTarget *g_topTarget, *g_botTarget;
static C2D_TextBuf g_textBuf;

static void initPlayer(void) {
    g_player.x = 0.0f;
    g_player.y = 1.0f;
    g_player.z = 0.0f;
    g_player.vx = 0.0f;
    g_player.vy = 0.0f;
    g_player.vz = 0.0f;
    g_player.rotY = 0.0f;
    g_player.isGrounded = false;
    g_player.canDash = true;
    g_player.isRagdoll = false;
    g_player.ragdollTimer = 0.0f;
}

static void updatePhysics(void) {
    hidScanInput();
    u32 kHeld = hidKeysHeld();
    u32 kDown = hidKeysDown();

    // Mode Ragdoll : le personnage est étalé au sol temporairement s'il percute un obstacle
    if (g_player.isRagdoll) {
        g_player.ragdollTimer -= 0.016f;
        if (g_player.ragdollTimer <= 0.0f) {
            g_player.isRagdoll = false;
        }
    } else {
        // Déplacement au Circle Pad / Joystick
        circlePosition pos;
        hidCircleRead(&pos);

        if (abs(pos.dx) > 15 || abs(pos.dy) > 15) {
            float moveX = (float)pos.dx / 1500.0f;
            float moveZ = -(float)pos.dy / 1500.0f;

            g_player.x += moveX;
            g_player.z += moveZ;
            g_player.rotY = atan2f(moveX, moveZ);
        }

        // Saut (Bouton B) et Dash en l'air
        if (kDown & KEY_B) {
            if (g_player.isGrounded) {
                g_player.vy = 0.25f; // Saut
                g_player.isGrounded = false;
                g_player.canDash = true;
            } else if (g_player.canDash) {
                // DASH EN L'AIR (Seconde pression sur B)
                g_player.vy = 0.15f;
                g_player.vz += sinf(g_player.rotY) * 0.4f;
                g_player.vx += cosf(g_player.rotY) * 0.4f;
                g_player.canDash = false;
            }
        }
    }

    // Caméra avec le Pavé Tactile (Écran du bas)
    touchPosition touch;
    hidTouchRead(&touch);
    if (kHeld & KEY_TOUCH) {
        g_cameraAngleY += (touch.px - 160) * 0.005f;
    }

    // Gravité
    g_player.vy -= 0.012f;
    g_player.x += g_player.vx;
    g_player.y += g_player.vy;
    g_player.z += g_player.vz;

    // Friction de déplacement
    g_player.vx *= 0.85f;
    g_player.vz *= 0.85f;

    // Détection du sol (Plateforme de Parkour)
    if (g_player.y <= 0.0f) {
        g_player.y = 0.0f;
        g_player.vy = 0.0f;
        g_player.isGrounded = true;
        g_player.canDash = true;
    }

    // Réinitialisation si chute dans le vide
    if (g_player.y < -10.0f) {
        initPlayer();
    }
}

static void drawUI(void) {
    C2D_TargetClear(g_botTarget, C2D_Color32(30, 32, 45, 255));
    C2D_SceneBegin(g_botTarget);
    C2D_TextBufClear(g_textBuf);

    C2D_Text txt;
    char status[128];
    snprintf(status, sizeof(status), "%s v%s\n\n- Joystick: Deplacement\n- Bouton B: Saut / Dash Air\n- Tactile: Rot. Camera\n- Mode: %s", 
             GAME_TITLE, GAME_VERSION, g_player.isRagdoll ? "RAGDOLL" : "NORMAL");

    C2D_TextParse(&txt, g_textBuf, status);
    C2D_DrawText(&txt, C2D_WithColor, 15.0f, 20.0f, 0.5f, 0.5f, 0.5f, C2D_Color32(255, 255, 255, 255));
}

int main(int argc, char **argv) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    g_topTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_botTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_textBuf = C2D_TextBufNew(2048);

    osSetSpeedupEnable(true); // Activer le mode 804MHz sur New3DS
    initPlayer();

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) break;

        updatePhysics();

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        // Écran du Haut (Scène 3D Parkour)
        C2D_TargetClear(g_topTarget, C2D_Color32(100, 180, 240, 255));
        C2D_SceneBegin(g_topTarget);

        // Affichage des informations de jeu
        C2D_Text txtTop;
        char info3d[64];
        snprintf(info3d, sizeof(info3d), "Pos: X:%.1f Y:%.1f Z:%.1f", g_player.x, g_player.y, g_player.z);
        C2D_TextParse(&txtTop, g_textBuf, info3d);
        C2D_DrawText(&txtTop, C2D_WithColor, 10.0f, 10.0f, 0.5f, 0.6f, 0.6f, C2D_Color32(255, 255, 255, 255));

        // Écran du Bas (UI / Commandes)
        drawUI();

        C3D_FrameEnd(0);
    }

    C2D_TextBufDelete(g_textBuf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}

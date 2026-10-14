// Stumble Guys 3DS - parkour 3D + ragdoll + dash
// Rendu 3D logiciel (projection + tri par profondeur) via citro2d : aucun shader requis.
//
// Contrôles :
//   Circle Pad : avancer       D-Pad : tourner la caméra
//   B : sauter   |   B en l'air : DASH
//   X : ragdoll (test)   SELECT : retour checkpoint   Y : recommencer
//   START : retour au menu (START dans le menu : quitter)

#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCR_W 400
#define SCR_H 240
#define FOCAL 270.0f
#define NEAR_Z 0.25f
#define TILE 3.0f
#define MAX_SOLIDS 96
#define MAX_BARS 4
#define MAX_ITEMS 900
#define PI_F 3.14159265f
#define GRAV 26.0f
#define JUMP_V 10.0f
#define MOVE_SPEED 8.0f
#define DASH_SPEED 17.0f
#define DASH_TIME 0.22f
#define PL_HW 0.35f
#define PL_H 1.25f
#define KILL_Y -14.0f
#define EPS 0.001f
#define SKY_R 140
#define SKY_G 200
#define SKY_B 255
#define RAG_N 7
#define RAG_DT (1.0f / 60.0f)
#define BD_HX 8.0f        // demi-largeur de l'arène (16 de large)
#define BD_HZ 9.0f        // demi-longueur de l'arène (18 de long)
#define BD_CELLS 8
#define BD_LIMIT 22.0f
#define LASER_X 7.0f      // lasers sur les côtés

typedef struct { float x, y, z; } V3;

static V3 vmk(float x, float y, float z) { V3 v = { x, y, z }; return v; }
static V3 vadd(V3 a, V3 b) { return vmk(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 vsub(V3 a, V3 b) { return vmk(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 vmul(V3 a, float s) { return vmk(a.x * s, a.y * s, a.z * s); }
static float vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float vlen(V3 a) { return sqrtf(vdot(a, a)); }
static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float approachf(float v, float t, float d) {
    if (v < t) { v += d; if (v > t) v = t; }
    else if (v > t) { v -= d; if (v < t) v = t; }
    return v;
}

// ---------------------------------------------------------------- monde
typedef struct {
    float cx, cy, cz, hx, hy, hz;   // centre courant + demi-tailles
    float bx, by, bz;               // centre de base
    float ax, ay, az, speed, phase; // mouvement sinusoidal
    float dx, dy, dz;               // déplacement de la frame
    u8 r, g, b;
    u8 coll, tile;
    float bias;
    float vx, vz;     // vitesse (blocs dynamiques de Block Dash)
    u8 dyn, active;
} Solid;

typedef struct { float px, py, pz, len, thick, height, speed, angle; } Bar;

static Solid S[MAX_SOLIDS];
static int nS = 0;
static Bar Bs[MAX_BARS];
static int nB = 0;
static float gTime = 0;

enum { MODE_MENU = 0, MODE_PARKOUR = 1, MODE_BLOCK = 2 };
static int gMode = MODE_MENU;
static float bestPk = 0, bestBD = 0;
static int bestBDWave = 0;

static void addBoxSolid(float cx, float cy, float cz, float hx, float hy, float hz,
                        u8 r, u8 g, u8 b, int coll, int tile, float bias) {
    if (nS >= MAX_SOLIDS) return;
    Solid *s = &S[nS++];
    memset(s, 0, sizeof(*s));
    s->cx = s->bx = cx; s->cy = s->by = cy; s->cz = s->bz = cz;
    s->hx = hx; s->hy = hy; s->hz = hz;
    s->r = r; s->g = g; s->b = b;
    s->coll = coll; s->tile = tile; s->bias = bias;
}

static void plat(float x0, float x1, float z0, float z1, float top, float thick, u8 r, u8 g, u8 b) {
    addBoxSolid((x0 + x1) / 2, top - thick / 2, (z0 + z1) / 2,
                (x1 - x0) / 2, thick / 2, (z1 - z0) / 2, r, g, b, 1, 1, 0);
}

static void mover(float ax, float ay, float az, float speed, float phase) {
    Solid *s = &S[nS - 1];
    s->ax = ax; s->ay = ay; s->az = az; s->speed = speed; s->phase = phase;
}

static void flag(float x, float top, float z, u8 r, u8 g, u8 b) {
    addBoxSolid(x, top + 1.2f, z, 0.08f, 1.2f, 0.08f, 240, 240, 240, 0, 0, 1.5f);
    addBoxSolid(x + 0.5f, top + 2.0f, z, 0.5f, 0.3f, 0.04f, r, g, b, 0, 0, 1.5f);
}

static void addBar(float px, float py, float pz, float len, float speed, float angle) {
    if (nB >= MAX_BARS) return;
    Bar *b = &Bs[nB++];
    b->px = px; b->py = py; b->pz = pz; b->len = len;
    b->thick = 0.7f; b->height = 1.0f; b->speed = speed; b->angle = angle;
    addBoxSolid(px, py + 0.7f, pz, 0.5f, 0.7f, 0.5f, 240, 240, 240, 1, 0, 1.5f);
}

static const V3 CPPOS[5] = { {0, 0.02f, 2}, {0, 1.52f, 78}, {0, 1.52f, 115}, {0, 2.02f, 151}, {0, 4.02f, 203} };
static const float CPZ[5] = { -999, 75, 113, 150, 200 };
#define LEVEL_END_Z 208.0f
#define LEVEL_LEN 218.0f

static void buildParkour(void) {
    nS = 0; nB = 0;
    // départ
    plat(-6, 6, -4, 14, 0, 1.5f, 90, 200, 120);
    plat(-1.6f, 1.6f, 14, 34, 0, 1.5f, 250, 160, 60);
    // plateformes à sauter
    for (int i = 0; i < 6; i++) {
        float cx = (i == 0) ? 0 : ((i & 1) ? 1.5f : -1.5f);
        plat(cx - 1.5f, cx + 1.5f, 38 + i * 6, 41 + i * 6, 0.3f * i, 1.5f, 80, 170, 250);
    }
    // checkpoint 1
    plat(-5, 5, 74, 86, 1.5f, 1.5f, 120, 220, 140);
    flag(3.5f, 1.5f, 78, 60, 200, 80);
    // plateformes mobiles
    addBoxSolid(0, 1.0f, 91, 1.8f, 0.5f, 1.8f, 255, 200, 60, 1, 1, 0.5f);
    mover(3.0f, 0, 0, 1.3f, 0);
    addBoxSolid(0, 1.0f, 97, 1.8f, 0.5f, 1.8f, 255, 140, 60, 1, 1, 0.5f);
    mover(-3.0f, 0, 0, 1.3f, 0);
    // arène aux barres tournantes
    plat(-5, 5, 102, 112, 1.5f, 1.5f, 120, 220, 140);
    plat(-9, 9, 112, 150, 1.5f, 1.5f, 200, 120, 240);
    flag(6.5f, 1.5f, 115, 60, 200, 80);
    addBar(0, 1.5f, 126, 8.5f, 1.5f, 0);
    addBar(0, 1.5f, 140, 8.5f, -2.0f, 1.0f);
    // escalier
    for (int i = 0; i < 6; i++)
        plat(-4, 4, 150 + 3 * i, 153 + 3 * i, 1.5f + 0.5f * (i + 1), 3.0f,
             (i & 1) ? 255 : 255, (i & 1) ? 215 : 170, (i & 1) ? 90 : 70);
    // poutre + bloqueurs
    plat(-1.2f, 1.2f, 168, 198, 4.0f, 1.5f, 250, 110, 110);
    for (int i = 0; i < 3; i++) {
        addBoxSolid(0, 4.45f, 174 + 8 * i, 0.9f, 0.45f, 0.5f, 60, 60, 70, 1, 0, 1.5f);
        mover(2.4f, 0, 0, 2.2f, i * 2.0f);
    }
    // arrivée
    plat(-6, 6, 201, 218, 4.0f, 1.5f, 120, 220, 140);
    addBoxSolid(-5.5f, 6.0f, 212, 0.3f, 2.0f, 0.3f, 255, 255, 255, 0, 0, 1.5f);
    addBoxSolid(5.5f, 6.0f, 212, 0.3f, 2.0f, 0.3f, 255, 255, 255, 0, 0, 1.5f);
    addBoxSolid(0, 8.2f, 212, 6.0f, 0.3f, 0.3f, 255, 80, 80, 0, 0, 1.5f);
    addBoxSolid(0, 4.01f, 212, 5.5f, 0.01f, 0.6f, 30, 30, 30, 0, 0, 1.0f);
}

static void updateSolids(float t, float dt) {
    for (int i = 0; i < nS; i++) {
        Solid *s = &S[i];
        if (s->dyn) {
            if (!s->active) { s->dx = s->dy = s->dz = 0; continue; }
            s->dx = s->vx * dt; s->dy = 0; s->dz = s->vz * dt;
            s->cx += s->dx; s->cz += s->dz;
            if (fabsf(s->cx) > BD_LIMIT || fabsf(s->cz) > BD_LIMIT) { s->active = 0; s->coll = 0; s->cy = -100; }
            continue;
        }
        if (s->speed == 0) { s->dx = s->dy = s->dz = 0; continue; }
        float k = sinf(t * s->speed + s->phase);
        float nx = s->bx + s->ax * k, ny = s->by + s->ay * k, nz = s->bz + s->az * k;
        s->dx = nx - s->cx; s->dy = ny - s->cy; s->dz = nz - s->cz;
        s->cx = nx; s->cy = ny; s->cz = nz;
    }
}

// ---------------------------------------------------------------- Block Dash
#define BD_POOL 56
#define MAX_SPK 24
#define BD_LOW_H 1.0f
#define BD_TALL_H 2.2f
#define SPK_H 0.9f

typedef struct { float cx, cz, vx, vz, hx, hz; u8 active; } Spike;
static Spike Spk[MAX_SPK];
static struct { int wave, over; float spawnT; } bd;
static unsigned rngState = 12345u;

static unsigned rnd(void) { rngState = rngState * 1664525u + 1013904223u; return rngState >> 8; }

static void buildBlockDash(void) {
    nS = 0; nB = 0;
    plat(-BD_HX, BD_HX, -BD_HZ, BD_HZ, 0, 2.0f, 105, 115, 235);
    for (int k = 0; k < 4; k++)   // poteaux des lasers
        addBoxSolid((k & 1) ? LASER_X : -LASER_X, 1.1f, (k & 2) ? BD_HZ - 0.3f : -BD_HZ + 0.3f,
                    0.2f, 1.1f, 0.2f, 60, 60, 85, 0, 0, 1.5f);
    for (int i = 0; i < BD_POOL; i++) {
        addBoxSolid(0, -100, 0, 1, 0.5f, 0.5f, 255, 170, 60, 0, 0, 1.5f);
        S[nS - 1].dyn = 1;
    }
}

static void bdReset(void) {
    for (int i = 0; i < nS; i++)
        if (S[i].dyn) { S[i].active = 0; S[i].coll = 0; S[i].cy = -100; S[i].dx = S[i].dz = 0; }
    memset(Spk, 0, sizeof(Spk));
    bd.wave = 0; bd.over = 0; bd.spawnT = 1.5f;
    rngState = (unsigned)osGetTime() | 1u;
}

static Solid *freeBlock(void) {
    for (int i = 0; i < nS; i++) if (S[i].dyn && !S[i].active) return &S[i];
    return NULL;
}
static Spike *freeSpike(void) {
    for (int i = 0; i < MAX_SPK; i++) if (!Spk[i].active) return &Spk[i];
    return NULL;
}

static void spawnWall(void) {
    int wave = bd.wave++;
    float speed = fminf(10.0f, 4.5f + 0.3f * wave);
    enum { N = BD_CELLS };
    int type[N]; // 0 trou, 1 bas, 2 haut, 3 piques
    int mixed = (wave >= 4) && (rnd() % 2);
    int tall = rnd() % 2;
    for (int i = 0; i < N; i++) type[i] = mixed ? (int)(1 + rnd() % 2) : (tall ? 2 : 1);
    int gaps = 1 + (wave >= 7) + (wave >= 15);
    for (int g = 0; g < gaps; g++) {
        int w = 2 + (int)(rnd() % 2);                 // trou de 2 ou 3 cases
        int pos = 1 + (int)(rnd() % (N - 1 - w));     // jamais contre les lasers
        for (int k = 0; k < w; k++) type[pos + k] = 0;
    }
    int spikes = wave >= 2 ? 1 + wave / 6 : 0;
    if (spikes > 3) spikes = 3;
    for (int k = 0; k < spikes; k++) { int idx = (int)(rnd() % N); if (type[idx] != 0) type[idx] = 3; }

    // toujours dans le même sens : du fond (+z) vers toi (-z)
    float vz = -speed, start = BD_HZ + 8.0f;

    static const u8 pal[4][3] = { {255,170,60}, {80,200,255}, {120,230,120}, {255,120,200} };
    const u8 *col = pal[rnd() % 4];
    for (int i = 0; i < N; i++) {
        float px = -BD_HX + 1.0f + 2.0f * i, pz = start;
        if (type[i] == 1 || type[i] == 2) {
            Solid *b = freeBlock();
            if (!b) continue;
            float h = type[i] == 2 ? BD_TALL_H : BD_LOW_H;
            b->hx = 1.0f; b->hz = 0.5f; b->hy = h * 0.5f;
            b->cx = px; b->cz = pz; b->cy = h * 0.5f;
            b->vx = 0; b->vz = vz; b->coll = 1; b->active = 1;
            if (type[i] == 2) { b->r = 150; b->g = 90; b->b = 235; }
            else { b->r = col[0]; b->g = col[1]; b->b = col[2]; }
        } else if (type[i] == 3) {
            Spike *sp = freeSpike();
            if (!sp) continue;
            sp->cx = px; sp->cz = pz; sp->vx = 0; sp->vz = vz;
            sp->hx = 1.0f; sp->hz = 0.5f; sp->active = 1;
        }
    }
}

static void bdUpdate(float dt) {
    bd.spawnT -= dt;
    if (bd.spawnT <= 0) { spawnWall(); bd.spawnT = fmaxf(1.7f, 3.5f - 0.07f * bd.wave); }
    for (int i = 0; i < MAX_SPK; i++) {
        Spike *sp = &Spk[i];
        if (!sp->active) continue;
        sp->cx += sp->vx * dt; sp->cz += sp->vz * dt;
        if (fabsf(sp->cx) > BD_LIMIT || fabsf(sp->cz) > BD_LIMIT) sp->active = 0;
    }
}

// ---------------------------------------------------------------- joueur
typedef struct {
    V3 p, v;
    float yaw, walk, coyote;
    int grounded, groundSolid, dashUsed;
    float dashT, dashDX, dashDZ;
    int ragdoll;
    float ragT, ragAcc;
    V3 rp[RAG_N], ro[RAG_N];
    int cp, deaths, finished;
    float time, finishTime;
} Player;

static Player P;

static const int RC[][2] = {
    {0, 1}, {1, 2}, {1, 3}, {1, 4}, {2, 5}, {2, 6}, {0, 2}, {3, 4}, {5, 6}, {0, 3}, {0, 4}, {1, 5}, {1, 6}
};
static const float RL[] = {
    0.42f, 0.50f, 0.60f, 0.60f, 0.70f, 0.70f, 0.90f, 0.85f, 0.32f, 0.95f, 0.95f, 1.20f, 1.20f
};
#define RC_N 13

static int hitsSolid(const Solid *s, V3 p) {
    return s->coll &&
           p.x + PL_HW > s->cx - s->hx && p.x - PL_HW < s->cx + s->hx &&
           p.y + PL_H > s->cy - s->hy && p.y < s->cy + s->hy &&
           p.z + PL_HW > s->cz - s->hz && p.z - PL_HW < s->cz + s->hz;
}

static void moveAxis(int axis, float d) {
    if (d == 0) return;
    if (axis == 0) P.p.x += d; else if (axis == 1) P.p.y += d; else P.p.z += d;
    for (int i = 0; i < nS; i++) {
        const Solid *s = &S[i];
        if (!hitsSolid(s, P.p)) continue;
        if (axis == 0) {
            P.p.x = d > 0 ? s->cx - s->hx - PL_HW - EPS : s->cx + s->hx + PL_HW + EPS;
            P.v.x = 0;
        } else if (axis == 2) {
            P.p.z = d > 0 ? s->cz - s->hz - PL_HW - EPS : s->cz + s->hz + PL_HW + EPS;
            P.v.z = 0;
        } else if (d > 0) {
            P.p.y = s->cy - s->hy - PL_H - EPS;
            if (P.v.y > 0) P.v.y = 0;
        } else {
            P.p.y = s->cy + s->hy + EPS;
            P.v.y = 0;
            P.grounded = 1; P.groundSolid = i; P.dashUsed = 0;
        }
    }
}

static void depenetrate(void) {
    for (int it = 0; it < 3; it++) {
        for (int i = 0; i < nS; i++) {
            const Solid *s = &S[i];
            if (!hitsSolid(s, P.p)) continue;
            float up = (s->cy + s->hy) - P.p.y;
            float pxp = (s->cx + s->hx + PL_HW) - P.p.x, pxn = P.p.x - (s->cx - s->hx - PL_HW);
            float pzp = (s->cz + s->hz + PL_HW) - P.p.z, pzn = P.p.z - (s->cz - s->hz - PL_HW);
            float m = up; int a = 0;
            if (pxp < m) { m = pxp; a = 1; }
            if (pxn < m) { m = pxn; a = 2; }
            if (pzp < m) { m = pzp; a = 3; }
            if (pzn < m) { m = pzn; a = 4; }
            switch (a) {
                case 0: P.p.y += up + EPS; break;
                case 1: P.p.x += pxp + EPS; break;
                case 2: P.p.x -= pxn + EPS; break;
                case 3: P.p.z += pzp + EPS; break;
                default: P.p.z -= pzn + EPS; break;
            }
        }
    }
}

static void respawn(void) {
    memset(&P.v, 0, sizeof(P.v));
    P.p = (gMode == MODE_PARKOUR) ? CPPOS[P.cp] : vmk(0, 0.02f, 0);
    P.ragdoll = 0; P.dashT = 0; P.dashUsed = 0; P.grounded = 0; P.groundSolid = -1;
    P.yaw = 0;
}

static void restartAll(void) {
    P.cp = 0; P.deaths = 0; P.finished = 0; P.time = 0; P.finishTime = 0;
    if (gMode != MODE_PARKOUR) bdReset();
    respawn();
}

// ---------------------------------------------------------------- pose / ragdoll
// indices : 0 tête, 1 torse, 2 hanche, 3 main G, 4 main D, 5 pied G, 6 pied D
static void poseFromPlayer(V3 *o) {
    float sy = sinf(P.yaw), cy = cosf(P.yaw);
    V3 f = vmk(sy, 0, cy), r = vmk(cy, 0, -sy), base = P.p;
    float hs = sqrtf(P.v.x * P.v.x + P.v.z * P.v.z);
    float amp = clampf(hs / MOVE_SPEED, 0, 1);
    float sw = sinf(P.walk) * amp;
    int air = !P.grounded;
    float lean = P.dashT > 0 ? 0.55f : 0.06f + 0.14f * amp;
    V3 hip = vadd(base, vmk(0, 0.75f, 0));
    V3 chest = vadd(vadd(hip, vmk(0, 0.5f, 0)), vmul(f, lean));
    V3 head = vadd(vadd(chest, vmk(0, 0.42f, 0)), vmul(f, lean * 0.5f));
    o[0] = head; o[1] = chest; o[2] = hip;
    for (int k = 0; k < 2; k++) {
        float side = k == 0 ? -1.0f : 1.0f;
        V3 hand, foot;
        if (P.dashT > 0) {
            hand = vadd(vadd(chest, vmul(f, -0.55f)), vadd(vmul(r, 0.3f * side), vmk(0, -0.1f, 0)));
            foot = vadd(vadd(base, vmul(r, 0.15f * side)), vadd(vmul(f, -0.45f), vmk(0, 0.35f, 0)));
        } else if (air) {
            hand = vadd(vadd(chest, vmul(r, 0.55f * side)), vmk(0, 0.25f, 0));
            foot = vadd(vadd(base, vmul(r, 0.15f * side)), vadd(vmul(f, 0.15f * side), vmk(0, 0.3f, 0)));
        } else {
            hand = vadd(vadd(chest, vmul(r, 0.42f * side)), vadd(vmk(0, -0.5f, 0), vmul(f, -sw * 0.45f * side)));
            foot = vadd(vadd(base, vmul(r, 0.15f * side)),
                        vadd(vmul(f, sw * 0.45f * side), vmk(0, 0.05f + fmaxf(0, sw * side) * 0.18f, 0)));
        }
        o[3 + k] = hand; o[5 + k] = foot;
    }
}

static void startRagdoll(V3 imp) {
    V3 pose[RAG_N];
    poseFromPlayer(pose);
    for (int i = 0; i < RAG_N; i++) {
        V3 vel = vadd(P.v, vmul(imp, 0.8f + 0.1f * i));
        P.rp[i] = pose[i];
        P.ro[i] = vsub(pose[i], vmul(vel, RAG_DT));
    }
    P.ragdoll = 1; P.ragT = 0; P.ragAcc = 0; P.dashT = 0;
}

static void ragdollStep(void) {
    for (int i = 0; i < RAG_N; i++) {
        V3 vel = vmul(vsub(P.rp[i], P.ro[i]), 0.993f);
        P.ro[i] = P.rp[i];
        P.rp[i] = vadd(P.rp[i], vel);
        P.rp[i].y -= GRAV * RAG_DT * RAG_DT;
    }
    for (int it = 0; it < 4; it++) {
        for (int c = 0; c < RC_N; c++) {
            int a = RC[c][0], b = RC[c][1];
            V3 d = vsub(P.rp[b], P.rp[a]);
            float len = vlen(d);
            if (len < 1e-5f) continue;
            float k = (len - RL[c]) / len * 0.5f;
            P.rp[a] = vadd(P.rp[a], vmul(d, k));
            P.rp[b] = vsub(P.rp[b], vmul(d, k));
        }
    }
    const float R = 0.14f;
    for (int i = 0; i < RAG_N; i++) {
        V3 *p = &P.rp[i], *o = &P.ro[i];
        for (int j = 0; j < nS; j++) {
            const Solid *s = &S[j];
            if (!s->coll) continue;
            if (p->x < s->cx - s->hx - R || p->x > s->cx + s->hx + R ||
                p->y < s->cy - s->hy - R || p->y > s->cy + s->hy + R ||
                p->z < s->cz - s->hz - R || p->z > s->cz + s->hz + R) continue;
            float up = (s->cy + s->hy + R) - p->y, dn = p->y - (s->cy - s->hy - R);
            float xp = (s->cx + s->hx + R) - p->x, xn = p->x - (s->cx - s->hx - R);
            float zp = (s->cz + s->hz + R) - p->z, zn = p->z - (s->cz - s->hz - R);
            float m = up; int a = 0;
            if (dn < m) { m = dn; a = 1; }
            if (xp < m) { m = xp; a = 2; }
            if (xn < m) { m = xn; a = 3; }
            if (zp < m) { m = zp; a = 4; }
            if (zn < m) { m = zn; a = 5; }
            switch (a) {
                case 0: p->y += up; o->y = p->y; o->x += (p->x - o->x) * 0.25f; o->z += (p->z - o->z) * 0.25f; break;
                case 1: p->y -= dn; o->y = p->y; break;
                case 2: p->x += xp; o->x = p->x; break;
                case 3: p->x -= xn; o->x = p->x; break;
                case 4: p->z += zp; o->z = p->z; break;
                default: p->z -= zn; o->z = p->z; break;
            }
        }
    }
}

static void updateRagdoll(float dt) {
    P.ragT += dt;
    P.ragAcc += dt;
    int n = 0;
    while (P.ragAcc >= RAG_DT && n < 3) { ragdollStep(); P.ragAcc -= RAG_DT; n++; }
    if (P.ragAcc > RAG_DT) P.ragAcc = 0;
    V3 hip = P.rp[2];
    P.p = vmk(hip.x, hip.y - 0.75f, hip.z);
    P.v = vmk(0, 0, 0);
    if (P.ragT > 2.0f) {
        float fy = fminf(P.rp[5].y, P.rp[6].y);
        P.p = vmk(hip.x, fy - 0.05f, hip.z);
        P.ragdoll = 0; P.dashUsed = 0; P.grounded = 0; P.groundSolid = -1;
        if (gMode == MODE_BLOCK) P.p.x = clampf(P.p.x, -(LASER_X - 0.6f), LASER_X - 0.6f);
        depenetrate();
    }
}

static void checkBars(void) {
    if (P.ragdoll) return;
    for (int i = 0; i < nB; i++) {
        const Bar *b = &Bs[i];
        float rx = P.p.x - b->px, rz = P.p.z - b->pz;
        float c = cosf(b->angle), s = sinf(b->angle);
        float lx = rx * c + rz * s, lz = -rx * s + rz * c;
        if (fabsf(lx) < b->len + PL_HW && fabsf(lz) < b->thick * 0.5f + PL_HW &&
            P.p.y < b->py + b->height && P.p.y + PL_H > b->py) {
            V3 imp = vmk(-rz * b->speed * 1.3f, 0, rx * b->speed * 1.3f);
            float m = vlen(imp);
            if (m > 20) imp = vmul(imp, 20 / m);
            if (m < 4) { imp.x += rx * 2.0f; imp.z += rz * 2.0f; }
            imp.y = 7.0f;
            startRagdoll(imp);
            return;
        }
    }
}

// Lasers sur les côtés : ils te renvoient vers le centre en ragdoll.
static void checkLasers(void) {
    if (P.ragdoll) return;
    if (fabsf(P.p.x) + PL_HW > LASER_X && P.p.y < 2.2f && P.p.y + PL_H > 0 &&
        fabsf(P.p.z) < BD_HZ + 0.5f) {
        float side = P.p.x > 0 ? -1.0f : 1.0f;
        P.v.x = 0; P.v.z *= 0.3f;           // on annule l'élan qui pousse vers l'extérieur
        startRagdoll(vmk(side * 6.0f, 6.5f, 0));
    }
}

// Seuls les piques provoquent un ragdoll (les murs, eux, poussent simplement).
static void checkSpikes(void) {
    if (P.ragdoll) return;
    for (int i = 0; i < MAX_SPK; i++) {
        const Spike *sp = &Spk[i];
        if (!sp->active) continue;
        if (fabsf(P.p.x - sp->cx) < sp->hx + PL_HW && fabsf(P.p.z - sp->cz) < sp->hz + PL_HW &&
            P.p.y < SPK_H && P.p.y + PL_H > 0) {
            startRagdoll(vmk(sp->vx * 0.9f, 6.5f, sp->vz * 0.9f));
            return;
        }
    }
}

// ---------------------------------------------------------------- mise à jour joueur
static float camYaw = 0, camPitch = 0.5f, camDist = 6.5f;
static V3 camTarget;

static void stepPlayer(float dt, circlePosition cp, u32 kDown) {
    P.time += dt;

    if (kDown & KEY_X) { if (!P.ragdoll) startRagdoll(vmk(P.v.x * 0.5f, 6, P.v.z * 0.5f)); }

    updateSolids(gTime, dt);
    if (gMode == MODE_BLOCK) bdUpdate(dt);

    if (P.ragdoll) {
        updateRagdoll(dt);
    } else {
        if (P.grounded && P.groundSolid >= 0) {
            const Solid *g = &S[P.groundSolid];
            P.p.x += g->dx; P.p.y += g->dy; P.p.z += g->dz;
        }
        depenetrate();

        float sx = cp.dx / 156.0f, sy = cp.dy / 156.0f;
        float mag = sqrtf(sx * sx + sy * sy);
        if (mag < 0.14f) { sx = sy = 0; mag = 0; }
        if (mag > 1) { sx /= mag; sy /= mag; mag = 1; }
        float cyw = cosf(camYaw), syw = sinf(camYaw);
        float wx = syw * sy + cyw * sx;
        float wz = cyw * sy - syw * sx;

        if (P.grounded) P.coyote = 0.12f; else P.coyote -= dt;

        if (P.dashT > 0) {
            P.dashT -= dt;
            P.v.x = P.dashDX * DASH_SPEED;
            P.v.z = P.dashDZ * DASH_SPEED;
            if (P.dashT <= 0) { P.v.x *= 0.55f; P.v.z *= 0.55f; }
        } else {
            float tx = wx * MOVE_SPEED, tz = wz * MOVE_SPEED;
            float acc = (mag > 0 ? (P.grounded ? 55.0f : 28.0f) : (P.grounded ? 70.0f : 6.0f)) * dt;
            P.v.x = approachf(P.v.x, tx, acc);
            P.v.z = approachf(P.v.z, tz, acc);
        }

        if (kDown & KEY_B) {
            if (P.grounded || P.coyote > 0) {
                P.v.y = JUMP_V; P.grounded = 0; P.coyote = 0; P.groundSolid = -1;
            } else if (!P.dashUsed) {
                float dx = wx, dz = wz;
                if (mag < 0.1f) { dx = sinf(P.yaw); dz = cosf(P.yaw); }
                else { float l = sqrtf(dx * dx + dz * dz); if (l > 0) { dx /= l; dz /= l; } }
                P.dashDX = dx; P.dashDZ = dz;
                P.dashT = DASH_TIME; P.dashUsed = 1;
                if (P.v.y < 3.0f) P.v.y = 3.0f;
            }
        }

        P.v.y -= (P.dashT > 0 ? GRAV * 0.2f : GRAV) * dt;
        if (P.v.y < -40) P.v.y = -40;

        float hs = sqrtf(P.v.x * P.v.x + P.v.z * P.v.z);
        if (hs > 0.8f) {
            float ty = atan2f(P.v.x, P.v.z), d = ty - P.yaw;
            while (d > PI_F) d -= 2 * PI_F;
            while (d < -PI_F) d += 2 * PI_F;
            P.yaw += d * clampf(14.0f * dt, 0, 1);
        }
        P.walk += hs * dt * 1.7f;

        moveAxis(0, P.v.x * dt);
        moveAxis(2, P.v.z * dt);
        P.grounded = 0; P.groundSolid = -1;
        moveAxis(1, P.v.y * dt);

        if (gMode == MODE_PARKOUR) checkBars();
        if (gMode == MODE_BLOCK) { checkSpikes(); checkLasers(); }
    }
    for (int i = 0; i < nB; i++) Bs[i].angle += Bs[i].speed * dt;

    if (P.p.y < KILL_Y) {
        if (gMode == MODE_BLOCK) {
            bd.over = 1;
            if (P.time > bestBD) { bestBD = P.time; bestBDWave = bd.wave; }
        } else { P.deaths++; respawn(); }
    }

    if (gMode == MODE_PARKOUR && !P.ragdoll && P.grounded) {
        for (int k = P.cp + 1; k < 5; k++)
            if (P.p.z > CPZ[k] && P.p.y > CPPOS[k].y - 1.0f) P.cp = k;
        if (!P.finished && P.p.z > LEVEL_END_Z && P.p.y > 3.0f) {
            P.finished = 1; P.finishTime = P.time;
            if (bestPk <= 0 || P.finishTime < bestPk) bestPk = P.finishTime;
        }
    }
}

// ---------------------------------------------------------------- caméra / rendu
static V3 camPos, camR, camU, camF;

static void updateCamera(float dt, u32 held) {
    if (held & KEY_DRIGHT) camYaw += 2.4f * dt;
    if (held & KEY_DLEFT)  camYaw -= 2.4f * dt;
    if (held & KEY_DDOWN)  camPitch += 1.4f * dt;
    if (held & KEY_DUP)    camPitch -= 1.4f * dt;
    camPitch = clampf(camPitch, 0.08f, 1.3f);
    V3 want = vadd(P.p, vmk(0, 1.0f, 0));
    float k = clampf(12.0f * dt, 0, 1);
    camTarget = vadd(camTarget, vmul(vsub(want, camTarget), k));
    float cy = cosf(camYaw), sy = sinf(camYaw), cp = cosf(camPitch), sp = sinf(camPitch);
    camF = vmk(cp * sy, -sp, cp * cy);
    camR = vmk(cy, 0, -sy);
    camU = vmk(sp * sy, cp, sp * cy);
    camPos = vsub(camTarget, vmul(camF, camDist));
}

static V3 toCam(V3 w) {
    V3 d = vsub(w, camPos);
    return vmk(vdot(d, camR), vdot(d, camU), vdot(d, camF));
}

static void drawPoly(const V3 *pts, int n, u32 clr) {
    V3 out[20];
    int m = 0;
    for (int i = 0; i < n; i++) {
        V3 a = pts[i], b = pts[(i + 1) % n];
        int ia = a.z >= NEAR_Z, ib = b.z >= NEAR_Z;
        if (ia) out[m++] = a;
        if (ia != ib) {
            float t = (NEAR_Z - a.z) / (b.z - a.z);
            out[m++] = vmk(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, NEAR_Z);
        }
    }
    if (m < 3) return;
    float sx[20], sy[20];
    for (int i = 0; i < m; i++) {
        sx[i] = SCR_W / 2 + FOCAL * out[i].x / out[i].z;
        sy[i] = SCR_H / 2 - FOCAL * out[i].y / out[i].z;
    }
    for (int i = 1; i < m - 1; i++)
        C2D_DrawTriangle(sx[0], sy[0], clr, sx[i], sy[i], clr, sx[i + 1], sy[i + 1], clr, 0);
}

static u32 shade(u8 r, u8 g, u8 b, float bright, float dist) {
    float f = clampf((dist - 30.0f) / 35.0f, 0, 1);
    float rr = r * bright, gg = g * bright, bb = b * bright;
    rr += (SKY_R - rr) * f; gg += (SKY_G - gg) * f; bb += (SKY_B - bb) * f;
    return C2D_Color32((u8)clampf(rr, 0, 255), (u8)clampf(gg, 0, 255), (u8)clampf(bb, 0, 255), 255);
}

typedef struct {
    float key, dist, cx, cy, cz, hx, hy, hz, yaw;
    u8 r, g, b, kind, mask;
} Item;

static Item items[MAX_ITEMS];
static int nI = 0;

static void pushBox(float dist, float bias, float cx, float cy, float cz, float hx, float hy, float hz,
                    float yaw, u8 r, u8 g, u8 b, u8 mask) {
    if (nI >= MAX_ITEMS) return;
    Item *it = &items[nI++];
    float kd = fmaxf(0, dist - bias);
    it->key = kd * kd; it->dist = dist;
    it->cx = cx; it->cy = cy; it->cz = cz; it->hx = hx; it->hy = hy; it->hz = hz;
    it->yaw = yaw; it->r = r; it->g = g; it->b = b; it->kind = 0; it->mask = mask;
}

static void pushPyramid(float dist, float cx, float cy, float cz, float half, float height) {
    int before = nI;
    pushBox(dist, 1.0f, cx, cy, cz, half, height, half, 0, 235, 45, 45, 0);
    if (nI > before) items[nI - 1].kind = 3;
}

static void pushLaser(float side, float dist) {
    int before = nI;
    pushBox(dist, 9.0f, side * LASER_X, 1.1f, 0, 0.05f, 1.1f, BD_HZ, 0, 255, 40, 80, 0);
    if (nI > before) items[nI - 1].kind = 4;
}

static void pushSpecial(int kind, float dist, float bias) {
    if (nI >= MAX_ITEMS) return;
    Item *it = &items[nI++];
    float kd = fmaxf(0, dist - bias);
    it->key = kd * kd; it->dist = dist; it->kind = kind;
}

static int cmpItem(const void *a, const void *b) {
    float ka = ((const Item *)a)->key, kb = ((const Item *)b)->key;
    return (ka < kb) - (ka > kb); // du plus loin au plus proche
}

static int visibleCenter(float cx, float cy, float cz, float margin, float *dist) {
    V3 d = vmk(cx - camPos.x, cy - camPos.y, cz - camPos.z);
    float zc = vdot(d, camF), xc = vdot(d, camR);
    if (zc < -margin) return 0;
    if (fabsf(xc) > (zc + margin) * 0.95f + margin) return 0;
    float dd = vdot(d, d);
    if (dd > 60.0f * 60.0f) return 0;
    *dist = sqrtf(dd);
    return 1;
}

static void buildItems(void) {
    nI = 0;
    for (int i = 0; i < nS; i++) {
        const Solid *s = &S[i];
        float dist;
        if (!s->tile) {
            if (!visibleCenter(s->cx, s->cy, s->cz, s->hx + s->hz + 3.0f, &dist)) continue;
            pushBox(dist, s->bias, s->cx, s->cy, s->cz, s->hx, s->hy, s->hz, 0, s->r, s->g, s->b, 63);
            continue;
        }
        int nx = (int)ceilf(2 * s->hx / TILE), nz = (int)ceilf(2 * s->hz / TILE);
        if (nx < 1) nx = 1;
        if (nz < 1) nz = 1;
        float tw = 2 * s->hx / nx, td = 2 * s->hz / nz;
        for (int a = 0; a < nx; a++) {
            for (int b = 0; b < nz; b++) {
                float cx = s->cx - s->hx + (a + 0.5f) * tw;
                float cz = s->cz - s->hz + (b + 0.5f) * td;
                if (!visibleCenter(cx, s->cy, cz, 3.5f, &dist)) continue;
                u8 mask = (1 << 2) | (1 << 3);
                if (a == 0) mask |= 1 << 0;
                if (a == nx - 1) mask |= 1 << 1;
                if (b == 0) mask |= 1 << 4;
                if (b == nz - 1) mask |= 1 << 5;
                float k = ((a + b) & 1) ? 0.86f : 1.0f;
                pushBox(dist, s->bias, cx, s->cy, cz, tw / 2 + 0.01f, s->hy, td / 2 + 0.01f, 0,
                        (u8)(s->r * k), (u8)(s->g * k), (u8)(s->b * k), mask);
            }
        }
    }
    for (int i = 0; i < nB; i++) {
        const Bar *b = &Bs[i];
        float dist;
        if (!visibleCenter(b->px, b->py + b->height * 0.5f, b->pz, b->len + 3.0f, &dist)) continue;
        pushBox(dist, 4.0f, b->px, b->py + b->height * 0.5f, b->pz, b->len, b->height * 0.5f, b->thick * 0.5f,
                b->angle, 225, 55, 55, 63);
    }
    for (int i = 0; i < MAX_SPK; i++) {
        const Spike *sp = &Spk[i];
        float dist;
        if (!sp->active) continue;
        if (!visibleCenter(sp->cx, 0.4f, sp->cz, 3.0f, &dist)) continue;
        float ox = sp->hx > sp->hz ? 0.5f : 0.0f, oz = sp->hx > sp->hz ? 0.0f : 0.5f;
        pushPyramid(dist, sp->cx - ox, 0, sp->cz - oz, 0.45f, SPK_H);
        pushPyramid(dist, sp->cx + ox, 0, sp->cz + oz, 0.45f, SPK_H);
    }
    if (gMode != MODE_PARKOUR) {
        for (int k = -1; k <= 1; k += 2) {
            float dist;
            if (visibleCenter(k * LASER_X, 1.1f, 0, BD_HZ + 3.0f, &dist)) pushLaser((float)k, dist);
        }
    }
    V3 c = vadd(P.p, vmk(0, 0.8f, 0));
    float cd = vlen(vsub(c, camPos));
    pushSpecial(1, cd, 2.4f);
    pushSpecial(2, cd, 2.2f);
}

static void drawItemBox(const Item *it) {
    float c = cosf(it->yaw), s = sinf(it->yaw);
    V3 ax[3] = { vmk(c, 0, s), vmk(0, 1, 0), vmk(-s, 0, c) };
    float h[3] = { it->hx, it->hy, it->hz };
    V3 ctr = vmk(it->cx, it->cy, it->cz);
    const V3 L = { -0.32f, 0.84f, -0.43f };
    for (int k = 0; k < 3; k++) {
        for (int sg = -1; sg <= 1; sg += 2) {
            int idx = k * 2 + (sg > 0);
            if (!(it->mask & (1 << idx))) continue;
            V3 n = vmul(ax[k], (float)sg);
            V3 fc = vadd(ctr, vmul(n, h[k]));
            if (vdot(n, vsub(camPos, fc)) <= 0) continue;
            int u = (k + 1) % 3, v = (k + 2) % 3;
            V3 U = vmul(ax[u], h[u]), W = vmul(ax[v], h[v]);
            V3 w[4];
            w[0] = toCam(vsub(vsub(fc, U), W));
            w[1] = toCam(vsub(vadd(fc, U), W));
            w[2] = toCam(vadd(vadd(fc, U), W));
            w[3] = toCam(vadd(vsub(fc, U), W));
            float bright = 0.58f + 0.42f * fmaxf(0, vdot(n, L));
            drawPoly(w, 4, shade(it->r, it->g, it->b, bright, it->dist));
        }
    }
}

static V3 vcross(V3 a, V3 b) {
    return vmk(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static void drawPyramid(const Item *it) {
    float h = it->hx;
    V3 ctr = vmk(it->cx, it->cy, it->cz);
    V3 ap = vmk(it->cx, it->cy + it->hy, it->cz);
    V3 bc[4] = { vmk(ctr.x - h, ctr.y, ctr.z - h), vmk(ctr.x + h, ctr.y, ctr.z - h),
                 vmk(ctr.x + h, ctr.y, ctr.z + h), vmk(ctr.x - h, ctr.y, ctr.z + h) };
    const V3 L = { -0.32f, 0.84f, -0.43f };
    for (int i = 0; i < 4; i++) {
        V3 a = bc[i], b = bc[(i + 1) % 4];
        V3 n = vcross(vsub(b, a), vsub(ap, a));
        float l = vlen(n);
        if (l < 1e-6f) continue;
        n = vmul(n, 1.0f / l);
        if (vdot(n, vsub(a, ctr)) < 0) n = vmul(n, -1.0f);
        if (vdot(n, vsub(camPos, a)) <= 0) continue;
        V3 t[3] = { toCam(a), toCam(b), toCam(ap) };
        float bright = 0.6f + 0.4f * fmaxf(0, vdot(n, L));
        drawPoly(t, 3, shade(it->r, it->g, it->b, bright, it->dist));
    }
}

static void drawLaser(const Item *it) {
    float x = it->cx, z0 = -it->hz, z1 = it->hz, top = it->hy * 2;
    float pulse = 0.5f + 0.5f * sinf(gTime * 6.0f);
    V3 q[4] = { toCam(vmk(x, 0, z0)), toCam(vmk(x, 0, z1)), toCam(vmk(x, top, z1)), toCam(vmk(x, top, z0)) };
    drawPoly(q, 4, C2D_Color32(255, 30, 70, (u8)(55 + 45 * pulse)));
    static const float bars[3] = { 0.25f, 1.05f, 1.85f };
    for (int i = 0; i < 3; i++) {
        V3 b[4] = { toCam(vmk(x, bars[i], z0)), toCam(vmk(x, bars[i], z1)),
                    toCam(vmk(x, bars[i] + 0.09f, z1)), toCam(vmk(x, bars[i] + 0.09f, z0)) };
        drawPoly(b, 4, C2D_Color32(255, 90, 120, 235));
    }
}

static void drawLine3D(V3 a, V3 b, float w, u32 clr) {
    V3 ca = toCam(a), cb = toCam(b);
    if (ca.z < NEAR_Z && cb.z < NEAR_Z) return;
    if (ca.z < NEAR_Z) { float t = (NEAR_Z - ca.z) / (cb.z - ca.z); ca = vadd(ca, vmul(vsub(cb, ca), t)); ca.z = NEAR_Z; }
    if (cb.z < NEAR_Z) { float t = (NEAR_Z - cb.z) / (ca.z - cb.z); cb = vadd(cb, vmul(vsub(ca, cb), t)); cb.z = NEAR_Z; }
    float ax = SCR_W / 2 + FOCAL * ca.x / ca.z, ay = SCR_H / 2 - FOCAL * ca.y / ca.z;
    float bx = SCR_W / 2 + FOCAL * cb.x / cb.z, by = SCR_H / 2 - FOCAL * cb.y / cb.z;
    float wa = w * 0.5f * FOCAL / ca.z, wb = w * 0.5f * FOCAL / cb.z;
    float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
    if (len > 0.5f) {
        float nx = -dy / len, ny = dx / len;
        C2D_DrawTriangle(ax + nx * wa, ay + ny * wa, clr, ax - nx * wa, ay - ny * wa, clr,
                         bx + nx * wb, by + ny * wb, clr, 0);
        C2D_DrawTriangle(ax - nx * wa, ay - ny * wa, clr, bx - nx * wb, by - ny * wb, clr,
                         bx + nx * wb, by + ny * wb, clr, 0);
    }
    C2D_DrawCircleSolid(ax, ay, 0, wa, clr);
    C2D_DrawCircleSolid(bx, by, 0, wb, clr);
}

static void drawCharacter(void) {
    V3 pt[RAG_N];
    if (P.ragdoll) memcpy(pt, P.rp, sizeof(pt)); else poseFromPlayer(pt);
    u32 pants = C2D_Color32(45, 70, 160, 255);
    u32 body = C2D_Color32(255, 140, 30, 255);
    u32 skin = C2D_Color32(255, 205, 150, 255);
    u32 glove = C2D_Color32(255, 225, 90, 255);
    drawLine3D(pt[2], pt[5], 0.24f, pants);
    drawLine3D(pt[2], pt[6], 0.24f, pants);
    drawLine3D(pt[1], pt[3], 0.20f, glove);
    drawLine3D(pt[1], pt[4], 0.20f, glove);
    drawLine3D(pt[2], pt[1], 0.46f, body);
    V3 hc = toCam(pt[0]);
    if (hc.z > NEAR_Z) {
        float sx = SCR_W / 2 + FOCAL * hc.x / hc.z, sy = SCR_H / 2 - FOCAL * hc.y / hc.z;
        float r = 0.27f * FOCAL / hc.z;
        C2D_DrawCircleSolid(sx, sy, 0, r, skin);
        // yeux (orientés dans le sens de la marche)
        float fx = sinf(P.yaw), fz = cosf(P.yaw);
        V3 eye = vadd(pt[0], vmk(fx * 0.2f, 0.04f, fz * 0.2f));
        V3 ec = toCam(eye);
        if (ec.z > NEAR_Z && vdot(vmk(fx, 0, fz), vsub(camPos, pt[0])) > 0) {
            float ex = SCR_W / 2 + FOCAL * ec.x / ec.z, ey = SCR_H / 2 - FOCAL * ec.y / ec.z;
            C2D_DrawCircleSolid(ex - r * 0.3f, ey, 0, r * 0.12f, C2D_Color32(20, 20, 20, 255));
            C2D_DrawCircleSolid(ex + r * 0.3f, ey, 0, r * 0.12f, C2D_Color32(20, 20, 20, 255));
        }
    }
}

static void drawShadow(void) {
    V3 c = P.ragdoll ? P.rp[2] : vadd(P.p, vmk(0, 0.75f, 0));
    float best = -1e9f;
    int found = 0;
    for (int i = 0; i < nS; i++) {
        const Solid *s = &S[i];
        if (!s->coll) continue;
        if (c.x > s->cx - s->hx - 0.2f && c.x < s->cx + s->hx + 0.2f &&
            c.z > s->cz - s->hz - 0.2f && c.z < s->cz + s->hz + 0.2f) {
            float top = s->cy + s->hy;
            if (top <= c.y + 0.3f && top > best) { best = top; found = 1; }
        }
    }
    if (!found) return;
    V3 w[10];
    float r = clampf(0.55f - (c.y - 0.75f - best) * 0.05f, 0.2f, 0.55f);
    for (int i = 0; i < 10; i++) {
        float a = i * 2 * PI_F / 10;
        w[i] = toCam(vmk(c.x + cosf(a) * r, best + 0.03f, c.z + sinf(a) * r));
    }
    drawPoly(w, 10, C2D_Color32(0, 0, 0, 90));
}

// ---------------------------------------------------------------- texte
static C2D_TextBuf gTB;

static void text(float x, float y, float sc, u32 clr, const char *s) {
    C2D_Text t;
    C2D_TextParse(&t, gTB, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, sc, sc, clr);
}

static void drawWorld(void) {
    C2D_DrawRectangle(0, 0, 0, SCR_W, SCR_H,
                      C2D_Color32(70, 140, 235, 255), C2D_Color32(70, 140, 235, 255),
                      C2D_Color32(SKY_R, SKY_G, SKY_B, 255), C2D_Color32(SKY_R, SKY_G, SKY_B, 255));
    buildItems();
    qsort(items, nI, sizeof(Item), cmpItem);
    for (int i = 0; i < nI; i++) {
        switch (items[i].kind) {
            case 0: drawItemBox(&items[i]); break;
            case 1: drawCharacter(); break;
            case 3: drawPyramid(&items[i]); break;
            case 4: drawLaser(&items[i]); break;
            default: drawShadow(); break;
        }
    }
}

static void panel(float x, float y, float w, float h, u32 clr) {
    C2D_DrawRectangle(x, y, 0, w, h, clr, clr, clr, clr);
}

static void renderTop(void) {
    drawWorld();
    char buf[96];
    u32 white = C2D_Color32(255, 255, 255, 255);
    u32 shadow = C2D_Color32(20, 20, 40, 255);
    u32 gold = C2D_Color32(255, 215, 70, 255);

    if (gMode == MODE_MENU) {
        text(42, 26, 1.15f, shadow, "STUMBLE GUYS");
        text(40, 24, 1.15f, gold, "STUMBLE GUYS");
        text(152, 74, 0.8f, shadow, "3DS");
        text(150, 72, 0.8f, white, "3DS");
        return;
    }

    float tshow = (gMode == MODE_PARKOUR && P.finished) ? P.finishTime : P.time;
    if (gMode == MODE_BLOCK) snprintf(buf, sizeof(buf), "Vague %d   %.1f s", bd.wave, tshow);
    else snprintf(buf, sizeof(buf), "%.1f s", tshow);
    text(8, 4, 0.7f, white, buf);
    if (P.dashUsed || P.dashT > 0) text(8, 24, 0.5f, C2D_Color32(255, 120, 120, 255), "DASH utilisé");
    else text(8, 24, 0.5f, C2D_Color32(120, 255, 140, 255), "DASH prêt");

    if (gMode == MODE_PARKOUR && P.finished) {
        panel(60, 80, 280, 70, C2D_Color32(0, 0, 0, 150));
        snprintf(buf, sizeof(buf), "ARRIVÉE !  %.2f s", P.finishTime);
        text(90, 92, 0.9f, gold, buf);
        text(110, 124, 0.5f, white, "Y : rejouer   START : menu");
    }
    if (gMode == MODE_BLOCK && bd.over) {
        panel(50, 70, 300, 100, C2D_Color32(0, 0, 0, 160));
        text(110, 80, 0.9f, C2D_Color32(255, 110, 110, 255), "ÉLIMINÉ !");
        snprintf(buf, sizeof(buf), "Vague %d  -  %.1f s", bd.wave, P.time);
        text(105, 112, 0.6f, white, buf);
        text(85, 140, 0.5f, white, "Y : rejouer      START : menu");
    }
}

static void renderBottom(int menuSel) {
    char buf[96];
    u32 white = C2D_Color32(255, 255, 255, 255);
    u32 grey = C2D_Color32(190, 200, 220, 255);

    if (gMode == MODE_MENU) {
        u32 on = C2D_Color32(255, 170, 40, 255), off = C2D_Color32(60, 75, 120, 255);
        panel(30, 58, 260, 60, menuSel == 0 ? on : off);
        panel(30, 128, 260, 60, menuSel == 1 ? on : off);
        text(88, 66, 0.9f, white, "PARKOUR");
        if (bestPk > 0) snprintf(buf, sizeof(buf), "Meilleur : %.1f s", bestPk); else snprintf(buf, sizeof(buf), "Cours jusqu'à l'arrivée");
        text(78, 96, 0.45f, white, buf);
        text(68, 136, 0.9f, white, "BLOCK DASH");
        if (bestBD > 0) snprintf(buf, sizeof(buf), "Record : %.1f s (vague %d)", bestBD, bestBDWave); else snprintf(buf, sizeof(buf), "Survis aux murs de blocs");
        text(72, 166, 0.45f, white, buf);
        text(20, 206, 0.45f, grey, "D-Pad : choisir    B ou toucher : lancer");
        text(20, 222, 0.42f, grey, "START : quitter");
        return;
    }

    text(10, 8, 0.7f, C2D_Color32(255, 200, 60, 255), gMode == MODE_PARKOUR ? "PARKOUR" : "BLOCK DASH");
    if (gMode == MODE_PARKOUR) {
        snprintf(buf, sizeof(buf), "Temps : %.1f s     Chutes : %d", P.finished ? P.finishTime : P.time, P.deaths);
        text(10, 38, 0.5f, white, buf);
        snprintf(buf, sizeof(buf), "Checkpoint : %d / 4", P.cp);
        text(10, 56, 0.5f, white, buf);
        float prog = clampf(P.p.z / LEVEL_LEN, 0, 1);
        panel(10, 80, 300, 10, C2D_Color32(60, 70, 90, 255));
        panel(10, 80, 300 * prog, 10, C2D_Color32(90, 220, 120, 255));
    } else {
        snprintf(buf, sizeof(buf), "Vague : %d      Temps : %.1f s", bd.wave, P.time);
        text(10, 38, 0.5f, white, buf);
        if (bestBD > 0) { snprintf(buf, sizeof(buf), "Record : %.1f s (vague %d)", bestBD, bestBDWave); text(10, 56, 0.5f, white, buf); }
        text(10, 80, 0.45f, C2D_Color32(255, 150, 150, 255), "Piques rouges et lasers : ragdoll.");
        text(10, 94, 0.45f, C2D_Color32(255, 150, 150, 255), "Les murs te poussent : reste sur la plateforme !");
    }
    text(10, 118, 0.45f, grey, "Circle Pad : avancer       D-Pad : caméra");
    text(10, 136, 0.45f, grey, "B : sauter      B en l'air : DASH");
    text(10, 154, 0.45f, grey, "X : ragdoll" );
    if (gMode == MODE_PARKOUR) text(120, 154, 0.45f, grey, "SELECT : checkpoint");
    text(10, 172, 0.45f, grey, "Y : recommencer      START : menu");
}

// ---------------------------------------------------------------- mode / main
static void enterMode(int m) {
    gMode = m;
    memset(&P, 0, sizeof(P));
    if (m == MODE_PARKOUR) { buildParkour(); camDist = 6.5f; camPitch = 0.5f; }
    else { buildBlockDash(); camDist = (m == MODE_BLOCK) ? 8.5f : 8.0f; camPitch = (m == MODE_BLOCK) ? 0.75f : 0.4f; }
    camYaw = 0;
    restartAll();
    camTarget = vadd(P.p, vmk(0, 1.0f, 0));
    updateCamera(0.0f, 0);
    if (m == MODE_MENU) { P.v = vmk(0, 0, 0); P.grounded = 1; }
}

int main(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(6000);
    C2D_Prepare();
    osSetSpeedupEnable(true);

    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    gTB = C2D_TextBufNew(4096);

    int menuSel = 0;
    enterMode(MODE_MENU);

    u64 last = osGetTime();
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown(), kHeld = hidKeysHeld();
        circlePosition cp;
        hidCircleRead(&cp);
        touchPosition tp;
        hidTouchRead(&tp);

        if (kDown & KEY_START) {
            if (gMode == MODE_MENU) break;
            enterMode(MODE_MENU);
            last = osGetTime();
            continue;
        }

        u64 now = osGetTime();
        float dt = (now - last) / 1000.0f;
        last = now;
        dt = clampf(dt, 0.001f, 0.05f);

        if (gMode == MODE_MENU) {
            int go = 0;
            if (kDown & KEY_DUP) menuSel = 0;
            if (kDown & KEY_DDOWN) menuSel = 1;
            if (kDown & (KEY_B | KEY_A)) go = 1;
            if ((kDown & KEY_TOUCH) && tp.px >= 30 && tp.px <= 290) {
                if (tp.py >= 58 && tp.py <= 118) { menuSel = 0; go = 1; }
                else if (tp.py >= 128 && tp.py <= 188) { menuSel = 1; go = 1; }
            }
            if (go) { enterMode(menuSel == 0 ? MODE_PARKOUR : MODE_BLOCK); last = osGetTime(); continue; }
            camYaw += 0.4f * dt;
            updateCamera(dt, 0);
        } else {
            if (kDown & KEY_Y) restartAll();
            if ((kDown & KEY_SELECT) && gMode == MODE_PARKOUR && !P.finished) respawn();

            int frozen = (gMode == MODE_PARKOUR && P.finished) ? 0 : (gMode == MODE_BLOCK && bd.over);
            if (!frozen) {
                int steps = dt > 0.025f ? 2 : 1;
                float h = dt / steps;
                for (int i = 0; i < steps; i++) {
                    gTime += h;
                    stepPlayer(h, cp, i == 0 ? kDown : 0);
                    if (gMode == MODE_BLOCK && bd.over) break;
                }
            }
            updateCamera(dt, kHeld);
        }

        C2D_TextBufClear(gTB);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, C2D_Color32(SKY_R, SKY_G, SKY_B, 255));
        C2D_SceneBegin(top);
        renderTop();
        C2D_TargetClear(bot, C2D_Color32(25, 30, 48, 255));
        C2D_SceneBegin(bot);
        renderBottom(menuSel);
        C3D_FrameEnd(0);
    }

    C2D_TextBufDelete(gTB);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}

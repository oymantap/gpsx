#include <sys/types.h>
#include <psxgpu.h>
#include <psxetc.h>
#include <psxapi.h>
#include <psxsio.h>
#include <psxpad.h>
#include <stdio.h>

#define SCREEN_X 320
#define SCREEN_Y 240
#define OT_LEN 8
#define PRIM_SIZE 64
#define MAX_ENEMIES 5
#define MAX_BULLETS 8

/* Game States */
typedef enum {
    STATE_INTRO,
    STATE_START_SCREEN,
    STATE_MAIN_MENU,
    STATE_GAMEPLAY,
    STATE_GAMEOVER
} GameState;

static DISPENV disp[2];
static DRAWENV draw[2];

static unsigned long ot[2][OT_LEN];
static unsigned char primbuff[2][PRIM_SIZE * 32];
static unsigned char padbuff[2][34];

static int db = 0;
static int font_id;
static GameState game_state = STATE_INTRO;

/* Entity Structs */
typedef struct {
    int x, y;
    int speed;
    int active;
} Enemy;

typedef struct {
    int x, y;
    int active;
} Bullet;

static Enemy enemies[MAX_ENEMIES];
static Bullet bullets[MAX_BULLETS];

static int player_x, player_y;
static int score;
static int frame_counter = 0;
static int menu_selection = 0; /* 0: Start, 1: Exit */

/* Boost & Cheat States */
static int boost_timer = 0;
static int god_mode = 0;
static int prev_pad_btn = 0xFFFF;

/* Cheat Sequence State: ATAS + (O, X, TRIANGLE, O) */
static int cheat_step = 0;

static void init_video(void)
{
    ResetGraph(0);

    SetDefDispEnv(&disp[0], 0, 0, SCREEN_X, SCREEN_Y);
    SetDefDispEnv(&disp[1], 0, SCREEN_Y, SCREEN_X, SCREEN_Y);

    SetDefDrawEnv(&draw[0], 0, SCREEN_Y, SCREEN_X, SCREEN_Y);
    SetDefDrawEnv(&draw[1], 0, 0, SCREEN_X, SCREEN_Y);

    draw[0].isbg = 1;
    draw[1].isbg = 1;

    setRGB0(&draw[0], 5, 8, 24);
    setRGB0(&draw[1], 5, 8, 24);

    PutDispEnv(&disp[0]);
    PutDrawEnv(&draw[0]);

    FntLoad(960, 0);
    font_id = FntOpen(8, 8, 304, 224, 0, 100);

    SetDispMask(1);
}

static void init_pad(void)
{
    EnterCriticalSection();
    InitPAD(padbuff[0], 34, padbuff[1], 34);
    StartPAD();
    ChangeClearPAD(1);
    ExitCriticalSection();
}

static void reset_game(void)
{
    int i;

    player_x = 150;
    player_y = 205;
    score = 0;
    frame_counter = 0;
    boost_timer = 0;

    for (i = 0; i < MAX_ENEMIES; ++i) {
        enemies[i].active = 0;
        enemies[i].x = 0;
        enemies[i].y = -40;
        enemies[i].speed = 2 + (i % 3);
    }

    for (i = 0; i < MAX_BULLETS; ++i) {
        bullets[i].active = 0;
    }
}

static void spawn_enemy(void)
{
    int i;
    for (i = 0; i < MAX_ENEMIES; ++i) {
        if (!enemies[i].active) {
            enemies[i].active = 1;
            enemies[i].x = 20 + ((frame_counter * 37 + i * 53) % 280);
            enemies[i].y = -18;
            enemies[i].speed = 2 + ((frame_counter + i) % 3);
            return;
        }
    }
}

static void shoot_bullet(void)
{
    int i;
    for (i = 0; i < MAX_BULLETS; ++i) {
        if (!bullets[i].active) {
            bullets[i].active = 1;
            bullets[i].x = player_x + 10; /* Pas di tengah meriam */
            bullets[i].y = player_y - 6;
            return;
        }
    }
}

static int overlap(int ax, int ay, int aw, int ah,
                   int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx &&
           ay < by + bh && ay + ah > by;
}

static void draw_tile(char **nextpri, int x, int y, int w, int h,
                      int r, int g, int b)
{
    TILE *tile = (TILE *)*nextpri;

    setTile(tile);
    setXY0(tile, x, y);
    setWH(tile, w, h);
    setRGB0(tile, r, g, b);

    addPrim(ot[db] + (OT_LEN - 1), tile);
    *nextpri += sizeof(TILE);
}

/* Deteksi tombol baru ditekan (Press Edge) */
static int is_btn_pressed(u_short btn_mask, u_short curr_btn)
{
    return (!(curr_btn & btn_mask)) && (prev_pad_btn & btn_mask);
}

static void check_cheat_code(u_short curr_btn)
{
    /* Syarat Wajib: Tombol ATAS harus ditahan (active low) */
    if (curr_btn & PAD_UP) {
        cheat_step = 0;
        return;
    }

    /* Urutan: O -> X -> TRIANGLE -> O */
    if (is_btn_pressed(PAD_CIRCLE, curr_btn)) {
        if (cheat_step == 0) cheat_step = 1;
        else if (cheat_step == 3) {
            god_mode = !god_mode; /* Toggle Immortal */
            cheat_step = 0;
        } else cheat_step = 0;
    } 
    else if (is_btn_pressed(PAD_CROSS, curr_btn)) {
        if (cheat_step == 1) cheat_step = 2;
        else cheat_step = 0;
    } 
    else if (is_btn_pressed(PAD_TRIANGLE, curr_btn)) {
        if (cheat_step == 2) cheat_step = 3;
        else cheat_step = 0;
    }
}

static void update_game(void)
{
    PADTYPE *pad = (PADTYPE *)padbuff[0];
    u_short btn;
    int i, j;

    if (pad->stat != 0)
        return;

    btn = pad->btn;
    ++frame_counter;

    /* 1. STATE INTRO */
    if (game_state == STATE_INTRO) {
        if (frame_counter > 150 || is_btn_pressed(PAD_START, btn) || is_btn_pressed(PAD_CROSS, btn)) {
            game_state = STATE_START_SCREEN;
            frame_counter = 0;
        }
    }
    /* 2. STATE START SCREEN */
    else if (game_state == STATE_START_SCREEN) {
        check_cheat_code(btn);

        if (is_btn_pressed(PAD_START, btn)) {
            game_state = STATE_MAIN_MENU;
        }
    }
    /* 3. STATE MAIN MENU */
    else if (game_state == STATE_MAIN_MENU) {
        if (is_btn_pressed(PAD_UP, btn) || is_btn_pressed(PAD_DOWN, btn)) {
            menu_selection = !menu_selection;
        }

        if (is_btn_pressed(PAD_CROSS, btn) || is_btn_pressed(PAD_START, btn)) {
            if (menu_selection == 0) {
                reset_game();
                game_state = STATE_GAMEPLAY;
            } else {
                /* Exit: kembali ke Start Screen */
                game_state = STATE_START_SCREEN;
            }
        }
    }
    /* 4. STATE GAMEPLAY */
    else if (game_state == STATE_GAMEPLAY) {
        int move_speed = (boost_timer > 0) ? 5 : 3;

        if (boost_timer > 0)
            --boost_timer;

        /* Gerakan Player */
        if (!(btn & PAD_LEFT))  player_x -= move_speed;
        if (!(btn & PAD_RIGHT)) player_x += move_speed;
        if (!(btn & PAD_UP))    player_y -= move_speed;
        if (!(btn & PAD_DOWN))  player_y += move_speed;

        if (player_x < 8) player_x = 8;
        if (player_x > 288) player_x = 288;
        if (player_y < 40) player_y = 40;
        if (player_y > 216) player_y = 216;

        /* Tembak Peluru (O / CIRCLE) */
        if (is_btn_pressed(PAD_CIRCLE, btn)) {
            shoot_bullet();
        }

        /* Aktifkan Boost Potion (R1) */
        if (is_btn_pressed(PAD_R1, btn)) {
            boost_timer = 120; /* 2 detik boost */
        }

        /* Update Peluru */
        for (i = 0; i < MAX_BULLETS; ++i) {
            if (!bullets[i].active) continue;

            bullets[i].y -= 6;
            if (bullets[i].y < 30) {
                bullets[i].active = 0;
            }
        }

        /* Spawn Musuh */
        if ((frame_counter % 40) == 0)
            spawn_enemy();

        /* Update Musuh & Tabrakan */
        for (i = 0; i < MAX_ENEMIES; ++i) {
            if (!enemies[i].active) continue;

            enemies[i].y += enemies[i].speed;

            /* Cek Kena Peluru */
            for (j = 0; j < MAX_BULLETS; ++j) {
                if (bullets[j].active && overlap(bullets[j].x, bullets[j].y, 4, 8,
                                                enemies[i].x, enemies[i].y, 18, 18)) {
                    bullets[j].active = 0;
                    enemies[i].active = 0;
                    score += 2;
                }
            }

            /* Cek Tabrakan dengan Player */
            if (enemies[i].active && overlap(player_x, player_y, 24, 18,
                                            enemies[i].x, enemies[i].y, 18, 18)) {
                if (!god_mode) {
                    game_state = STATE_GAMEOVER;
                }
            }

            if (enemies[i].y > SCREEN_Y) {
                enemies[i].active = 0;
                ++score;
            }
        }
    }
    /* 5. STATE GAMEOVER */
    else if (game_state == STATE_GAMEOVER) {
        if (is_btn_pressed(PAD_START, btn) || is_btn_pressed(PAD_CROSS, btn)) {
            game_state = STATE_MAIN_MENU;
        }
    }

    prev_pad_btn = btn;
}

static void draw_game(void)
{
    char *nextpri = (char *)primbuff[db];
    int i;

    ClearOTagR(ot[db], OT_LEN);

    if (game_state == STATE_INTRO) {
        FntPrint(font_id, "\n\n\n\n\n\n       POWERED BY RYCL\n");
    }
    else if (game_state == STATE_START_SCREEN) {
        FntPrint(font_id, "\n\n\n    NEON RUNNER PSX\n\n\n"
                          "   PRESS START BUTTON\n");
        if (god_mode) {
            FntPrint(font_id, "\n\n  [ CHEAT: GODMODE ON ]");
        }
    }
    else if (game_state == STATE_MAIN_MENU) {
        FntPrint(font_id, "\n\n\n     MAIN MENU\n\n"
                          "  %c START GAME\n"
                          "  %c EXIT\n",
                          (menu_selection == 0) ? '>' : ' ',
                          (menu_selection == 1) ? '>' : ' ');
    }
    else if (game_state == STATE_GAMEPLAY || game_state == STATE_GAMEOVER) {
        /* Render Player (Meriam) */
        draw_tile(&nextpri, player_x, player_y, 24, 18, 
                  (boost_timer > 0) ? 255 : 40, 210, 255);

        /* Render Peluru */
        for (i = 0; i < MAX_BULLETS; ++i) {
            if (bullets[i].active)
                draw_tile(&nextpri, bullets[i].x, bullets[i].y, 4, 8, 255, 255, 0);
        }

        /* Render Musuh */
        for (i = 0; i < MAX_ENEMIES; ++i) {
            if (enemies[i].active)
                draw_tile(&nextpri, enemies[i].x, enemies[i].y, 18, 18, 255, 60, 80);
        }

        /* Separator Line HUD */
        draw_tile(&nextpri, 0, 30, 320, 2, 80, 90, 150);
        draw_tile(&nextpri, 0, 232, 320, 2, 80, 90, 150);

        if (game_state == STATE_GAMEOVER) {
            FntPrint(font_id, "\n GAME OVER!\n SCORE: %d\n\n PRESS START", score);
        } else {
            FntPrint(font_id, "\n NEON RUNNER   SCORE %d   %s %s", 
                     score, 
                     (boost_timer > 0) ? "[BOOST!]" : "",
                     god_mode ? "[GOD]" : "");
        }
    }

    FntFlush(font_id);
    DrawOTag(ot[db] + (OT_LEN - 1));
}

static void display(void)
{
    DrawSync(0);
    VSync(0);

    db = !db;

    PutDispEnv(&disp[db]);
    PutDrawEnv(&draw[db]);

    SetDispMask(1);
}

int main(void)
{
    init_video();
    init_pad();

    while (1) {
        update_game();
        draw_game();
        display();
    }

    return 0;
}

#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psxgpu.h>
#include <psxetc.h>
#include <psxapi.h>
#include <psxsio.h>
#include <psxpad.h>
#include <psxcd.h>

#define SCREEN_X 320
#define SCREEN_Y 240
#define OT_LEN 8
#define PRIM_SIZE 128
#define MAX_ENEMIES 5
#define MAX_BULLETS 8

typedef enum {
    STATE_INTRO,
    STATE_START_SCREEN,
    STATE_MAIN_MENU,
    STATE_GAMEPLAY,
    STATE_GAMEOVER
} GameState;

typedef struct {
    u_short tpage, clut;
    u_char u, v;
    u_short w, h;
    int loaded;
} TextureAsset;

typedef struct {
    int x, y, speed, active;
} Enemy;

typedef struct {
    int x, y, active;
} Bullet;

typedef struct {
    int x, y, active;
} PotionItem;

static DISPENV disp[2];
static DRAWENV draw[2];
static unsigned long ot[2][OT_LEN];
static unsigned char primbuff[2][PRIM_SIZE * 32];
static unsigned char padbuff[2][34];

static int db = 0;
static int font_id;

static GameState game_state = STATE_INTRO;

static TextureAsset tex_player;
static TextureAsset tex_enemy;
static TextureAsset tex_potion;

static Enemy enemies[MAX_ENEMIES];
static Bullet bullets[MAX_BULLETS];
static PotionItem potion;

static int player_x;
static int player_y;
static int score;
static int frame_counter;
static int menu_selection;

static int boost_timer = 0;
static int god_mode = 0;

static int prev_pad_btn = 0xFFFF;
static int cheat_step = 0;


/* ============================================================
   CD TIM LOADER
   ============================================================ */

static int load_tim_from_cd(
    const char *filename,
    TextureAsset *tex,
    int vram_x,
    int vram_y,
    int clut_x,
    int clut_y
) {
    CdlFILE file;
    int sectors;
    int mode;
    uint32_t *file_buf;
    TIM_IMAGE tim;
    RECT img_rect;
    RECT clut_rect;

    memset(tex, 0, sizeof(*tex));

    if (CdSearchFile(&file, filename) == NULL)
        return 0;

    sectors = (file.size + 2047) / 2048;

    if (sectors <= 0)
        return 0;

    file_buf = (uint32_t *)malloc((size_t)sectors * 2048);

    if (!file_buf)
        return 0;

    CdControl(CdlSetloc, (u_char *)&file.pos, 0);

    CdRead(sectors, file_buf, CdlModeSpeed);

    if (CdReadSync(0, 0) < 0) {
        free(file_buf);
        return 0;
    }

    GetTimInfo(file_buf, &tim);

    if (!tim.prect || !tim.paddr) {
        free(file_buf);
        return 0;
    }

    mode = tim.mode & 3;

    img_rect.x = vram_x;
    img_rect.y = vram_y;
    img_rect.w = tim.prect->w;
    img_rect.h = tim.prect->h;

    LoadImage(&img_rect, tim.paddr);
    DrawSync(0);

    if ((tim.mode & 8) && tim.crect && tim.caddr) {
        clut_rect.x = clut_x;
        clut_rect.y = clut_y;
        clut_rect.w = tim.crect->w;
        clut_rect.h = tim.crect->h;

        LoadImage(&clut_rect, tim.caddr);
        DrawSync(0);

        tex->clut = getClut(clut_x, clut_y);
    }

    tex->tpage = getTPage(
        mode,
        0,
        vram_x,
        vram_y
    );

    if (mode == 0) {
        tex->u = (vram_x & 0x3f) * 4;
        tex->w = tim.prect->w * 4;
    } else if (mode == 1) {
        tex->u = (vram_x & 0x3f) * 2;
        tex->w = tim.prect->w * 2;
    } else {
        tex->u = vram_x & 0x3f;
        tex->w = tim.prect->w;
    }

    tex->v = vram_y & 0xff;
    tex->h = tim.prect->h;
    tex->loaded = 1;

    free(file_buf);

    return 1;
}


/* ============================================================
   VIDEO
   ============================================================ */

static void init_video(void)
{
    ResetGraph(0);

    SetDefDispEnv(
        &disp[0],
        0,
        0,
        SCREEN_X,
        SCREEN_Y
    );

    SetDefDispEnv(
        &disp[1],
        0,
        SCREEN_Y,
        SCREEN_X,
        SCREEN_Y
    );

    SetDefDrawEnv(
        &draw[0],
        0,
        SCREEN_Y,
        SCREEN_X,
        SCREEN_Y
    );

    SetDefDrawEnv(
        &draw[1],
        0,
        0,
        SCREEN_X,
        SCREEN_Y
    );

    draw[0].isbg = 1;
    draw[1].isbg = 1;

    setRGB0(
        &draw[0],
        3,
        5,
        18
    );

    setRGB0(
        &draw[1],
        3,
        5,
        18
    );

    PutDispEnv(&disp[0]);
    PutDrawEnv(&draw[0]);

    FntLoad(960, 0);

    font_id = FntOpen(
        8,
        8,
        304,
        224,
        0,
        100
    );

    SetDispMask(1);
}


/* ============================================================
   PAD
   ============================================================ */

static void init_pad(void)
{
    EnterCriticalSection();

    InitPAD(
        padbuff[0],
        34,
        padbuff[1],
        34
    );

    StartPAD();
    ChangeClearPAD(1);

    ExitCriticalSection();
}


/* ============================================================
   RESET GAME
   ============================================================ */

static void reset_game(void)
{
    int i;

    player_x = 150;
    player_y = 205;

    score = 0;
    frame_counter = 0;

    boost_timer = 0;

    potion.active = 0;
    potion.x = 0;
    potion.y = -20;

    for (i = 0; i < MAX_ENEMIES; ++i) {
        enemies[i].active = 0;
        enemies[i].x = 0;
        enemies[i].y = -40;
        enemies[i].speed = 2 + (i % 3);
    }

    for (i = 0; i < MAX_BULLETS; ++i)
        bullets[i].active = 0;
}


/* ============================================================
   SPAWN ENEMY
   ============================================================ */

static void spawn_enemy(void)
{
    int i;

    for (i = 0; i < MAX_ENEMIES; ++i) {
        if (!enemies[i].active) {
            enemies[i].active = 1;

            enemies[i].x =
                20 +
                ((frame_counter * 37 + i * 53) % 280);

            enemies[i].y = -18;

            enemies[i].speed =
                2 +
                ((frame_counter + i) % 3);

            return;
        }
    }
}


/* ============================================================
   SPAWN POTION
   ============================================================ */

static void spawn_potion(void)
{
    if (!potion.active) {
        potion.active = 1;

        potion.x =
            30 +
            (frame_counter * 17) % 260;

        potion.y = -16;
    }
}


/* ============================================================
   SHOOT
   ============================================================ */

static void shoot_bullet(void)
{
    int i;

    for (i = 0; i < MAX_BULLETS; ++i) {
        if (!bullets[i].active) {
            bullets[i].active = 1;

            bullets[i].x =
                player_x + 10;

            bullets[i].y =
                player_y - 6;

            return;
        }
    }
}


/* ============================================================
   COLLISION
   ============================================================ */

static int overlap(
    int ax,
    int ay,
    int aw,
    int ah,
    int bx,
    int by,
    int bw,
    int bh
) {
    return
        ax < bx + bw &&
        ax + aw > bx &&
        ay < by + bh &&
        ay + ah > by;
}


/* ============================================================
   DRAW TILE
   ============================================================ */

static void draw_tile(
    char **nextpri,
    int x,
    int y,
    int w,
    int h,
    int r,
    int g,
    int b
) {
    TILE *tile =
        (TILE *)*nextpri;

    setTile(tile);

    setXY0(
        tile,
        x,
        y
    );

    setWH(
        tile,
        w,
        h
    );

    setRGB0(
        tile,
        r,
        g,
        b
    );

    addPrim(
        ot[db] + OT_LEN - 1,
        tile
    );

    *nextpri += sizeof(TILE);
}


/* ============================================================
   DRAW SPRITE
   ============================================================ */

static void draw_sprite(
    char **nextpri,
    TextureAsset *tex,
    int x,
    int y,
    int w,
    int h,
    int r,
    int g,
    int b
) {
    if (!tex->loaded) {
        draw_tile(
            nextpri,
            x,
            y,
            w,
            h,
            r,
            g,
            b
        );

        return;
    }

    SPRT *sprt =
        (SPRT *)*nextpri;

    DR_TPAGE *tpage =
        (DR_TPAGE *)(*nextpri + sizeof(SPRT));

    setSprt(sprt);

    setXY0(
        sprt,
        x,
        y
    );

    setWH(
        sprt,
        w,
        h
    );

    setUV0(
        sprt,
        tex->u,
        tex->v
    );

    setRGB0(
        sprt,
        r,
        g,
        b
    );

    sprt->clut = tex->clut;

    setDrawTPage(
        tpage,
        0,
        1,
        tex->tpage
    );

    addPrim(
        ot[db] + OT_LEN - 1,
        sprt
    );

    addPrim(
        ot[db] + OT_LEN - 1,
        tpage
    );

    *nextpri +=
        sizeof(SPRT) +
        sizeof(DR_TPAGE);
}


/* ============================================================
   PANEL
   ============================================================ */

static void draw_panel(
    char **nextpri,
    int x,
    int y,
    int w,
    int h
) {
    draw_tile(
        nextpri,
        x + 2,
        y + 2,
        w,
        h,
        0,
        0,
        0
    );

    draw_tile(
        nextpri,
        x,
        y,
        w,
        h,
        10,
        14,
        38
    );

    draw_tile(
        nextpri,
        x,
        y,
        w,
        2,
        35,
        220,
        255
    );

    draw_tile(
        nextpri,
        x,
        y + h - 2,
        w,
        2,
        110,
        40,
        255
    );
}


/* ============================================================
   LINE
   ============================================================ */

static void draw_line(
    char **nextpri,
    int x,
    int y,
    int w,
    int r,
    int g,
    int b
) {
    draw_tile(
        nextpri,
        x,
        y,
        w,
        1,
        r,
        g,
        b
    );
}


/* ============================================================
   BUTTON
   ============================================================ */

static int is_btn_pressed(
    u_short mask,
    u_short curr
) {
    return
        (!(curr & mask)) &&
        (prev_pad_btn & mask);
}


/* ============================================================
   CHEAT
   ============================================================ */

static void check_cheat_code(u_short curr)
{
    if (is_btn_pressed(PAD_CIRCLE, curr)) {

        if (cheat_step == 0)
            cheat_step = 1;

        else if (cheat_step == 3) {
            god_mode = !god_mode;
            cheat_step = 0;
        }

        else
            cheat_step = 0;

    } else if (is_btn_pressed(PAD_CROSS, curr)) {

        cheat_step =
            cheat_step == 1 ? 2 : 0;

    } else if (is_btn_pressed(PAD_TRIANGLE, curr)) {

        cheat_step =
            cheat_step == 2 ? 3 : 0;
    }
}


/* ============================================================
   UPDATE
   ============================================================ */

static void update_game(void)
{
    PADTYPE *pad =
        (PADTYPE *)padbuff[0];

    u_short btn;

    int i;
    int j;

    if (pad->stat != 0)
        return;

    btn = pad->btn;

    ++frame_counter;


    /* INTRO */

    if (game_state == STATE_INTRO) {

        if (frame_counter >= 150) {
            game_state =
                STATE_START_SCREEN;

            frame_counter = 0;
        }


    /* START SCREEN */

    } else if (game_state == STATE_START_SCREEN) {

        check_cheat_code(btn);

        if (is_btn_pressed(PAD_START, btn))
            game_state =
                STATE_MAIN_MENU;


    /* MAIN MENU */

    } else if (game_state == STATE_MAIN_MENU) {

        if (
            is_btn_pressed(PAD_UP, btn) ||
            is_btn_pressed(PAD_DOWN, btn)
        ) {
            menu_selection =
                !menu_selection;
        }

        if (
            is_btn_pressed(PAD_CROSS, btn) ||
            is_btn_pressed(PAD_START, btn)
        ) {
            if (menu_selection == 0) {

                reset_game();

                game_state =
                    STATE_GAMEPLAY;

            } else {

                game_state =
                    STATE_START_SCREEN;
            }
        }


    /* GAMEPLAY */

    } else if (game_state == STATE_GAMEPLAY) {

        int move_speed =
            boost_timer > 0 ? 5 : 3;

        if (boost_timer > 0)
            --boost_timer;


        if (!(btn & PAD_LEFT))
            player_x -= move_speed;

        if (!(btn & PAD_RIGHT))
            player_x += move_speed;

        if (!(btn & PAD_UP))
            player_y -= move_speed;

        if (!(btn & PAD_DOWN))
            player_y += move_speed;


        if (player_x < 8)
            player_x = 8;

        if (player_x > 288)
            player_x = 288;

        if (player_y < 40)
            player_y = 40;

        if (player_y > 216)
            player_y = 216;


        if (is_btn_pressed(PAD_CIRCLE, btn))
            shoot_bullet();


        /* BULLETS */

        for (i = 0; i < MAX_BULLETS; ++i) {

            if (bullets[i].active) {

                bullets[i].y -= 6;

                if (bullets[i].y < 30)
                    bullets[i].active = 0;
            }
        }


        /* POTION */

        if ((frame_counter % 300) == 0)
            spawn_potion();

        if (potion.active) {

            potion.y += 2;

            if (
                overlap(
                    player_x,
                    player_y,
                    24,
                    18,
                    potion.x,
                    potion.y,
                    16,
                    16
                )
            ) {
                potion.active = 0;
                boost_timer = 180;
            }

            if (potion.y > SCREEN_Y)
                potion.active = 0;
        }


        /* ENEMY SPAWN */

        if ((frame_counter % 40) == 0)
            spawn_enemy();


        /* ENEMIES */

        for (i = 0; i < MAX_ENEMIES; ++i) {

            if (!enemies[i].active)
                continue;

            enemies[i].y +=
                enemies[i].speed;


            /* BULLET COLLISION */

            for (j = 0; j < MAX_BULLETS; ++j) {

                if (
                    bullets[j].active &&
                    overlap(
                        bullets[j].x,
                        bullets[j].y,
                        4,
                        8,
                        enemies[i].x,
                        enemies[i].y,
                        18,
                        18
                    )
                ) {
                    bullets[j].active = 0;
                    enemies[i].active = 0;
                    score += 2;
                }
            }


            /* PLAYER COLLISION */

            if (
                enemies[i].active &&
                overlap(
                    player_x,
                    player_y,
                    24,
                    18,
                    enemies[i].x,
                    enemies[i].y,
                    18,
                    18
                )
            ) {
                if (!god_mode)
                    game_state =
                        STATE_GAMEOVER;
            }


            /* ENEMY PASSED */

            if (enemies[i].y > SCREEN_Y) {

                enemies[i].active = 0;

                ++score;
            }
        }


    /* GAMEOVER */

    } else if (game_state == STATE_GAMEOVER) {

        if (
            is_btn_pressed(PAD_START, btn) ||
            is_btn_pressed(PAD_CROSS, btn)
        ) {
            game_state =
                STATE_MAIN_MENU;
        }
    }

    prev_pad_btn = btn;
}


/* ============================================================
   BACKGROUND
   ============================================================ */

static void draw_background(char **nextpri)
{
    int i;

    draw_tile(
        nextpri,
        0,
        0,
        320,
        240,
        3,
        5,
        18
    );


    for (i = 0; i < 12; ++i) {

        int x =
            (i * 67 + frame_counter / 4) % 320;

        int y =
            (i * 43) % 225;

        draw_tile(
            nextpri,
            x,
            y,
            1,
            1,
            35,
            80,
            110
        );
    }


    draw_line(
        nextpri,
        0,
        31,
        320,
        30,
        190,
        255
    );

    draw_line(
        nextpri,
        0,
        32,
        320,
        10,
        45,
        80
    );

    /* FIX: draw_line menerima 7 argumen total */
    draw_line(
        nextpri,
        0,
        231,
        320,
        70,
        80,
        150
    );
}


/* ============================================================
   DRAW GAME
   ============================================================ */

static void draw_game(void)
{
    char *nextpri =
        (char *)primbuff[db];

    int i;

    ClearOTagR(
        ot[db],
        OT_LEN
    );


    /* INTRO */

    if (game_state == STATE_INTRO) {

        draw_tile(
            &nextpri,
            0,
            0,
            320,
            240,
            2,
            3,
            12
        );

        draw_tile(
            &nextpri,
            38,
            91,
            244,
            58,
            10,
            14,
            35
        );

        draw_tile(
            &nextpri,
            40,
            89,
            240,
            58,
            3,
            8,
            25
        );

        draw_line(
            &nextpri,
            40,
            89,
            240,
            35,
            220,
            255
        );

        draw_line(
            &nextpri,
            40,
            145,
            240,
            100,
            35,
            255
        );

        FntPrint(
            font_id,
            "\n\n\n\n\n        ODEN STUDIO\n\n          PRESENTS"
        );

        FntFlush(font_id);


    /* START SCREEN */

    } else if (game_state == STATE_START_SCREEN) {

        draw_background(&nextpri);

        draw_panel(
            &nextpri,
            35,
            55,
            250,
            130
        );

        draw_line(
            &nextpri,
            55,
            92,
            210,
            50,
            220,
            255
        );

        FntPrint(
            font_id,
            "\n\n\n       N E O N   R U N N E R\n\n\n        P S X   E D I T I O N\n\n\n          PRESS START"
        );

        if (god_mode)
            FntPrint(
                font_id,
                "\n\n             GOD MODE"
            );

        FntFlush(font_id);


    /* MAIN MENU */

    } else if (game_state == STATE_MAIN_MENU) {

        draw_background(&nextpri);

        draw_panel(
            &nextpri,
            42,
            40,
            236,
            165
        );

        draw_tile(
            &nextpri,
            58,
            65,
            204,
            30,
            15,
            24,
            55
        );

        draw_line(
            &nextpri,
            58,
            65,
            204,
            40,
            220,
            255
        );

        FntPrint(
            font_id,
            "\n\n\n       NEON RUNNER\n\n\n          MAIN MENU\n\n\n       %c  START RUN\n\n       %c  EXIT",
            menu_selection == 0 ? '>' : ' ',
            menu_selection == 1 ? '>' : ' '
        );

        FntFlush(font_id);


    /* GAMEPLAY + GAMEOVER */

    } else {

        draw_background(&nextpri);


        /* HUD */

        draw_tile(
            &nextpri,
            8,
            6,
            304,
            20,
            8,
            12,
            32
        );

        draw_tile(
            &nextpri,
            8,
            6,
            3,
            20,
            40,
            220,
            255
        );

        FntPrint(
            font_id,
            "\n SCORE %04d                 %s %s",
            score,
            boost_timer > 0 ? "BOOST" : "",
            god_mode ? "GOD" : ""
        );


        /* PLAYFIELD */

        draw_line(
            &nextpri,
            0,
            34,
            320,
            20,
            80,
            120
        );

        draw_line(
            &nextpri,
            0,
            231,
            320,
            20,
            80,
            120
        );


        /* PLAYER */

        draw_sprite(
            &nextpri,
            &tex_player,
            player_x,
            player_y,
            24,
            18,
            boost_timer > 0 ? 150 : 100,
            220,
            255
        );


        /* POTION */

        if (potion.active) {

            draw_sprite(
                &nextpri,
                &tex_potion,
                potion.x,
                potion.y,
                16,
                16,
                255,
                255,
                255
            );
        }


        /* BULLETS */

        for (i = 0; i < MAX_BULLETS; ++i) {

            if (bullets[i].active) {

                draw_tile(
                    &nextpri,
                    bullets[i].x - 1,
                    bullets[i].y,
                    6,
                    9,
                    255,
                    220,
                    60
                );

                draw_tile(
                    &nextpri,
                    bullets[i].x,
                    bullets[i].y - 2,
                    4,
                    3,
                    255,
                    255,
                    180
                );
            }
        }


        /* ENEMIES */

        for (i = 0; i < MAX_ENEMIES; ++i) {

            if (enemies[i].active) {

                draw_sprite(
                    &nextpri,
                    &tex_enemy,
                    enemies[i].x,
                    enemies[i].y,
                    18,
                    18,
                    255,
                    255,
                    255
                );
            }
        }


        /* GAMEOVER */

        if (game_state == STATE_GAMEOVER) {

            draw_tile(
                &nextpri,
                25,
                65,
                270,
                120,
                5,
                5,
                18
            );

            draw_tile(
                &nextpri,
                28,
                68,
                264,
                114,
                15,
                12,
                40
            );

            draw_line(
                &nextpri,
                28,
                68,
                264,
                255,
                45,
                100
            );

            draw_line(
                &nextpri,
                28,
                180,
                264,
                80,
                20,
                120
            );

            FntPrint(
                font_id,
                "\n\n\n\n          RUN OVER\n\n\n       SCORE  %04d\n\n\n       PRESS START",
                score
            );
        }

        FntFlush(font_id);
    }


    DrawOTag(
        ot[db] + OT_LEN - 1
    );
}


/* ============================================================
   DISPLAY
   ============================================================ */

static void display(void)
{
    DrawSync(0);

    VSync(0);

    db = !db;

    PutDispEnv(&disp[db]);
    PutDrawEnv(&draw[db]);

    SetDispMask(1);
}


/* ============================================================
   MAIN
   ============================================================ */

int main(void)
{
    init_video();

    CdInit();

    init_pad();


    /*
     * VRAM:
     * framebuffer 0-319,
     * textures mulai 640 agar tidak tabrakan.
     */

    load_tim_from_cd(
        "\\PLAYER.TIM;1",
        &tex_player,
        640,
        0,
        640,
        240
    );

    load_tim_from_cd(
        "\\ENEMY.TIM;1",
        &tex_enemy,
        768,
        0,
        768,
        240
    );

    load_tim_from_cd(
        "\\POTION.TIM;1",
        &tex_potion,
        896,
        0,
        896,
        240
    );


    reset_game();


    while (1) {

        update_game();

        draw_game();

        display();
    }


    return 0;
}
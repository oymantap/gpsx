#include <sys/types.h>
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
    u_short tpage;
    u_short clut;
    u_char u, v;
    u_short w, h;
    int loaded;
} TextureAsset;

typedef struct {
    int x, y;
    int speed;
    int active;
} Enemy;

typedef struct {
    int x, y;
    int active;
} Bullet;

typedef struct {
    int x, y;
    int active;
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


/*
 * Load a TIM directly from the PlayStation CD.
 *
 * IMPORTANT:
 * Do not use POSIX open()/read()/lseek()/close() here.
 * PSn00bSDK's PSX toolchain does not provide fcntl.h/POSIX file I/O.
 */
static int load_tim_from_cd(const char *filename, TextureAsset *tex)
{
    CdlFILE file;
    int sectors;
    u_long *file_buf;
    TIM_IMAGE tim;

    memset(tex, 0, sizeof(*tex));

    if (CdSearchFile(&file, filename) == NULL) {
        return 0;
    }

    sectors = (file.size + 2047) / 2048;

    if (sectors <= 0) {
        return 0;
    }

    file_buf = (u_long *)malloc((size_t)sectors * 2048);

    if (file_buf == NULL) {
        return 0;
    }

    /*
     * PSn00bSDK v0.24:
     * CdlFILE uses .pos, not .loc.
     */
    CdControl(
        CdlSetloc,
        (u_char *)&file.pos,
        0
    );

    CdRead(
        sectors,
        file_buf,
        CdlModeSpeed
    );

    if (CdReadSync(0, 0) < 0) {
        free(file_buf);
        return 0;
    }

    GetTimInfo(
        file_buf,
        &tim
    );

    if (tim.prect == NULL) {
        free(file_buf);
        return 0;
    }

    LoadImage(
        tim.prect,
        tim.paddr
    );

    DrawSync(0);

    if ((tim.mode & 0x8) && tim.crect != NULL) {
        LoadImage(
            tim.crect,
            tim.caddr
        );

        DrawSync(0);
    }

    tex->tpage = getTPage(
        tim.mode & 0x3,
        0,
        tim.prect->x,
        tim.prect->y
    );

    if ((tim.mode & 0x8) && tim.crect != NULL) {
        tex->clut = getClut(
            tim.crect->x,
            tim.crect->y
        );
    }
    else {
        tex->clut = 0;
    }

    switch (tim.mode & 0x3) {
        case 0:
            tex->u = (tim.prect->x & 0x3f) * 4;
            tex->w = tim.prect->w * 4;
            break;

        case 1:
            tex->u = (tim.prect->x & 0x3f) * 2;
            tex->w = tim.prect->w * 2;
            break;

        default:
            tex->u = tim.prect->x & 0x3f;
            tex->w = tim.prect->w;
            break;
    }

    tex->v = tim.prect->y & 0xff;
    tex->h = tim.prect->h;
    tex->loaded = 1;

    free(file_buf);

    return 1;
}


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

    setRGB0(&draw[0], 5, 8, 24);
    setRGB0(&draw[1], 5, 8, 24);

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
            enemies[i].x =
                20 + ((frame_counter * 37 + i * 53) % 280);
            enemies[i].y = -18;
            enemies[i].speed =
                2 + ((frame_counter + i) % 3);

            return;
        }
    }
}


static void spawn_potion(void)
{
    if (!potion.active) {
        potion.active = 1;
        potion.x = 30 + (frame_counter * 17) % 260;
        potion.y = -16;
    }
}


static void shoot_bullet(void)
{
    int i;

    for (i = 0; i < MAX_BULLETS; ++i) {
        if (!bullets[i].active) {
            bullets[i].active = 1;
            bullets[i].x = player_x + 10;
            bullets[i].y = player_y - 6;
            return;
        }
    }
}


static int overlap(
    int ax,
    int ay,
    int aw,
    int ah,
    int bx,
    int by,
    int bw,
    int bh
)
{
    return
        ax < bx + bw &&
        ax + aw > bx &&
        ay < by + bh &&
        ay + ah > by;
}


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
)
{
    if (!tex->loaded) {
        TILE *tile = (TILE *)*nextpri;

        setTile(tile);
        setXY0(tile, x, y);
        setWH(tile, w, h);
        setRGB0(tile, r, g, b);

        addPrim(
            ot[db] + (OT_LEN - 1),
            tile
        );

        *nextpri += sizeof(TILE);
        return;
    }

    SPRT *sprt = (SPRT *)*nextpri;

    setSprt(sprt);
    setXY0(sprt, x, y);
    setWH(sprt, w, h);
    setUV0(sprt, tex->u, tex->v);
    setRGB0(sprt, r, g, b);

    sprt->clut = tex->clut;

    DR_TPAGE *tpage =
        (DR_TPAGE *)(*nextpri + sizeof(SPRT));

    setDrawTPage(
        tpage,
        0,
        1,
        tex->tpage
    );

    addPrim(
        ot[db] + (OT_LEN - 1),
        sprt
    );

    addPrim(
        ot[db] + (OT_LEN - 1),
        tpage
    );

    *nextpri +=
        sizeof(SPRT) + sizeof(DR_TPAGE);
}


static void draw_tile(
    char **nextpri,
    int x,
    int y,
    int w,
    int h,
    int r,
    int g,
    int b
)
{
    TILE *tile = (TILE *)*nextpri;

    setTile(tile);
    setXY0(tile, x, y);
    setWH(tile, w, h);
    setRGB0(tile, r, g, b);

    addPrim(
        ot[db] + (OT_LEN - 1),
        tile
    );

    *nextpri += sizeof(TILE);
}


static int is_btn_pressed(
    u_short mask,
    u_short curr
)
{
    return
        (!(curr & mask)) &&
        (prev_pad_btn & mask);
}


static void check_cheat_code(u_short curr)
{
    if (is_btn_pressed(PAD_CIRCLE, curr)) {
        if (cheat_step == 0) {
            cheat_step = 1;
        }
        else if (cheat_step == 3) {
            god_mode = !god_mode;
            cheat_step = 0;
        }
        else {
            cheat_step = 0;
        }
    }
    else if (is_btn_pressed(PAD_CROSS, curr)) {
        if (cheat_step == 1) {
            cheat_step = 2;
        }
        else {
            cheat_step = 0;
        }
    }
    else if (is_btn_pressed(PAD_TRIANGLE, curr)) {
        if (cheat_step == 2) {
            cheat_step = 3;
        }
        else {
            cheat_step = 0;
        }
    }
}


static void update_game(void)
{
    PADTYPE *pad = (PADTYPE *)padbuff[0];

    u_short btn;

    int i;
    int j;

    if (pad->stat != 0) {
        return;
    }

    btn = pad->btn;

    ++frame_counter;

    if (game_state == STATE_INTRO) {
        if (frame_counter >= 240) {
            game_state = STATE_START_SCREEN;
            frame_counter = 0;
        }
    }

    else if (game_state == STATE_START_SCREEN) {
        check_cheat_code(btn);

        if (is_btn_pressed(PAD_START, btn)) {
            game_state = STATE_MAIN_MENU;
        }
    }

    else if (game_state == STATE_MAIN_MENU) {
        if (
            is_btn_pressed(PAD_UP, btn) ||
            is_btn_pressed(PAD_DOWN, btn)
        ) {
            menu_selection = !menu_selection;
        }

        if (
            is_btn_pressed(PAD_CROSS, btn) ||
            is_btn_pressed(PAD_START, btn)
        ) {
            if (menu_selection == 0) {
                reset_game();
                game_state = STATE_GAMEPLAY;
            }
            else {
                game_state = STATE_START_SCREEN;
            }
        }
    }

    else if (game_state == STATE_GAMEPLAY) {
        int move_speed =
            (boost_timer > 0) ? 5 : 3;

        if (boost_timer > 0) {
            --boost_timer;
        }

        if (!(btn & PAD_LEFT)) {
            player_x -= move_speed;
        }

        if (!(btn & PAD_RIGHT)) {
            player_x += move_speed;
        }

        if (!(btn & PAD_UP)) {
            player_y -= move_speed;
        }

        if (!(btn & PAD_DOWN)) {
            player_y += move_speed;
        }

        if (player_x < 8)
            player_x = 8;

        if (player_x > 288)
            player_x = 288;

        if (player_y < 40)
            player_y = 40;

        if (player_y > 216)
            player_y = 216;

        if (is_btn_pressed(PAD_CIRCLE, btn)) {
            shoot_bullet();
        }

        for (i = 0; i < MAX_BULLETS; ++i) {
            if (bullets[i].active) {
                bullets[i].y -= 6;

                if (bullets[i].y < 30) {
                    bullets[i].active = 0;
                }
            }
        }

        if ((frame_counter % 300) == 0) {
            spawn_potion();
        }

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

            if (potion.y > SCREEN_Y) {
                potion.active = 0;
            }
        }

        if ((frame_counter % 40) == 0) {
            spawn_enemy();
        }

        for (i = 0; i < MAX_ENEMIES; ++i) {
            if (enemies[i].active) {
                enemies[i].y += enemies[i].speed;

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
    }

    else if (game_state == STATE_GAMEOVER) {
        if (
            is_btn_pressed(PAD_START, btn) ||
            is_btn_pressed(PAD_CROSS, btn)
        ) {
            game_state = STATE_MAIN_MENU;
        }
    }

    prev_pad_btn = btn;
}


static void draw_game(void)
{
    char *nextpri =
        (char *)primbuff[db];

    int i;

    ClearOTagR(
        ot[db],
        OT_LEN
    );

    if (game_state == STATE_INTRO) {
        setRGB0(
            &draw[db],
            0,
            0,
            0
        );

        FntPrint(
            font_id,
            "\n\n\n\n\n\n\n\n          ODEN STUDIO"
        );
    }

    else if (game_state == STATE_START_SCREEN) {
        setRGB0(
            &draw[db],
            5,
            8,
            24
        );

        FntPrint(
            font_id,
            "\n\n\n\n        NEON RUNNER PSX\n\n\n\n\n\n\n\n      - PRESS START BUTTON -"
        );

        if (god_mode) {
            FntPrint(
                font_id,
                "\n\n     [ CHEAT: GODMODE ON ]"
            );
        }
    }

    else if (game_state == STATE_MAIN_MENU) {
        setRGB0(
            &draw[db],
            5,
            8,
            24
        );

        FntPrint(
            font_id,
            "\n\n\n\n        NEON RUNNER PSX\n\n\n\n           MAIN MENU\n\n        %c START GAME\n        %c EXIT GAME",
            menu_selection == 0 ? '>' : ' ',
            menu_selection == 1 ? '>' : ' '
        );
    }

    else {
        setRGB0(
            &draw[db],
            5,
            8,
            24
        );

        draw_sprite(
            &nextpri,
            &tex_player,
            player_x,
            player_y,
            24,
            18,
            (boost_timer > 0) ? 255 : 200,
            255,
            255
        );

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

        for (i = 0; i < MAX_BULLETS; ++i) {
            if (bullets[i].active) {
                draw_tile(
                    &nextpri,
                    bullets[i].x,
                    bullets[i].y,
                    4,
                    8,
                    255,
                    255,
                    0
                );
            }
        }

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

        draw_tile(
            &nextpri,
            0,
            30,
            320,
            2,
            80,
            90,
            150
        );

        draw_tile(
            &nextpri,
            0,
            232,
            320,
            2,
            80,
            90,
            150
        );

        if (game_state == STATE_GAMEOVER) {
            FntPrint(
                font_id,
                "\n\n\n\n         GAME OVER!\n\n        FINAL SCORE: %d\n\n      PRESS START TO MENU",
                score
            );
        }
        else {
            FntPrint(
                font_id,
                "\n NEON RUNNER   SCORE %d   %s %s",
                score,
                (boost_timer > 0) ? "[BOOST!]" : "",
                god_mode ? "[GOD]" : ""
            );
        }
    }

    FntFlush(font_id);

    DrawOTag(
        ot[db] + (OT_LEN - 1)
    );
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
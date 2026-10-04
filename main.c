/* TOMOCORE - PSP homebrew (text + ASCII art)  build: make -> EBOOT.PBP */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PSP_MODULE_INFO("TOMOCORE", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define P pspDebugScreenPrintf
#define MAXR 100      /* residents */
#define MAXH 100      /* houses (house i belongs to resident slot i) */
#define NHOTEL 5
#define NMEAL 10
#define NCAT 5        /* hair, lash, face, cloth, voice */
#define PERPAGE 20
#define NPAGE 2
#define NBOARD 8
#define SAVEFILE "ms0:/PSP/GAME/TOMOCORE/save.dat"

/* ---------- exit callback ---------- */
static int exit_cb(int a, int b, void *c) { sceKernelExitGame(); return 0; }
static int cb_thread(SceSize args, void *argp) {
    int id = sceKernelCreateCallback("exit_cb", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int t = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, NULL);
    if (t >= 0) sceKernelStartThread(t, 0, NULL);
}

/* ---------- data ---------- */
typedef struct {
    int used;
    char name[12];
    int look[NCAT];   /* 0..39 each */
    int hotel;        /* favorite hotel 0..NHOTEL-1 */
    int stay;         /* 1 = staying at hotel */
    int mood;         /* 0..100  "zekkocho" */
    int money;
    int pawned;
    int meal;         /* today's meal */
} Res;

static Res R[MAXR];
static unsigned char F[MAXR][MAXR];       /* friendship 0..100 */
static char board[NBOARD][56];
static int bpos = 0, day = 1, houses_used = 0;

static const char *CATN[NCAT] = {"Hair", "Eyelash", "Face", "Clothes", "Voice"};
static const char *HOTELN[NHOTEL] = {"Sakura Inn", "Umi Hotel", "Hoshi Resort", "Yuki Lodge", "Tsuki Palace"};
static const int HOTELFEE[NHOTEL] = {50, 80, 120, 160, 250};
static const char *MEALN[NMEAL] = {"Rice ball", "Curry", "Ramen", "Sushi", "Pizza",
                                   "Tempura", "Omelet rice", "Udon", "Hamburg steak", "Parfait"};
static const char *SYL[] = {"ka", "mi", "to", "ya", "su", "ke", "na", "ri", "ho", "yu", "ta", "mo", "ko", "ra", "shi", "no"};

/* ---------- ASCII art parts (11 chars wide) ---------- */
static const char *HAIR[8] = {"  ,-----.  ", " /#######\\ ", " /vvvvvvv\\ ", " (@@@@@@@) ",
                              " /~~~~~~~\\ ", " ,;;;;;;;, ", " /\"\"\"\"\"\"\"\\ ", " _/\\/\\/\\/\\_ "};
static const char *LASH[8] = {"  |     |  ", " ' '   ' ' ", "  \\\\   //  ", "  ///  \\\\\\  ",
                              "  ~     ~  ", " ,,     ,, ", "  ^^   ^^  ", "  ==   ==  "};
static const char *EYES[8] = {" (  o o  ) ", " (  O O  ) ", " (  - -  ) ", " (  ^ ^  ) ",
                              " (  * *  ) ", " (  @ @  ) ", " (  > <  ) ", " (  . .  ) "};
static const char *MOUTH[5] = {" (   w   ) ", " (   v   ) ", " (  ---  ) ", " (   o   ) ", " (  \\_/  ) "};
static const char *CLO1[8] = {"  /|###|\\  ", "  /|~~~|\\  ", "  /|+++|\\  ", " //|===|\\\\ ",
                              "  /|:::|\\  ", "  /|***|\\  ", "  /|///|\\  ", "  /|@@@|\\  "};
static const char *CLO2[8] = {"   |###|   ", "   |~~~|   ", "   |+++|   ", "   |===|   ",
                              "   |:::|   ", "   |***|   ", "   |///|   ", "   |@@@|   "};

static void draw_face(const Res *r) {
    P("   %s\n", HAIR[r->look[0] % 8]);
    P("   %s\n", LASH[r->look[1] % 8]);
    P("   %s\n", EYES[r->look[2] % 8]);
    P("   %s\n", MOUTH[(r->look[2] / 8) % 5]);
    P("   %s\n", CLO1[r->look[3] % 8]);
    P("   %s\n", CLO2[r->look[3] % 8]);
}

/* ---------- input ---------- */
static SceCtrlData pad;
static unsigned oldb = 0;
static unsigned pressed(void) {
    sceCtrlPeekBufferPositive(&pad, 1);
    unsigned n = pad.Buttons & ~oldb;
    oldb = pad.Buttons;
    return n;
}
static unsigned waitkey(void) {
    unsigned k;
    do { sceDisplayWaitVblankStart(); k = pressed(); } while (!k);
    return k;
}
static void cls(void) { pspDebugScreenClear(); pspDebugScreenSetXY(0, 0); }
static void pause_msg(void) { P("\n[X] continue"); while (!(waitkey() & PSP_CTRL_CROSS)); }

/* ---------- helpers ---------- */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int count_res(void) { int n = 0; for (int i = 0; i < MAXR; i++) n += R[i].used; return n; }

static void post(const char *msg) {
    snprintf(board[bpos], sizeof(board[0]), "D%d %s", day, msg);
    bpos = (bpos + 1) % NBOARD;
}

static void add_resident(void) {
    for (int i = 0; i < MAXR; i++) {
        if (R[i].used) continue;
        memset(&R[i], 0, sizeof(Res));
        R[i].used = 1;
        snprintf(R[i].name, sizeof(R[i].name), "%s%s%s",
                 SYL[rand() % 16], SYL[rand() % 16], (rand() % 2) ? SYL[rand() % 16] : "");
        R[i].name[0] -= 32; /* capitalize */
        for (int c = 0; c < NCAT; c++) R[i].look[c] = rand() % (PERPAGE * NPAGE);
        R[i].hotel = rand() % NHOTEL;
        R[i].mood = 50;
        R[i].money = 1000;
        R[i].meal = rand() % NMEAL;
        for (int j = 0; j < MAXR; j++) { F[i][j] = 0; F[j][i] = 0; }
        char m[56];
        snprintf(m, sizeof(m), "%s moved into house No.%d!", R[i].name, i + 1);
        post(m);
        return;
    }
}

/* generic vertical menu: returns index or -1 on Circle */
static int menu(const char *title, const char **items, int n) {
    int cur = 0;
    for (;;) {
        cls();
        P("=== %s ===\n\n", title);
        for (int i = 0; i < n; i++) P(" %c %s\n", i == cur ? '>' : ' ', items[i]);
        P("\n[UP/DOWN] move  [X] ok  [O] back");
        unsigned k = waitkey();
        if ((k & PSP_CTRL_UP) && cur > 0) cur--;
        if ((k & PSP_CTRL_DOWN) && cur < n - 1) cur++;
        if (k & PSP_CTRL_CROSS) return cur;
        if (k & PSP_CTRL_CIRCLE) return -1;
    }
}

/* pick a resident: 10 per page, LEFT/RIGHT = page */
static int pick_res(const char *title) {
    int cur = 0;
    for (;;) {
        cls();
        int page = cur / 10;
        P("=== %s ===  houses:%d/%d\n", title, count_res(), MAXH);
        for (int i = page * 10; i < page * 10 + 10; i++) {
            if (R[i].used)
                P(" %c%3d %-11s mood:%3d $%d\n", i == cur ? '>' : ' ', i + 1, R[i].name, R[i].mood, R[i].money);
            else
                P(" %c%3d -----\n", i == cur ? '>' : ' ', i + 1);
        }
        P("\npage %d/10  [UP/DOWN] [L/R:page] [X] ok [O] back", page + 1);
        unsigned k = waitkey();
        if ((k & PSP_CTRL_UP) && cur > 0) cur--;
        if ((k & PSP_CTRL_DOWN) && cur < MAXR - 1) cur++;
        if ((k & PSP_CTRL_LEFT) && cur >= 10) cur -= 10;
        if ((k & PSP_CTRL_RIGHT) && cur + 10 < MAXR) cur += 10;
        if ((k & PSP_CTRL_CROSS) && R[cur].used) return cur;
        if (k & PSP_CTRL_CIRCLE) return -1;
    }
}

/* ---------- screens ---------- */
static void show_resident(int i) {
    Res *r = &R[i];
    cls();
    P("=== %s  (House %d) ===\n\n", r->name, i + 1);
    draw_face(r);
    P("\nHair:%02d Lash:%02d Face:%02d Clothes:%02d Voice:%02d\n",
      r->look[0] + 1, r->look[1] + 1, r->look[2] + 1, r->look[3] + 1, r->look[4] + 1);
    P("Voice pitch: %d%%  robot: %d%%\n", 50 + r->look[4] * 3, (r->look[4] * 7) % 100);
    P("Mood: %d   Money: $%d%s\n", r->mood, r->money, r->pawned ? "   [PAWNED]" : "");
    P("Today's meal: %s\n", MEALN[r->meal]);
    P("Hotel: %s (%s)\n", HOTELN[r->hotel], r->stay ? "staying" : "at home");
    int best = -1, bv = -1;
    for (int j = 0; j < MAXR; j++)
        if (j != i && R[j].used && F[i][j] > bv) { bv = F[i][j]; best = j; }
    if (best >= 0) P("Best friend: %s (%d)\n", R[best].name, bv);
    pause_msg();
}

static void screen_dress(void) {
    int i = pick_res("Dress-up: who?");
    if (i < 0) return;
    for (;;) {
        const char *items[NCAT];
        for (int c = 0; c < NCAT; c++) items[c] = CATN[c];
        int c = menu("Category", items, NCAT);
        if (c < 0) return;
        int page = 0, cur = 0;
        for (;;) {
            cls();
            P("=== %s : page %d/%d ===\n", CATN[c], page + 1, NPAGE);
            draw_face(&R[i]);
            P("\n");
            /* 20 items in 4 columns x 5 rows */
            for (int row = 0; row < 5; row++) {
                for (int col = 0; col < 4; col++) {
                    int idx = row * 4 + col;
                    int no = page * PERPAGE + idx;
                    P("%c%02d%c ", idx == cur ? '>' : ' ', no + 1, R[i].look[c] == no ? '*' : ' ');
                }
                P("\n");
            }
            P("\n[ARROWS] move [L/R trig] page [X] set [O] back");
            unsigned k = waitkey();
            if ((k & PSP_CTRL_UP) && cur >= 4) cur -= 4;
            if ((k & PSP_CTRL_DOWN) && cur < 16) cur += 4;
            if ((k & PSP_CTRL_LEFT) && cur % 4 > 0) cur--;
            if ((k & PSP_CTRL_RIGHT) && cur % 4 < 3) cur++;
            if ((k & PSP_CTRL_LTRIGGER) && page > 0) page--;
            if ((k & PSP_CTRL_RTRIGGER) && page < NPAGE - 1) page++;
            if (k & PSP_CTRL_CROSS) R[i].look[c] = page * PERPAGE + cur;
            if (k & PSP_CTRL_CIRCLE) break;
        }
    }
}

static void screen_hotel(void) {
    int i = pick_res("Hotel: who?");
    if (i < 0) return;
    const char *items[NHOTEL + 1];
    char buf[NHOTEL][40];
    for (int h = 0; h < NHOTEL; h++) {
        int n = 0;
        for (int j = 0; j < MAXR; j++) n += (R[j].used && R[j].stay && R[j].hotel == h);
        snprintf(buf[h], 40, "%-13s $%3d/day  guests:%d", HOTELN[h], HOTELFEE[h], n);
        items[h] = buf[h];
    }
    items[NHOTEL] = R[i].stay ? "Check out (go home)" : "Stay at chosen hotel";
    int s = menu(R[i].name, items, NHOTEL + 1);
    if (s < 0) return;
    if (s < NHOTEL) R[i].hotel = s;
    else R[i].stay = !R[i].stay;
}

static void screen_pawn(void) {
    int i = pick_res("Pawn shop: who?");
    if (i < 0) return;
    cls();
    P("=== PAWN SHOP ===\n\n   [ $ ]  Welcome!  (o_o)/\n\n");
    if (!R[i].pawned) {
        R[i].pawned = 1; R[i].money += 300; R[i].mood = clampi(R[i].mood - 5, 0, 100);
        P("%s pawned an outfit for $300.\n", R[i].name);
    } else if (R[i].money >= 400) {
        R[i].pawned = 0; R[i].money -= 400; R[i].mood = clampi(R[i].mood + 5, 0, 100);
        P("%s redeemed the outfit for $400.\n", R[i].name);
    } else P("%s cannot afford $400 to redeem.\n", R[i].name);
    pause_msg();
}

static void screen_board(void) {
    const char *items[] = {"Read board", "Post: Hello everyone!", "Post: Anyone hungry?", "Post: Party at my house!", "Post: Looking for a friend"};
    for (;;) {
        int s = menu("Bulletin board", items, 5);
        if (s < 0) return;
        if (s == 0) {
            cls();
            P("+------------------------------------------------------+\n");
            P("|                   BULLETIN BOARD                     |\n");
            P("+------------------------------------------------------+\n");
            for (int k = 0; k < NBOARD; k++) {
                int idx = (bpos + NBOARD - 1 - k) % NBOARD;
                P("| %-52s |\n", board[idx][0] ? board[idx] : "");
            }
            P("+------------------------------------------------------+\n");
            pause_msg();
        } else {
            int w = pick_res("Who posts?");
            if (w < 0) continue;
            char m[56];
            snprintf(m, sizeof(m), "%s: %s", R[w].name, items[s] + 6);
            post(m);
        }
    }
}

static void screen_rank(void) {
    const char *items[] = {"Zekkocho (mood) ranking", "Nakayoshi (friend) ranking"};
    int s = menu("Ranking", items, 2);
    if (s < 0) return;
    cls();
    if (s == 0) {
        int idx[MAXR], n = 0;
        for (int i = 0; i < MAXR; i++) if (R[i].used) idx[n++] = i;
        for (int a = 0; a < n; a++)
            for (int b = a + 1; b < n; b++)
                if (R[idx[b]].mood > R[idx[a]].mood) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
        P("=== ZEKKOCHO RANKING ===\n\n");
        for (int r = 0; r < n && r < 10; r++)
            P(" %2d. %-11s %3d %.*s\n", r + 1, R[idx[r]].name, R[idx[r]].mood, R[idx[r]].mood / 5, "####################");
    } else {
        int ta[10], tb[10], tv[10], n = 0;
        for (int i = 0; i < MAXR; i++) {
            if (!R[i].used) continue;
            for (int j = i + 1; j < MAXR; j++) {
                if (!R[j].used) continue;
                int v = F[i][j], p = n < 10 ? n : 9;
                if (n < 10 || v > tv[9]) {
                    if (n < 10) n++;
                    while (p > 0 && tv[p - 1] < v) { ta[p] = ta[p - 1]; tb[p] = tb[p - 1]; tv[p] = tv[p - 1]; p--; }
                    ta[p] = i; tb[p] = j; tv[p] = v;
                }
            }
        }
        P("=== NAKAYOSHI RANKING ===\n\n");
        for (int r = 0; r < n; r++)
            P(" %2d. %-9s <3 %-9s %3d\n", r + 1, R[ta[r]].name, R[tb[r]].name, tv[r]);
        if (n == 0) P(" (need 2+ residents)\n");
    }
    pause_msg();
}

static void screen_meals(void) {
    cls();
    P("=== TODAY'S MEALS (day %d) ===\n\n", day);
    int shown = 0;
    for (int i = 0; i < MAXR && shown < 28; i++)
        if (R[i].used) { P(" %-11s : %s\n", R[i].name, MEALN[R[i].meal]); shown++; }
    if (!shown) P(" nobody lives here yet.\n");
    pause_msg();
}

/* ---------- time passes ---------- */
static void next_day(void) {
    day++;
    int n = count_res();
    for (int i = 0; i < MAXR; i++) {
        Res *r = &R[i];
        if (!r->used) continue;
        r->meal = rand() % NMEAL;
        int d = rand() % 7 - 3;
        if (r->meal == r->hotel * 2 % NMEAL) d += 5;  /* favorite-ish meal */
        r->money += 100 + rand() % 150;
        if (r->stay) {
            if (r->money >= HOTELFEE[r->hotel]) { r->money -= HOTELFEE[r->hotel]; d += 3 + r->hotel * 2; }
            else r->stay = 0;
        }
        r->mood = clampi(r->mood + d, 0, 100);
        if (n > 1) {
            for (int t = 0; t < 2; t++) {
                int j = rand() % MAXR;
                if (j == i || !R[j].used) continue;
                int v = clampi(F[i][j] + rand() % 6, 0, 100);
                F[i][j] = F[j][i] = v;
                if (v >= 90 && rand() % 4 == 0) {
                    char m[56];
                    snprintf(m, sizeof(m), "%s & %s are best friends!", r->name, R[j].name);
                    post(m);
                }
            }
        }
        if (rand() % 12 == 0) {
            char m[56];
            snprintf(m, sizeof(m), "%s ate %s. %s", r->name, MEALN[r->meal], r->mood > 70 ? "Yummy!" : "Meh...");
            post(m);
        }
    }
    cls();
    P("   .   *   .  \n  *  Good morning!  .\n   .   *   .\n\n  Day %d begins. Residents: %d\n", day, n);
    pause_msg();
}

/* ---------- save / load ---------- */
static void do_save(void) {
    FILE *f = fopen(SAVEFILE, "wb");
    cls();
    if (!f) { P("Save failed (create ms0:/PSP/GAME/TOMOCORE/ ?)\n"); pause_msg(); return; }
    int magic = 0x544F4D4F;
    fwrite(&magic, 4, 1, f); fwrite(&day, 4, 1, f); fwrite(&bpos, 4, 1, f);
    fwrite(R, sizeof(R), 1, f); fwrite(F, sizeof(F), 1, f); fwrite(board, sizeof(board), 1, f);
    fclose(f);
    P("Saved!  (^_^)b\n");
    pause_msg();
}
static void do_load(void) {
    FILE *f = fopen(SAVEFILE, "rb");
    cls();
    int magic = 0;
    if (!f) { P("No save data.\n"); pause_msg(); return; }
    if (fread(&magic, 4, 1, f) != 1 || magic != 0x544F4D4F) { fclose(f); P("Bad save.\n"); pause_msg(); return; }
    fread(&day, 4, 1, f); fread(&bpos, 4, 1, f);
    fread(R, sizeof(R), 1, f); fread(F, sizeof(F), 1, f); fread(board, sizeof(board), 1, f);
    fclose(f);
    P("Loaded!  (^o^)\n");
    pause_msg();
}

/* ---------- main ---------- */
int main(void) {
    setup_callbacks();
    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    srand(sceKernelGetSystemTimeLow());
    for (int i = 0; i < 3; i++) add_resident();

    const char *items[] = {"Next day", "Residents", "Move in new resident", "Dress-up", "Meals today", "Hotels",
                           "Pawn shop", "Bulletin board", "Rankings", "Save", "Load"};
    cls();
    P("\n   _____ ___  __  __  ___   ____ ___  ____  _____\n");
    P("  |_   _/ _ \\|  \\/  |/ _ \\ / ___/ _ \\|  _ \\| ____|\n");
    P("    | || | | | |\\/| | | | | |  | | | | |_) |  _|\n");
    P("    | || |_| | |  | | |_| | |__| |_| |  _ <| |___\n");
    P("    |_| \\___/|_|  |_|\\___/ \\____\\___/|_| \\_\\_____|\n\n");
    P("          ( ^_^) Press X to start\n");
    while (!(waitkey() & PSP_CTRL_CROSS));

    for (;;) {
        char title[48];
        snprintf(title, sizeof(title), "TOMOCORE Day %d  pop:%d/%d", day, count_res(), MAXR);
        int s = menu(title, items, 11);
        switch (s) {
        case 0: next_day(); break;
        case 1: { int i = pick_res("Residents"); if (i >= 0) show_resident(i); break; }
        case 2: if (count_res() < MAXR) add_resident(); break;
        case 3: screen_dress(); break;
        case 4: screen_meals(); break;
        case 5: screen_hotel(); break;
        case 6: screen_pawn(); break;
        case 7: screen_board(); break;
        case 8: screen_rank(); break;
        case 9: do_save(); break;
        case 10: do_load(); break;
        default: break;
        }
    }
    return 0;
}

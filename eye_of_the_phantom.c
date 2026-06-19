/**
 * eye_of_the_phantom.c — Eye of the Phantom
 *
 * IR/RF monster battler roguelite for Flipper Zero.
 * Scan real-world remotes (IR) and 433MHz devices (Sub-GHz)
 * to procedurally generate Phantoms, then battle through
 * an endless turn-based dungeon.
 *
 * Dependencies: gui, infrared, storage, notification
 */

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <infrared.h>
#include <infrared_worker.h>
#include <math.h>

#include "src/phantom_sprites.h"

/* ================================================================
 * CONSTANTS
 * ================================================================ */

#define SCREEN_W              128
#define SCREEN_H              64

#define MAX_STORED_PHANTOMS   10
#define SAVE_FILE             "/ext/apps_data/eotp/save.bin"
#define SAVE_DIR              "/ext/apps_data/eotp"

#define PHANTOM_NAME_LEN      16
#define COMBAT_LOG_LEN        32

/* ================================================================
 * TYPES
 * ================================================================ */

typedef enum {
    SCENE_TITLE,
    SCENE_MENU,
    SCENE_SCAN,
    SCENE_SUMMON,
    SCENE_PHANTOM,
    SCENE_COMBAT,
    SCENE_VICTORY,
    SCENE_DEFEAT,
    SCENE_CAMP,
    SCENE_UPGRADE,
    SCENE_COLLECTION,
} Scene;

typedef enum {
    CLASS_BRAWLER,
    CLASS_DEFENDER,
    CLASS_TECHNICIAN,
    CLASS_GLITCH,
} PhantomClass;

typedef enum {
    PROTO_NEC,
    PROTO_SONY,
    PROTO_SAMSUNG,
    PROTO_RC5
} SignalProtocol;

typedef struct {
    uint8_t head_idx;
    uint8_t body_idx;
    uint8_t feet_idx;
} SpriteParts;

typedef struct {
    char     name[PHANTOM_NAME_LEN];
    PhantomClass cls;
    SignalProtocol protocol;
    uint32_t address;
    uint32_t command;
    uint8_t  head_idx;
    uint8_t  body_idx;
    uint8_t  feet_idx;
    uint8_t  sprite[PHANTOM_SPRITE_BYTES];
    bool     is_elite;
    struct {
        int16_t hp;
        int16_t atk;
        int16_t def;
        int16_t spd;
    } stats;
    struct {
        uint8_t hp;
        uint8_t atk;
        uint8_t def;
        uint8_t spd;
    } upgrades;
} Phantom;

typedef struct {
    Phantom phantom;
    int16_t hp;
    int16_t max_hp;
    int16_t heat;
    bool    overloaded;
    bool    defending;
    struct {
        int16_t hp;
        int16_t atk;
        int16_t def;
        int16_t spd;
    } stats;
} CombatUnit;

typedef enum {
    COMBAT_PLAYER_TURN,
    COMBAT_ENEMY_TURN,
    COMBAT_WIN,
    COMBAT_LOSE,
} CombatState;

typedef struct {
    CombatUnit  player;
    CombatUnit  enemy;
    CombatState state;
    uint16_t    floor;
    char        log[COMBAT_LOG_LEN];
} Combat;

typedef struct {
    /* Flipper Zero subsystems */
    Gui*              gui;
    View*             view;
    ViewPort*         view_port;
    NotificationApp*  notifications;
    Storage*          storage;
    FuriTimer*        tick_timer;

    /* Scan workers */
    InfraredWorker*   ir_worker;
    bool              ir_running;
    bool              ir_done;       /* signal captured, needs processing */
    uint16_t          scan_timer;    /* timeout counter */
    Scene       scene;
    Phantom     pending_phantom;
    Phantom     active_phantom;
    bool        has_active;
    Phantom     stored[MAX_STORED_PHANTOMS];
    uint8_t     stored_count;
    uint32_t    data_shards;
    uint16_t    current_floor;
    uint8_t     cursor;
    uint8_t     max_cursor;
    uint32_t    tick;
    Combat      combat;
    uint8_t     collection_idx;
    char        message[32];
    uint8_t     message_timer;
    bool        summon_reveal;
    uint8_t     reveal_timer;
    bool        running;
} EyePhantomApp;

/* ================================================================
 * PROTOCOL / CLASS DATA
 * ================================================================ */

static const char* CLASS_NAMES[] = {"BRAWLER","DEFENDER","TECHNICIAN","GLITCH"};
static const char* PROTO_NAMES[] = {"NEC","SONY","SAMSUNG","RC5"};

static const struct { float hp, atk, def, spd; } CLASS_BIAS[] = {
    {1.1, 1.4, 0.9, 0.7},  /* BRAWLER   */
    {1.3, 0.7, 1.4, 0.8},  /* DEFENDER  */
    {1.0, 1.0, 1.0, 1.1},  /* TECHNICIAN*/
    {0.7, 1.3, 0.6, 1.5},  /* GLITCH    */
};

static const char* SYL1[] = {"ZAR","NEX","VOL","KRI","PHA","DRE","MOX","SYN",
                              "VEX","LUR","GHO","TYR","BYT","NIX","ORC","HEX"};
static const char* SYL2[] = {"TON","REX","IUS","ARA","BIT","ION","OID","ULK",
                              "ASH","IRE","ALT","ORB","INK","OSS","EEL","AMP"};

/* ================================================================
 * SIMPLE PRNG (for seeded enemy generation)
 * ================================================================ */

static uint32_t prng_state;

static void prng_seed(uint32_t seed) {
    prng_state = seed;
}

static uint32_t prng_next(void) {
    prng_state ^= prng_state << 13;
    prng_state ^= prng_state >> 17;
    prng_state ^= prng_state << 5;
    return prng_state;
}

/* ================================================================
 * SPRITE HELPERS
 * ================================================================ */

static void phantom_build_sprite(Phantom* p) {
    phantom_sprite_composite(
        p->sprite, p->head_idx, p->body_idx, p->feet_idx);
    if(p->is_elite) {
        phantom_sprite_invert(p->sprite);
    }
}

/* ================================================================
 * PHANTOM GENERATION
 * ================================================================ */

static void phantom_generate(Phantom* p, SignalProtocol proto,
                              uint32_t address, uint32_t command) {
    PhantomClass cls = (PhantomClass)(proto);  /* proto 0-3 maps to class 0-3 */

    uint32_t seed = command & 0xFF;
    int16_t base_hp  = 30 + (seed % 20);
    int16_t base_atk = 8  + ((seed * 3 + 7) % 12);
    int16_t base_def = 6  + ((seed * 5 + 13) % 12);
    int16_t base_spd = 5  + ((seed * 7 + 3) % 10);

    p->stats.hp  = (int16_t)(base_hp  * CLASS_BIAS[cls].hp);
    p->stats.atk = (int16_t)(base_atk * CLASS_BIAS[cls].atk);
    p->stats.def = (int16_t)(base_def * CLASS_BIAS[cls].def);
    p->stats.spd = (int16_t)(base_spd * CLASS_BIAS[cls].spd);

    p->cls      = cls;
    p->protocol = proto;
    p->address  = address;
    p->command  = command;
    p->is_elite = false;

    /* Sprite parts from address bits */
    p->head_idx = address & 0x03;
    p->body_idx = (address >> 2) & 0x03;
    p->feet_idx = (address >> 4) & 0x03;

    phantom_build_sprite(p);

    /* Procedural name */
    uint8_t idx1 = (address + command) % 16;
    uint8_t idx2 = (command * 3 + address) % 16;
    snprintf(p->name, PHANTOM_NAME_LEN, "%s%s", SYL1[idx1], SYL2[idx2]);

    memset(&p->upgrades, 0, sizeof(p->upgrades));
}

static void phantom_get_effective(const Phantom* p, int16_t* hp, int16_t* atk,
                                   int16_t* def, int16_t* spd) {
    *hp  = p->stats.hp  + p->upgrades.hp  * 3;
    *atk = p->stats.atk + p->upgrades.atk * 2;
    *def = p->stats.def + p->upgrades.def * 2;
    *spd = p->stats.spd + p->upgrades.spd * 1;
}

static void phantom_generate_enemy(Phantom* p, uint16_t floor) {
    prng_seed(floor * 2654435761u);
    uint8_t pi = prng_next() % 4;
    SignalProtocol proto = (SignalProtocol)pi;
    uint32_t addr = prng_next() & 0xFF;
    uint32_t cmd  = prng_next() & 0xFF;

    phantom_generate(p, proto, addr, cmd);

    /* Prefix name */
    char orig[PHANTOM_NAME_LEN];
    strncpy(orig, p->name, PHANTOM_NAME_LEN);
    orig[PHANTOM_NAME_LEN - 1] = '\0';
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "C-%s", orig);
    strncpy(p->name, tmp, PHANTOM_NAME_LEN);
    p->name[PHANTOM_NAME_LEN - 1] = '\0';

    /* Scale stats with floor */
    float scale = 1.0f + floor * 0.12f;
    p->stats.hp  = (int16_t)(p->stats.hp  * scale);
    p->stats.atk = (int16_t)(p->stats.atk * scale);
    p->stats.def = (int16_t)(p->stats.def * scale);
    p->stats.spd = (int16_t)(p->stats.spd * scale);
}

/* ================================================================
 * COMBAT ENGINE
 * ================================================================ */

static void combat_start(EyePhantomApp* app) {
    Combat* c = &app->combat;
    Phantom* enemy = malloc(sizeof(Phantom));
    phantom_generate_enemy(enemy, app->current_floor);

    /* Player */
    c->player.phantom = app->active_phantom;
    int16_t hp, atk, def, spd;
    phantom_get_effective(&app->active_phantom, &hp, &atk, &def, &spd);
    c->player.hp     = hp;
    c->player.max_hp  = hp;
    c->player.heat   = 0;
    c->player.overloaded = false;
    c->player.defending  = false;
    c->player.stats.hp  = hp;
    c->player.stats.atk = atk;
    c->player.stats.def = def;
    c->player.stats.spd = spd;

    /* Enemy */
    c->enemy.phantom = *enemy;
    phantom_get_effective(enemy, &hp, &atk, &def, &spd);
    c->enemy.hp     = hp;
    c->enemy.max_hp  = hp;
    c->enemy.heat   = 0;
    c->enemy.overloaded = false;
    c->enemy.defending  = false;
    c->enemy.stats.hp  = hp;
    c->enemy.stats.atk = atk;
    c->enemy.stats.def = def;
    c->enemy.stats.spd = spd;

    free(enemy);

    c->state = COMBAT_PLAYER_TURN;
    c->floor = app->current_floor;
    snprintf(c->log, COMBAT_LOG_LEN, "FLOOR %u - FIGHT!", app->current_floor);
    app->cursor = 0;
}

static int16_t combat_calc_damage(CombatUnit* attacker, CombatUnit* defender,
                                   bool is_skill, bool* out_crit) {
    int16_t base = attacker->stats.atk - defender->stats.def / 2;
    if(base < 1) base = 1;
    if(is_skill) base = base * 3 / 2;

    /* Crit check: SPD * 2 / 100, capped at 30% */
    uint8_t crit_chance = attacker->stats.spd * 2;
    if(crit_chance > 30) crit_chance = 30;
    bool crit = (prng_next() % 100) < crit_chance;
    if(crit) base = base * 9 / 5;

    if(out_crit) *out_crit = crit;

    /* ±15% variance */
    int16_t variance = 85 + (prng_next() % 30);
    int16_t dmg = base * variance / 100;
    if(dmg < 1) dmg = 1;
    return dmg;
}

static void combat_player_action(EyePhantomApp* app, uint8_t action) {
    Combat* c = &app->combat;
    if(c->state != COMBAT_PLAYER_TURN) return;

    if(c->player.overloaded) {
        snprintf(c->log, COMBAT_LOG_LEN, "OVERLOADED!");
        c->player.overloaded = false;
        c->player.heat = c->player.heat > 50 ? c->player.heat - 50 : 0;
    } else {
        switch(action) {
            case 0: { /* ATK */
                bool crit;
                int16_t dmg = combat_calc_damage(&c->player, &c->enemy, false, &crit);
                c->enemy.hp -= dmg;
                c->player.heat += 20;
                snprintf(c->log, COMBAT_LOG_LEN, "ATK: %d%s", dmg, crit ? " CRIT!" : "");
                break;
            }
            case 1: { /* BURST */
                bool crit;
                int16_t dmg = combat_calc_damage(&c->player, &c->enemy, true, &crit);
                c->enemy.hp -= dmg;
                c->player.heat += 35;
                snprintf(c->log, COMBAT_LOG_LEN, "BURST: %d%s", dmg, crit ? " CRIT!" : "");
                break;
            }
            case 2: { /* DEF */
                c->player.heat = c->player.heat > 30 ? c->player.heat - 30 : 0;
                c->player.defending = true;
                snprintf(c->log, COMBAT_LOG_LEN, "DEFEND! -30 HEAT");
                break;
            }
        }
    }

    /* Check overload */
    if(c->player.heat >= 100) {
        c->player.overloaded = true;
        c->player.heat = 100;
        int16_t ov_dmg = c->player.max_hp * 8 / 100;
        c->player.hp -= ov_dmg;
        snprintf(c->log, COMBAT_LOG_LEN, "OVERLOAD! -%d", ov_dmg);
    }

    /* Check player defeat from overload */
    if(c->player.hp <= 0) {
        c->player.hp = 0;
        c->state = COMBAT_LOSE;
        return;
    }

    /* Check enemy defeat */
    if(c->enemy.hp <= 0) {
        c->enemy.hp = 0;
        c->state = COMBAT_WIN;
        return;
    }

    c->state = COMBAT_ENEMY_TURN;
}

static void combat_enemy_action(EyePhantomApp* app) {
    Combat* c = &app->combat;
    if(c->state != COMBAT_ENEMY_TURN) return;

    if(c->enemy.overloaded) {
        snprintf(c->log, COMBAT_LOG_LEN, "FOE OVERLOADED!");
        c->enemy.overloaded = false;
        c->enemy.heat = c->enemy.heat > 50 ? c->enemy.heat - 50 : 0;
    } else {
        /* AI: weighted random based on heat */
        uint8_t roll = prng_next() % 100;
        uint8_t action;

        if(c->enemy.heat >= 80) {
            action = (roll < 10) ? 0 : (roll < 10) ? 1 : 2;  /* 10/0/90 */
        } else if(c->enemy.heat >= 50) {
            action = (roll < 30) ? 0 : (roll < 40) ? 1 : 2;  /* 30/10/60 */
        } else {
            action = (roll < 70) ? 0 : (roll < 90) ? 1 : 2;  /* 70/20/10 */
        }

        switch(action) {
            case 0: { /* ATK */
                bool crit;
                int16_t dmg = combat_calc_damage(&c->enemy, &c->player, false, &crit);
                if(c->player.defending) dmg /= 2;
                c->player.hp -= dmg;
                c->enemy.heat += 20;
                c->player.defending = false;
                snprintf(c->log, COMBAT_LOG_LEN, "FOE ATK: %d%s", dmg, crit ? " CRIT!" : "");
                break;
            }
            case 1: { /* BURST */
                bool crit;
                int16_t dmg = combat_calc_damage(&c->enemy, &c->player, true, &crit);
                if(c->player.defending) dmg /= 2;
                c->player.hp -= dmg;
                c->enemy.heat += 35;
                c->player.defending = false;
                snprintf(c->log, COMBAT_LOG_LEN, "FOE BURST: %d%s", dmg, crit ? " CRIT!" : "");
                break;
            }
            case 2: { /* DEF */
                c->enemy.heat = c->enemy.heat > 30 ? c->enemy.heat - 30 : 0;
                c->enemy.defending = true;
                snprintf(c->log, COMBAT_LOG_LEN, "FOE DEFENDS");
                break;
            }
        }
    }

    /* Check enemy overload */
    if(c->enemy.heat >= 100) {
        c->enemy.overloaded = true;
        c->enemy.heat = 100;
        int16_t ov_dmg = c->enemy.max_hp * 8 / 100;
        c->enemy.hp -= ov_dmg;
    }

    /* Check enemy defeat from overload */
    if(c->enemy.hp <= 0) {
        c->enemy.hp = 0;
        c->state = COMBAT_WIN;
        return;
    }

    /* Check player defeat */
    if(c->player.hp <= 0) {
        c->player.hp = 0;
        c->state = COMBAT_LOSE;
        return;
    }

    c->state = COMBAT_PLAYER_TURN;
}

/* ================================================================
 * SAVE / LOAD
 * ================================================================ */

#define SAVE_MAGIC  0x45505450  /* "EPTP" */
#define SAVE_VER    1

#pragma pack(push, 1)
typedef struct {
    char     name[PHANTOM_NAME_LEN];
    uint8_t  cls;
    uint8_t  protocol;
    uint32_t address;
    uint32_t command;
    uint8_t  head_idx;
    uint8_t  body_idx;
    uint8_t  feet_idx;
    bool     is_elite;
    int16_t  hp, atk, def, spd;
    uint8_t  up_hp, up_atk, up_def, up_spd;
} SavedPhantom;

typedef struct {
    uint32_t magic;
    uint8_t  ver;
    bool     has_active;
    SavedPhantom active;
    uint8_t  stored_count;
    SavedPhantom stored[MAX_STORED_PHANTOMS];
    uint32_t data_shards;
    uint16_t current_floor;
} SaveData;
#pragma pack(pop)

static void save_load_phantom(Phantom* p, const SavedPhantom* sp) {
    strncpy(p->name, sp->name, PHANTOM_NAME_LEN);
    p->cls      = (PhantomClass)sp->cls;
    p->protocol = (SignalProtocol)sp->protocol;
    p->address  = sp->address;
    p->command  = sp->command;
    p->head_idx = sp->head_idx;
    p->body_idx = sp->body_idx;
    p->feet_idx = sp->feet_idx;
    p->is_elite = sp->is_elite;
    p->stats.hp  = sp->hp;
    p->stats.atk = sp->atk;
    p->stats.def = sp->def;
    p->stats.spd = sp->spd;
    p->upgrades.hp  = sp->up_hp;
    p->upgrades.atk = sp->up_atk;
    p->upgrades.def = sp->up_def;
    p->upgrades.spd = sp->up_spd;
    phantom_build_sprite(p);
}

static void save_save_phantom(const Phantom* p, SavedPhantom* sp) {
    strncpy(sp->name, p->name, PHANTOM_NAME_LEN);
    sp->cls      = (uint8_t)p->cls;
    sp->protocol = (uint8_t)p->protocol;
    sp->address  = p->address;
    sp->command  = p->command;
    sp->head_idx = p->head_idx;
    sp->body_idx = p->body_idx;
    sp->feet_idx = p->feet_idx;
    sp->is_elite = p->is_elite;
    sp->hp  = p->stats.hp;
    sp->atk = p->stats.atk;
    sp->def = p->stats.def;
    sp->spd = p->stats.spd;
    sp->up_hp  = p->upgrades.hp;
    sp->up_atk = p->upgrades.atk;
    sp->up_def = p->upgrades.def;
    sp->up_spd = p->upgrades.spd;
}

static void game_save(EyePhantomApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, SAVE_DIR);

    SaveData data;
    memset(&data, 0, sizeof(data));
    data.magic  = SAVE_MAGIC;
    data.ver    = SAVE_VER;
    data.has_active = app->has_active;
    if(app->has_active) save_save_phantom(&app->active_phantom, &data.active);
    data.stored_count = app->stored_count;
    for(uint8_t i = 0; i < app->stored_count; i++)
        save_save_phantom(&app->stored[i], &data.stored[i]);
    data.data_shards   = app->data_shards;
    data.current_floor = app->current_floor;

    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, SAVE_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, &data, sizeof(data));
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static bool game_load(EyePhantomApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    SaveData data;
    bool ok = false;

    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, SAVE_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        if(storage_file_read(file, &data, sizeof(data)) == sizeof(data) &&
           data.magic == SAVE_MAGIC && data.ver == SAVE_VER) {
            app->has_active = data.has_active;
            if(data.has_active) save_load_phantom(&app->active_phantom, &data.active);
            app->stored_count = data.stored_count;
            for(uint8_t i = 0; i < data.stored_count; i++)
                save_load_phantom(&app->stored[i], &data.stored[i]);
            app->data_shards   = data.data_shards;
            app->current_floor = data.current_floor;
            ok = true;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

/* ================================================================
 * HAPTICS
 * ================================================================ */

static void haptic_double_buzz(EyePhantomApp* app) __attribute__((used));
static void haptic_double_buzz(EyePhantomApp* app) {
    notification_message(app->notifications, &sequence_set_vibro_on);
    furi_delay_ms(40);
    notification_message(app->notifications, &sequence_reset_vibro);
    furi_delay_ms(40);
    notification_message(app->notifications, &sequence_set_vibro_on);
    furi_delay_ms(40);
    notification_message(app->notifications, &sequence_reset_vibro);
}

static void haptic_long_buzz(EyePhantomApp* app) __attribute__((used));
static void haptic_long_buzz(EyePhantomApp* app) {
    notification_message(app->notifications, &sequence_set_vibro_on);
    furi_delay_ms(300);
    notification_message(app->notifications, &sequence_reset_vibro);
}

/* ================================================================
 * INFRARED SCAN
 * ================================================================ */

static void ir_callback(void* context, InfraredWorkerSignal* signal) {
    EyePhantomApp* app = (EyePhantomApp*)context;
    if(!signal || !app->ir_running || app->ir_done) return;

    /* Generate phantom — processing deferred to tick to avoid reentrant stop */
    uint32_t seed = furi_hal_rtc_get_timestamp();
    uint32_t addr = seed & 0xFF;
    uint32_t cmd  = (seed >> 8) & 0xFF;

    phantom_generate(&app->pending_phantom, PROTO_NEC, addr, cmd);
    app->ir_done = true;
}

static void scan_ir_start(EyePhantomApp* app) {
    if(!app->ir_worker) return;
    if(app->ir_running) {
        infrared_worker_rx_stop(app->ir_worker);
        app->ir_running = false;
    }
    app->ir_done = false;
    app->scan_timer = 0;
    app->ir_running = true;
    infrared_worker_rx_set_received_signal_callback(app->ir_worker, ir_callback, app);
    infrared_worker_rx_start(app->ir_worker);
}

static void scan_ir_stop(EyePhantomApp* app) {
    if(!app->ir_running || !app->ir_worker) return;
    infrared_worker_rx_stop(app->ir_worker);
    app->ir_running = false;
}

/* ================================================================
 * RENDERING HELPERS
 * ================================================================ */

/* 4x5 bitmap font */
static const uint8_t FONT_DATA[][5] = {
    ['A']={0x6,0x9,0x9,0xF,0x9},['B']={0xE,0x9,0xE,0x9,0xE},['C']={0x7,0x8,0x8,0x8,0x7},
    ['D']={0xE,0x9,0x9,0x9,0xE},['E']={0xF,0x8,0xE,0x8,0xF},['F']={0xF,0x8,0xE,0x8,0x8},
    ['G']={0x7,0x8,0xB,0x9,0x7},['H']={0x9,0x9,0xF,0x9,0x9},['I']={0xE,0x4,0x4,0x4,0xE},
    ['J']={0x7,0x1,0x1,0x9,0x6},['K']={0x9,0xA,0xC,0xA,0x9},['L']={0x8,0x8,0x8,0x8,0xF},
    ['M']={0x9,0xF,0xF,0x9,0x9},['N']={0x9,0xD,0xB,0x9,0x9},['O']={0x6,0x9,0x9,0x9,0x6},
    ['P']={0xE,0x9,0xE,0x8,0x8},['Q']={0x6,0x9,0x9,0xA,0x5},['R']={0xE,0x9,0xE,0xA,0x9},
    ['S']={0x7,0x8,0x6,0x1,0xE},['T']={0xF,0x4,0x4,0x4,0x4},['U']={0x9,0x9,0x9,0x9,0x6},
    ['V']={0x9,0x9,0x9,0x6,0x6},['W']={0x9,0x9,0xF,0xF,0x9},['X']={0x9,0x9,0x6,0x9,0x9},
    ['Y']={0x9,0x9,0x6,0x4,0x4},['Z']={0xF,0x1,0x6,0x8,0xF},
    ['0']={0x6,0x9,0x9,0x9,0x6},['1']={0x4,0xC,0x4,0x4,0xE},['2']={0x6,0x9,0x2,0x4,0xF},
    ['3']={0xE,0x1,0x6,0x1,0xE},['4']={0x9,0x9,0xF,0x1,0x1},['5']={0xF,0x8,0xE,0x1,0xE},
    ['6']={0x7,0x8,0xF,0x9,0x6},['7']={0xF,0x1,0x2,0x4,0x4},['8']={0x6,0x9,0x6,0x9,0x6},
    ['9']={0x6,0x9,0x7,0x1,0x6},
    [' ']={0,0,0,0,0},['!']={0x4,0x4,0x4,0x0,0x4},['?']={0x6,0x9,0x2,0x0,0x2},
    [':']={0x0,0x4,0x0,0x4,0x0},['.']={0,0,0,0,0x4},['-']={0,0,0xF,0,0},
    ['+']={0x0,0x4,0xE,0x4,0x0},['/']={0x1,0x2,0x4,0x8,0x0},['%']={0x9,0x1,0x6,0x8,0x9},
    ['#']={0xA,0xF,0xA,0xF,0xA},['>']={0x8,0x4,0x2,0x4,0x8},['<']={0x2,0x4,0x8,0x4,0x2},
    ['*']={0xA,0x4,0xE,0x4,0xA},['=']={0x0,0xF,0x0,0xF,0x0},['_']={0,0,0,0,0xF},
    ['(']={0x2,0x4,0x4,0x4,0x2},[')']={0x4,0x2,0x2,0x2,0x4},[',']={0,0,0,0x2,0x4},
    ['\'']={0x4,0x4,0,0,0},['"']={0xA,0xA,0,0,0},['~']={0,0x5,0xA,0,0},
    ['$']={0x6,0x9,0x7,0x1,0x6},['x']={0,0x9,0x6,0x6,0x9},
};

#define FONT_W  4
#define FONT_H  5
#define CHAR_W  (FONT_W + 1)
#define CHAR_H  (FONT_H + 1)

static void draw_char(Canvas* c, char ch, int x, int y) {
    if(ch < 32 || ch > 127) return;
    const uint8_t* glyph = FONT_DATA[(uint8_t)ch];
    for(int row = 0; row < FONT_H; row++) {
        for(int col = 0; col < FONT_W; col++) {
            if((glyph[row] >> (FONT_W - 1 - col)) & 1) {
                canvas_draw_dot(c, x + col, y + row);
            }
        }
    }
}

static void draw_str(Canvas* c, const char* str, int x, int y) {
    int cx = x;
    while(*str) {
        draw_char(c, *str, cx, y);
        cx += CHAR_W;
        str++;
    }
}

static void draw_str_centered(Canvas* c, const char* str, int y) {
    int w = strlen(str) * CHAR_W;
    draw_str(c, str, (SCREEN_W - w) / 2, y);
}

static void draw_hbar(Canvas* c, int x, int y, int w, int h, float ratio) {
    if(ratio < 0) ratio = 0;
    if(ratio > 1) ratio = 1;
    canvas_draw_frame(c, x, y, w, h);
    int fill = (int)((w - 2) * ratio);
    if(fill > 0) canvas_draw_box(c, x + 1, y + 1, fill, h - 2);
}

static void draw_sprite(Canvas* c, const uint8_t* sprite, int x, int y) {
    canvas_draw_xbm(c, x, y, PHANTOM_SPRITE_W, PHANTOM_SPRITE_H, sprite);
}

/* ================================================================
 * SCENE RENDERERS
 * ================================================================ */

static void render_title(Canvas* c, EyePhantomApp* app) {
    canvas_clear(c);
    uint32_t t = app->tick;

    /* Eye logo */
    int cx = 64, cy = 20;
    canvas_draw_circle(c, cx, cy, 12);
    canvas_draw_circle(c, cx, cy, 10);
    canvas_draw_disc(c, cx, cy, 5);

    /* Pupil movement */
    int px = cx + (int)(sinf(t * 0.05f) * 3);
    int py = cy + (int)(cosf(t * 0.07f) * 2);
    canvas_draw_disc(c, px, py, 2);

    /* Blink every ~3s */
    if((t % 90) < 5) {
        canvas_invert_color(c);
        canvas_draw_box(c, cx - 12, cy - 12, 25, 13);
        canvas_invert_color(c);
    }

    draw_str_centered(c, "EYE OF THE", 36);
    draw_str_centered(c, "PHANTOM", 44);

    if((t / 20) % 2 == 0)
        draw_str_centered(c, "PRESS OK", 56);
}

static void render_menu(Canvas* c, EyePhantomApp* app) {
    canvas_clear(c);
    draw_str(c, "MENU", 2, 2);

    char buf[32];
    snprintf(buf, sizeof(buf), "$%lu", app->data_shards);
    draw_str(c, buf, 80, 2);
    snprintf(buf, sizeof(buf), "F%u", app->current_floor);
    draw_str(c, buf, 108, 2);

    canvas_draw_line(c, 0, 9, 127, 9);

    const char* items[] = {"SCAN", "MY PHANTOM", "ASCEND", "DESCEND", "CAMP"};
    for(int i = 0; i < 5; i++) {
        int y = 14 + i * 10;
        if(i == app->cursor) {
            canvas_draw_box(c, 0, y - 1, 128, 7);
            canvas_invert_color(c);
            draw_str(c, items[i], 12, y);
            draw_str(c, ">", 3, y);
            canvas_invert_color(c);
        } else {
            draw_str(c, items[i], 12, y);
        }
    }
}

static void render_scan(Canvas* c, EyePhantomApp* app) {
    canvas_clear(c);
    uint32_t t = app->tick;

    if(app->ir_running) {
        draw_str_centered(c, "IR SCANNING...", 4);
        int cx = 64, cy = 32;
        for(int r = 1; r < 4; r++) {
            int radius = ((t + r * 16) % 56) + 2;
            if(radius < 25) canvas_draw_circle(c, cx, cy, radius);
        }
    } else {
        draw_str_centered(c, "INFRARED SCAN", 4);
        canvas_draw_line(c, 0, 12, 127, 12);
        canvas_draw_circle(c, 64, 34, 8);
        canvas_draw_disc(c, 64, 34, 4);
        draw_str_centered(c, "POINT REMOTE", 48);
        draw_str_centered(c, "PRESS OK TO SCAN", 56);
    }
}

static void render_summon(Canvas* c, EyePhantomApp* app) {
    canvas_clear(c);
    Phantom* p = &app->pending_phantom;

    if(app->summon_reveal) {
        app->reveal_timer++;
        if(app->reveal_timer < 50) {
            float progress = app->reveal_timer / 50.0f;
            /* Noise reveal */
            for(int y = 0; y < SCREEN_H; y++)
                for(int x = 0; x < SCREEN_W; x++)
                    if((rand() % 100) > progress * 70)
                        if(rand() % 2) canvas_draw_dot(c, x, y);
            draw_str_centered(c, "SIGNAL FOUND!", 28);
            return;
        }
        app->summon_reveal = false;
        app->cursor = 0;
    }

    draw_str(c, "SUMMONED!", 2, 2);
    canvas_draw_line(c, 0, 9, 127, 9);

    /* Sprite */
    draw_sprite(c, p->sprite, 4, 13);
    if(p->is_elite) canvas_draw_frame(c, 2, 11, 20, 20);

    /* Info */
    draw_str(c, p->name, 26, 13);
    draw_str(c, CLASS_NAMES[p->cls], 26, 21);

    char buf[32];
    snprintf(buf, sizeof(buf), "HP %d", p->stats.hp);
    draw_str(c, buf, 26, 31);
    snprintf(buf, sizeof(buf), "ATK%d", p->stats.atk);
    draw_str(c, buf, 70, 31);
    snprintf(buf, sizeof(buf), "DEF%d", p->stats.def);
    draw_str(c, buf, 26, 39);
    snprintf(buf, sizeof(buf), "SPD%d", p->stats.spd);
    draw_str(c, buf, 70, 39);

    /* Protocol */
    const char* origin = "IR:";
    snprintf(buf, sizeof(buf), "%s%s %02lX:%02lX", origin,
             PROTO_NAMES[p->protocol], p->address & 0xFF, p->command & 0xFF);
    draw_str(c, buf, 2, 48);

    /* Keep / Discard */
    if(app->cursor == 0) {
        canvas_draw_box(c, 2, 56, 58, 8);
        canvas_invert_color(c);
        draw_str(c, "KEEP", 6, 57);
        canvas_invert_color(c);
        draw_str(c, "DISCARD", 68, 57);
    } else {
        draw_str(c, "KEEP", 6, 57);
        canvas_draw_box(c, 62, 56, 60, 8);
        canvas_invert_color(c);
        draw_str(c, "DISCARD", 68, 57);
        canvas_invert_color(c);
    }
}

static void render_phantom(Canvas* c, EyePhantomApp* app) {
    canvas_clear(c);
    Phantom* p = &app->active_phantom;

    if(!app->has_active) {
        draw_str_centered(c, "NO PHANTOM", 28);
        return;
    }

    draw_str(c, p->name, 2, 2);
    canvas_draw_line(c, 0, 9, 127, 9);

    draw_sprite(c, p->sprite, 4, 14);
    draw_str(c, CLASS_NAMES[p->cls], 26, 14);

    int16_t hp, atk, def, spd;
    phantom_get_effective(p, &hp, &atk, &def, &spd);

    char buf[32];
    snprintf(buf, sizeof(buf), "HP  %d", hp);
    draw_str(c, buf, 26, 24);
    snprintf(buf, sizeof(buf), "ATK %d", atk);
    draw_str(c, buf, 26, 32);
    snprintf(buf, sizeof(buf), "DEF %d", def);
    draw_str(c, buf, 76, 24);
    snprintf(buf, sizeof(buf), "SPD %d", spd);
    draw_str(c, buf, 76, 32);

    if(p->upgrades.hp || p->upgrades.atk || p->upgrades.def || p->upgrades.spd) {
        char upbuf[32];
        snprintf(upbuf, sizeof(upbuf), "+%d +%d +%d +%d",
                 p->upgrades.hp, p->upgrades.atk, p->upgrades.def, p->upgrades.spd);
        draw_str(c, upbuf, 26, 42);
    }

    draw_str(c, PROTO_NAMES[p->protocol], 2, 56);
    draw_str(c, "BACK", 100, 56);
}

static void render_combat(Canvas* c, EyePhantomApp* app) {
    canvas_clear(c);
    Combat* co = &app->combat;
    uint32_t t = app->tick;

    /* --- TOP: HP bars (rows 0-6) --- */
    draw_str(c, "YOU", 1, 1);
    draw_hbar(c, 20, 1, 42, 5, (float)co->player.hp / co->player.max_hp);
    draw_str(c, "FOE", 68, 1);
    draw_hbar(c, 88, 1, 38, 5, (float)co->enemy.hp / co->enemy.max_hp);

    /* Heat bars (rows 7-11) */
    draw_str(c, "H", 1, 8);
    draw_hbar(c, 8, 8, 54, 4, co->player.heat / 100.0f);
    draw_str(c, "H", 68, 8);
    draw_hbar(c, 76, 8, 50, 4, co->enemy.heat / 100.0f);

    /* Heat danger flash on the bar itself */
    if(co->player.heat >= 70 && (t % 16 < 8))
        canvas_draw_frame(c, 6, 7, 58, 6);
    if(co->enemy.heat >= 70 && (t % 16 < 8))
        canvas_draw_frame(c, 74, 7, 54, 6);

    canvas_draw_line(c, 0, 13, 127, 13);

    /* --- MIDDLE: sprites (rows 14-41) --- */
    int sy = 18;
    bool p_inv = co->player.overloaded && (t % 6 < 3);
    bool e_inv = co->enemy.overloaded && (t % 6 < 3);

    if(p_inv) canvas_invert_color(c);
    draw_sprite(c, co->player.phantom.sprite, 8, sy);
    if(p_inv) canvas_invert_color(c);

    if(e_inv) canvas_invert_color(c);
    draw_sprite(c, co->enemy.phantom.sprite, 104, sy);
    if(e_inv) canvas_invert_color(c);

    /* HP readouts below sprites */
    char buf[20];
    snprintf(buf, sizeof(buf), "%d/%d", co->player.hp, co->player.max_hp);
    draw_str(c, buf, 4, 36);
    snprintf(buf, sizeof(buf), "%d/%d", co->enemy.hp, co->enemy.max_hp);
    draw_str(c, buf, 96, 36);

    draw_str(c, "VS", 58, 24);

    /* --- Combat log (rows 42-47) --- */
    canvas_draw_line(c, 0, 42, 127, 42);
    draw_str(c, co->log, 2, 44);

    /* --- BOTTOM: actions (rows 48-63) --- */
    canvas_draw_line(c, 0, 49, 127, 49);

    if(co->state == COMBAT_PLAYER_TURN) {
        const char* acts[] = {"ATK", "BURST", "DEF"};
        int px[] = {2, 44, 86};

        for(int i = 0; i < 3; i++) {
            char label[16];
            snprintf(label, sizeof(label), "%s:%s",
                     i == 0 ? "UP" : i == 1 ? "RT" : "DN", acts[i]);
            if(i == app->cursor) {
                int w = strlen(label) * CHAR_W + 4;
                canvas_draw_box(c, px[i], 51, w, 6);
                canvas_invert_color(c);
                draw_str(c, label, px[i] + 2, 52);
                canvas_invert_color(c);
            } else {
                draw_str(c, label, px[i] + 2, 52);
            }
        }

        /* Compact hint line at the very bottom */
        const char* h = (app->cursor == 0) ? "HIT  +20h" :
                        (app->cursor == 1) ? "x1.5 +35h" : "BLOCK -30h";
        draw_str_centered(c, h, 59);
    } else if(co->state == COMBAT_ENEMY_TURN) {
        draw_str_centered(c, "ENEMY TURN...", 55);
    } else if(co->state == COMBAT_WIN) {
        draw_str_centered(c, "VICTORY!", 55);
    } else if(co->state == COMBAT_LOSE) {
        draw_str_centered(c, "DEFEATED...", 55);
    }
}

static void render_victory(Canvas* c, EyePhantomApp* app) {
    canvas_clear(c);
    uint32_t t = app->tick;

    draw_str_centered(c, "VICTORY!", 4);
    canvas_draw_line(c, 0, 12, 127, 12);

    if(app->has_active) {
        int bounce = (int)(sinf(t * 0.15f) * 3);
        draw_sprite(c, app->active_phantom.sprite, 56, 16 + bounce);
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "FLOOR %u CLEARED!", app->current_floor - 1);
    draw_str_centered(c, buf, 36);

    uint32_t shards = app->current_floor; /* floor already incremented */
    snprintf(buf, sizeof(buf), "+%lu DATA SHARDS", shards);
    draw_str_centered(c, buf, 44);
    snprintf(buf, sizeof(buf), "TOTAL: %lu", app->data_shards);
    draw_str_centered(c, buf, 52);

    if((t / 20) % 2 == 0)
        draw_str_centered(c, "PRESS OK", 58);
}

static void render_defeat(Canvas* c, EyePhantomApp* app) {
    canvas_clear(c);
    uint32_t t = app->tick;

    draw_str_centered(c, "DEFEATED", 8);
    canvas_draw_line(c, 0, 16, 127, 16);

    if(app->has_active) {
        /* Glitchy phantom */
        for(int y = 0; y < 16; y++)
            for(int x = 0; x < 16; x++)
                if(app->active_phantom.sprite[y * 2 + x / 8] & (1 << (x % 8)))
                    if(rand() % 10 > 3)
                        canvas_draw_dot(c, 56 + x, 22 + y);
    }

    draw_str_centered(c, "YOUR PHANTOM", 42);
    draw_str_centered(c, "WAS KILLED", 50);

    if((t / 20) % 2 == 0)
        draw_str_centered(c, "PRESS OK", 58);
}

static void render_camp(Canvas* c, EyePhantomApp* app) {
    canvas_clear(c);
    const char* items[] = {"UPGRADE STATS", "COLLECTION", "BACK"};

    draw_str(c, "CAMP", 2, 2);
    char buf[32];
    snprintf(buf, sizeof(buf), "$%lu", app->data_shards);
    draw_str(c, buf, 80, 2);
    canvas_draw_line(c, 0, 9, 127, 9);

    for(int i = 0; i < 3; i++) {
        int y = 14 + i * 12;
        if(i == app->cursor) {
            canvas_draw_box(c, 0, y - 1, 128, 7);
            canvas_invert_color(c);
            draw_str(c, items[i], 12, y);
            draw_str(c, ">", 3, y);
            canvas_invert_color(c);
        } else {
            draw_str(c, items[i], 12, y);
        }
    }
}

static void render_upgrade(Canvas* c, EyePhantomApp* app) {
    canvas_clear(c);
    Phantom* p = &app->active_phantom;

    draw_str(c, "UPGRADE", 2, 2);
    char buf[32];
    snprintf(buf, sizeof(buf), "$%lu", app->data_shards);
    draw_str(c, buf, 80, 2);
    canvas_draw_line(c, 0, 9, 127, 9);

    int16_t hp, atk, def, spd;
    phantom_get_effective(p, &hp, &atk, &def, &spd);

    const char* stat_names[] = {"HP", "ATK", "DEF", "SPD"};
    int16_t vals[] = {hp, atk, def, spd};
    uint8_t* ups[] = {&p->upgrades.hp, &p->upgrades.atk, &p->upgrades.def, &p->upgrades.spd};

    for(int i = 0; i < 4; i++) {
        int y = 14 + i * 10;
        uint32_t cost = (*ups[i] + 1) * 3;
        snprintf(buf, sizeof(buf), "%s %d +%d ($%lu)",
                 stat_names[i], vals[i], *ups[i], cost);
        if(i == app->cursor) {
            canvas_draw_box(c, 0, y - 1, 128, 7);
            canvas_invert_color(c);
            draw_str(c, buf, 4, y);
            canvas_invert_color(c);
        } else {
            draw_str(c, buf, 4, y);
        }
    }

    if(app->cursor == 4) {
        canvas_draw_box(c, 0, 53, 128, 7);
        canvas_invert_color(c);
        draw_str(c, "BACK", 4, 54);
        canvas_invert_color(c);
    } else {
        draw_str(c, "BACK", 4, 54);
    }
}

static void render_collection(Canvas* c, EyePhantomApp* app) {
    canvas_clear(c);

    uint8_t total = app->stored_count + (app->has_active ? 1 : 0);

    draw_str(c, "COLLECTION", 2, 2);
    char buf[32];
    snprintf(buf, sizeof(buf), "%u/11", total);
    draw_str(c, buf, 90, 2);
    canvas_draw_line(c, 0, 9, 127, 9);

    if(total == 0) {
        draw_str_centered(c, "EMPTY", 28);
        draw_str_centered(c, "SCAN PHANTOMS FIRST", 38);
    } else {
        Phantom* p;
        if(app->has_active && app->collection_idx == 0) {
            p = &app->active_phantom;
        } else {
            uint8_t si = app->collection_idx - (app->has_active ? 1 : 0);
            p = &app->stored[si];
        }

        draw_sprite(c, p->sprite, 4, 14);
        draw_str(c, p->name, 26, 14);
        draw_str(c, CLASS_NAMES[p->cls], 26, 22);

        int16_t hp, atk, def, spd;
        phantom_get_effective(p, &hp, &atk, &def, &spd);
        snprintf(buf, sizeof(buf), "HP%d A%d", hp, atk);
        draw_str(c, buf, 26, 32);
        snprintf(buf, sizeof(buf), "D%d S%d", def, spd);
        draw_str(c, buf, 26, 40);

        snprintf(buf, sizeof(buf), "%u/%u", app->collection_idx + 1, total);
        draw_str(c, buf, 2, 56);
        draw_str(c, "OK=SET", 44, 56);
    }
}

static void render_message(Canvas* c, EyePhantomApp* app) {
    if(app->message_timer == 0) return;
    int w = strlen(app->message) * CHAR_W + 8;
    int x = (SCREEN_W - w) / 2;
    int y = 24;
    canvas_draw_frame(c, x - 2, y, w + 4, 14);
    canvas_draw_box(c, x - 1, y + 1, w + 2, 12);
    /* Invert so text is visible over the filled box */
    canvas_invert_color(c);
    draw_str(c, app->message, x + 4, y + 4);
    canvas_invert_color(c);
}

/* ================================================================
 * MAIN DRAW CALLBACK
 * ================================================================ */

static void app_draw(Canvas* c, void* context) {
    EyePhantomApp* app = (EyePhantomApp*)context;

    switch(app->scene) {
        case SCENE_TITLE:      render_title(c, app); break;
        case SCENE_MENU:       render_menu(c, app); break;
        case SCENE_SCAN:       render_scan(c, app); break;
        case SCENE_SUMMON:     render_summon(c, app); break;
        case SCENE_PHANTOM:    render_phantom(c, app); break;
        case SCENE_COMBAT:     render_combat(c, app); break;
        case SCENE_VICTORY:    render_victory(c, app); break;
        case SCENE_DEFEAT:     render_defeat(c, app); break;
        case SCENE_CAMP:       render_camp(c, app); break;
        case SCENE_UPGRADE:    render_upgrade(c, app); break;
        case SCENE_COLLECTION: render_collection(c, app); break;
    }

    if(app->message_timer > 0) render_message(c, app);
}

/* ================================================================
 * INPUT HANDLING
 * ================================================================ */

static void app_input(InputEvent* event, void* context) {
    EyePhantomApp* app = (EyePhantomApp*)context;
    if(event->type != InputTypePress && event->type != InputTypeShort &&
       event->type != InputTypeLong) return;

    InputKey key = event->key;
    bool is_nav = (event->type == InputTypePress);   /* d-pad: responsive */
    bool is_act = (event->type == InputTypeShort || event->type == InputTypeLong); /* OK/Back: single fire */

    switch(app->scene) {
        case SCENE_TITLE: {
            if(key == InputKeyOk && is_act) {
                game_load(app);
                app->scene = SCENE_MENU;
                app->cursor = 0;
            }
            if(key == InputKeyBack) {  /* any type — Press fires before GUI consumes it, Short is standard */
                app->running = false;
            }
            break;
        }
        case SCENE_MENU: {
            if(is_nav) {
                if(key == InputKeyUp)   { if(app->cursor > 0) app->cursor--; }
                if(key == InputKeyDown) { if(app->cursor < 4) app->cursor++; }
            }
            if(key == InputKeyOk && is_act) {
                switch(app->cursor) {
                    case 0: scan_ir_start(app); app->scene = SCENE_SCAN; break;
                    case 1:
                        if(!app->has_active) {
                            strncpy(app->message, "NO PHANTOM YET!", 32);
                            app->message_timer = 60;
                        } else app->scene = SCENE_PHANTOM;
                        break;
                    case 2:
                        if(app->current_floor > 1) {
                            app->current_floor--;
                            game_save(app);
                            strncpy(app->message, "ASCENDED!", 32);
                            app->message_timer = 60;
                        } else {
                            strncpy(app->message, "AT SURFACE!", 32);
                            app->message_timer = 60;
                        }
                        break;
                    case 3:
                        if(!app->has_active) {
                            strncpy(app->message, "SCAN FIRST!", 32);
                            app->message_timer = 60;
                        } else {
                            combat_start(app);
                            app->scene = SCENE_COMBAT;
                        }
                        break;
                    case 4: app->scene = SCENE_CAMP; app->cursor = 0; break;
                }
            }
            if(key == InputKeyBack && is_act) {
                app->scene = SCENE_TITLE;
                app->cursor = 0;
            }
            break;
        }
        case SCENE_SCAN: {
            if(key == InputKeyBack && is_act) {
                if(app->ir_running) scan_ir_stop(app);
                app->scene = SCENE_MENU;
            }
            if(key == InputKeyOk && is_act && !app->ir_running) {
                scan_ir_start(app);
            }
            break;
        }
        case SCENE_SUMMON: {
            if(is_nav && (key == InputKeyLeft || key == InputKeyRight))
                app->cursor = app->cursor ? 0 : 1;
            if(key == InputKeyOk && is_act) {
                if(app->cursor == 0) { /* KEEP */
                    if(app->has_active) {
                        if(app->stored_count < MAX_STORED_PHANTOMS) {
                            app->stored[app->stored_count++] = app->active_phantom;
                        } else {
                            memmove(app->stored, app->stored + 1,
                                    (MAX_STORED_PHANTOMS - 1) * sizeof(Phantom));
                            app->stored[MAX_STORED_PHANTOMS - 1] = app->active_phantom;
                        }
                    }
                    app->active_phantom = app->pending_phantom;
                    app->has_active = true;
                    game_save(app);
                    strncpy(app->message, "ACQUIRED!", 32);
                    app->message_timer = 60;
                }
                app->scene = SCENE_MENU;
            }
            if(key == InputKeyBack && is_act) { app->scene = SCENE_MENU; }
            break;
        }
        case SCENE_PHANTOM: {
            if(key == InputKeyBack && is_act) app->scene = SCENE_MENU;
            break;
        }
        case SCENE_COMBAT: {
            Combat* co = &app->combat;
            if(co->state != COMBAT_PLAYER_TURN) break;

            if(is_nav) {
                if(key == InputKeyUp)    app->cursor = (app->cursor + 2) % 3;
                if(key == InputKeyRight) app->cursor = (app->cursor + 1) % 3;
                if(key == InputKeyDown)  app->cursor = (app->cursor + 1) % 3;
                if(key == InputKeyLeft)  app->cursor = (app->cursor + 2) % 3;
            }

            if(key == InputKeyOk && is_act) {
                combat_player_action(app, app->cursor);
                if(co->state == COMBAT_WIN) {
                    uint32_t shards = app->current_floor + 1;
                    app->data_shards += shards;
                    app->current_floor++;
                    game_save(app);
                    app->scene = SCENE_VICTORY;
                } else if(co->state == COMBAT_LOSE) {
                    app->has_active = false;
                    memset(&app->active_phantom, 0, sizeof(Phantom));
                    game_save(app);
                    app->scene = SCENE_DEFEAT;
                }
            }
            break;
        }
        case SCENE_VICTORY:
        case SCENE_DEFEAT: {
            if(key == InputKeyOk && is_act) {
                if(app->scene == SCENE_DEFEAT) {
                    /* Phantom already wiped in combat loss handler */
                }
                app->scene = SCENE_MENU;
            }
            break;
        }
        case SCENE_CAMP: {
            if(is_nav) {
                if(key == InputKeyUp)   { if(app->cursor > 0) app->cursor--; }
                if(key == InputKeyDown) { if(app->cursor < 2) app->cursor++; }
            }
            if(key == InputKeyOk && is_act) {
                if(app->cursor == 0) {
                    if(!app->has_active) {
                        strncpy(app->message, "NO PHANTOM!", 32);
                        app->message_timer = 60;
                    } else {
                        app->scene = SCENE_UPGRADE;
                        app->cursor = 0;
                    }
                }
                if(app->cursor == 1) {
                    app->scene = SCENE_COLLECTION;
                    app->collection_idx = 0;
                }
                if(app->cursor == 2) app->scene = SCENE_MENU;
            }
            if(key == InputKeyBack && is_act) app->scene = SCENE_MENU;
            break;
        }
        case SCENE_UPGRADE: {
            if(is_nav) {
                if(key == InputKeyUp)   { if(app->cursor > 0) app->cursor--; }
                if(key == InputKeyDown) { if(app->cursor < 4) app->cursor++; }
            }
            if(key == InputKeyOk && is_act) {
                if(app->cursor < 4) {
                    Phantom* p = &app->active_phantom;
                    uint8_t* ups[] = {&p->upgrades.hp, &p->upgrades.atk,
                                      &p->upgrades.def, &p->upgrades.spd};
                    uint32_t cost = (*ups[app->cursor] + 1) * 3;
                    if(app->data_shards >= cost) {
                        app->data_shards -= cost;
                        (*ups[app->cursor])++;
                        game_save(app);
                    } else {
                        strncpy(app->message, "NOT ENOUGH!", 32);
                        app->message_timer = 60;
                    }
                } else {
                    app->scene = SCENE_CAMP;
                }
            }
            if(key == InputKeyBack && is_act) app->scene = SCENE_CAMP;
            break;
        }
        case SCENE_COLLECTION: {
            uint8_t total = app->stored_count + (app->has_active ? 1 : 0);
            if(is_nav) {
                if(key == InputKeyUp || key == InputKeyLeft) {
                    if(app->collection_idx > 0) app->collection_idx--;
                }
                if(key == InputKeyDown || key == InputKeyRight) {
                    if(app->collection_idx + 1 < total) app->collection_idx++;
                }
            }
            if(key == InputKeyOk && is_act && total > 0) {
                /* Selecting the active phantom is a no-op, just go back */
                if(app->has_active && app->collection_idx == 0) {
                    app->scene = SCENE_CAMP;
                } else {
                    uint8_t si = app->collection_idx - (app->has_active ? 1 : 0);
                    Phantom selected = app->stored[si];
                    if(app->has_active) {
                        app->stored[si] = app->active_phantom;
                    } else {
                        memmove(&app->stored[si],
                                &app->stored[si + 1],
                                (app->stored_count - si - 1) * sizeof(Phantom));
                        app->stored_count--;
                    }
                    app->active_phantom = selected;
                    app->has_active = true;
                    game_save(app);
                    strncpy(app->message, "SET ACTIVE!", 32);
                    app->message_timer = 60;
                    app->scene = SCENE_CAMP;
                }
            }
            if(key == InputKeyBack && is_act) app->scene = SCENE_CAMP;
            break;
        }
        default:
            break;
    }
}

/* ================================================================
 * TIMER TICK (handles combat enemy turn delay)
 * ================================================================ */

static void app_tick(void* context) {
    EyePhantomApp* app = (EyePhantomApp*)context;
    app->tick++;

    /* Process IR signal captured in callback (safe to stop worker here) */
    if(app->ir_done) {
        app->ir_done = false;
        infrared_worker_rx_stop(app->ir_worker);
        app->ir_running = false;
        app->scene = SCENE_SUMMON;
        app->summon_reveal = true;
        app->reveal_timer = 0;
    }

    /* Scan timeout — auto-stop after ~30 seconds of no signal */
    if(app->ir_running && !app->ir_done) {
        app->scan_timer++;
        if(app->scan_timer > 900) {
            infrared_worker_rx_stop(app->ir_worker);
            app->ir_running = false;
            strncpy(app->message, "NO SIGNAL", 32);
            app->message_timer = 90;
            app->scene = SCENE_MENU;
        }
    }

    /* Combat enemy turn after delay */
    if(app->scene == SCENE_COMBAT && app->combat.state == COMBAT_ENEMY_TURN) {
        if(app->tick % 15 == 0) { /* ~250ms delay */
            combat_enemy_action(app);
            if(app->combat.state == COMBAT_WIN) {
                uint32_t shards = app->current_floor + 1;
                app->data_shards += shards;
                app->current_floor++;
                game_save(app);
                app->scene = SCENE_VICTORY;
            } else if(app->combat.state == COMBAT_LOSE) {
                app->has_active = false;
                memset(&app->active_phantom, 0, sizeof(Phantom));
                game_save(app);
                app->scene = SCENE_DEFEAT;
            }
        }
    }

    /* Message timer */
    if(app->message_timer > 0) {
        app->message_timer--;
        if(app->message_timer == 0) app->message[0] = '\0';
    }

    /* Summon reveal timer */
    if(app->scene == SCENE_SUMMON && app->summon_reveal) {
        app->reveal_timer++;
        if(app->reveal_timer >= 50) {
            app->summon_reveal = false;
        }
    }

    view_port_update(app->view_port);
}

/* ================================================================
 * APP ENTRY / EXIT
 * ================================================================ */

static EyePhantomApp* app_alloc(void) {
    EyePhantomApp* app = malloc(sizeof(EyePhantomApp));
    furi_check(app);
    memset(app, 0, sizeof(EyePhantomApp));

    app->gui          = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->storage       = furi_record_open(RECORD_STORAGE);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, app_draw, app);
    view_port_input_callback_set(app->view_port, app_input, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    /* Tick timer for animations and combat delay (~30fps) */
    app->tick_timer = furi_timer_alloc(app_tick, FuriTimerTypePeriodic, app);
    if(app->tick_timer) {
        furi_timer_start(app->tick_timer, 33); /* ~30fps in ms */
    }

    /* IR worker — always available on Flipper */
    app->ir_worker = infrared_worker_alloc();

    app->scene     = SCENE_TITLE;
    app->running   = true;

    return app;
}

static void app_free(EyePhantomApp* app) {
    /* Stop any running scans */
    if(app->ir_running && app->ir_worker) scan_ir_stop(app);

    if(app->tick_timer) {
        furi_timer_stop(app->tick_timer);
        furi_timer_free(app->tick_timer);
    }

    if(app->ir_worker) infrared_worker_free(app->ir_worker);

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);

    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

/* ================================================================
 * MAIN ENTRY POINT
 * ================================================================ */

int32_t eye_phantom_app(void* p) {
    UNUSED(p);
    EyePhantomApp* app = app_alloc();
    view_port_update(app->view_port);

    /* Stay alive until user presses Back on title screen */
    while(app->running) {
        furi_delay_ms(33);
    }

    app_free(app);
    return 0;
}

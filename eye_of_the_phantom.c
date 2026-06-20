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
#include <nfc/nfc.h>
#include <nfc/nfc_device.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>
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
#define MAX_HEAT              100
#define COMBAT_DELAY_TICKS    45
#define GUARD_COOLING         30
#define OVERLOAD_HEAT_PENALTY 50
#define OVERLOAD_DMG_PCT      8
#define HEAT_DANGER_THRESHOLD 70   /* 70% heat = danger flash */

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
    SCENE_INFO,
} Scene;

typedef enum {
    CLASS_BRAWLER,
    CLASS_DEFENDER,
    CLASS_GLITCH,
} PhantomClass;

typedef enum {
    PROTO_NEC,
    PROTO_SONY,
    PROTO_SAMSUNG,
    PROTO_RC5,
    PROTO_NFC
} SignalProtocol;

typedef enum {
    ACTION_STRIKE  = 0,
    ACTION_SURGE   = 1,
    ACTION_GUARD   = 2,
    ACTION_REVERSE = 3,
} CombatAction;

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
    COMBAT_DELAY,
} CombatState;

typedef struct {
    CombatUnit  player;
    CombatUnit  enemy;
    CombatState state;
    uint16_t    floor;
    char        log[COMBAT_LOG_LEN];
    uint8_t     delay_timer;
    CombatState next_state;
} Combat;

typedef struct {
    /* Flipper Zero subsystems */
    Gui*              gui;
    ViewPort*         view_port;
    NotificationApp*  notifications;
    Storage*          storage;
    FuriTimer*        tick_timer;

    /* Scan workers */
    InfraredWorker*   ir_worker;
    bool              ir_running;
    bool              ir_done;       /* signal captured, needs processing */
    Nfc*              nfc;
    NfcDevice*        nfc_device;
    NfcPoller*        nfc_poller;
    bool              nfc_running;
    bool              nfc_done;      /* NFC tag detected, needs processing */
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
    uint32_t    tick;
    Combat      combat;
    uint8_t     collection_idx;
    char        message[32];
    uint8_t     message_timer;
    bool        summon_reveal;
    uint8_t     reveal_timer;
    bool        running;
    /* Persistent combat — keep damaged enemy between phantom swaps */
    bool        has_saved_combat;
    Phantom     saved_enemy_phantom;
    int16_t     saved_enemy_hp;
    int16_t     saved_enemy_heat;
    bool        saved_enemy_overloaded;
    uint16_t    saved_combat_floor;
    bool        pending_save;
} EyePhantomApp;

/* ================================================================
 * PROTOCOL / CLASS DATA
 * ================================================================ */

static const char* CLASS_NAMES[] = {"BRAWLER","DEFENDER","GLITCH"};
static const char* PROTO_NAMES[] = {"NEC","SONY","SAMSUNG","RC5","NFC"};

static const struct { float hp, atk, def, spd; } CLASS_BIAS[] = {
    {1.2, 1.5, 0.8, 0.7},  /* BRAWLER   */
    {1.4, 0.7, 1.5, 0.8},  /* DEFENDER  */
    {0.7, 1.1, 0.6, 1.6},  /* GLITCH    */
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
    PhantomClass cls;
    switch(proto) {
        case PROTO_NEC:     cls = CLASS_BRAWLER;  break;
        case PROTO_SONY:    cls = CLASS_DEFENDER;  break;
        case PROTO_SAMSUNG: cls = CLASS_GLITCH;    break;
        case PROTO_NFC:     cls = (PhantomClass)((address + command) % 3); break;
        default:            cls = (PhantomClass)((address + command) % 3); break;
    }

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
    phantom_sprite_indices_from_address(
        (uint16_t)address, &p->head_idx, &p->body_idx, &p->feet_idx);

    phantom_build_sprite(p);

    /* Procedural name */
    uint8_t idx1 = (address + command) % 16;
    uint8_t idx2 = (command * 3 + address) % 16;
    snprintf(p->name, PHANTOM_NAME_LEN, "%s%s", SYL1[idx1], SYL2[idx2]);

    memset(&p->upgrades, 0, sizeof(p->upgrades));
}

static void phantom_get_effective(const Phantom* p, int16_t* hp, int16_t* atk,
                                   int16_t* def, int16_t* spd) {
    *hp  = p->stats.hp  + p->upgrades.hp;
    *atk = p->stats.atk + p->upgrades.atk;
    *def = p->stats.def + p->upgrades.def;
    *spd = p->stats.spd + p->upgrades.spd;
}

static void phantom_generate_enemy(Phantom* p, uint16_t floor) {
    prng_seed(floor * 2654435761u);
    uint8_t pi = prng_next() % 3;
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

/* Class advantage: BRAWLER>GLITCH>DEFENDER>BRAWLER */
static float combat_class_advantage(PhantomClass attacker, PhantomClass defender) {
    if((attacker == CLASS_BRAWLER  && defender == CLASS_GLITCH)   ||
       (attacker == CLASS_GLITCH   && defender == CLASS_DEFENDER) ||
       (attacker == CLASS_DEFENDER && defender == CLASS_BRAWLER)) {
        return 1.3f;
    }
    if((attacker == CLASS_GLITCH   && defender == CLASS_BRAWLER)  ||
       (attacker == CLASS_DEFENDER && defender == CLASS_GLITCH)   ||
       (attacker == CLASS_BRAWLER  && defender == CLASS_DEFENDER)) {
        return 0.8f;
    }
    return 1.0f;
}

static void combat_init_unit(CombatUnit* unit, Phantom* phantom) {
    unit->phantom = *phantom;
    int16_t hp, atk, def, spd;
    phantom_get_effective(phantom, &hp, &atk, &def, &spd);
    unit->hp         = hp;
    unit->max_hp     = hp;
    unit->heat       = 0;
    unit->overloaded = false;
    unit->defending  = false;
    unit->stats.hp   = hp;
    unit->stats.atk  = atk;
    unit->stats.def  = def;
    unit->stats.spd  = spd;
}

/* Shared turn-order setup: decides who goes first based on SPD */
static void combat_set_turn_order(Combat* c, uint16_t floor,
                                   const char* foe_fast_msg, const char* player_first_msg) {
    c->floor = floor;
    c->delay_timer = 0;

    if(c->enemy.stats.spd > c->player.stats.spd) {
        c->state = COMBAT_ENEMY_TURN;
        snprintf(c->log, COMBAT_LOG_LEN, foe_fast_msg, floor);
    } else {
        c->state = COMBAT_PLAYER_TURN;
        snprintf(c->log, COMBAT_LOG_LEN, player_first_msg, floor);
    }
}

static void combat_start(EyePhantomApp* app) {
    Combat* c = &app->combat;
    app->cursor = 0;

    combat_init_unit(&c->player, &app->active_phantom);

    /* Check for a saved (damaged) enemy on this floor */
    if(app->has_saved_combat && app->saved_combat_floor == app->current_floor) {
        /* Restore saved enemy with its damaged state */
        c->enemy.phantom = app->saved_enemy_phantom;
        int16_t hp, atk, def, spd;
        phantom_get_effective(&app->saved_enemy_phantom, &hp, &atk, &def, &spd);
        c->enemy.max_hp     = hp;
        c->enemy.hp         = app->saved_enemy_hp;
        c->enemy.heat       = app->saved_enemy_heat;
        c->enemy.overloaded = app->saved_enemy_overloaded;
        c->enemy.defending  = false;
        c->enemy.stats.hp   = hp;
        c->enemy.stats.atk  = atk;
        c->enemy.stats.def  = def;
        c->enemy.stats.spd  = spd;

        app->has_saved_combat = false;
        combat_set_turn_order(c, app->current_floor,
            "F%u REMATCH-FOE FAST", "F%u - REMATCH!");
        return;
    }

    /* New enemy for this floor */
    Phantom enemy;
    phantom_generate_enemy(&enemy, app->current_floor);
    combat_init_unit(&c->enemy, &enemy);

    app->has_saved_combat = false;
    combat_set_turn_order(c, app->current_floor,
        "F%u - FOE IS FASTER!", "F%u - FIGHT!");
}

static int16_t combat_calc_damage(CombatUnit* attacker, CombatUnit* defender,
                                   CombatAction action_type, bool* out_crit) {
    int32_t base;
    if(action_type == ACTION_SURGE) {
        base = (int32_t)attacker->stats.atk * 3 / 2;     /* Surge: 1.5x */
    } else if(action_type == ACTION_REVERSE) {
        base = (int32_t)attacker->stats.atk * 3 / 10;    /* Reverse: 0.3x */
        if(base < 1) base = 1;
    } else {
        base = (int32_t)attacker->stats.atk;              /* Strike: 1.0x */
    }

    /* Flat DEF reduction (DEF/3) */
    base = base - defender->stats.def / 3;
    if(base < 1) base = 1;

    /* Class advantage multiplier */
    float advantage = combat_class_advantage(
        attacker->phantom.cls, defender->phantom.cls);
    base = (int32_t)(base * advantage);
    if(base < 1) base = 1;

    /* Crit check: SPD * 2 / 100, capped at 25% */
    uint8_t crit_chance = attacker->stats.spd * 2;
    if(crit_chance > 25) crit_chance = 25;
    bool crit = (prng_next() % 100) < crit_chance;
    if(crit) base = base * 9 / 5;

    if(out_crit) *out_crit = crit;

    /* ±15% variance */
    int32_t variance = 85 + (prng_next() % 31);
    int32_t dmg = base * variance / 100;
    if(dmg < 1) dmg = 1;
    if(dmg > 30000) dmg = 30000;
    return (int16_t)dmg;
}

static void combat_unit_add_heat(CombatUnit* unit, int16_t base_heat) {
    int16_t spd_reduce = unit->stats.spd / 2;
    int16_t heat = base_heat - spd_reduce;
    if(heat < 0) heat = 0;
    unit->heat += heat;
}

/* ------------------------------------------------------------------
 * Shared helpers: overload skip, action execution, overload check,
 * death check — used by both player and enemy turn handlers.
 * ------------------------------------------------------------------ */

/* Handle "unit was overloaded last turn, skip this turn" */
static void combat_handle_overload_skip(CombatUnit* unit, char* log, size_t log_len,
                                         const char* prefix) {
    snprintf(log, log_len, "%sOVERLOADED!", prefix);
    unit->overloaded = false;
    unit->heat = unit->heat > OVERLOAD_HEAT_PENALTY
                 ? unit->heat - OVERLOAD_HEAT_PENALTY : 0;
}

/* Apply a damage-dealing action (strike/surge/reverse).
 * action is ACTION_STRIKE, ACTION_SURGE, or ACTION_REVERSE.
 * Returns the raw damage dealt (before overload deductions). */
static int16_t combat_apply_attack(CombatUnit* attacker, CombatUnit* defender,
                                    CombatAction action, bool* out_crit) {
    bool crit;
    int16_t dmg = combat_calc_damage(attacker, defender, action, &crit);
    if(defender->defending) { dmg /= 2; defender->defending = false; }
    defender->hp -= dmg;
    if(out_crit) *out_crit = crit;

    /* Self-heat per action type */
    uint8_t base_heat = (action == ACTION_STRIKE)  ? 20 :
                        (action == ACTION_SURGE)   ? 35 : 10;
    combat_unit_add_heat(attacker, base_heat);

    /* Reverse transfers extra heat to foe */
    if(action == ACTION_REVERSE) {
        defender->heat += 20;
    }
    return dmg;
}

/* Check a unit for overload (heat >= MAX_HEAT). Returns overload damage or 0. */
static int16_t combat_check_overload(CombatUnit* unit) {
    if(unit->heat >= MAX_HEAT) {
        unit->overloaded = true;
        unit->heat = MAX_HEAT;
        int16_t ov_dmg = unit->max_hp * OVERLOAD_DMG_PCT / 100;
        unit->hp -= ov_dmg;
        return ov_dmg;
    }
    return 0;
}

/* After an action, check deaths and set up delay transition.
 * `check_enemy_first` controls priority when both die simultaneously
 * (player's turn → player first; enemy's turn → enemy first, matching
 * the original "self-inflicted overload death" priority). */
static bool combat_resolve_turn(Combat* c, bool check_enemy_first,
                                 CombatState on_player_death,
                                 CombatState on_enemy_death,
                                 CombatState next_turn) {
    bool player_dead = (c->player.hp <= 0);
    bool enemy_dead  = (c->enemy.hp <= 0);

    if(check_enemy_first) {
        if(enemy_dead)       { c->enemy.hp = 0; c->next_state = on_enemy_death; }
        else if(player_dead) { c->player.hp = 0; c->next_state = on_player_death; }
        else                 { c->next_state = next_turn; }
    } else {
        if(player_dead)      { c->player.hp = 0; c->next_state = on_player_death; }
        else if(enemy_dead)  { c->enemy.hp = 0; c->next_state = on_enemy_death; }
        else                 { c->next_state = next_turn; }
    }

    if(c->next_state == next_turn) {
        c->state = next_turn;
        return false;
    }
    c->delay_timer = COMBAT_DELAY_TICKS;
    c->state = COMBAT_DELAY;
    return true;
}

/* ------------------------------------------------------------------
 * Combat action tables — damage descriptions and heat costs.
 * ------------------------------------------------------------------ */

static const char* ACTION_LOG_PLAYER[] = {
    "STRIKE:%d%s",         /* STRIKE */
    "SURGE:%d%s",          /* SURGE  */
    "GUARD! -30 HEAT",     /* GUARD  */
    "REV:%d +20H%s",       /* REVERSE (no foe overload) */
};
static const char* ACTION_LOG_ENEMY[] = {
    "FOE STR:%d%s",        /* STRIKE */
    "FOE SRG:%d%s",        /* SURGE  */
    "FOE GUARDS",          /* GUARD  */
    "FOE REV:%d +20H",     /* REVERSE (no foe overload) */
};

/* ------------------------------------------------------------------
 * PLAYER TURN
 * ------------------------------------------------------------------ */

static void combat_player_action(EyePhantomApp* app, CombatAction action) {
    Combat* c = &app->combat;
    if(c->state != COMBAT_PLAYER_TURN) return;

    c->player.defending = false;

    /* Overloaded — skip this turn */
    if(c->player.overloaded) {
        combat_handle_overload_skip(&c->player, c->log, COMBAT_LOG_LEN, "");
        goto check_deaths;
    }

    if(action == ACTION_GUARD) {
        c->player.heat = c->player.heat > GUARD_COOLING
                         ? c->player.heat - GUARD_COOLING : 0;
        c->player.defending = true;
        snprintf(c->log, COMBAT_LOG_LEN, "%s", ACTION_LOG_PLAYER[ACTION_GUARD]);
        goto check_overload;
    }

    /* Striking / Surging / Reversing */
    bool crit;
    int16_t dmg = combat_apply_attack(&c->player, &c->enemy, action, &crit);
    int16_t foe_ov = 0;

    /* Reverse: check if foe overloaded from heat transfer immediately */
    if(action == ACTION_REVERSE) {
        foe_ov = combat_check_overload(&c->enemy);
        if(foe_ov) {
            snprintf(c->log, COMBAT_LOG_LEN, "REV! FOE OVLD -%d", foe_ov);
        } else {
            snprintf(c->log, COMBAT_LOG_LEN, ACTION_LOG_PLAYER[ACTION_REVERSE],
                     dmg, crit ? " CR" : "");
        }
    } else {
        snprintf(c->log, COMBAT_LOG_LEN, ACTION_LOG_PLAYER[action], dmg,
                 crit ? " CRIT!" : "");
    }

check_overload:
    /* Player overload from self-heat */
    {
        int16_t ov = combat_check_overload(&c->player);
        if(ov) snprintf(c->log, COMBAT_LOG_LEN, "OVERLOAD! -%d", ov);
    }

check_deaths:
    /* Player's turn: check player death first (self-overload priority) */
    combat_resolve_turn(c, false, COMBAT_LOSE, COMBAT_WIN, COMBAT_ENEMY_TURN);
}

/* ------------------------------------------------------------------
 * ENEMY TURN (AI)
 * ------------------------------------------------------------------ */

static CombatAction combat_ai_pick_action(const CombatUnit* enemy) {
    uint8_t roll = prng_next() % 100;
    if(enemy->heat >= 80) {
        /* 5/0/70/25 — never Surge near overload */
        return (roll < 5) ? ACTION_STRIKE : (roll < 75) ? ACTION_GUARD : ACTION_REVERSE;
    } else if(enemy->heat >= 50) {
        /* 20/5/50/25 */
        return (roll < 20) ? ACTION_STRIKE :
               (roll < 25) ? ACTION_SURGE  :
               (roll < 75) ? ACTION_GUARD  : ACTION_REVERSE;
    } else {
        /* 50/15/10/25 */
        return (roll < 50) ? ACTION_STRIKE :
               (roll < 65) ? ACTION_SURGE  :
               (roll < 75) ? ACTION_GUARD  : ACTION_REVERSE;
    }
}

static void combat_enemy_action(EyePhantomApp* app) {
    Combat* c = &app->combat;
    if(c->state != COMBAT_ENEMY_TURN) return;

    c->enemy.defending = false;

    /* Overloaded — skip this turn */
    if(c->enemy.overloaded) {
        combat_handle_overload_skip(&c->enemy, c->log, COMBAT_LOG_LEN, "FOE ");
        goto check_deaths;
    }

    CombatAction action = combat_ai_pick_action(&c->enemy);

    if(action == ACTION_GUARD) {
        c->enemy.heat = c->enemy.heat > GUARD_COOLING
                        ? c->enemy.heat - GUARD_COOLING : 0;
        c->enemy.defending = true;
        snprintf(c->log, COMBAT_LOG_LEN, "%s", ACTION_LOG_ENEMY[ACTION_GUARD]);
        goto check_overload;
    }

    bool crit;
    int16_t dmg = combat_apply_attack(&c->enemy, &c->player, action, &crit);
    int16_t foe_ov = 0;

    /* Reverse: check if player overloaded from heat transfer immediately */
    if(action == ACTION_REVERSE) {
        foe_ov = combat_check_overload(&c->player);
        if(foe_ov) {
            snprintf(c->log, COMBAT_LOG_LEN, "FOE REV! OVLD -%d", foe_ov);
        } else {
            snprintf(c->log, COMBAT_LOG_LEN, ACTION_LOG_ENEMY[ACTION_REVERSE], dmg);
        }
    } else {
        snprintf(c->log, COMBAT_LOG_LEN, ACTION_LOG_ENEMY[action], dmg,
                 crit ? " CRIT!" : "");
    }

check_overload:
    /* Enemy overload from self-heat */
    {
        int16_t ov = combat_check_overload(&c->enemy);
        if(ov) snprintf(c->log, COMBAT_LOG_LEN, "FOE OVERLOAD! -%d", ov);
    }

check_deaths:
    /* Enemy's turn: check enemy death first (self-overload priority) */
    combat_resolve_turn(c, true, COMBAT_LOSE, COMBAT_WIN, COMBAT_PLAYER_TURN);
}

/* ================================================================
 * SAVE / LOAD
 * ================================================================ */

#define SAVE_MAGIC  0x45505450  /* "EPTP" */
#define SAVE_VER    3

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
    /* v2: persistent damaged enemy */
    bool     has_saved_combat;
    uint16_t saved_combat_floor;
    SavedPhantom saved_enemy;
    int16_t  saved_enemy_hp;
    int16_t  saved_enemy_heat;
    bool     saved_enemy_overloaded;
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
    /* v2: persistent combat */
    data.has_saved_combat      = app->has_saved_combat;
    data.saved_combat_floor    = app->saved_combat_floor;
    if(app->has_saved_combat) {
        save_save_phantom(&app->saved_enemy_phantom, &data.saved_enemy);
        data.saved_enemy_hp         = app->saved_enemy_hp;
        data.saved_enemy_heat       = app->saved_enemy_heat;
        data.saved_enemy_overloaded = app->saved_enemy_overloaded;
    }

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
           data.magic == SAVE_MAGIC && data.ver >= 2 && data.ver <= SAVE_VER) {
            app->has_active = data.has_active;
            if(data.has_active) save_load_phantom(&app->active_phantom, &data.active);
            app->stored_count = data.stored_count;
            for(uint8_t i = 0; i < data.stored_count; i++)
                save_load_phantom(&app->stored[i], &data.stored[i]);
            app->data_shards   = data.data_shards;
            app->current_floor = data.current_floor;
            /* v2: persistent combat */
            if(data.ver >= 2) {
                app->has_saved_combat      = data.has_saved_combat;
                app->saved_combat_floor    = data.saved_combat_floor;
                if(data.has_saved_combat) {
                    save_load_phantom(&app->saved_enemy_phantom, &data.saved_enemy);
                    app->saved_enemy_hp         = data.saved_enemy_hp;
                    app->saved_enemy_heat       = data.saved_enemy_heat;
                    app->saved_enemy_overloaded = data.saved_enemy_overloaded;
                }
            }
            /* v3: remap old TECHNICIAN/GLITCH to new GLITCH */
            if(data.ver < 3) {
                if(app->has_active && (uint8_t)app->active_phantom.cls >= 2)
                    app->active_phantom.cls = CLASS_GLITCH;
                for(uint8_t i = 0; i < app->stored_count; i++)
                    if((uint8_t)app->stored[i].cls >= 2)
                        app->stored[i].cls = CLASS_GLITCH;
                if(app->has_saved_combat && (uint8_t)app->saved_enemy_phantom.cls >= 2)
                    app->saved_enemy_phantom.cls = CLASS_GLITCH;
            }
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
 * NFC SCAN
 * ================================================================ */

static NfcCommand nfc_poller_callback(NfcGenericEvent event, void* context) {
    EyePhantomApp* app = (EyePhantomApp*)context;

    if(event.protocol != NfcProtocolIso14443_3a) return NfcCommandContinue;
    if(!app->nfc_running || app->nfc_done) return NfcCommandStop;

    const Iso14443_3aPollerEvent* iso3_event = event.event_data;
    if(iso3_event->type != Iso14443_3aPollerEventTypeReady)
        return NfcCommandContinue;

    /* Card activated — copy data to device and extract UID */
    nfc_device_set_data(
        app->nfc_device, NfcProtocolIso14443_3a,
        nfc_poller_get_data(app->nfc_poller));

    size_t uid_len = 0;
    const uint8_t* uid = nfc_device_get_uid(app->nfc_device, &uid_len);
    if(uid_len == 0) return NfcCommandContinue;

    /* Build address using FNV-1a hash over all UID bytes.
     * This gives a well-distributed 32-bit value where every byte
     * of the UID contributes — no truncation, strong avalanche. */
    uint32_t addr = 0x811C9DC5; /* FNV-1a offset basis */
    for(size_t i = 0; i < uid_len; i++) {
        addr ^= uid[i];
        addr *= 0x01000193;      /* FNV-1a prime */
    }

    /* Build command from reversed UID using rotate-mix for
     * orthogonal variation from the address — ensures addr and
     * cmd are meaningfully different even for short UIDs. */
    uint32_t cmd = 0;
    for(size_t i = 0; i < uid_len; i++) {
        cmd ^= (uint32_t)uid[uid_len - 1 - i] << ((i % 4) * 8);
    }
    /* Mix thoroughly so every input byte affects the output byte
     * that phantom_generate will use as seed (command & 0xFF) */
    cmd ^= cmd >> 16;
    cmd *= 0x45D9F3B;
    cmd ^= cmd >> 16;

    phantom_generate(&app->pending_phantom, PROTO_NFC, addr, cmd);
    app->nfc_done = true;
    return NfcCommandStop;
}

static void scan_nfc_start(EyePhantomApp* app) {
    if(!app->nfc) return;
    if(app->nfc_running) {
        nfc_poller_stop(app->nfc_poller);
        nfc_poller_free(app->nfc_poller);
        app->nfc_poller = NULL;
        app->nfc_running = false;
    }
    app->nfc_done = false;

    /* Create poller for ISO14443-3A (covers Mifare, DESFire, NTAG, etc.) */
    app->nfc_poller = nfc_poller_alloc(app->nfc, NfcProtocolIso14443_3a);
    nfc_poller_start(app->nfc_poller, nfc_poller_callback, app);
    app->nfc_running = true;
}

static void scan_nfc_stop(EyePhantomApp* app) {
    if(!app->nfc_running || !app->nfc_poller) return;
    nfc_poller_stop(app->nfc_poller);
    nfc_poller_free(app->nfc_poller);
    app->nfc_poller = NULL;
    app->nfc_running = false;
}

static bool scan_is_any_active(EyePhantomApp* app) {
    return app->ir_running || app->nfc_running;
}

static void scan_stop_all(EyePhantomApp* app) {
    if(app->ir_running) scan_ir_stop(app);
    if(app->nfc_running) scan_nfc_stop(app);
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

/* Draw sprite with optional invert flash (used for overload animation) */
static void draw_sprite_invertible(Canvas* c, const uint8_t* sprite,
                                    int x, int y, bool invert) {
    if(invert) canvas_invert_color(c);
    draw_sprite(c, sprite, x, y);
    if(invert) canvas_invert_color(c);
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

static void get_floor_name(uint16_t floor, char* buf, size_t buf_len) {
    static const char* NAMES[] = {
        "ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", 
        "SIX", "SEVEN", "EIGHT", "NINE", "TEN",
        "ELEVEN", "TWELVE", "THIRTEEN", "FOURTEEN", "FIFTEEN"
    };
    if(floor <= 15) {
        snprintf(buf, buf_len, "FLOOR %s", NAMES[floor]);
    } else {
        snprintf(buf, buf_len, "FLOOR %u", floor);
    }
}

static void render_menu(Canvas* c, EyePhantomApp* app) {
    canvas_clear(c);
    draw_str(c, "MENU", 2, 2);

    char buf[32];
    snprintf(buf, sizeof(buf), "XP:%lu", app->data_shards);
    draw_str(c, buf, 42, 2);

    char fl_buf[24];
    get_floor_name(app->current_floor, fl_buf, sizeof(fl_buf));
    int fl_w = strlen(fl_buf) * CHAR_W;
    draw_str(c, fl_buf, SCREEN_W - fl_w - 2, 2);

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
    int cx = 64, cy = 32;

    if(scan_is_any_active(app)) {
        draw_str_centered(c, "SCANNING", 3);

        /* Expanding radar rings — thick, pulsing outward from center */
        for(int r = 1; r < 4; r++) {
            int radius = ((t + r * 16) % 56) + 2;
            if(radius >= 28) continue;
            /* Thick ring: draw three adjacent circles */
            canvas_draw_circle(c, cx, cy, radius);
            canvas_draw_circle(c, cx, cy, radius + 1);
            canvas_draw_circle(c, cx, cy, radius - 1);
        }

        /* Center dot */
        canvas_draw_disc(c, cx, cy, 2);
    } else {
        draw_str_centered(c, "SCAN", 4);
        canvas_draw_line(c, 0, 12, 127, 12);

        /* Simple radiating icon at center */
        for(int i = 0; i < 8; i++) {
            float angle = i * 3.14159f / 4.0f;
            int ex = cx + (int)(cosf(angle) * 12);
            int ey = cy + (int)(sinf(angle) * 12);
            int bx = cx + (int)(cosf(angle) * 3);
            int by = cy + (int)(sinf(angle) * 3);
            if(abs(ex - bx) > abs(ey - by)) {
                int x1 = (bx < ex) ? bx : ex;
                int x2 = (bx > ex) ? bx : ex;
                int y0 = (by + ey) / 2;
                canvas_draw_box(c, x1, y0 - 1, x2 - x1 + 1, 3);
            } else {
                int y1 = (by < ey) ? by : ey;
                int y2 = (by > ey) ? by : ey;
                int x0 = (bx + ex) / 2;
                canvas_draw_box(c, x0 - 1, y1, 3, y2 - y1 + 1);
            }
        }
        canvas_draw_disc(c, cx, cy, 2);

        draw_str_centered(c, "POINT REMOTE OR TAP CARD", 56);
    }
}

static void render_summon(Canvas* c, EyePhantomApp* app) {
    canvas_clear(c);
    Phantom* p = &app->pending_phantom;

    if(app->summon_reveal) {
        if(app->reveal_timer < 80) {
            /* Phantom sprite at 3× scale — fills the centre */
            #define SCALE 3
            int spr_x = (SCREEN_W - PHANTOM_SPRITE_W * SCALE) / 2;
            int spr_y = (SCREEN_H - PHANTOM_SPRITE_H * SCALE) / 2 - 4;
            for(int row = 0; row < PHANTOM_SPRITE_H; row++) {
                for(int col = 0; col < PHANTOM_SPRITE_W; col++) {
                    if(p->sprite[row * 2 + col / 8] & (1 << (col % 8)))
                        canvas_draw_box(c, spr_x + col * SCALE, spr_y + row * SCALE,
                                        SCALE, SCALE);
                }
            }
            #undef SCALE

            /* "PHANTOM FOUND!" banner at bottom */
            int msg_w = 14 * CHAR_W;
            int msg_x = (SCREEN_W - msg_w) / 2;
            canvas_draw_box(c, msg_x - 4, 54, msg_w + 8, 9);
            canvas_invert_color(c);
            draw_str_centered(c, "PHANTOM FOUND!", 55);
            canvas_invert_color(c);
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
    const char* origin = (p->protocol == PROTO_NFC) ? "NFC:" : "IR:";
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

    /* Name and class banner at top */
    draw_str(c, p->name, 2, 2);
    canvas_draw_line(c, 0, 9, 127, 9);

    /* Large phantom sprite on the left (3× scale) */
    #define SCALE 3
    int spr_x = 2;
    int spr_y = 14;
    for(int row = 0; row < PHANTOM_SPRITE_H; row++) {
        for(int col = 0; col < PHANTOM_SPRITE_W; col++) {
            if(p->sprite[row * 2 + col / 8] & (1 << (col % 8)))
                canvas_draw_box(c, spr_x + col * SCALE, spr_y + row * SCALE,
                                SCALE, SCALE);
        }
    }
    #undef SCALE

    /* Stats on the right */
    int16_t hp, atk, def, spd;
    phantom_get_effective(p, &hp, &atk, &def, &spd);

    int sx = 56;
    char buf[32];
    snprintf(buf, sizeof(buf), "%s", CLASS_NAMES[p->cls]);
    draw_str(c, buf, sx, 14);

    if(p->upgrades.hp > 0) {
        snprintf(buf, sizeof(buf), "HP  %d (+%d)", hp, p->upgrades.hp);
    } else {
        snprintf(buf, sizeof(buf), "HP  %d", hp);
    }
    draw_str(c, buf, sx, 23);

    if(p->upgrades.atk > 0) {
        snprintf(buf, sizeof(buf), "ATK %d (+%d)", atk, p->upgrades.atk);
    } else {
        snprintf(buf, sizeof(buf), "ATK %d", atk);
    }
    draw_str(c, buf, sx, 31);

    if(p->upgrades.def > 0) {
        snprintf(buf, sizeof(buf), "DEF %d (+%d)", def, p->upgrades.def);
    } else {
        snprintf(buf, sizeof(buf), "DEF %d", def);
    }
    draw_str(c, buf, sx, 39);

    if(p->upgrades.spd > 0) {
        snprintf(buf, sizeof(buf), "SPD %d (+%d)", spd, p->upgrades.spd);
    } else {
        snprintf(buf, sizeof(buf), "SPD %d", spd);
    }
    draw_str(c, buf, sx, 47);

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
    draw_hbar(c, 8, 8, 54, 4, (float)co->player.heat / MAX_HEAT);
    draw_str(c, "H", 68, 8);
    draw_hbar(c, 76, 8, 50, 4, (float)co->enemy.heat / MAX_HEAT);

    /* Heat danger flash */
    if(co->player.heat >= HEAT_DANGER_THRESHOLD && (t % 16 < 8))
        canvas_draw_frame(c, 6, 7, 58, 6);
    if(co->enemy.heat >= HEAT_DANGER_THRESHOLD && (t % 16 < 8))
        canvas_draw_frame(c, 74, 7, 54, 6);

    canvas_draw_line(c, 0, 13, 127, 13);

    /* --- MIDDLE: sprites (rows 14-41) --- */
    int sy = 18;
    bool p_flash = co->player.overloaded && (t % 6 < 3);
    bool e_flash = co->enemy.overloaded && (t % 6 < 3);

    draw_sprite_invertible(c, co->player.phantom.sprite, 8, sy, p_flash);
    draw_sprite_invertible(c, co->enemy.phantom.sprite, 104, sy, e_flash);

    /* HP readouts below sprites */
    char buf[20];
    snprintf(buf, sizeof(buf), "%d/%d", co->player.hp, co->player.max_hp);
    draw_str(c, buf, 4, 36);
    snprintf(buf, sizeof(buf), "%d/%d", co->enemy.hp, co->enemy.max_hp);
    draw_str(c, buf, 96, 36);

    /* Class advantage indicator between sprites */
    float adv = combat_class_advantage(
        co->player.phantom.cls, co->enemy.phantom.cls);
    if(adv > 1.0f) {
        draw_str(c, ">>", 58, 22);
    } else if(adv < 1.0f) {
        draw_str(c, "<<", 58, 22);
    } else {
        draw_str(c, "==", 58, 22);
    }

    /* Class abbreviations */
    static const char* CLS_SHORT[] = {"BRL", "DEF", "GLI"};
    draw_str(c, CLS_SHORT[co->player.phantom.cls], 30, 26);
    draw_str(c, CLS_SHORT[co->enemy.phantom.cls], 80, 26);

    /* --- Combat log (rows 42-47) --- */
    canvas_draw_line(c, 0, 42, 127, 42);
    draw_str(c, co->log, 2, 44);

    /* --- BOTTOM: actions (rows 49-63) --- */
    canvas_draw_line(c, 0, 49, 127, 49);

    if(co->state == COMBAT_PLAYER_TURN || 
       (co->state == COMBAT_DELAY && co->next_state == COMBAT_ENEMY_TURN)) {
        /* 2x2 grid: STRIKE  SURGE / GUARD  REV */
        const char* acts[] = {"STRIKE", "SURGE", "GUARD", "REV"};
        int px[] = {12, 68, 12, 68};
        int py[] = {51, 51, 58, 58};

        for(int i = 0; i < 4; i++) {
            if(co->state == COMBAT_PLAYER_TURN && i == app->cursor) {
                int w = (int)strlen(acts[i]) * CHAR_W + 4;
                canvas_draw_box(c, px[i], py[i] - 1, w, 7);
                canvas_invert_color(c);
                draw_str(c, acts[i], px[i] + 2, py[i]);
                canvas_invert_color(c);
            } else {
                draw_str(c, acts[i], px[i] + 2, py[i]);
            }
        }
    } else if(co->state == COMBAT_ENEMY_TURN || 
              (co->state == COMBAT_DELAY && co->next_state == COMBAT_PLAYER_TURN)) {
        draw_str_centered(c, "ENEMY TURN...", 55);
    } else if(co->state == COMBAT_WIN || 
              (co->state == COMBAT_DELAY && co->next_state == COMBAT_WIN)) {
        draw_str_centered(c, "VICTORY!", 55);
    } else if(co->state == COMBAT_LOSE || 
              (co->state == COMBAT_DELAY && co->next_state == COMBAT_LOSE)) {
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

    char fl_name[24];
    get_floor_name(app->current_floor - 1, fl_name, sizeof(fl_name));
    char buf[64];
    snprintf(buf, sizeof(buf), "%s CLEARED!", fl_name);
    draw_str_centered(c, buf, 36);

    uint32_t shards = app->current_floor * 3; /* floor already incremented */
    snprintf(buf, sizeof(buf), "+%lu XP", shards);
    draw_str_centered(c, buf, 44);
    snprintf(buf, sizeof(buf), "TOTAL XP: %lu", app->data_shards);
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
                    if(prng_next() % 10 > 3)
                        canvas_draw_dot(c, 56 + x, 22 + y);
    }

    draw_str_centered(c, "YOUR PHANTOM", 42);
    draw_str_centered(c, "WAS KILLED", 50);

    if((t / 20) % 2 == 0)
        draw_str_centered(c, "PRESS OK", 58);
}

static void render_camp(Canvas* c, EyePhantomApp* app) {
    canvas_clear(c);
    const char* items[] = {"UPGRADE STATS", "COLLECTION", "COMBAT INFO", "BACK"};

    draw_str(c, "CAMP", 2, 2);
    char buf[32];
    snprintf(buf, sizeof(buf), "XP:%lu", app->data_shards);
    draw_str(c, buf, 80, 2);
    canvas_draw_line(c, 0, 9, 127, 9);

    for(int i = 0; i < 4; i++) {
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
    snprintf(buf, sizeof(buf), "XP:%lu", app->data_shards);
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
        snprintf(buf, sizeof(buf), "%s %d +%d (%lu XP)",
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

static void render_info(Canvas* c, EyePhantomApp* app) {
    canvas_clear(c);
    canvas_draw_line(c, 0, 9, 127, 9);
    
    char buf[32];
    char title_buf[32];
    snprintf(title_buf, sizeof(title_buf), "< %d/3 >", (int)app->cursor + 1);
    draw_str(c, "COMBAT INFO", 2, 2);
    draw_str(c, title_buf, 90, 2);

    if(app->cursor == 0) {
        /* Page 1: Triangle diagram */
        draw_str_centered(c, "BRAWLER", 14);
        draw_str(c, "GLITCH", 10, 38);
        draw_str(c, "DEFENDER", 78, 38);

        /* Arrows */
        /* BRAWLER -> GLITCH (down-left) */
        canvas_draw_line(c, 54, 21, 30, 35);
        canvas_draw_line(c, 30, 35, 30, 32);
        canvas_draw_line(c, 30, 35, 34, 35);

        /* GLITCH -> DEFENDER (right) */
        canvas_draw_line(c, 42, 40, 74, 40);
        canvas_draw_line(c, 74, 40, 71, 38);
        canvas_draw_line(c, 74, 40, 71, 42);

        /* DEFENDER -> BRAWLER (up-left) */
        canvas_draw_line(c, 92, 35, 68, 21);
        canvas_draw_line(c, 68, 21, 68, 24);
        canvas_draw_line(c, 68, 21, 72, 21);

        draw_str_centered(c, "ADVANTAGE: DEALS 1.3X DMG", 48);
        draw_str_centered(c, "DISADVANTAGE: 0.8X DMG", 56);
    } else if(app->cursor == 1) {
        /* Page 2: Actions */
        draw_str(c, "UP:STRIKE", 2, 13);
        draw_str(c, "1.0X, +20H", 48, 13);

        draw_str(c, "RT:SURGE", 2, 23);
        draw_str(c, "1.5X, +35H", 48, 23);

        draw_str(c, "DN:GUARD", 2, 33);
        draw_str(c, "BLOCK, -30H", 48, 33);

        draw_str(c, "LT:REV", 2, 43);
        draw_str(c, "0.3X, +10H/+20F", 48, 43);

        snprintf(buf, sizeof(buf), "%d HEAT = OVERLOAD (%d%%HP)",
                 MAX_HEAT, OVERLOAD_DMG_PCT);
        draw_str_centered(c, buf, 55);
    } else {
        /* Page 3: Stats */
        draw_str(c, "HP : HEALTH POINTS", 2, 13);
        draw_str(c, "ATK: BASE DMG MULTIPLIER", 2, 23);
        draw_str(c, "DEF: REDUCES DAMAGE TAKEN", 2, 33);
        draw_str(c, "SPD: ACTS FIRST & CRIT %", 2, 43);

        draw_str_centered(c, "COOLING: SPD/2 PER ACTION", 55);
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
        case SCENE_INFO:       render_info(c, app); break;
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
                    case 0: scan_ir_start(app); scan_nfc_start(app); app->scan_timer = 0; app->scene = SCENE_SCAN; break;
                    case 1:
                        if(!app->has_active) {
                            strncpy(app->message, "NO PHANTOM YET!", 32);
                            app->message_timer = 60;
                        } else app->scene = SCENE_PHANTOM;
                        break;
                    case 2:
                        if(app->current_floor > 1) {
                            app->current_floor--;
                            app->pending_save = true;
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
                scan_stop_all(app);
                app->scene = SCENE_MENU;
            }
            if(key == InputKeyOk && is_act && !scan_is_any_active(app)) {
                scan_ir_start(app);
                scan_nfc_start(app);
                app->scan_timer = 0;
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
                    app->pending_save = true;
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
                if(key == InputKeyUp) {
                    if(app->cursor >= 2) app->cursor -= 2;
                }
                if(key == InputKeyDown) {
                    if(app->cursor <= 1) app->cursor += 2;
                }
                if(key == InputKeyLeft) {
                    if(app->cursor == 1 || app->cursor == 3) app->cursor -= 1;
                }
                if(key == InputKeyRight) {
                    if(app->cursor == 0 || app->cursor == 2) app->cursor += 1;
                }
            }

            if(key == InputKeyOk && is_act) {
                combat_player_action(app, app->cursor);
            }
            if(key == InputKeyBack && is_act) {
                app->saved_enemy_phantom   = co->enemy.phantom;
                app->saved_enemy_hp        = co->enemy.hp;
                app->saved_enemy_heat      = co->enemy.heat;
                app->saved_enemy_overloaded = co->enemy.overloaded;
                app->saved_combat_floor    = app->current_floor;
                app->has_saved_combat      = true;
                app->pending_save = true;

                app->scene = SCENE_MENU;
                app->cursor = 0;
                strncpy(app->message, "RETREATED!", 32);
                app->message_timer = 40;
            }
            break;
        }
        case SCENE_VICTORY:
        case SCENE_DEFEAT: {
            if(key == InputKeyOk && is_act) {
                app->scene = SCENE_MENU;
            }
            break;
        }
        case SCENE_CAMP: {
            if(is_nav) {
                if(key == InputKeyUp)   { if(app->cursor > 0) app->cursor--; }
                if(key == InputKeyDown) { if(app->cursor < 3) app->cursor++; }
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
                if(app->cursor == 2) {
                    app->scene = SCENE_INFO;
                    app->cursor = 0;
                }
                if(app->cursor == 3) app->scene = SCENE_MENU;
            }
            if(key == InputKeyBack && is_act) app->scene = SCENE_MENU;
            break;
        }
        case SCENE_INFO: {
            if(is_nav) {
                if(key == InputKeyLeft) {
                    if(app->cursor > 0) app->cursor--;
                }
                if(key == InputKeyRight) {
                    if(app->cursor < 2) app->cursor++;
                }
            }
            if(key == InputKeyBack && is_act) {
                app->scene = SCENE_CAMP;
                app->cursor = 2;
            }
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
                        app->pending_save = true;
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
                    app->pending_save = true;
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
        scan_ir_stop(app);
        /* Stop NFC too — one phantom at a time */
        if(app->nfc_running) scan_nfc_stop(app);
        app->scene = SCENE_SUMMON;
        app->summon_reveal = true;
        app->reveal_timer = 0;
    }

    /* Process NFC tag detected in callback */
    if(app->nfc_done && !app->ir_done) {
        app->nfc_done = false;
        scan_nfc_stop(app);
        /* Stop IR too — one phantom at a time */
        if(app->ir_running) scan_ir_stop(app);
        app->scene = SCENE_SUMMON;
        app->summon_reveal = true;
        app->reveal_timer = 0;
    }

    /* Scan timeout — auto-stop after ~30 seconds of no signal */
    if(scan_is_any_active(app) && !app->ir_done && !app->nfc_done) {
        app->scan_timer++;
        if(app->scan_timer > 900) {
            scan_stop_all(app);
            strncpy(app->message, "NO SIGNAL", 32);
            app->message_timer = 90;
            app->scene = SCENE_MENU;
        }
    }

    /* Combat delay timer processing */
    if(app->scene == SCENE_COMBAT && app->combat.state == COMBAT_DELAY) {
        if(app->combat.delay_timer > 0) {
            app->combat.delay_timer--;
            if(app->combat.delay_timer == 0) {
                if(app->combat.next_state == COMBAT_WIN) {
                    uint32_t shards = (app->current_floor + 1) * 3;
                    app->data_shards += shards;
                    app->current_floor++;
                    app->has_saved_combat = false;
                    app->pending_save = true;
                    app->scene = SCENE_VICTORY;
                } else if(app->combat.next_state == COMBAT_LOSE) {
                    /* Save enemy state so player can swap phantom and rematch */
                    app->saved_enemy_phantom   = app->combat.enemy.phantom;
                    app->saved_enemy_hp        = app->combat.enemy.hp;
                    app->saved_enemy_heat      = app->combat.enemy.heat;
                    app->saved_enemy_overloaded = app->combat.enemy.overloaded;
                    app->saved_combat_floor    = app->current_floor;
                    app->has_saved_combat      = true;
                    app->has_active = false;
                    memset(&app->active_phantom, 0, sizeof(Phantom));
                    app->pending_save = true;
                    app->scene = SCENE_DEFEAT;
                } else {
                    app->combat.state = app->combat.next_state;
                }
            }
        }
    }

    /* Combat enemy turn */
    if(app->scene == SCENE_COMBAT && app->combat.state == COMBAT_ENEMY_TURN) {
        combat_enemy_action(app);
    }

    /* Message timer */
    if(app->message_timer > 0) {
        app->message_timer--;
        if(app->message_timer == 0) app->message[0] = '\0';
    }

    /* Summon reveal timer */
    if(app->scene == SCENE_SUMMON && app->summon_reveal) {
        app->reveal_timer++;
        if(app->reveal_timer >= 80) {
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

    /* NFC — for RFID/NFC card detection */
    app->nfc = nfc_alloc();
    app->nfc_device = nfc_device_alloc();
    app->nfc_poller = NULL;

    app->scene     = SCENE_TITLE;
    app->running   = true;

    return app;
}

static void app_free(EyePhantomApp* app) {
    /* Stop any running scans */
    scan_stop_all(app);

    if(app->tick_timer) {
        furi_timer_stop(app->tick_timer);
        furi_timer_free(app->tick_timer);
    }

    if(app->ir_worker) infrared_worker_free(app->ir_worker);
    if(app->nfc_device) nfc_device_free(app->nfc_device);
    if(app->nfc) nfc_free(app->nfc);

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
        if(app->pending_save) {
            app->pending_save = false;
            game_save(app);
        }
        furi_delay_ms(33);
    }

    app_free(app);
    return 0;
}

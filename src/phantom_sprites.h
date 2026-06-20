/**
 * phantom_sprites.h — Eye of the Phantom
 * Modular 16x16 Sprite System for Flipper Zero
 *
 * Each creature is built from 3 parts: Head + Body + Feet.
 * Parts are stored as full 16x16 XBM bitmaps (32 bytes each)
 * with only their active rows populated — the rest are 0x00.
 *
 * Compositing is trivial: OR all 32 bytes together.
 *
 *   uint8_t sprite[32];
 *   phantom_sprite_composite(sprite, head_idx, body_idx, feet_idx);
 *   canvas_draw_xbm(canvas, x, y, 16, 16, sprite);
 *
 * Layout within a 16x16 grid:
 *   Rows  0–5  : Head   (6 rows)
 *   Rows  4–10 : Body   (7 rows, overlaps head at 4-5)
 *   Rows  9–15 : Feet   (7 rows, overlaps body at 9-10)
 *
 * XBM byte order: row-major, 2 bytes per row, LSB = leftmost pixel.
 *
 * 8 heads × 8 bodies × 8 feet = 512 unique creatures
 * Total ROM: 24 parts × 32 bytes = 768 bytes
 */

#ifndef PHANTOM_SPRITES_H
#define PHANTOM_SPRITES_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * DIMENSIONS
 * ================================================================ */
#define PHANTOM_SPRITE_W      16
#define PHANTOM_SPRITE_H      16
#define PHANTOM_SPRITE_BYTES  32  /* 16 rows × 2 bytes/row */

#define PHANTOM_HEAD_COUNT    8
#define PHANTOM_BODY_COUNT    8
#define PHANTOM_FEET_COUNT    8

/* ================================================================
 * HEAD PARTS (active rows 0–5)
 *
 * 0 = Skull      Round dome with hollow eye sockets
 * 1 = Cyclops    Single oversized eye
 * 2 = Static     Noisy/glitched TV rectangle
 * 3 = Crown      Spiked horns, regal
 * 4 = Foxy       Pointed fox ears, big vertical eyes
 * 5 = Axo        Axolotl gills, cute smile
 * 6 = Ghosty     Wizard cowl/hood, mischievous eyes
 * 7 = Sprout     Leafy sprout budding from bulb head
 * ================================================================ */

/*  ---- Head 0: Skull ----
 *  ................
 *  ....########....
 *  ...#........#...
 *  ..#..##..##..#..
 *  ..#..##..##..#..
 *  ...#...##...#...
 */
static const uint8_t phantom_head_skull[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0xF0,0x0F, /* row  1 */
    0x08,0x10, /* row  2 */
    0x64,0x26, /* row  3 */
    0x64,0x26, /* row  4 */
    0x88,0x11, /* row  5 */
    0x00,0x00, /* row  6 */
    0x00,0x00, /* row  7 */
    0x00,0x00, /* row  8 */
    0x00,0x00, /* row  9 */
    0x00,0x00, /* row 10 */
    0x00,0x00, /* row 11 */
    0x00,0x00, /* row 12 */
    0x00,0x00, /* row 13 */
    0x00,0x00, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Head 1: Cyclops ----
 *  ................
 *  .....######.....
 *  ...##......##...
 *  ..#...####...#..
 *  ..#..######..#..
 *  ...##..##..##...
 */
static const uint8_t phantom_head_cyclops[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0xE0,0x07, /* row  1 */
    0x18,0x18, /* row  2 */
    0xC4,0x23, /* row  3 */
    0xE4,0x27, /* row  4 */
    0x98,0x19, /* row  5 */
    0x00,0x00, /* row  6 */
    0x00,0x00, /* row  7 */
    0x00,0x00, /* row  8 */
    0x00,0x00, /* row  9 */
    0x00,0x00, /* row 10 */
    0x00,0x00, /* row 11 */
    0x00,0x00, /* row 12 */
    0x00,0x00, /* row 13 */
    0x00,0x00, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Head 2: Static ----
 *  ................
 *  ..############..
 *  ..#.#.##.#.#.#..
 *  ..##.#..#.##.#..
 *  ..#.##.##.#..#..
 *  ..############..
 */
static const uint8_t phantom_head_static[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0xFC,0x3F, /* row  1 */
    0xD4,0x2A, /* row  2 */
    0x2C,0x2D, /* row  3 */
    0xB4,0x25, /* row  4 */
    0xFC,0x3F, /* row  5 */
    0x00,0x00, /* row  6 */
    0x00,0x00, /* row  7 */
    0x00,0x00, /* row  8 */
    0x00,0x00, /* row  9 */
    0x00,0x00, /* row 10 */
    0x00,0x00, /* row 11 */
    0x00,0x00, /* row 12 */
    0x00,0x00, /* row 13 */
    0x00,0x00, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Head 3: Crown ----
 *  .#...######...#.
 *  .##.#......#.##.
 *  ..###......###..
 *  ...#..#..#..#...
 *  ...#..####..#...
 *  ...##########...
 */
static const uint8_t phantom_head_crown[PHANTOM_SPRITE_BYTES] = {
    0xE2,0x47, /* row  0 */
    0x16,0x68, /* row  1 */
    0x1C,0x38, /* row  2 */
    0x48,0x12, /* row  3 */
    0xC8,0x13, /* row  4 */
    0xF8,0x1F, /* row  5 */
    0x00,0x00, /* row  6 */
    0x00,0x00, /* row  7 */
    0x00,0x00, /* row  8 */
    0x00,0x00, /* row  9 */
    0x00,0x00, /* row 10 */
    0x00,0x00, /* row 11 */
    0x00,0x00, /* row 12 */
    0x00,0x00, /* row 13 */
    0x00,0x00, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Head 4: Foxy ----
 *  .##.........##..
 *  .###.......###..
 *  .####.....####..
 *  #...##...##...#.
 *  #..###...###..#.
 *  .#...######...#.
 */
static const uint8_t phantom_head_foxy[PHANTOM_SPRITE_BYTES] = {
    0x06,0x30, /* row  0 */
    0x0E,0x38, /* row  1 */
    0x1E,0x3C, /* row  2 */
    0x31,0x46, /* row  3 */
    0x39,0x4E, /* row  4 */
    0xE2,0x47, /* row  5 */
    0x00,0x00, /* row  6 */
    0x00,0x00, /* row  7 */
    0x00,0x00, /* row  8 */
    0x00,0x00, /* row  9 */
    0x00,0x00, /* row 10 */
    0x00,0x00, /* row 11 */
    0x00,0x00, /* row 12 */
    0x00,0x00, /* row 13 */
    0x00,0x00, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Head 5: Axo ----
 *  ...##.....##....
 *  ..####...####...
 *  .#############..
 *  #..##.....##...#
 *  #..##..#..##...#
 *  .#############..
 */
static const uint8_t phantom_head_axo[PHANTOM_SPRITE_BYTES] = {
    0x18,0x0C, /* row  0 */
    0x3C,0x1E, /* row  1 */
    0xFE,0x3F, /* row  2 */
    0x19,0x8C, /* row  3 */
    0x99,0x8C, /* row  4 */
    0xFE,0x3F, /* row  5 */
    0x00,0x00, /* row  6 */
    0x00,0x00, /* row  7 */
    0x00,0x00, /* row  8 */
    0x00,0x00, /* row  9 */
    0x00,0x00, /* row 10 */
    0x00,0x00, /* row 11 */
    0x00,0x00, /* row 12 */
    0x00,0x00, /* row 13 */
    0x00,0x00, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Head 6: Ghosty ----
 *  .......##.......
 *  ......####......
 *  .....######.....
 *  ....#.#..#.#....
 *  ....#.####.#....
 *  ....########....
 */
static const uint8_t phantom_head_ghosty[PHANTOM_SPRITE_BYTES] = {
    0x80,0x01, /* row  0 */
    0xC0,0x03, /* row  1 */
    0xE0,0x07, /* row  2 */
    0x50,0x0A, /* row  3 */
    0xD0,0x0B, /* row  4 */
    0xF0,0x0F, /* row  5 */
    0x00,0x00, /* row  6 */
    0x00,0x00, /* row  7 */
    0x00,0x00, /* row  8 */
    0x00,0x00, /* row  9 */
    0x00,0x00, /* row 10 */
    0x00,0x00, /* row 11 */
    0x00,0x00, /* row 12 */
    0x00,0x00, /* row 13 */
    0x00,0x00, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Head 7: Sprout ----
 *  ....#......#....
 *  .....#....#.....
 *  ......####......
 *  ..############..
 *  ..#..##..##..#..
 *  ..############..
 */
static const uint8_t phantom_head_sprout[PHANTOM_SPRITE_BYTES] = {
    0x10,0x08, /* row  0 */
    0x20,0x04, /* row  1 */
    0xC0,0x03, /* row  2 */
    0xFC,0x3F, /* row  3 */
    0x64,0x26, /* row  4 */
    0xFC,0x3F, /* row  5 */
    0x00,0x00, /* row  6 */
    0x00,0x00, /* row  7 */
    0x00,0x00, /* row  8 */
    0x00,0x00, /* row  9 */
    0x00,0x00, /* row 10 */
    0x00,0x00, /* row 11 */
    0x00,0x00, /* row 12 */
    0x00,0x00, /* row 13 */
    0x00,0x00, /* row 14 */
    0x00,0x00, /* row 15 */
};

/* ================================================================
 * BODY PARTS (active rows 4–10)
 *
 * 0 = Slime      Rounded amorphous blob
 * 1 = Mech       Boxy mechanical chassis
 * 2 = Beast      Broad organic torso with shoulders
 * 3 = Crystal    Geometric diamond facets
 * 4 = Chubby     Fat round belly with tiny paws
 * 5 = Winged     Slender body with flared wings
 * 6 = Fluffy     Cloud mane/collar, fluffy ruff
 * 7 = Shell      Segmented turtle carapace
 * ================================================================ */

/*  ---- Body 0: Slime ----
 *                          (row 4)  ................
 *                          (row 5)  ....########....
 *                          (row 6)  ...##########...
 *                          (row 7)  ..############..
 *                          (row 8)  ...##########...
 *                          (row 9)  ....########....
 *                          (row 10) ...##......##...
 */
static const uint8_t phantom_body_slime[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0x00,0x00, /* row  1 */
    0x00,0x00, /* row  2 */
    0x00,0x00, /* row  3 */
    0x00,0x00, /* row  4 */
    0xF0,0x0F, /* row  5 */
    0xF8,0x1F, /* row  6 */
    0xFC,0x3F, /* row  7 */
    0xF8,0x1F, /* row  8 */
    0xF0,0x0F, /* row  9 */
    0x18,0x18, /* row 10 */
    0x00,0x00, /* row 11 */
    0x00,0x00, /* row 12 */
    0x00,0x00, /* row 13 */
    0x00,0x00, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Body 1: Mech ----
 *                          (row 4)  ................
 *                          (row 5)  ...##########...
 *                          (row 6)  ...#..####..#...
 *                          (row 7)  ...#.#....#.#...
 *                          (row 8)  ...#..####..#...
 *                          (row 9)  ...##########...
 *                          (row 10) ....#......#....
 */
static const uint8_t phantom_body_mech[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0x00,0x00, /* row  1 */
    0x00,0x00, /* row  2 */
    0x00,0x00, /* row  3 */
    0x00,0x00, /* row  4 */
    0xF8,0x1F, /* row  5 */
    0xC8,0x13, /* row  6 */
    0x28,0x14, /* row  7 */
    0xC8,0x13, /* row  8 */
    0xF8,0x1F, /* row  9 */
    0x10,0x08, /* row 10 */
    0x00,0x00, /* row 11 */
    0x00,0x00, /* row 12 */
    0x00,0x00, /* row 13 */
    0x00,0x00, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Body 2: Beast ----
 *                          (row 4)  ................
 *                          (row 5)  ..#..######..#..
 *                          (row 6)  ..##.######.##..
 *                          (row 7)  ...##########...
 *                          (row 8)  ...##########...
 *                          (row 9)  ....########....
 *                          (row 10) ....#......#....
 */
static const uint8_t phantom_body_beast[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0x00,0x00, /* row  1 */
    0x00,0x00, /* row  2 */
    0x00,0x00, /* row  3 */
    0x00,0x00, /* row  4 */
    0xE4,0x27, /* row  5 */
    0xEC,0x37, /* row  6 */
    0xF8,0x1F, /* row  7 */
    0xF8,0x1F, /* row  8 */
    0xF0,0x0F, /* row  9 */
    0x10,0x08, /* row 10 */
    0x00,0x00, /* row 11 */
    0x00,0x00, /* row 12 */
    0x00,0x00, /* row 13 */
    0x00,0x00, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Body 3: Crystal ----
 *                          (row 4)  ................
 *                          (row 5)  ......####......
 *                          (row 6)  .....##..##.....
 *                          (row 7)  ....##....##....
 *                          (row 8)  .....##..##.....
 *                          (row 9)  ......####......
 *                          (row 10) .....#....#.....
 */
static const uint8_t phantom_body_crystal[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0x00,0x00, /* row  1 */
    0x00,0x00, /* row  2 */
    0x00,0x00, /* row  3 */
    0x00,0x00, /* row  4 */
    0xC0,0x03, /* row  5 */
    0x60,0x06, /* row  6 */
    0x30,0x0C, /* row  7 */
    0x60,0x06, /* row  8 */
    0xC0,0x03, /* row  9 */
    0x20,0x04, /* row 10 */
    0x00,0x00, /* row 11 */
    0x00,0x00, /* row 12 */
    0x00,0x00, /* row 13 */
    0x00,0x00, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Body 4: Chubby ----
 *                          (row 4)  ................
 *                          (row 5)  ....########....
 *                          (row 6)  ..############..
 *                          (row 7)  .#.##########.#.
 *                          (row 8)  ..############..
 *                          (row 9)  ....########....
 *                          (row 10) ...##......##...
 */
static const uint8_t phantom_body_chubby[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0x00,0x00, /* row  1 */
    0x00,0x00, /* row  2 */
    0x00,0x00, /* row  3 */
    0x00,0x00, /* row  4 */
    0xF0,0x0F, /* row  5 */
    0xFC,0x3F, /* row  6 */
    0xFA,0x5F, /* row  7 */
    0xFC,0x3F, /* row  8 */
    0xF0,0x0F, /* row  9 */
    0x18,0x18, /* row 10 */
    0x00,0x00, /* row 11 */
    0x00,0x00, /* row 12 */
    0x00,0x00, /* row 13 */
    0x00,0x00, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Body 5: Winged ----
 *                          (row 4)  ................
 *                          (row 5)  .##..######..##.
 *                          (row 6)  .###.######.###.
 *                          (row 7)  ..##.######.##..
 *                          (row 8)  ....######......
 *                          (row 9)  .....######.....
 *                          (row 10) ....#......#....
 */
static const uint8_t phantom_body_winged[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0x00,0x00, /* row  1 */
    0x00,0x00, /* row  2 */
    0x00,0x00, /* row  3 */
    0x00,0x00, /* row  4 */
    0xE6,0x67, /* row  5 */
    0xEE,0x77, /* row  6 */
    0xEC,0x37, /* row  7 */
    0xF0,0x03, /* row  8 */
    0xE0,0x07, /* row  9 */
    0x10,0x08, /* row 10 */
    0x00,0x00, /* row 11 */
    0x00,0x00, /* row 12 */
    0x00,0x00, /* row 13 */
    0x00,0x00, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Body 6: Fluffy ----
 *                          (row 4)  ................
 *                          (row 5)  ...##########...
 *                          (row 6)  .##############.
 *                          (row 7)  .##############.
 *                          (row 8)  ...##########...
 *                          (row 9)  ....########....
 *                          (row 10) ...##......##...
 */
static const uint8_t phantom_body_fluffy[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0x00,0x00, /* row  1 */
    0x00,0x00, /* row  2 */
    0x00,0x00, /* row  3 */
    0x00,0x00, /* row  4 */
    0xF8,0x1F, /* row  5 */
    0xFE,0x7F, /* row  6 */
    0xFE,0x7F, /* row  7 */
    0xF8,0x1F, /* row  8 */
    0xF0,0x0F, /* row  9 */
    0x18,0x18, /* row 10 */
    0x00,0x00, /* row 11 */
    0x00,0x00, /* row 12 */
    0x00,0x00, /* row 13 */
    0x00,0x00, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Body 7: Shell ----
 *                          (row 4)  ................
 *                          (row 5)  ....########....
 *                          (row 6)  ..###.##.##.###.
 *                          (row 7)  ..#.#.##.##.#.#.
 *                          (row 8)  ..###.##.##.###.
 *                          (row 9)  ....########....
 *                          (row 10) ...##......##...
 */
static const uint8_t phantom_body_shell[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0x00,0x00, /* row  1 */
    0x00,0x00, /* row  2 */
    0x00,0x00, /* row  3 */
    0x00,0x00, /* row  4 */
    0xF0,0x0F, /* row  5 */
    0xDC,0x76, /* row  6 */
    0xD4,0x56, /* row  7 */
    0xDC,0x76, /* row  8 */
    0xF0,0x0F, /* row  9 */
    0x18,0x18, /* row 10 */
    0x00,0x00, /* row 11 */
    0x00,0x00, /* row 12 */
    0x00,0x00, /* row 13 */
    0x00,0x00, /* row 14 */
    0x00,0x00, /* row 15 */
};

/* ================================================================
 * FEET PARTS (active rows 9–15)
 *
 * 0 = Claws      Sharp pointed appendages spreading outward
 * 1 = Hover      Floating platform with energy lines below
 * 2 = Tentacles  Wavy undulating tendrils
 * 3 = Legs       Simple sturdy bipedal legs
 * 4 = Stubby     Rounded chubby flat pads
 * 5 = Swirl      Cute ghost tail swirl curved to side
 * 6 = Fin        Mermaid tail with double flukes
 * 7 = Rollers    Circular toy-like wheel rollers
 * ================================================================ */

/*  ---- Feet 0: Claws ----
 *                          (row 9)  ................
 *                          (row 10) ....#......#....
 *                          (row 11) ...##......##...
 *                          (row 12) ..###......###..
 *                          (row 13) .##.#......#.##.
 *                          (row 14) #...#......#...#
 *                          (row 15) ................
 */
static const uint8_t phantom_feet_claws[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0x00,0x00, /* row  1 */
    0x00,0x00, /* row  2 */
    0x00,0x00, /* row  3 */
    0x00,0x00, /* row  4 */
    0x00,0x00, /* row  5 */
    0x00,0x00, /* row  6 */
    0x00,0x00, /* row  7 */
    0x00,0x00, /* row  8 */
    0x00,0x00, /* row  9 */
    0x10,0x08, /* row 10 */
    0x18,0x18, /* row 11 */
    0x1C,0x38, /* row 12 */
    0x16,0x68, /* row 13 */
    0x11,0x88, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Feet 1: Hover ----
 *                          (row 9)  ................
 *                          (row 10) ....########....
 *                          (row 11) ................
 *                          (row 12) ..#..#..#..#.#..
 *                          (row 13) ................
 *                          (row 14) .#.#.#..#.#.#...
 *                          (row 15) ................
 */
static const uint8_t phantom_feet_hover[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0x00,0x00, /* row  1 */
    0x00,0x00, /* row  2 */
    0x00,0x00, /* row  3 */
    0x00,0x00, /* row  4 */
    0x00,0x00, /* row  5 */
    0x00,0x00, /* row  6 */
    0x00,0x00, /* row  7 */
    0x00,0x00, /* row  8 */
    0x00,0x00, /* row  9 */
    0xF0,0x0F, /* row 10 */
    0x00,0x00, /* row 11 */
    0x24,0x29, /* row 12 */
    0x00,0x00, /* row 13 */
    0x2A,0x15, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Feet 2: Tentacles ----
 *                          (row 9)  ................
 *                          (row 10) ...#..#..#..#...
 *                          (row 11) ..#..#..#..#....
 *                          (row 12) ...#..#..#..#...
 *                          (row 13) ..#..#..#..#....
 *                          (row 14) .#..#..#..#.....
 *                          (row 15) ................
 */
static const uint8_t phantom_feet_tentacles[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0x00,0x00, /* row  1 */
    0x00,0x00, /* row  2 */
    0x00,0x00, /* row  3 */
    0x00,0x00, /* row  4 */
    0x00,0x00, /* row  5 */
    0x00,0x00, /* row  6 */
    0x00,0x00, /* row  7 */
    0x00,0x00, /* row  8 */
    0x00,0x00, /* row  9 */
    0x48,0x12, /* row 10 */
    0x24,0x09, /* row 11 */
    0x48,0x12, /* row 12 */
    0x24,0x09, /* row 13 */
    0x92,0x04, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Feet 3: Legs ----
 *                          (row 9)  ................
 *                          (row 10) ....##....##....
 *                          (row 11) ....##....##....
 *                          (row 12) ....##....##....
 *                          (row 13) ...##.....###...
 *                          (row 14) ...###....###...
 *                          (row 15) ................
 */
static const uint8_t phantom_feet_legs[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0x00,0x00, /* row  1 */
    0x00,0x00, /* row  2 */
    0x00,0x00, /* row  3 */
    0x00,0x00, /* row  4 */
    0x00,0x00, /* row  5 */
    0x00,0x00, /* row  6 */
    0x00,0x00, /* row  7 */
    0x00,0x00, /* row  8 */
    0x00,0x00, /* row  9 */
    0x30,0x0C, /* row 10 */
    0x30,0x0C, /* row 11 */
    0x30,0x0C, /* row 12 */
    0x18,0x1C, /* row 13 */
    0x38,0x1C, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Feet 4: Stubby ----
 *                          (row 9)  ................
 *                          (row 10) ...##......##...
 *                          (row 11) ..###......###..
 *                          (row 12) ..###......###..
 *                          (row 13) .####......####.
 *                          (row 14) ..##........##..
 *                          (row 15) ................
 */
static const uint8_t phantom_feet_stubby[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0x00,0x00, /* row  1 */
    0x00,0x00, /* row  2 */
    0x00,0x00, /* row  3 */
    0x00,0x00, /* row  4 */
    0x00,0x00, /* row  5 */
    0x00,0x00, /* row  6 */
    0x00,0x00, /* row  7 */
    0x00,0x00, /* row  8 */
    0x00,0x00, /* row  9 */
    0x18,0x18, /* row 10 */
    0x1C,0x38, /* row 11 */
    0x1C,0x38, /* row 12 */
    0x1E,0x78, /* row 13 */
    0x0C,0x30, /* row 14 */
    0x00,0x00, /* row 15 */
};

/*  ---- Feet 5: Swirl ----
 *                          (row 9)  ................
 *                          (row 10) ....########....
 *                          (row 11) .....######.....
 *                          (row 12) ......####......
 *                          (row 13) .......###......
 *                          (row 14) ........###.....
 *                          (row 15) .........##.....
 */
static const uint8_t phantom_feet_swirl[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0x00,0x00, /* row  1 */
    0x00,0x00, /* row  2 */
    0x00,0x00, /* row  3 */
    0x00,0x00, /* row  4 */
    0x00,0x00, /* row  5 */
    0x00,0x00, /* row  6 */
    0x00,0x00, /* row  7 */
    0x00,0x00, /* row  8 */
    0x00,0x00, /* row  9 */
    0xF0,0x0F, /* row 10 */
    0xE0,0x07, /* row 11 */
    0xC0,0x03, /* row 12 */
    0x80,0x03, /* row 13 */
    0x00,0x07, /* row 14 */
    0x00,0x06, /* row 15 */
};

/*  ---- Feet 6: Fin ----
 *                          (row 9)  ................
 *                          (row 10) ...##......##...
 *                          (row 11) ....##....##....
 *                          (row 12) .....##..##.....
 *                          (row 13) ......####......
 *                          (row 14) ....########....
 *                          (row 15) ...##......##...
 */
static const uint8_t phantom_feet_fin[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0x00,0x00, /* row  1 */
    0x00,0x00, /* row  2 */
    0x00,0x00, /* row  3 */
    0x00,0x00, /* row  4 */
    0x00,0x00, /* row  5 */
    0x00,0x00, /* row  6 */
    0x00,0x00, /* row  7 */
    0x00,0x00, /* row  8 */
    0x00,0x00, /* row  9 */
    0x18,0x18, /* row 10 */
    0x30,0x0C, /* row 11 */
    0x60,0x06, /* row 12 */
    0xC0,0x03, /* row 13 */
    0xF0,0x0F, /* row 14 */
    0x18,0x18, /* row 15 */
};

/*  ---- Feet 7: Rollers ----
 *                          (row 9)  ................
 *                          (row 10) ...##......##...
 *                          (row 11) ...##......##...
 *                          (row 12) ..####....####..
 *                          (row 13) .##..##..##..##.
 *                          (row 14) ..####....####..
 *                          (row 15) ................
 */
static const uint8_t phantom_feet_rollers[PHANTOM_SPRITE_BYTES] = {
    0x00,0x00, /* row  0 */
    0x00,0x00, /* row  1 */
    0x00,0x00, /* row  2 */
    0x00,0x00, /* row  3 */
    0x00,0x00, /* row  4 */
    0x00,0x00, /* row  5 */
    0x00,0x00, /* row  6 */
    0x00,0x00, /* row  7 */
    0x00,0x00, /* row  8 */
    0x00,0x00, /* row  9 */
    0x18,0x18, /* row 10 */
    0x18,0x18, /* row 11 */
    0x3C,0x3C, /* row 12 */
    0x66,0x66, /* row 13 */
    0x3C,0x3C, /* row 14 */
    0x00,0x00, /* row 15 */
};

/* ================================================================
 * LOOKUP TABLES
 * ================================================================ */

static const uint8_t* const phantom_heads[PHANTOM_HEAD_COUNT] = {
    phantom_head_skull,     /* 0 */
    phantom_head_cyclops,   /* 1 */
    phantom_head_static,    /* 2 */
    phantom_head_crown,     /* 3 */
    phantom_head_foxy,      /* 4 */
    phantom_head_axo,       /* 5 */
    phantom_head_ghosty,    /* 6 */
    phantom_head_sprout,    /* 7 */
};

static const uint8_t* const phantom_bodies[PHANTOM_BODY_COUNT] = {
    phantom_body_slime,     /* 0 */
    phantom_body_mech,      /* 1 */
    phantom_body_beast,     /* 2 */
    phantom_body_crystal,   /* 3 */
    phantom_body_chubby,    /* 4 */
    phantom_body_winged,    /* 5 */
    phantom_body_fluffy,    /* 6 */
    phantom_body_shell,     /* 7 */
};

static const uint8_t* const phantom_feet_parts[PHANTOM_FEET_COUNT] = {
    phantom_feet_claws,     /* 0 */
    phantom_feet_hover,     /* 1 */
    phantom_feet_tentacles, /* 2 */
    phantom_feet_legs,      /* 3 */
    phantom_feet_stubby,    /* 4 */
    phantom_feet_swirl,     /* 5 */
    phantom_feet_fin,       /* 6 */
    phantom_feet_rollers,   /* 7 */
};

/* Part name strings (useful for UI display) */
static const char* const phantom_head_names[PHANTOM_HEAD_COUNT] = {
    "Skull", "Cyclops", "Static", "Crown",
    "Foxy", "Axo", "Ghosty", "Sprout",
};

static const char* const phantom_body_names[PHANTOM_BODY_COUNT] = {
    "Slime", "Mech", "Beast", "Crystal",
    "Chubby", "Winged", "Fluffy", "Shell",
};

static const char* const phantom_feet_names[PHANTOM_FEET_COUNT] = {
    "Claws", "Hover", "Tentacles", "Legs",
    "Stubby", "Swirl", "Fin", "Rollers",
};

/* ================================================================
 * COMPOSITOR
 *
 * OR-blends head + body + feet into a single 16x16 XBM buffer.
 * The output buffer must be at least PHANTOM_SPRITE_BYTES (32) bytes.
 * ================================================================ */

static inline void phantom_sprite_composite(
    uint8_t* output,
    uint8_t  head_idx,
    uint8_t  body_idx,
    uint8_t  feet_idx)
{
    const uint8_t* head = phantom_heads[head_idx % PHANTOM_HEAD_COUNT];
    const uint8_t* body = phantom_bodies[body_idx % PHANTOM_BODY_COUNT];
    const uint8_t* feet = phantom_feet_parts[feet_idx % PHANTOM_FEET_COUNT];

    for(uint8_t i = 0; i < PHANTOM_SPRITE_BYTES; i++) {
        output[i] = head[i] | body[i] | feet[i];
    }
}

/* ================================================================
 * INVERT (for Elite Phantom "glow" aura)
 *
 * Flips all bits in the sprite to create the inverted-pixel effect.
 * ================================================================ */

static inline void phantom_sprite_invert(uint8_t* sprite) {
    for(uint8_t i = 0; i < PHANTOM_SPRITE_BYTES; i++) {
        sprite[i] = ~sprite[i];
    }
}

/* ================================================================
 * DERIVE PART INDICES FROM IR ADDRESS
 *
 * Given a raw IR address (uint16_t), extract the three part indices
 * that determine the creature's visual appearance.
 * ================================================================ */

static inline void phantom_sprite_indices_from_address(
    uint16_t address,
    uint8_t* head_idx,
    uint8_t* body_idx,
    uint8_t* feet_idx)
{
    *head_idx = address & 0x07;          /* bits 0-2  (0-7) */
    *body_idx = (address >> 3) & 0x07;   /* bits 3-5  (0-7) */
    *feet_idx = (address >> 6) & 0x07;   /* bits 6-8  (0-7) */
}

#ifdef __cplusplus
}
#endif

#endif /* PHANTOM_SPRITES_H */

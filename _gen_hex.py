# Convert sprite ASCII art to C XBM hex format
# LSB = leftmost pixel (Flipper XBM convention)

def ascii_to_xbm_hex_rows(rows, start_row=0, total_rows=16):
    result = []
    for r in range(total_rows):
        part_row = r - start_row
        if part_row >= 0 and part_row < len(rows):
            line = rows[part_row]
            b0 = 0
            b1 = 0
            for bit in range(8):
                if line[bit] == '#':
                    b0 |= (1 << bit)
            for bit in range(8):
                if line[bit + 8] == '#':
                    b1 |= (1 << bit)
            result.append((b0, b1))
        else:
            result.append((0, 0))
    return result

def format_c_array(name, hex_rows, comment_prefix=""):
    lines = []
    lines.append("/* " + comment_prefix + " */")
    lines.append("static const uint8_t " + name + "[PHANTOM_SPRITE_BYTES] = {")
    for r, (b0, b1) in enumerate(hex_rows):
        h0 = format(b0, '02X')
        h1 = format(b1, '02X')
        lines.append("    0x" + h0 + ",0x" + h1 + ", /* row " + format(r, '2d') + " */")
    lines.append("};")
    return "\n".join(lines)

# ============================================================
# CORRECTED CUTE SPRITE DATA - all exactly 16 chars
# ============================================================

CUTE_HEADS = [
    [ # Foxy - pointed fox ears, big vertical eyes
        ".##.........##..",
        ".###.......###..",
        ".####.....####..",
        "#...##...##...#.",
        "#..###...###..#.",
        ".#...######...#.",
    ],
    [ # Axo - Axolotl gills, cute eyes
        "...##.....##....",
        "..####...####...",
        ".#############..",
        "#..##.....##...#",
        "#..##..#..##...#",
        ".#############..",
    ],
    [ # Ghosty - Wizard cowl/hood
        ".......##.......",
        "......####......",
        ".....######.....",
        "....#.#..#.#....",
        "....#.####.#....",
        "....########....",
    ],
    [ # Sprout - Leafy sprout bulb head
        "....#......#....",
        ".....#....#.....",
        "......####......",
        "..############..",
        "..#..##..##..#..",
        "..############..",
    ],
]

CUTE_BODIES = [
    [ # Chubby - fat round belly with tiny paws
        "................",
        "....########....",
        "..############..",
        ".#.##########.#.",
        "..############..",
        "....########....",
        "...##......##...",
    ],
    [ # Winged - slender body with flared wings
        "................",
        ".##..######..##.",
        ".###.######.###.",
        "..##.######.##..",
        "....######......",
        ".....######.....",
        "....#......#....",
    ],
    [ # Fluffy - cloud mane/collar
        "................",
        "...##########...",
        ".##############.",
        ".##############.",
        "...##########...",
        "....########....",
        "...##......##...",
    ],
    [ # Shell - segmented turtle carapace
        "................",
        "....########....",
        "..###.##.##.###.",
        "..#.#.##.##.#.#.",
        "..###.##.##.###.",
        "....########....",
        "...##......##...",
    ],
]

CUTE_FEET = [
    [ # Stubby - rounded chubby flat paws
        "................",
        "...##......##...",
        "..###......###..",
        "..###......###..",
        ".####......####.",
        "..##........##..",
        "................",
    ],
    [ # Swirl - ghost tail swirl
        "................",
        "....########....",
        ".....######.....",
        "......####......",
        ".......###......",
        "........###.....",
        ".........##.....",
    ],
    [ # Fin - mermaid tail with double flukes
        "................",
        "...##......##...",
        "....##....##....",
        ".....##..##.....",
        "......####......",
        "....########....",
        "...##......##...",
    ],
    [ # Rollers - circular toy-like wheel rollers
        "................",
        "...##......##...",
        "...##......##...",
        "..####....####..",
        ".##..##..##..##.",
        "..####....####..",
        "................",
    ],
]

# Verify
print("=== VERIFYING LENGTHS ===")
all_good = True
for tag, parts in [("HEADS", CUTE_HEADS), ("BODIES", CUTE_BODIES), ("FEET", CUTE_FEET)]:
    for i, p in enumerate(parts):
        for j, row in enumerate(p):
            if len(row) != 16:
                print("ERROR: " + tag + "[" + str(i) + "] row " + str(j) + ": len=" + str(len(row)) + " -> " + repr(row))
                all_good = False

if not all_good:
    print("FIX ERRORS ABOVE")
    exit(1)
print("All strings are 16 chars - OK\!")

# Generate hex
print("\n\n// ============================================")
print("// ADDITIONAL CUTE HEADS (indices 4-7)")
print("// ============================================\n")

names_h = ["Foxy", "Axo", "Ghosty", "Sprout"]
for i, head in enumerate(CUTE_HEADS):
    hex_rows = ascii_to_xbm_hex_rows(head, start_row=0)
    name = "phantom_head_cute" + str(i)
    comment = "Head " + str(i+4) + ": Cute " + names_h[i]
    print(format_c_array(name, hex_rows, comment))
    print()

print("// ============================================")
print("// ADDITIONAL CUTE BODIES (indices 4-7)")
print("// ============================================\n")

names_b = ["Chubby", "Winged", "Fluffy", "Shell"]
for i, body in enumerate(CUTE_BODIES):
    hex_rows = ascii_to_xbm_hex_rows(body, start_row=4)
    name = "phantom_body_cute" + str(i)
    comment = "Body " + str(i+4) + ": Cute " + names_b[i]
    print(format_c_array(name, hex_rows, comment))
    print()

print("// ============================================")
print("// ADDITIONAL CUTE FEET (indices 4-7)")
print("// ============================================\n")

names_f = ["Stubby", "Swirl", "Fin", "Rollers"]
for i, feet in enumerate(CUTE_FEET):
    hex_rows = ascii_to_xbm_hex_rows(feet, start_row=9)
    name = "phantom_feet_cute" + str(i)
    comment = "Feet " + str(i+4) + ": Cute " + names_f[i]
    print(format_c_array(name, hex_rows, comment))
    print()

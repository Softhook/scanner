# Game Design Document: Eye of the Phantom (MVP — Single Creature)

## 1. Vision Statement
**Eye of the Phantom** is an original, hardware-integrated roguelite monster battler designed exclusively for the Flipper Zero. It transforms the player's real-world environment into the game's ecosystem by using the Flipper's built-in Infrared (IR) receiver. Players scan real-world remotes (TVs, AC units, led strips) to procedurally generate "Phantoms" and dive into an endless digital dungeon.

> **MVP Scope:** The player has a **single active Phantom** at a time. No roster swapping. This keeps the codebase small and the gameplay focused on the core scan → fight loop.

**Platform:** Flipper Zero  
**Display:** 128x64 Monochrome LCD  
**Controls:** D-Pad + OK / Back  
**Key Hardware Integration:** IR Receiver

---

## 2. Core Gameplay Loop
The game revolves around three distinct phases that encourage the player to constantly switch between real-world exploration and on-device gameplay.

1.  **Scan (The Real World):** Point external IR remotes at the Flipper to capture raw hexadecimal signals.
2.  **Summon (The Forge):** Convert that hex data into a unique, playable Phantom. The player can choose to keep it or discard it.
3.  **Descend (The Mainframe):** Enter an endless sequence of turn-based RPG battles to earn upgrade currency.

---

## 3. Procedural Generation Engine (The "Summoning")
The magic of the game relies on deterministic generation. The same TV remote button will *always* generate the exact same Phantom, allowing players to share real-world "recipes" online.

When a signal is captured, the Flipper parses three components: Protocol, Address, and Command.

### A. Element / Class (Based on Protocol)
The type of remote defines the combat archetype:
*   **NEC Protocol (Standard TVs):** `Brawler` (Favors ATK and HP)
*   **Sony / SIRC (Sony Devices):** `Defender` (Favors DEF and HP)
*   **Samsung Protocol:** `Glitch` (Favors SPD and ATK)
*   **RC5 / Unknown:** Mapped dynamically using `(address + command) % 3` into one of the three classes above.

### Class Matchup Advantage (Rock-Paper-Scissors)
The three classes form a clear tactical triangle:
*   **BRAWLER beats GLITCH:** Massive ATK overwhelms Glitch's low HP/DEF before speed becomes a decisive factor.
*   **GLITCH beats DEFENDER:** High Speed allows Glitch to act first and chip away with highly heat-efficient attacks, bypass Defender's defense, and force Overload.
*   **DEFENDER beats BRAWLER:** High HP and DEF absorbs Brawler's heavy blows, outlasting them as the Brawler overheats from attacking.

*Matchup Multipliers:*
*   **Advantaged Matchup (e.g. Brawler vs Glitch):** Deal **1.3×** damage.
*   **Disadvantaged Matchup (e.g. Glitch vs Brawler):** Deal **0.8×** damage.
*   **Neutral Matchup (Mirror):** Deal **1.0×** damage.

### B. Base Stats (Based on Hex Command)
The raw data determines the stat spread (HP, ATK, DEF, SPD):
*   **HP (Health):** Overall durability.
*   **ATK (Attack):** Scale factor for damage dealt.
*   **DEF (Defense):** Reduces damage taken via flat subtraction (`DEF / 3`).
*   **SPD (Speed):** Multi-purpose stat:
    *   **Turn Order:** The unit with the higher SPD acts first every turn.
    *   **Critical Rate:** Crit chance scales with `SPD × 2%` (capped at 25%).
    *   **Heat Reduction:** On any action, self-heat generation is reduced by `SPD / 2` (minimum 0).

### C. Visual Sprite (Based on Hex Address)
The 16x16 pixel sprites are modular. The Flipper reads the hex address to pick parts from a sprite sheet:
*   `Digit 1:` Head shape (e.g., Skull, Eye, Static)
*   `Digit 2:` Body type (e.g., Slime, Mech, Beast)
*   `Digit 3:` Appendages (e.g., Claws, Wheels, Tentacles)

---

## 4. Combat Mechanics: The Mainframe
Combat is designed for quick, one-handed play. It is turn-based, utilizing a simple menu system mapped directly to the D-Pad.

### Layout (128x64 screen)
*   **Left 42px:** Your active Phantom sprite.
*   **Right 42px:** Enemy "Corrupted" Phantom sprite.
*   **Top 12px:** HP bars and Heat gauges for both players.
*   **Middle:** Matchup advantage indicator (`>>` / `<<` / `==`) and class abbreviation (BRL, DEF, GLI) between sprites.
*   **Bottom:** 2x2 grid of actions navigated via the D-Pad and executed with OK.

### Grid-Navigated Combat Actions
Each of the 4 actions serves a specific purpose in a battle:

| Action | Grid Pos | Damage | Self Heat | Enemy Heat | Description & Tactical Purpose |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **STRIKE** | Top-Left | 1.0× ATK | +20 | — | Reliable workhorse. Managed heat, decent output. |
| **SURGE** | Top-Right | 1.5× ATK | +35 | — | Heavy finisher. Risky at high heat due to self-overload. |
| **GUARD** | Bottom-Left | 0 | −30 | — | Cools down self-heat and reduces incoming damage by 50%. |
| **REVERSE** | Bottom-Right | 0.3× ATK | +10 | +20 | Strategic choice. Chips HP, transfers heat to the enemy to force Overload. |

### Heat & Overload
*   Max Heat capacity is **100**.
*   Every action increases self-heat (Strike +20, Surge +35, Reverse +10), which is reduced by `SPD / 2`.
*   Reaching **100 Heat** causes **Overload**:
    *   The unit instantly takes damage equal to **8% of their max HP**.
    *   The unit skips their next turn.
    *   The Heat is then reduced by **50** (returning to 50 Heat).

### Damage Calculation Formula
Damage is computed using a flat defense subtraction and class multipliers:
`damage = max(1, (attacker_atk × action_mult - defender_def / 3) × advantage_mult × random_variance)`
*   `action_mult`: 1.0 for Strike, 1.5 for Surge, 0.3 for Reverse.
*   `advantage_mult`: 1.3 if advantaged, 0.8 if disadvantaged, 1.0 if neutral.
*   `random_variance`: Random multiplier between 0.85 and 1.15.
*   `critical`: Critical hits deal **1.8×** the final damage.

### Retreat (Back Key)
During the player's turn, the player can press the **Back** key to retreat from combat:
*   The enemy's current HP, Heat, and Overloaded state are saved.
*   The player returns to the main menu without losing their Phantom.
*   Starting combat again on the same floor acts as a **rematch**, resuming the fight with the saved enemy state. This allows players to swap active Phantoms at Camp and return to finish off a tough opponent.

---

## 5. Progression & The Metagame
To keep the player engaged, the game features long-term progression loops.

### Data Shards
Defeating enemies yields `Data Shards` (equal to `floor_number + 1`). These are spent in the "Camp" menu to permanently increase the active Phantom's base stats.

### Stored Phantoms
The player can store up to **10 Phantoms** in their collection. Only one is active at a time. Switching the active Phantom is done from the Collection screen inside the Camp menu.

### Rarity & The Deep Scan
Holding down a remote button sends repeated IR frames. If the game detects a burst of **10+ consecutive repeats** of the same signal, it generates an `Elite Phantom` with a glowing (inverted pixel) aura and +20% base stats.

---

## 6. Technical & UX Constraints
Developing for the Flipper Zero requires strict adherence to its limitations.

*   **Memory:** Stored Phantom limit is capped at 10 to keep the save file size minimal.
*   **Haptics:** The internal vibration motor triggers on critical hits (short double buzz) and when a Phantom is defeated (long heavy buzz).
*   **Visual Style:** High-contrast, 1-bit pixel art. UI uses thick, solid borders to separate combatants and menus clearly. No gray-scaling; just pure black and white.
*   **Persistence:** The game saves automatically after every battle, purchase, or retreat to prevent data loss if the Flipper reboots or exits the app. Old save files are automatically migrated (remap old Technician phantoms to Glitch).

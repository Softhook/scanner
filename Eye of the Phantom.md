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
* **NEC Protocol (Standard TVs):** `Brawler` (High Attack, Low Speed)
* **Sony / SIRC (Sony Devices):** `Defender` (High Armor, Low Attack)
* **Samsung Protocol:** `Technician` (Balanced Stats, High Crit Chance)
* **RC5 / Unknown:** `Glitch` (Glass Cannon, High Speed)

### B. Base Stats (Based on Hex Command)
The raw data determines the stat spread (HP, ATK, DEF, SPD). 
* *Example:* Command `0x4A` (74 in decimal). The engine uses 74 as the seed. If the number is even, it favors Defense. If odd, it favors Attack.

### C. Visual Sprite (Based on Hex Address)
The 16x16 pixel sprites are modular. The Flipper reads the hex address to pick parts from a sprite sheet:
* `Digit 1:` Head shape (e.g., Skull, Eye, Static)
* `Digit 2:` Body type (e.g., Slime, Mech, Beast)
* `Digit 3:` Appendages (e.g., Claws, Wheels, Tentacles)

---

## 4. Combat Mechanics: The Mainframe
Combat is built for quick, one-handed play. It is turn-based, utilizing a simple menu system tailored to the D-Pad.

### Layout (128x64 screen)
* **Left 32px:** Your active Phantom sprite.
* **Right 32px:** Enemy "Corrupted" Phantom sprite.
* **Top 8px:** Two simple, thin HP bars spanning the screen. Heat gauge shown as a small pip below HP.
* **Bottom 24px:** D-Pad mapped menu (Up: Attack, Right: Skill, Down: Defend).

### Flow & Strategy
The game uses a **Stamina / Overheat** system instead of complex elemental typing.
* Each Phantom has a `Heat` gauge (0–100). 
* **Attacking** generates Heat (+20 base, modified by SPD).
* **Using a Skill** generates more Heat (+35 base) but deals extra damage.
* If a Phantom hits 100% Heat, they **Overload**, skipping their next turn and taking residual damage.
* **Defending** cools the Phantom down (−30 Heat) and reduces incoming damage by 50%.
* With no teammate to swap to, Heat management becomes the central tactical decision: *push for damage and risk Overloading, or play it safe and Defend to cool down.*

### Enemy AI
Enemies use weighted random action selection:
* Below 50% Heat → 70% Attack, 20% Skill, 10% Defend
* Above 50% Heat → 30% Attack, 10% Skill, 60% Defend
* Above 80% Heat → 10% Attack, 0% Skill, 90% Defend

### Enemy Generation
Enemy "Corrupted Phantoms" are generated using a **floor-seeded PRNG**. Each floor number seeds a deterministic generator that produces the enemy's class, stats, and sprite. Stats scale with floor depth:
* `enemy_stat = base_stat + (floor_number × 2)`

---

## 5. Progression & The Metagame
To keep the player engaged after scanning their entire living room, the game features long-term progression loops.

### Data Shards
Defeating enemies yields `Data Shards` (equal to `floor_number + 1`). These are spent in the "Camp" menu to permanently increase the active Phantom's base stats. 

### Stored Phantoms
The player can store up to **10 Phantoms** in their collection. Only one is active at a time. Switching the active Phantom is done from the Camp menu (not mid-battle).

### Rarity & The Deep Scan
Holding down a remote button sends repeated IR frames. If the game detects a burst of **10+ consecutive repeats** of the same signal, it generates an `Elite Phantom` with a glowing (inverted pixel) aura and +20% base stats.

---

## 6. Technical & UX Constraints
Developing for the Flipper Zero requires strict adherence to its limitations.

* **Memory:** Stored Phantom limit is capped at 10 to keep the save file size minimal. 
* **Haptics:** The internal vibration motor triggers on critical hits (short double buzz) and when a Phantom faints (long heavy buzz).
* **Visual Style:** High-contrast, 1-bit pixel art. UI uses thick, solid borders to separate combatants and menus clearly. No gray-scaling; just pure black and white.
* **Persistence:** The game saves automatically after every battle to prevent data loss if the Flipper reboots or exits the app.

# Orcs & Elves II — PlayStation Vita Port

Native PlayStation Vita port of **Orcs & Elves II** (BREW 7.0.41 C reconstruction) with both **Classic** BREW graphics and **Remastered** dynamic point lighting, geometric ray shadows, and contact ambient occlusion.

## Package & Paths
- **Package**: `dist/Orcs-Elves-II-Vita.vpk` (Title ID: `ORCS00002`)
- **Bundled Game Data**: `app0:gamedata/orcsandelves2.bar`, `app0:gamedata/orcsandelves2.mod`, and `app0:gamedata/audio/*.pcm` (self-contained inside the `.vpk`)
- **Persistent Saves & Config**: `ux0:data/oe2/` (`Config`, `SD_0`..`SD_3`)

## PS Vita Controls

| Button | Action (Exploration) | Action (Menus / Dialogs / Lockpicking) |
|---|---|---|
| **D-Pad Up / Down** or **Left Stick Up / Down** | Move Forward / Backward | Navigate Up / Down (Adjust merchant offer +1 / -1) |
| **D-Pad Left / Right** or **Right Stick Left / Right** | Turn Left / Right | Page Up / Down, Yes/No choice, Lockpick Left / Right (Merchant offer -10 / +10) |
| **L Trigger / R Trigger** or **Left Stick Left / Right** | Strafe Left / Strafe Right | Hold modifier |
| **Cross (`X`)** | Attack / Interact / Use | Select / Confirm / Advance Dialog |
| **Circle (`O`)** | Pass Turn (`L + Circle`: Previous Weapon) | Back / Cancel / Delete Digit |
| **Triangle (`/\\`)** | Next Weapon (`L + Triangle`: Prev Weapon, `R + Triangle`: Quick Potion/Item) | Skip Cinematic / Right Softkey |
| **Square (`[]`)** | Toggle Automap (`L + Square`: Quick Weapon List) | Back / Cancel |
| **Start** | Open Main / Pause Menu (`R + Start`: Toggle Fast 60 Hz / Original 25 Hz Tick Speed) | Left Softkey / Back |
| **Select** | **Toggle Remastered / Classic Graphics** (`L + Select`: Cycle Light Range Low/Med/High, `R + Select`: Toggle Integer Scaling) | **Toggle Remastered / Classic Graphics** |

## Building from Source
Ensure `gamedata/orcsandelves2.bar`, `gamedata/orcsandelves2.mod`, and `gamedata/audio/*.pcm` are present, and VitaSDK is installed at `tools/vendor/vitasdk` (or pointed to by `VITASDK`):

```sh
python3 tools/build_vita.py
```


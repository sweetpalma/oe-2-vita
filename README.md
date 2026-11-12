# Orcs & Elves II — PlayStation Vita

PlayStation Vita port of **Orcs & Elves II**.

## Controls

| Button                                                | Action (Exploration)                                                                                    | Action (Menus / Dialogs / Lockpicking)                                          |
| ----------------------------------------------------- | ------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| **D-Pad Up / Down** or **Left Stick Up / Down**       | Move Forward / Backward                                                                                 | Navigate Up / Down (Adjust merchant offer +1 / -1)                              |
| **D-Pad Left / Right** or **Left Stick Left / Right** | Turn Left / Right                                                                                       | Page Up / Down, Yes/No choice, Lockpick Left / Right (Merchant offer -10 / +10) |
| **L Trigger / R Trigger**                             | Strafe Left / Strafe Right                                                                              | Hold modifier                                                                   |
| **Cross (`X`)**                                       | Attack / Interact / Use                                                                                 | Select / Confirm / Advance Dialog                                               |
| **Circle (`O`)**                                      | Cancel / Close Automap (`L + Circle`: Previous Weapon)                                                  | Back / Cancel / Delete Digit                                                    |
| **Triangle (`/\\`)**                                  | Next Weapon (`L + Triangle`: Prev Weapon, `R + Triangle`: Quick Potion/Item)                            | Skip Cinematic / Right Softkey                                                  |
| **Square (`[]`)**                                     | **Skip Move / Pass Turn** (`L + Square`: Quick Weapon List)                                             | Back / Cancel                                                                   |
| **Start**                                             | Open Main / Pause Menu (`R + Start`: Toggle Fast 60 Hz / Original 25 Hz Tick Speed)                     | Left Softkey / Back                                                             |
| **Select**                                            | **Toggle Automap** (`L + Select`: Cycle Light Range Low/Med/High, `R + Select`: Toggle Integer Scaling) | Close Automap                                                                   |

## Building from Source

Ensure `data/orcsandelves2.bar` and `data/orcsandelves2.mod` are present, and VitaSDK is installed at `tools/vendor/vitasdk` (or pointed to by `VITASDK`). Then:

```sh
python3 tools/build_vita.py
```

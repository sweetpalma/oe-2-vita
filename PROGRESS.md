# BREW port progress — 2026-11-04

## Verified

- Separate BREW project created; the previous source project and WolfMoon were not modified.
- Original English BREW release 7.0.41 obtained. BAR and MOD hashes recorded; MOD size and CRC exactly match the engine's supported build.
- Reconstructed C engine pinned to public revision 2e9c7bfb79694ffbd8e648513edd229ee212c10a. Native desktop build boots the original game.
- Browser host built using Emscripten 6.0.13. The C gameplay core runs in WebAssembly with the original 40 ms simulation step.
- All 115 original audio resources decoded successfully; browser host plays PCM through WebAudio.
- Prepared Godot UI widgets reused: theme/icons, HUD, dialog panel, lockpick panel, map and language picker. Gameplay comes only from BREW.
- Fixed-layout game view and modal dialog input blocking implemented. Read-only world and story captures exclude the original navigation overlays.
- Dedicated BREW save namespace with persistent browser filesystem implemented; exported input staging excludes saves.
- Native/WebAssembly replay passed 1140 comparisons over 1058 simulation ticks: complete exposed model, full-frame and world RGBA checksums match exactly. Covered startup, menus, story, travel, loading, exploration, dialogs, map, saving and the initial cinematic.
- Four generated binary save files (SD_0, SD_1, SD_2, SD_3) matched byte for byte. Loading resumed the saved level in both targets.
- Persistent browser storage tested in an isolated origin: a complete page reload restored the saved level, position, direction, health, weapon and ammunition.
- The same browser save was opened through the Godot UI's Continue flow. F5 saved without reloading the page; M opened the custom discovered-cell map.
- Browser gameplay visually confirmed with the custom HUD; the sampled scene logged 0.7–0.8 ms ordinary core ticks (observed maximum 22.3 ms including earlier work). This is not a whole-game performance claim.
- Godot imports and Web exports complete without parse errors. Browser language selection and custom menu transitions checked visually without console errors.

## GitHub publication — 2026-11-04

- BREW source published in `PilotOfAsuka/orcs-elves-ii-remastered`, branch `codex/brew-port`, now the repository's default branch.
- Public game: https://pilotofasuka.github.io/orcs-elves-ii-remastered/brew/
- Pages deployment `804cc66b6f0e0d9e92d19d954d4921c1cfda2d1a` completed successfully. The preview manifest records source `74e9d658f80ec2b3d210587042658bc6698032ec` and exported file hashes.
- Live page checked in the browser: Russian UI selection, startup menus, original story pages, first level, custom modal dialog and return to the world view. No console errors were reported.
- Published HTML, UI pack, browser host, BREW loader, BREW WebAssembly and BREW resource package were verified against the deployment manifest.
- The existing Pages root is preserved; the repository homepage and README point directly to the BREW preview. QA pages and local saves are excluded from publication.

## Current scope

This is the first BREW browser foundation. The parity comparison is against the same reconstructed C engine on the native host, not a certification of all original ARM behavior. Full-game playthroughs and remaining presentation states still need validation.

Both modes display original BREW art. Remastered now adds point lighting and geometric ray shadows, with the same core and saves. The user chose this direction instead of generated HD replacements.

The interface has Russian/English selection. Original BREW story art and game text are English; a full BREW translation is outstanding.

## Original-art lighting and interface revision — 2026-11-04

- Surveyed and decoded all 220 wall variants, 24 floor/ceiling materials and 1057 sprite frames (205 sprite identities), using all 507 palettes. Local contact sheets were visually checked. Sources, candidates and limits are recorded in docs/LIGHTING.md.
- Added an independent Remastered buffer: warm fire/torch/candle light, coloured magical effects, molten floor light and a held magical light. Wall/door segments block shadow rays. Classic pixels, simulation, RNG and binary saves are untouched by the getter.
- Lowered ambient light slightly at the user's request; retained all original BREW textures and sprites.
- Fixed the cinematic capture path: the separate camera renderer and post-fade/particle image now reach the browser before native captions/letterbox overlays. The replay now observes 105 distinct cinematic world frames.
- Enemy HP and event text now overlay the top of a framed game view. Removed the gameplay title and BREW footer. Menu/map/inventory are compact top icons; native Options contains language/fullscreen instead of permanent menu footer buttons.
- The graphics switch is a fourth compact icon with a Classic/Remastered caption; the selection persists locally. Lower touch controls now expand vertically into the available screen space, with larger icons and no visible keyboard-letter captions.
- Updated replay passed 1140 model/Classic pixel checks over 1058 ticks and byte-identical SD_0–SD_3 saves. Geometric shadow tests cover blocked, parallel, above-wall and opened-wall rays. Lighting getter purity is checked for model, Classic pixels and saves.
- Measured 912 lit replay frames: mean 3.30 ms, p95 8.60 ms, maximum 11.52 ms in this desktop run. A live narrow-browser scene logged lighting around 1.6–3.7 ms and ordinary simulation ticks around 0.5–1.3 ms; this is not a whole-game/mobile performance guarantee.
- Local browser visually verified the darker Remastered view, compact graphics icon, enlarged lower buttons and fixed HUD. No warning/error console entries were reported.
- Publication now targets the existing Pages root; `/brew/` redirects to it. Content-named UI packs and versioned core/runtime requests prevent older cached root assets from masking the new build.

- Extended shadows to enemy/object alpha silhouettes; tests cover opaque pixels, transparent gaps, moving casters and hidden casters. Fire on the spit (116/118) follows the original meat-present bit; before/after emission tests pass. Violet decorative bones are no longer misidentified as an emitter.
- Added soft shadow interpolation, visible-emitter halos, contact shading and subtle depth haze. Magical projectile sources follow their live sprite coordinates and expire with them.
- Added persisted Remastered quality in Options: Low 176×164, Medium 352×328, High 704×656. Higher qualities replay resolved raster commands on an independent framebuffer, without replaying game logic. The visible UI frame remains independent of render resolution.
- All 1140 comparisons over 1058 native simulation ticks still pass with High getters sampled during the replay. Extra Staff/menu fixtures use 16 ticks. Getters at all three resolutions preserve the model, canonical world image and saved files. Measured sampled resolution-pass means: Low 1.71 ms, Medium 8.18 ms, High 32.18 ms. Live browser High measured about 29.6–32 ms in a different saved scene.
- Corrected the Staff presentation’s 7-source-pixel gap using a separate background/weapon layer; canonical native captures stay unchanged. Tested actual 704×656 output and the Options dropdown in the browser without warning/error logs.

- Corrected shadow height intervals for multi-piece upper walls (64..ceiling) and added upper-wall/below-opening regression rays.

- At the user’s request for a much stronger lighting influence, lowered unlit ambient to 0.17/0.20/0.27, increased direct-light gain to 2.1× and strengthened emitter halos. Classic and UI brightness remain unchanged.

## Contact AO, Russian presentation, saves and HUD — 2026-11-05

- Added stronger smooth contact AO at floor/wall and ceiling/wall seams and perpendicular wall corners. Separate height-aware distance fields prevent a false floor seam beneath floating upper wall pieces. The effect retains world-space size at all three render qualities. A wall-column cache preserves exact rendered pixels while reducing repeated distance lookups.
- Corrected billboard lighting normals, quality-scaled emitter halos, held magic colors and Flaming Sword light. Original art and the 40 ms simulation remain unchanged.
- Added a complete separate Russian presentation catalog: 1690 of 1690 original text records, with exact/static and formatted combat/stat checks, argument validation, actor case forms and sampled runtime text checks. Full linguistic playthrough review remains outstanding.
- Original parchment artwork now has separate Cyrillic intro/ending text masks. Travel place names, complete notebook entries, item descriptions, help and credits use the custom presentation. Wrapped confirmations are reassembled before translating; new dialogue resets its text scroll. English keeps original game strings and story masks, with browser-control help.
- Replaced the HP/armor panel with fixed-height bronze instruments, separate red/blue bars, readable counts, weapon/ammo strip and a low-health pulse. Verified it in the browser at desktop size and 390×844, with large lower controls and modal dialogue overlay.
- The persistent browser filesystem stays at /oe2-brew-v1 across releases. Added Settings download/import backups with per-file checksums, staged replacement and rollback. Actual browser download produced Config and SD_0..SD_3; all checksums verified. The same backup was imported through the file picker and resumed the level. A full reload after rebuilding the UI also restored position/orientation and HP/armor 100/100, Wand and 10 charges. Corrupted backup rejection and exact binary restoration pass in the isolated replay.
- Native/WASM replay again passed 1140 full model/frame/world comparisons over 1058 simulation ticks, with 16 extra Staff/menu fixture ticks. All saved SD_0..SD_3 files match exactly. All quality getters preserve the model, canonical world image and save bytes. All 105 cinematic frames remain distinct in the replay. Synthetic contact tests cover the seams, clear centres, perpendicular corners and upper-wall gap.
- Sampled resolution-pass means in the final desktop replay: Low 2.01 ms, Medium 9.43 ms, High 39.80 ms. Across 912 lit replay frames, mean 3.68 ms, p95 9.51 ms. These are scene-specific CPU measurements, not a whole-game/mobile guarantee.
- Final browser Low scene logged lighting 2.3–2.4 ms and ordinary core ticks 0.6 ms (observed maximum 14.2 ms including loading). RU story texture, travel label, initial dialogue, notebook, runtime RU/EN changes, quality dropdown and backup flow were checked visually. No warning/error console entries were reported.
- Godot import/export, strict catalog build, clean-checkout catalog rebuild equivalence, translator checks and diff whitespace checks pass.

- Final live inspection found a composed weapon tutorial whose title translated but phone-wrapped body did not. The translator now matches complete resource paragraphs across native wrapping; the dagger regression fixture passes.
- The custom Continue/choice actions now consume original dialogue typing/pagination through the original C handler, stopping at the next dialogue or game state. This avoids repeated clicks for invisible phone pages. An isolated original intro plus multi-page dagger tutorial test confirms one-click continuation without advancing simulation time, moving the player or changing health. Full native/WASM parity still passes.

## Published verification — 2026-11-05

- Permanent Pages root now serves source 9a4f24d9c78d1ba9bd23e77a0fd2b52af328dfc1, deployment a7cbf2402daf9f02c2c262fcbe2ae3c39e292388. GitHub reports this exact deployment built successfully. All 12 public files match the release manifest by size and SHA-256.
- Live page resumed the previous BREW checkpoint after the update. The full Russian dagger tutorial translated and closed in one click; switching to Staff retained the lit view and bottom anchoring. New HUD and contact AO were visually verified on the live page, with no warning/error console entries. Screenshot: ignored build/contact-ao-public.png.

## Vertical English trailer — 2026-11-05

- Authored a 47.6-second vertical English trailer and a 21.8-second short cut, both 1080×1920/60 FPS. Captions demonstrate Classic versus Remastered High, touch controls, combat, lighting/shadows, the map, language selection, saving and browser play. The complete HUD and controls stay in frame.
- Camera shots and actual gameplay come from the first dungeon of the BREW engine. Classic and High comparison shots use the same camera position; High rasterizes at 704×656. Original title music and captured gameplay sounds form the soundtrack, with low editorial transition impacts. The main trailer briefly changes the UI to Russian; all promotional captions and the complete short cut are English.
- Filming runs in a disposable nonpersistent instance. Optional camera precision hooks are guarded by OE2_CINEMATIC_CAPTURE, with a separate build/cinematic/core output. Normal builds and the user's saves remain independent. The source workflow is documented in tools/cinematic/README.md; generated movies/frames are ignored.
- After adding the guarded filming hooks, the normal native/WASM replay again passed 1140 checks over 1058 ticks, including exact binary save parity. The current Pages deployment is unchanged by the offline filming tools.
- Visually checked a 12-shot contact sheet and the full-resolution poster. Both final MP4s pass full FFmpeg decoding and format verification: 2856/1308 video frames, 47.6/21.8 seconds, 1080×1920 at 60 FPS, limited-range BT.709 H.264 yuv420p, 48 kHz AAC. Main trailer is about 48 MB. Files are in videos/; verification report is in ignored build/cinematic/video-verified.json.

## Lockpicking, impact light, diagnostics and Android test — 2026-11-05

- Corrected the lock adapter: length comes from the actual native combination, completed positions from lockSlot and the intermediate state from lockCorrectHits. Attempt feedback uses an unsigned per-session count rather than the packed lifetime accuracy statistic. Entered directions, not the secret code, are exposed by the release adapter.
- Touch and keyboard directions use the same native directional keys; Cancel uses native Clear. The original twice-correct input rule, error handling, turns, skill checks, XP and chest/door actions are preserved. The custom modal shows native padlock frames in a bronze mount, a short hint and progress pips, with brief input feedback. Removed the duplicate phone lock overlay from the bridged world view; the standalone engine keeps it.
- Native padlock identity 13 frames 0/1 were inspected in the decoded catalog and original render path. A build-time decoder preserves their pixels, replacing their old pixel border with our modal frame. Derived game artwork stays ignored rather than entering the source repository.
- Added a 320 ms colored arrival flash before a luminous projectile is retired. It follows the original endpoint, decays independently of the deleted sprite, respects geometry and remains within the existing light budget. It does not affect damage, simulation, RNG, Classic world buffers or save data.
- Added persisted Settings → Debug checkbox: an overlay shows UI FPS, actual render resolution, last native step time and the extra Remastered pass time. FPS counts presentation frames, not the fixed 25 Hz simulation. The label ignores pointer events and does not change the world viewport or enable native cheats.
- Added a private offline Android WebView test shell, using exactly the normal Web export. It includes safe display insets, lifecycle pause/audio suspension, Back handling and document-picker save import/export. APK and test signing key are local ignored artifacts; no store or public APK publication is configured.
- Normal native/WASM replay passed 1140 checks across 1058 ticks with exact binary save parity. Isolated original first-dungeon chests and the first locked chest in map 3 were exercised through native inputs: wrong guesses, partial reset, two correct presses, completed-position preservation, cancel/retry and actual unlock to native tile 143. The map 3 lock has four positions. Actual projectile resolution produces a visible decaying flash; all three quality getters preserve canonical buffers and the model.
- A separate browser fixture at 390×844 verified native padlock frame feedback, green completed positions using touch and keyboard, full modal layout, disabled underlying controls, Settings checkbox and live diagnostic overlay. The arrival peak was visually compared against the same unlit scene. Android save-bridge JSON and checksum round trip and debug preference persistence pass. The physical Android launch/document picker still require device testing.

- Published source d9c977934b416e1181a172ca7d31ea87450bd11b at the permanent Pages root (web-build 6b1f6d2ae9e640397dd0f5d353f6785516dc5047). The final local APK is dist/Orcs-Elves-II-0.2.1-test.apk, 17204682 bytes, signed with APK v2/v3 and ZIP-aligned; all packaged game asset hashes match the Web export. No APK was uploaded publicly.
- User requests local Web verification only for future publications, without repeat downloads or browser runs against the public release. Recorded this in AGENTS.md and REMAINING_WORK.md.

## Status-effect column — 2026-11-05

- Restored the missing timed modifier presentation from the actual native Buff array, including original signed values, true remaining turns, compound modifier aggregation and familiar-form visibility. No status rules, save layout or clocks changed.
- Decoded all twelve original status icons from resource 3030 with native RGB565 colors and magenta transparency. The art is generated into an ignored folder during the UI build.
- Added a compact vertical column inside the left edge of the game view, following the user's final placement/size preference. Native icons are 14 units, row pitch 23, column width 58. Enemy HP/events sit beside it; the viewport and fixed health/armor HUD do not resize. Counts remain visible; expiring icons pulse gently. Both graphics modes and RU/EN labels use the same presentation.
- Native/WASM replay passed 1140 checks over 1058 ticks, with exact binary save parity. The dedicated native-rule fixture passes positive/negative values, no countdown from idle ticks, player-turn decrement, expiration, compound-effect timing, familiar-only energy, save/load durations and pure getters at all three render qualities.
- Godot layout checks passed 24 cases across 360×640, 390×844 and 1280×720, Classic/Remastered and 0/1/5/12 entries. World rectangles remain identical and icons stay inside the view beside the encounter overlay. Local visual checks also cover multiple effects and actual counter changes.

- Published the locally verified status HUD at the permanent root: source 171bc349cb3607cde0ca70f1589686dacb8ed00a, web-build 8dd38124ad02be2aceb73a1e5a17b604776295cb. No public-page/asset recheck was run, per the user's traffic preference.
- Updated private APK to 0.2.2-test (versionCode 2), 17212874 bytes, using the existing package ID/signing key. Signature, alignment and packaged asset hashes pass; its game files match the local Pages release manifest. The APK remains local, and on-device testing is not claimed.


## World candles, menus and conversation identity — 2026-11-05

- Matched the skull-and-bars candle in the supplied screenshot to wall identities 61/69 (materials 140/164). Warm local sources follow the actual flame texel, repeated/mirrored segments, wall height pieces and hidden geometry. The same artwork on wall-aligned props is covered, including five such props in map 6. Wood-grate identity 68 is not an emitter.
- Original lamp/candle sprites 150/181 stop emitting on their native fixed extinguished frame 4. Hidden/replaced sources are reevaluated each presentation pass. The original script opcode extinguishes and relights an actual map-2 lamp in the isolated fixture; all lit/unlit/hidden frames and real map-4 wall lighting are checked. Reading all three qualities preserves game state, Classic pixels and native saves.
- Held starter-wand light now uses radius 105/power 0.28 instead of 210/1.15. Environmental and spell lights retain their power. Broad halo radius/gain were reduced; translucent world/weapon glow passes keep the background's lighting receiver instead of marking its already blended pixels as solid foreground. This reduces bright silhouettes without changing native sprite art or Classic compositing.
- Main menu now contains the primary play actions, Settings and Help. Pause groups inventory, character, quests and map; Save/Load sit together, while restart/quit and secondary actions live under More. Short panels fit their contents, and all windows remain overlays over a fixed game viewport.
- The top backpack opens weapons inventory directly, with four category tabs, original item icons, separate details actions and close/return controls. Escape/Android Back returns directly to play instead of reopening the obsolete category hub. Original familiar/frozen restrictions remain.
- Trading uses original stock icons, prices and descriptions, a numeric offer field and explicit Offer/Cancel. All purchases, haggling outcomes, stock limits and gold changes still go through native handlers. Browser interaction verified typed offers and refusal without a gold deduction; native fixtures also cover acceptance and cancellation.
- Level completion and character statistics use readable value cards. On level completion, only Continue is actionable. Native resource fragments and shortened shop descriptions now translate completely; quantity suffixes and native check/ellipsis glyphs are preserved by the adapter.
- Upper-left encounter overlay includes the actual look-at entity name and up to two current event lines. Names disappear when focus clears; the compact left modifier column and world rectangle remain independent.
- Conversation modals show the current speaker and original player/wand portraits. Other scripted speakers use their own native sprite closeups; attribution first follows the script's event tile and then the facing conversational entity. Original notice icons/titles are supported too. Dialogue progression and choices retain their native handlers.
- Verification: native/WASM replay passes 1140 checks/1058 ticks with exact Classic/full-frame and binary-save parity. New interaction fixtures cover direct inventory/tabs/Back, original offer acceptance/refusal/cancel, completion metrics, look-at names and speaker portraits. Responsive layout passes 114 menu cases (19 screens × RU/EN × three phone/desktop sizes) plus 24 modifier layouts. The 1690-record Russian catalog and assembled text checks pass. APK bridge/debug persistence and native lock/impact fixtures also pass.
- Remaining review: later multi-character scripted conversations, all merchant discount/story paths and device GPU behavior still need longer gameplay coverage; these checks do not constitute a whole-game playthrough.


## Equipped ring and adjustable light reach — 2026-11-05

- Recovered the native equipped checkmark in inventory rows. The core supports one active ring: HUD now reads that actual owned/equipped ring, its original 18×18 icon (BAR 3031), name and description. Its small badge shares the fixed lower row with weapon/ammo under health and armor; tapping it opens the rings tab directly. It is hidden until the player owns a ring, and remains inactive behind modals.
- Added persisted Settings → Lighting distance, 100–150% in 5% steps. 100% is the current look; 150% multiplies source radii by 1.5 while leaving source power and tight halos unchanged. The existing light budget and geometry shadows remain in use. This preference is separate from native game saves and render resolution.
- New isolated test selects all eight native rings, checks the native strength/health bonuses and checkmarks, removes a ring, and round-trips an equipped ring through native save/load. It also verifies visible range changes and endpoint clamping at Low/Medium/High, without changing simulation, Classic pixels or binary saves. Preference persistence is covered alongside the Android backup/debug bridge.

- Final local browser review covered phone and desktop pause/trade/inventory layouts, actual typed merchant input, ring selection/removal indicators and the HUD badge's direct return to the rings tab. Settings slider reached 150% through pointer input; player/wand portraits and speaker changes were visually checked. UI export now fails on reported Godot script errors instead of relying on its process exit status. No public-site browser/asset check was performed.


## Publication and private APK — 2026-11-05

- Pushed source b0db407589a41349a34572536c35e5ba7c4f0ad0 and replaced web-build with acd4a3d8de21ceeb7c3bed74a1207bc75e4b0974 at the same Pages root. No public asset downloads or live-site browser checks were made after publication.
- Private dist/Orcs-Elves-II-0.2.3-test.apk: versionCode 3, 17700298 bytes, SHA-256 23647047f6f6ce116b60370d5cddbdc9036b1bea19b95b6e305aa4fcd18b4519. It embeds source b0db407589a41349a34572536c35e5ba7c4f0ad0 and the exact normal Web bundle, with no QA functions. Signature/alignment and all embedded file hashes pass; the existing test signing key is retained. Actual-device testing remains with the user. No APK was uploaded publicly.
- Final visual evidence is kept locally in ignored build/proofs/final-ring-hud.png. The new ring badge, direct selection route, both speaker portraits and the 100–150% slider were verified in the local browser; layouts pass 114 menu, 24 ring-HUD and 24 modifier cases.


## Dense-light performance and clean focus names — 2026-11-06 (local only)

- Investigated the user’s slow Dark Forest (map 5) and Sol (map 6) scenes, using real source positions to find views with eight active lights. Added reproducible local survey/timing and exact-reference comparison tools; initial and optimized reports remain in ignored build artifacts.
- Accelerated unchanged native wall intersections with a uniform-grid broad phase. Retained the original exact line/height test and full-scan fallback. Static wall sample masks are cached separately per light, so billboard animation no longer forces all wall rays to be repeated. Geometry/light changes still invalidate those samples.
- Reused identical vertical shadow terms and held-weapon energy. At Medium/High, reused smooth incident light at one-world-unit intervals within the same frame and same receiver, with exact illumination close to emitters. Actual raster resolution, original textures, eight light slots, shadow softness, contact shade, source colors/power/range, flicker and arrival flashes are preserved.
- Desktop High/150% warm-pass medians: Dark Forest 63.8 → 32.2 ms, Sol 98.5 → 37.3 ms. Camera-motion Sol median 125.8 → 39.8 ms. These include high-resolution replay and describe this local CPU pass, not phone FPS. Detailed all-quality/reach data and design references are in docs/LIGHTING.md.
- Exact acceleration stage had identical hashes for 18 static and 144 motion frames. Final 18 static views differ by at most one RGB-channel value out of 255; Low is exact. Additional QA covers 144,000 full-scan/accelerated rays on original/changed walls and 288 advancing camera frames at all qualities/reach endpoints: max difference 1/255, worst scene RMSE 0.048/255. Native simulation/Classic/full-frame/binary-save parity still passes 1140 checks over 1058 ticks.
- Filtered original technical collision labels NONOBSPRITEWALL, SPRITEWALL and FAMILIAR_SPRITEWALL before localization; their Russian translations cannot appear as look-at names. Also follows the original HUD’s world/hidden-sign name exclusions. Named characters, enemies and usable items remain named. Six direct/fallback native-resource cases, normal focus and focus clearing pass.
- Existing candle geometry/extinguish/relight, native lock/impact, eight-ring bonuses/removal/save-load and 100–150% reach/purity fixtures pass. Normal core and Godot UI export succeed. No source/Pages publication or APK build was performed; the user explicitly requested local work.

- Follow-up: increased the complete preference/core/UI range to 100–250%, preserving existing selected values. Native and runtime overflow clamp to 250%; the real 150% and 250% outputs differ at all qualities, with model/Classic/save purity intact. Saved preference tests cover the prior 150% value and new 250% limit.
- Added conservative camera-neighborhood/view-cone source selection: nearby sources survive all look directions, while farther sources are tested by their influence disk, retaining off-screen spill onto visible geometry. Held light stays reserved. QA covers 64 near-source directions, excluded far disks and visible-receiver coverage; the exact-illumination sweep now passes 432 frames over 100/150/250% (max 2/255, Forest/Sol max 1/255).
- Built a separate original-algorithm control with only its clamp extended to 250%, then compared fixed original poses at High/250% in two alternating before/after runs. Both versions use eight active sources. Aggregate medians and raw frames are recorded locally in build/lighting-bench/maximum-summary.json and documented in docs/LIGHTING.md. Normal native/WASM parity, original lock/impact/candle behavior, ring selection and preference persistence remain verified. No publication or APK build was performed.
- Final local browser check verified the actual Settings slider at High 704×656 and 250%, then returned to Sol gameplay. Original look-at names and technical-name suppression, Forest/Sol lighting, actual camera input and Classic/Remastered switching were checked locally. Proofs: build/proofs/lighting-settings-250.png and lighting-sol-high-250.png. The isolated preview has persistence disabled; the normal updated build remains at 127.0.0.1:8782 with its existing save namespace.
- Follow-up startup fix: gameplay/navigation controls ignore mouse hover while booting and while inactive behind modals, preventing the deferred “Skip turn · Space” tooltip from appearing over startup/main-menu/dialog windows. Active-game input and accessible button names remain available.


## Three lighting-distance presets — 2026-11-06 (local preparation)

- Replaced the distance slider with a mutually exclusive Low / Medium / High button row. The internal radius multipliers are 1.5× / 2.5× / 5×; no numeric distance or percentage appears in the selector. Render-resolution quality remains an independent setting.
- Raised the core limit to 500 and the minimum/default to 150. Browser preferences select the nearest supported preset, preserving existing 150/250 choices and mapping older intermediate values consistently. Gameplay/save data is unchanged.
- Updated range/persistence/benchmark fixtures for the new presets, without running them. Per the user's request, this revision is prepared by compiling the normal Web core and exporting the UI; browser playtesting and new performance measurements are left to the user. Earlier measurements apply to their recorded settings and do not establish performance at the new High radius.
- Work remains local; no publication or APK build is part of this request.


## Corrected processing distance in walking steps — 2026-11-06

- The user clarified that Low/Medium/High must control how far the scene receives lighting work, with High reaching ten walking steps. Replaced the previous source-radius multipliers with 4/7/10-step processing presets (64 native world units per step).
- All source falloff radii now use the fixed former 120% appearance; source power is independent of the processing preset. The held weapon and environmental/missile/impact sources share this fixed reference. Flicker phase now follows source identity so changing the selected light list does not reset a retained source's flicker.
- Candidates conservatively intersect a finite view cone plus the surrounding three-step neighborhood. Off-screen/outside-center sources are retained when their influence overlaps the processing region. Removed the old source-radius-relative camera distance gate, which could incorrectly reject lights inside the requested ten-step view.
- Direct-light/shadow evaluation stops outside the processing radius; the final step fades into ambient. Contact shade and original art remain, while held-weapon presentation is kept independent of distant-receiver cutoff. Emitter halos respect the same processing boundary.
- The menu has Low/Medium/High only and calls the setting Lighting draw distance. A separate processing-steps preference migrates the old preset names consistently (150→Low, 250→Medium, 500→High); old numeric values are never treated as step counts.
- Prepared the normal local core/UI build. Per the user's continuing preference, no browser/gameplay/performance suite is run and no fresh performance/visual-equivalence claim is made. Existing test fixtures were updated for the clarified semantics; earlier timing tables belong to the former renderer settings. No publication or APK build.


## Approved Web publication — 2026-11-06

- The user approved publishing the prepared Web revision with optimized dense-scene lighting, clean focus labels, startup hover suppression and corrected 4/7/10-step processing presets at the fixed 120% appearance.
- Publication preflight confirms normal-core/UI export success, matching source/runtime bytes, matching content-hashed UI pack and no QA exports in the shipped WASM. The prepared artifact is used as-is. No new gameplay/performance/browser suite was run after the user's self-playtest preference.
- Publish source and the Web bundle to the existing repository/Pages root with the same save namespace and legacy redirect. Do not build/upload APK or download public assets for a redundant live-site check.
- Published source e2cd42c6b03e59e9c40869f10d3d8399785d6a41 and web-build 9829fbcc737b97e28282a4ef00c98be3bb3d9f79. GitHub Pages API reports this exact build as built without errors at the permanent root. All 12 release files match the prepared local bundle and manifest hashes. No public asset download/browser recheck or APK build was performed.


## Approved processing-distance adjustment — 2026-11-06

- At the user's request, Low/Medium/High are now 5/15/20 walking steps (320/960/1280 world units); the fixed former 120% source appearance and the surrounding three-step processing neighborhood are preserved.
- Preference persistence now stores the preset name, so future distance changes retain the selected level. Existing 4/7/10-step values and the older percentage values migrate by their Low/Medium/High identity.
- The user explicitly requested immediate Web publication of this adjustment at the permanent Pages root. Prepare the normal core/UI, verify release-package consistency, publish without extra browser/gameplay/performance suites, and do not build APK. Weapon PNG extraction is a separate local handoff artifact.

- Published the requested 5/15/20-step adjustment: source cc740a9a841054265ae6cb1fd9d6e50f722b37f5, web-build 64eee153fef1db49853d7808360d774cc30d9289. The normal bundle passed export/hash consistency checks; no new gameplay/performance/browser suite or APK build was run.

## Weapon artist handoff — 2026-11-06

- Exported every first-person weapon resource for all 11 native weapon definitions: 65 individual 88×88 RGBA frames/layers, 11 native 24×24 inventory icons and 17 reference shared-effect frames. Names and weapon IDs come from original BAR entity/string resources rather than generic upstream weapon comments.
- Original RGB565 colors, native bitshape alpha masks, untrimmed canvases and frame positions are preserved; all 65 held-weapon PNG pixel buffers match the existing original-resource catalog exactly. Fully opaque original inventory-icon backgrounds are retained rather than inventing transparency. Native additive/mixed glow layers are exported separately and described in the artist notes.
- Local dist/Orcs-Elves-II-Weapon-Art-Kit-BREW-7.0.41.zip contains the PNGs, a complete original-size contact sheet, Russian/English handoff notes and a machine-readable manifest with native sprite/frame/material/texel/palette IDs, anchor values, alias groups and checksums. ZIP CRC and all PNG dimensions/RGBA round trips pass. No art replacements are installed in the game; integration waits for the artist's returned files.
- Reproducible exporter: tools/export_weapon_art_kit.py, with an optional new --output folder. Existing handoff folders/ZIPs are protected. Derived art stays ignored and is not uploaded to the source or Pages branch.
- GitHub Pages API confirms web-build 64eee153fef1db49853d7808360d774cc30d9289 completed successfully without errors. The artist handoff ZIP is local only (524090 bytes, SHA-256 8b71f97abe34e457f6368b8bcdb25bc292a75405370df0de3a978d5818c5a364).


## Full-circle lighting and Flaming Sword alignment — 2026-11-06

- Removed all view-cone/angular filtering. Processing now uses the full Low/Medium/High radius (5/15/20 cells) around the controlled first-person position. Both source eligibility and receiver cutoff are independent of look direction. Sources whose falloff overlaps the circle remain eligible, including sources behind the player. Fixed former 120% source appearance and the last-cell distance fade remain.
- Added a small incumbent preference when selecting the bounded nearby-light set, reducing swaps between nearly equidistant sources during movement. Source-identity flicker phases remain stable. No simulation/RNG/save behavior changed.
- Found the Flaming Sword error in the reconstructed Combat_drawWeapon idle branch: screenX subtracts the 32-unit weapon-space center, while its caller already supplied wx−19. The original BREW ARM at 0x130f0..0x13100 uses center + scale×(wx−19) directly. Passing wx+13 through screenX restores that formula. The original vertical offset and attack frame are retained; raw sprite art is unchanged.
- Isolated before/after fixtures cover all four idle fire frames and attack rendering at all three resolutions (24 cases), with Native ARM-derived expected anchors and pure image getters. The old build fails horizontal alignment; the corrected build passes. A second 24-case fixture renders in an actual dungeon; native/High visual outputs confirm the flame sits on the blade.
- Circular-region/source-selection checks cover 144 look directions across maps 4/5/6 and every radius preset, with identical selected source IDs through each full turn and pure getters. Original wall-candle geometry/extinguish behavior is also checked as a focused regression. No broad gameplay/performance/browser suite or user-save interaction is part of this change.
- Local evidence: build/sword-alignment/{before,after}/idle-0-q0.png, build/sword-alignment/after-scene/idle-high-scene.png, build/sword-alignment/after/report.json and build/radius-lighting-check.json. The user explicitly requested Web publication after completion; no APK build is requested.
- Published source 66fd4d0321c060108f24a6a89f9ff3247c70256a and web-build 6489a5fd84cce3954226fed5ce6c9679dc7a053f at the permanent Pages root. GitHub Pages API confirms this exact build completed successfully. All 12 deployed files match the checked local bundle. No public-page/asset recheck, live-user-game interaction or APK build was performed.


## Dynamic GPU lighting investigation — 2026-11-06 (local)

- The user reported 14 FPS / 59 ms Remastered at Medium on an iPhone, requested investigation in Gnomgrad and other locations, and suggested the video accelerator. Built a deferred GPU browser path on Godot's existing WebGL 2 context. C retains native rasterization and cached three-ray wall/door/silhouette visibility; the GPU computes per-pixel source falloff/facing/color, shadow interpolation, contact shade, tone mapping, haze and halos. Original art, actual quality resolutions, 5/15/20-cell circle and the 120% source appearance remain.
- Byte textures avoid mobile float-target extensions. Receiver axes/planes are retained at diagonal/cell boundaries; source-relative sub-unit coordinates preserve sharp light response very close to emissive billboards. Visibility/contact atlases upload only when their contents change. There is no offline level-variant compilation or gameplay GPU readback. The CPU renderer is retained via ?lighting=cpu, and allocation/input failure falls back to it.
- All seven original maps surveyed at eight reachable-cell poses, all three distances and qualities: 504 comparisons. Getters preserve the exposed game model and canonical pixels. Maximum old/new RGB-channel difference 10/255; worst aggregate RMSE 0.168/255. This is a documented quantized presentation approximation, not exact Remastered pixel parity. Lamp extinguish/relight and arrival-flash inclusion/expiry pass at every quality.
- Real native move/turn inputs in disposable Dwarkar games, including the introductory cinematic, produced the same 1254-frame routes and events in separate CPU and GPU runs (AI disabled to isolate the renderer). Medium: 18 successful steps, 24 visited cells; High: 20 steps, 22 visited cells. CPU pass medians 8.49/26.25 ms versus GPU preparation 3.94/14.96 ms; p95 11.78/33.06 versus 5.07/15.57 ms. These preparation figures exclude GPU draw and Godot transfer and are not phone FPS predictions.
- The shared actual GLSL pass was executed on WebGL/ANGLE Metal (Apple M1) in 15 static fixtures, including the near-emitter case. It matches the independent C byte-contract decoder within 1/255 and has no browser/GL errors. No readback is used outside these QA fixtures.
- An isolated Godot browser game verifies texture/uniform wiring, CPU fallback and Classic/Remastered toggles with unchanged player pose. Final full preparation/transport medians in the same Dwarkar view: Medium 10.27 → 7.18 ms; High 24.99 → 16.88 ms. Both backends reached the desktop UI refresh limit, so no desktop FPS multiplier or iPhone FPS claim is made. GPU is identified in the debug overlay; its timing is preparation, not GPU execution time.
- Normal core, native driver and Godot Web export pass. Native/WASM replay still has 1140 exact model/Classic comparisons over 1058 ticks and byte-identical SD_0–SD_3. New GPU getters at all qualities preserve model, canonical pixels and saved bytes. The normal bundle has a matching core/runtime/UI content tag and no QA exports.
- Evidence is local under build/gpu-check/ (report.json, browser-report.json, ui-report.json, walk-cpu-report.json, walk-gpu-report.json and screenshots), build/gpu-parity.log and build/parity-result.json. QA browser games use disposable profiles/origins and persist:false; the user's live game and saves were not opened/reloaded. This new revision remains local, with no source/Pages publication or APK build.


## Approved GPU-lighting Web publication

- The user explicitly requested publishing the completed local GPU revision. Release preflight confirms matching core/runtime/UI tag 5c22dc5225a3a140 and no QA exports. Existing local evidence remains valid: 504 all-map comparisons, 15 actual WebGL fixtures, separate matching Dwarkar walks, isolated Godot wiring/toggles and 1140 native/WASM parity checks with exact saved bytes.
- Publish the prepared source and normal Web bundle to the existing permanent Pages root. No new gameplay/browser suites, public asset downloads, live-user-game interaction or APK build are required.

## macOS native SDL2 build and PlayStation Vita port — 2026-11-11 (local)

- Updated `engine/CMakeLists.txt` with macOS fallback targets for system `zlib`, Homebrew `SDL2`, and `libavcodec`/`libavutil` when `pkg-config` is unavailable, and wired `vita/standalone_bridge.c` + `host/lighting.c` + `host/presentation.c` into the native macOS `oe2` target (`F2` toggles Remastered/Classic, `F3` cycles lighting range).
- Added PlayStation Vita port in `vita/` (`vita/main_vita.c`, `vita/platform_vita.c`, `vita/standalone_bridge.c`, `vita/pmd_vita.c`, `vita/README.md`) and builder `tools/build_vita.py`.
- The Vita port uses `SceCtrl` dual-stick + button polling (`Select` toggles Remastered/Classic, `L + Select` cycles lighting range, `R + Select` toggles integer scaling, D-pad adjusts merchant offers in menu `0x135`), pre-decoded 8 kHz PCM audio from `app0:gamedata/audio/`, read-only BAR/MOD assets from `app0:gamedata/`, and persistent saves in `ux0:data/oe2/`.
- Built `dist/Orcs-Elves-II-Vita.vpk` (6,749,695 bytes, SHA-256 `55eb0fd843a67ab698960fcf7ac2a44c9eec644a33841f324b2490cf6a40be49`, Title ID `ORCS00002`) with original-art LiveArea assets (`icon0.png`, `bg.png`, `startup.png`) and all 115 pre-decoded audio clips bundled.
- Optimized PS Vita and standalone lighting/frame pacing (`host/lighting.c`, `vita/standalone_bridge.c`, `vita/main_vita.c`, `engine/src/main.c`, `tools/build_vita.py`):
  - Fixed timer drift where `nextTick` was scheduled after `Applet_Tick` + VSync instead of relative to the tick start, and added a toggleable fast-tick mode (`R + Start` on Vita, `F4` on desktop).
  - Replaced full-grid 393 KB `checked` and 49 KB `wallSampleReady` per-step `memset`s with per-light cache hashes (`lightCacheHash[8]`) and sparse dirty-cell clearing so moving the held weapon light no longer invalidates world light shadow caches or zeroes untouched memory.
  - Accelerated `wallDistanceAt` by querying the 64×64 spatial `traceHead` grid within a 40.5-unit neighborhood instead of iterating over all ~600 map lines per newly uncovered 16×16 floor/ceiling/wall cell (~100× fewer line-segment projections during turns and door animations).
  - Skipped redundant shadow-grid tracing and lookups for the camera-held weapon light (`key == 2000`), compacted active shadow-casting sprites into `activeCasterList`, and used single center-ray visibility per 16×16×8 cell on standalone builds while retaining trilinear soft-shadow interpolation.
  - Replaced 24-iteration linear emitter/caster table scans with O(1) lookup tables (`sourceByTile[256]`, `casterEligibleTile[256]`) and cached normalized wall normals across columns of the same line segment in `BrewLight_span`.
  - Restricted per-frame `shadowColumns`, `cornerCache`, and `floorDirections` zeroing to the active viewport dimensions (`176x164` instead of `704x880`), added 16×16 floor/ceiling cell caches for `shadow` and `roomDistance`, expanded native `176x164` surface-aware irradiance interpolation to 4×2 horizontal spans (`compatibleSpan4`), folded highlight rolloff + `sqrtf` into a single `toneLUT[4097]` to eliminate 3 floating-point divisions per pixel across all 28,864 pixels, precomputed `lut565ToRgba`, set default Vita light range to `5` steps (`LIGHT: LOW`), and enabled `-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -ftree-vectorize -ffast-math -funroll-loops` on Vita lighting units.
- Verified native `arm64` compilation and headless startup/cinematic/first-dungeon smoke runs on macOS (400-tick headless benchmark improved from ~5.72 ms/tick originally -> 1.56 ms/tick -> 0.73 ms/tick). Physical PS Vita / Vita3K controller playtesting remains with the user.

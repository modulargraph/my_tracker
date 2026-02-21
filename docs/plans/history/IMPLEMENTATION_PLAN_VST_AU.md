# VST/AU + Multi-Note-Lane + Automation Implementation Plan (for Claude Code)

## Execution Directive (Mandatory)
- Use **Opus subagents only** for all delegated tasks.
- Do **not** use Sonnet/Haiku or fallback models.
- Use separate worktrees and branches (prefix `codex/`) per major stream.
- Merge in dependency order (defined below), run tests after each merge, and only then continue.

## What This Plan Solves
- VST effect inserts in mixer (with `+` button and rows), placed between compressor and sends.
- VST/AU instrument support (non-sample instruments).
- Instrument editor/type behavior for plugin instruments (embedded editor when possible; otherwise popup window).
- Audio settings dialog (output/sample-rate/block/device; no input handling required).
- Plugin scan path settings + scan persistence across restart/rebuild.
- Multiple note lanes per track (implemented before VST instrument workflow).
- Instrument ownership rule: plugin instrument is owned by one track (with multiple note lanes allowed on that track).
- Red status text for ownership violations (no beeps).
- New bottom automation panel for plugin parameters with point drawing + interpolation, modulation-first workflow.
- Constraint: automation and notes for a plugin instrument must not live on different tracks.

## Confirmed Product Decisions (from user)
- Plugin instrument ownership is **per track** (not per track+lane).
- A track can have multiple note lanes, and note/release handling is **per note lane**.
- Max limits: **8 note lanes** per track, **8 insert slots** per track.
- Plugin instrument UI preference: **embedded first**, popup fallback when embedding is not viable.
- Automation is **pattern-scoped**:
  - different patterns can have different automation
  - duplicated/cloned patterns must clone automation data too
  - when switching to a new pattern, automation modulation is reset (baseline/default behavior)
- Track content mode is exclusive:
  - a track is either **sample track** or **plugin-instrument track**
  - mixed sample + plugin note playback on the same track is out of scope for v1.
- Multiple VST/AU instruments on the same owning track are required.
- Multiple VST/AU instruments on one track use per-note-lane/per-row instrument selection (existing instrument-column semantics), allowing simultaneous playback across lanes.

---

## Current Architecture Constraints You Must Respect
- App currently disables plugin hosting:
  - `JUCE_PLUGINHOST_AU=0`
  - `JUCE_PLUGINHOST_VST3=0`
  - in `/Users/mhidding/Desktop/my_tracker/CMakeLists.txt`
- Track audio chain today:
  - `TrackerSamplerPlugin -> InstrumentEffectsPlugin -> MixerPlugin`
  - `MixerPlugin` currently does `EQ -> Compressor -> Sends -> Volume/Pan` internally.
- Pattern model currently supports:
  - one note lane per track cell (`note/instrument/volume`)
  - multiple FX lanes per track (already implemented).
- Project serialization is custom XML via:
  - `/Users/mhidding/Desktop/my_tracker/src/ui/ProjectSerializer.cpp`
- Tracktion Engine plugin metadata persistence already exists:
  - `PluginManager.knownPluginList` is stored in Tracktion property storage (`Settings.xml` under app data).

Implication:
- You cannot insert third-party effects **between compressor and sends** while everything remains in one `MixerPlugin`.
- Must split or refactor channel processing so insert point exists at plugin-chain level.

---

## High-Level Technical Decisions

## D1. Multiple note lanes first
- Implement note lanes before VST instrument ownership/automation.
- Keep FX lane system unchanged.

## D2. Enable both AU + VST3 on macOS
- On macOS build, enable:
  - `JUCE_PLUGINHOST_AU=1`
  - `JUCE_PLUGINHOST_VST3=1`
- Keep tests/build targets stable (if plugin-hosting complicates tests, isolate host-only paths from unit tests).

## D3. Effect insert placement
- Split mixer DSP responsibilities:
  - `ChannelStripPlugin` (EQ + Compressor)
  - `TrackOutputPlugin` (Sends + Pan + Volume + Meter)
- Chain target:
  - `Instrument source -> InstrumentEffectsPlugin -> ChannelStripPlugin -> [External Insert Plugins...] -> TrackOutputPlugin`
- This gives exact “between compressor and sends” behavior.

## D4. Plugin instrument routing model (v1)
- Bind each plugin instrument to one owning track.
- Notes for that plugin instrument on other tracks are rejected with red status text.
- Multiple plugin instruments may be owned by the same track.
- Track mode is exclusive (sample-only or plugin-instrument-only).

## D5. Automation model
- Store automation points as modulation-oriented values.
- At sync time, resolve modulation against baseline parameter value into target parameter automation curves.
- Enforce ownership consistency:
  - plugin notes + plugin automation must share owning track.
- Automation is stored per pattern, duplicated with pattern clone, and reset to baseline/default when pattern changes.

---

## Worktree and Agent Orchestration

## Worktrees
- Main integration worktree: `codex/vst-au-integration`
- Feature worktrees:
  - `codex/note-lanes`
  - `codex/plugin-host-settings`
  - `codex/mixer-inserts`
  - `codex/instrument-binding`
  - `codex/plugin-automation`

## Agent assignment (Opus only)
1. Agent A (Opus): Multi-note-lane data/model/grid/engine
2. Agent B (Opus): Plugin host + settings dialog + scanning + persistence
3. Agent C (Opus): Mixer insert architecture + UI rows/+ button
4. Agent D (Opus): Plugin instrument binding rules + Instrument tab behavior
5. Agent E (Opus): Automation panel + modulation mapping + ownership enforcement
6. Agent F (Opus, reviewer): Regression pass + serializer migration + QA checklist execution

## Parallelization rules
- A and B can run in parallel.
- C depends on B.
- D depends on A + B.
- E depends on C + D (+ A).
- F runs after all merges.

## Merge sequence
1. Merge A
2. Merge B
3. Resolve shared conflicts in:
   - `MainComponent.*`
   - `TrackerEngine.*`
   - `ProjectSerializer.*`
4. Merge C
5. Merge D
6. Merge E
7. Run full build/tests/manual smoke
8. Apply F findings

---

## Phase-by-Phase Plan

## Phase 0: Preflight
Goal:
- Stabilize baseline and create scaffolding for safe incremental changes.

Tasks:
- Build and run existing tests.
- Capture baseline behavior list (transport, sample loading, mixer, save/load).
- Add feature flags:
  - `kEnablePluginHosting`
  - `kEnableNoteLanes`
  - `kEnablePluginAutomationPanel`
- Add temporary dev logs for plugin scan and plugin-load failures.

Deliverables:
- Green baseline build.
- Baseline QA notes.

---

## Phase 1 (Agent A): Multiple Note Lanes Per Track (must ship before VST instruments)
Goal:
- Add multi-note-lane pattern model, editor, playback conversion, and persistence.

Primary files:
- `/Users/mhidding/Desktop/my_tracker/src/data/PatternData.h`
- `/Users/mhidding/Desktop/my_tracker/src/data/PatternData.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/data/TrackLayout.h`
- `/Users/mhidding/Desktop/my_tracker/src/ui/TrackerGrid.h`
- `/Users/mhidding/Desktop/my_tracker/src/ui/TrackerGrid.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/audio/TrackerEngine.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/ui/ProjectSerializer.cpp`
- `/Users/mhidding/Desktop/my_tracker/tests/TrackerAdjustTests.cpp`

Detailed implementation:
1. Extend track layout with note lane counts:
   - add `trackNoteLaneCounts[kNumTracks]`, default `1`, min/max clamp (suggest 1..8).
   - mirror existing FX lane count API style.
2. Extend `Cell` note representation:
   - add lane-aware note accessors:
     - `getNoteLane(laneIndex)`, `ensureNoteLanes(count)`, `getNumNoteLanes()`.
   - keep backward compatibility with current single-lane save format.
3. Tracker grid:
   - repeat `NOTE/INST/VOL` subcolumns per note lane.
   - add note-lane cursor index.
   - update tab/shift-tab and hit testing for note lanes + FX lanes.
   - add track header menu actions:
     - `Add Note Lane`
     - `Remove Note Lane`
4. Pattern-to-MIDI conversion:
   - iterate note lanes and emit events per lane state.
   - maintain lane-specific `lastPlayingNote`/state.
   - preserve current OFF/KILL semantics lane-wise.
5. Serialization:
   - bump project version.
   - write/read note lane counts and lane payload.
   - backward compatibility: old projects load into lane 0.
6. Unit tests:
   - roundtrip note-lane serialization.
   - MIDI generation sanity for multiple lanes.

Acceptance criteria:
- Multiple note lanes visible/editable.
- No regression in existing single-lane project files.
- Playback still stable for samples and FX lanes.

---

## Phase 2 (Agent B): Plugin Host + Audio/Plugin Settings + Scan Persistence
Goal:
- Turn on AU/VST3 hosting, add settings UI for audio output + scan paths, and persist scan data.

Primary files:
- `/Users/mhidding/Desktop/my_tracker/CMakeLists.txt`
- `/Users/mhidding/Desktop/my_tracker/src/audio/TrackerEngine.h`
- `/Users/mhidding/Desktop/my_tracker/src/audio/TrackerEngine.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/ui/MainComponent.h`
- `/Users/mhidding/Desktop/my_tracker/src/ui/MainComponent.cpp`
- new:
  - `/Users/mhidding/Desktop/my_tracker/src/ui/AudioPluginSettingsComponent.h`
  - `/Users/mhidding/Desktop/my_tracker/src/ui/AudioPluginSettingsComponent.cpp`
  - `/Users/mhidding/Desktop/my_tracker/src/audio/PluginCatalogService.h`
  - `/Users/mhidding/Desktop/my_tracker/src/audio/PluginCatalogService.cpp`

Detailed implementation:
1. Enable hosting in app target:
   - AU + VST3 host macros to `1` (macOS build).
2. Add plugin catalog service around Tracktion plugin manager:
   - read from `engine.getPluginManager().knownPluginList`.
   - expose filtered lists:
     - effects
     - instruments/synths
     - by format (`VST3`, `AudioUnit`).
3. Settings dialog:
   - Audio output section using `AudioDeviceSelectorComponent` with input disabled.
   - Plugin section:
     - scan paths list (editable)
     - scan/rescan button
     - discovered plugin list
   - no audio input controls.
4. Default macOS paths:
   - `~/Library/Audio/Plug-Ins/VST3`
   - `/Library/Audio/Plug-Ins/VST3`
   - (AU does not require custom path selection but include AU scan in supported formats).
5. Persistence:
   - rely on Tracktion KnownPluginList persistence for plugin metadata.
   - persist custom scan path list in app prefs.
6. Wire menu action:
   - add “Audio & Plugins Settings…” from main menu.

Acceptance criteria:
- User can choose output device/sample-rate/block size.
- User can edit plugin scan paths and rescan.
- Scan results survive restart.

---

## Phase 3 (Agent C): Mixer Insert Slots Between Compressor and Sends
Goal:
- Add per-track insert slots with `+` button and row display in mixer view in correct signal location.

Primary files:
- `/Users/mhidding/Desktop/my_tracker/src/audio/MixerPlugin.h`
- `/Users/mhidding/Desktop/my_tracker/src/audio/MixerPlugin.cpp`
- new:
  - `/Users/mhidding/Desktop/my_tracker/src/audio/ChannelStripPlugin.h`
  - `/Users/mhidding/Desktop/my_tracker/src/audio/ChannelStripPlugin.cpp`
  - `/Users/mhidding/Desktop/my_tracker/src/audio/TrackOutputPlugin.h`
  - `/Users/mhidding/Desktop/my_tracker/src/audio/TrackOutputPlugin.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/audio/TrackerEngine.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/ui/MixerComponent.h`
- `/Users/mhidding/Desktop/my_tracker/src/ui/MixerComponent.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/ui/ProjectSerializer.cpp`

Detailed implementation:
1. Refactor mixer DSP chain:
   - migrate EQ+Comp to `ChannelStripPlugin`.
   - migrate Sends+Pan+Volume+Meter to `TrackOutputPlugin`.
2. Track plugin chain assembly:
   - fixed order:
     - sampler/instrument source
     - instrument FX
     - channel strip
     - variable external insert plugins
     - track output
3. Insert state model:
   - add per-track insert slot state with plugin descriptor + plugin ValueTree snapshot.
4. Mixer UI:
   - add `INSERTS` section between COMP and SEND.
   - show rows (name/bypass/open/remove).
   - `+` opens plugin picker from scanned effects list.
5. Plugin editor open action:
   - use plugin window state for popup editor.

Acceptance criteria:
- Inserts appear in mixer strips with `+`.
- Signal flow is objectively between compressor and sends.
- Insert plugins open/close and survive project save/load.

---

## Phase 4 (Agent D): Plugin Instruments + Ownership Rules + Instrument Tabs
Goal:
- Add non-sample instruments and ownership constraints with clear UX feedback.

Primary files:
- `/Users/mhidding/Desktop/my_tracker/src/ui/InstrumentPanel.h`
- `/Users/mhidding/Desktop/my_tracker/src/ui/InstrumentPanel.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/ui/MainComponent.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/audio/TrackerEngine.h`
- `/Users/mhidding/Desktop/my_tracker/src/audio/TrackerEngine.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/ui/TrackerGrid.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/ui/SampleEditorComponent.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/ui/ProjectSerializer.cpp`

Detailed implementation:
1. Instrument source type:
   - each instrument slot can be:
     - sample
     - hosted plugin instrument
2. Instrument panel actions:
   - set/replace instrument plugin
   - clear instrument plugin
   - open instrument UI
3. Ownership rule:
   - plugin instrument has one owning track.
   - reject note entry for that instrument on another track.
   - show bottom status text in red (no alert beep).
   - allow multiple plugin instruments to share that owning track.
4. Track mode rule:
   - track mode is `Sample` or `PluginInstrument`.
   - block illegal note entry/routing that mixes both modes on one track.
   - surface clear red status message when blocked.
5. Status system:
   - add temporary status message override with severity colors.
   - timeout restore to normal transport status.
6. Instrument Edit / Instrument Type tabs:
   - if sample instrument: existing sample editor behavior.
   - if plugin instrument: embedded editor first; fallback “Open Plugin Window” popup.
7. Playback routing:
   - ensure plugin instrument notes route to owning track synth plugin.
   - enforce no split-note ownership mismatch.
   - preserve per-note-lane OFF/KILL/release semantics.

Acceptance criteria:
- User can pick AU/VST instrument per instrument slot.
- Notes for plugin instrument are blocked on non-owner tracks with red status text.
- Instrument tab behavior is useful for plugin instruments (embedded or popup).

---

## Phase 5 (Agent E): Bottom Automation Panel (Modulation-first)
Goal:
- Add per-pattern automation drawing for plugin parameters with interpolation and ownership checks.

Primary files:
- new:
  - `/Users/mhidding/Desktop/my_tracker/src/data/PluginAutomationData.h`
  - `/Users/mhidding/Desktop/my_tracker/src/ui/PluginAutomationComponent.h`
  - `/Users/mhidding/Desktop/my_tracker/src/ui/PluginAutomationComponent.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/ui/MainComponent.h`
- `/Users/mhidding/Desktop/my_tracker/src/ui/MainComponent.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/audio/TrackerEngine.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/data/PatternData.h`
- `/Users/mhidding/Desktop/my_tracker/src/ui/ProjectSerializer.cpp`

Detailed implementation:
1. Data model:
   - automation lane contains:
     - target plugin instance ID
     - target parameter ID
     - owning track
     - modulation points (row/value/curve).
2. UI panel placement:
   - add component above bottom status row in tracker tab.
   - left controls:
     - plugin dropdown (current track instrument plugin + track insert plugins)
     - parameter dropdown (automatable params of selected plugin)
   - right graph:
     - x-axis follows pattern length
     - y-axis follows parameter value range
     - draw/edit points with linear interpolation.
   - show baseline (current explicit parameter value).
3. Engine sync:
   - map modulation points to target parameter automation curve per sync.
   - clear/rebuild parameter curve deterministically from pattern data.
   - on pattern switch, reset modulation contribution so baseline/default parameter value is restored unless the new pattern has automation.
4. Ownership constraints:
   - do not allow plugin automation to target a track different from plugin-note owner.
   - if moving notes to another track while automation exists:
     - prompt: “Automation still assigned to track XX. Clear automation?”
     - block until resolved.
5. Pattern operations:
   - duplicate/clone pattern must clone automation lanes and points.
   - serializer migration must preserve this behavior for new project versions.

Acceptance criteria:
- Automation panel works end-to-end.
- Curves interpolate audibly.
- Ownership constraints prevent split-track automation/notes.

---

## Phase 6 (Agent F): Stabilization, Migration, QA
Goal:
- Eliminate regressions and lock down migration behavior.

Tasks:
1. Serializer/version migration:
   - verify old project files load safely.
   - verify new project files roundtrip:
     - note lanes
     - inserts
     - plugin instruments
     - automation lanes
2. Regression tests:
   - add focused logic tests for:
     - ownership checks
     - note-lane serialization
     - automation mapping from rows -> curve points
3. Manual QA matrix:
   - scan paths
   - add/remove inserts
   - plugin editor open/close
   - assign plugin instrument and enforce owner track
   - move notes + automation conflict prompt
   - project save/load/reopen
   - song mode playback with automation.

Release gate:
- No crashes during plugin scan/load, chain edits, save/load.
- Existing sample-only workflow unchanged for non-plugin projects.

---

## Integration Hotspots (expect conflicts here)
- `/Users/mhidding/Desktop/my_tracker/src/ui/MainComponent.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/audio/TrackerEngine.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/ui/ProjectSerializer.cpp`
- `/Users/mhidding/Desktop/my_tracker/src/ui/TrackerGrid.cpp`
- `/Users/mhidding/Desktop/my_tracker/CMakeLists.txt`

When merging:
- Resolve data-model migrations first.
- Then engine routing.
- Then UI wiring.
- Re-run tests after each hotspot resolution.

---

## Risk Register + Mitigations
- Risk: Third-party plugin scan/load crashes.
  - Mitigation: keep out-of-process scan enabled; robust error status; skip bad plugins.
- Risk: Insert placement wrong due chain order mistakes.
  - Mitigation: explicit chain builder function + debug chain printout.
- Risk: Serialization breakage.
  - Mitigation: version bump + backward parser + roundtrip tests.
- Risk: UI complexity in TrackerGrid with note lanes.
  - Mitigation: land model + hit-testing first, then rendering polish.
- Risk: Plugin UI embedding instability.
  - Mitigation: popup window fallback always available.

---

## Suggested Commit Plan
1. `feat: add multi-note-lane model and serialization`
2. `feat: tracker grid note-lane editing and navigation`
3. `feat: enable AU/VST3 hosting and plugin scan settings dialog`
4. `refactor: split mixer dsp into channel strip and track output`
5. `feat: mixer insert slots and external effect hosting`
6. `feat: plugin instruments with track ownership enforcement`
7. `feat: bottom plugin automation panel (modulation curves)`
8. `test: add regression tests for serialization and ownership rules`
9. `chore: docs and qa checklist`

---

## Final instruction for Claude Code run
- Execute in the phase order above.
- Use Opus subagents only.
- Keep PRs/commits small per phase.
- Don’t start automation panel work until note lanes + hosting + inserts + instrument ownership are merged and stable.

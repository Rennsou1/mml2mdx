# mml2mdx MML Syntax Manual

> **mml2mdx** — MML compiler for MXDRV2, compatible with NOTE v0.85  
> Compiles MML (Music Macro Language) source files into MDX format

---

## MML Structure

### File Header

An MML file consists of `#` directives (pseudo-commands) and channel MML data. The header typically includes a title and an optional PCM file reference:

```mml
#title "Song Name"             ; Title (required)
#pcmfile "drums.pdx"           ; PDX file (required for PCM channels)
```

A complete MML file example:

```mml
#title "demo"
#pcmfile "drums.pdx"

@1={                                       ; Voice definition
 31,  0,  0, 15,  0,  0, 0, 1, 0, 0, 0,   ; OP1
 31,  0,  0, 15,  0,  0, 0, 1, 0, 0, 0,   ; OP2
 31,  0,  0, 15,  0,  0, 0, 1, 0, 0, 0,   ; OP3
 31,  0,  0, 15,  0,  0, 0, 1, 0, 0, 0,   ; OP4
  0,  7, 15                                ; CON, FL, OP-mask
}

A t120@1v15l4o4 cdefgab>c                  ; Channel A
B      @1v12l4o3 efgab>cde                 ; Channel B
```

### Voice Definition

FM voices are defined using `@N={...}` syntax, containing **47 numeric values**:

```mml
@<voice_number>={
  AR, DR, SR, RR, SL, OL, KS, ML, DT1, DT2, AME,   ; OP1 (M1)
  AR, DR, SR, RR, SL, OL, KS, ML, DT1, DT2, AME,   ; OP2 (C1)
  AR, DR, SR, RR, SL, OL, KS, ML, DT1, DT2, AME,   ; OP3 (M2)
  AR, DR, SR, RR, SL, OL, KS, ML, DT1, DT2, AME,   ; OP4 (C2)
  CON, FL, OP
}
```

| Parameter | Description | Range |
|-----------|-------------|-------|
| AR | Attack Rate | 0-31 |
| DR | Decay Rate | 0-31 |
| SR | Sustain Rate | 0-31 |
| RR | Release Rate | 0-15 |
| SL | Sustain Level | 0-15 |
| OL | Output Level (Total Level) | 0-127 |
| KS | Key Scaling | 0-3 |
| ML | Multiplier | 0-15 |
| DT1 | Detune 1 | 0-7 |
| DT2 | Detune 2 | 0-3 |
| AME | AM Enable | 0-1 |
| CON | Connection (Algorithm) | 0-7 |
| FL | Feedback Level | 0-7 |
| OP | Operator Mask | 0-15 (bit0-3 = OP1-4) |

#### Voice Macro

After the 47 parameters, you may append MML commands that execute automatically when switching to this voice:

```mml
@5={
 ...47 parameters...,
 MP1,4,20 MD5       ; Voice macro: auto-execute MP and MD when switching to @5
}
```

Use `SMON` / `SMOF` to control voice macro expansion:

```mml
SMON                ; Enable voice macro expansion (default)
SMOF                ; Disable voice macro expansion
```

### Channel Identifiers

| Channel | Description |
|---------|-------------|
| `A` ~ `H` | FM channels 1-8 (OPM synthesis) |
| `P` ~ `W` | PCM/ADPCM channels 1-8 |

- **Multi-channel prefix**: `ABC` means channels A, B, and C share the same MML line
- **Channel split**: Use `|...:...|` to assign different content to each channel in a multi-channel line

```mml
ABC |cdef:efga:gab>c|4    ; A=cdef, B=efga, C=gab>c (shared duration 4)
```

### Pages ★

The page feature allows splitting a single channel's MML into multiple "pages". Higher-priority pages take precedence when playing notes; when a higher-priority page is resting, the lower-priority page's output is used instead.

**Syntax**: Append `_` after the channel identifier for lower-priority pages.

| Identifier | Description |
|------------|-------------|
| `A` | Page 0 (highest priority) |
| `A_` | Page 1 |
| `A__` | Page 2 (and so on) |

**Switching rules**:
- At each tick, the highest-priority page that is **playing a note** is selected
- On page switch, voice (@), volume (v/@v), and pan (p) commands are auto-inserted
- All pages advance in parallel regardless of which is active

**Example**:

```mml
; Page 0: high priority — plays first half, rests second half
A  @1v15l8o4 cder rrrr

; Page 1: low priority — rests first half, plays second half
A_ @1v10l8o3 rrrr cder
```

After merging, equivalent to:

```mml
A @1v15l8o4 cder v10o3 cder
```

> [!WARNING]
> Page merging **unrolls all `[]` loops**. The merged channel will not contain loop instructions.
> The `L` (infinite loop) marker is taken from Page 0's position.

### PCM Frequency (F Values)

PCM channels use the `F` command to set the sample rate. See [PCM Setting.md](PCM%20Setting.md) for the complete table.

#### Basic ADPCM (without #ex-pcm)

| F Value | Sample Rate |
|:-------:|------------:|
| F0 | 3,906 Hz |
| F1 | 5,208 Hz |
| F2 | 7,812 Hz |
| F3 | 10,416 Hz |
| F4 | 15,625 Hz (default) |

#### PCM8A Extended (#ex-pcm)

| F Value | Format | Sample Rate |
|:-------:|--------|------------:|
| F5 | 16-bit PCM | 15,625 Hz |
| F6 | 8-bit PCM | 15,625 Hz |
| F7-F12 | Various | 20,833-31,250 Hz |

#### PCM8++ Extended

F7-F41 support various formats (16-bit/8-bit/ADPCM), sample rates (15.6k-48k), stereo, and variable frequency modes. See [PCM Setting.md](PCM%20Setting.md) for details.

### Comments

```mml
; End-of-line comment (semicolon to end of line)
// End-of-line comment (double-slash to end of line)
/* ... */ Block comment (only valid inside voice definitions)
```

---

## Pseudo-Commands (# Directives)

All `#` directives must appear at the beginning of a line (before channel MML data or on a standalone line).

### Basic Settings

| Directive | Description |
|-----------|-------------|
| `#title "name"` | Set song title (required) |
| `#pcmfile "file.pdx"` | Specify PCM data file |
| `#zenlen <value>` | Whole note tick count (default 192) |
| `#octave-rev` | Reverse `<` `>` direction |

### Transpose / Pitch

| Directive | Description |
|-----------|-------------|
| `#tps <value>` | Global transpose (semitones) |
| `#tps-all` | Apply `#tps` to all channels (default: channel A only) |
| `#detune <value>` | Global detune offset |
| `#toneofs <value>` | Voice number offset |
| `#flat "notes"` | Always flatten specified notes, e.g. `#flat "be"` |
| `#sharp "notes"` | Always sharpen specified notes |
| `#natural "notes"` | Cancel flat/sharp assignments |
| `#normal "notes"` | Same as `#natural` |

### Chord / Effect Control

| Directive | Description |
|-----------|-------------|
| `#coder` | Auto-pad chords with rests when notes < channels (min 1 per side) |
| `#ncoder` | No auto-padding (default) |
| `#glide [0\|1]` | Glide processing condition (0=process after `&` too, 1=first note only) |
| `#reste [0\|1]` | Wave effect handling on rests |
| `#nreste` | Disable wave effects on rests |
| `#cont [0\|1\|2]` | Wave phase continuation across `&` ties |
| `#wcmd [0\|1\|2]` | Suppress related commands during wave effects |
| `#wavemem` | Enable waveform memory mode |

### Compiler Options

| Directive | Description |
|-----------|-------------|
| `#noreturn` | Remove unused voice data from MDX output |
| `#overwrite` | Allow voice redefinition (later overrides earlier) |
| `#compress [0\|1]` | Duration compression (0=rests only, 1=notes too) |
| `#opt [flags]` | Optimization (`*`=all, `dvqpt012`=selective) |
| `#ex-pcm` | Enable PCM8/PCM8++ extended frequency modes |

### File Operations

| Directive | Description |
|-----------|-------------|
| `#include "file.mml"` | Include another MML file (recursive) |
| `#load-tone "file"` | Load binary voice data (overwrites current definitions) |
| `#load-wave "file"` | Load waveform data |
| `#save-tone ["file"]` | Export voice data (default: tone.bin) |
| `#save-wave ["file"]` | Export waveform data (default: wave.bin) |

### Debug / Utility

| Directive | Description |
|-----------|-------------|
| `#list` | Display MML listing |
| `#nlist` | Display note parsing listing |
| `#ver [0\|1]` | Verbose mode |
| `#beep` | Beep on error |
| `#remove` | Delete output MDX on error |
| `#pcmlist [0\|1]` | Output PCM usage map file |

---

## MML Commands

### Notes and Rests

| Command | Description |
|---------|-------------|
| `c` `d` `e` `f` `g` `a` `b` | Note names (C D E F G A B) |
| `c+` or `c#` | Sharp |
| `c-` | Flat |
| `c=` | Natural |
| `n<0-95>` | Direct note number |
| `r` | Rest |

### Duration

| Command | Description |
|---------|-------------|
| `l<1-64>` | Set default duration |
| `c4` `c8` `c16` ... | Note with duration (4=quarter, 8=eighth...) |
| `c4.` | Dotted (1.5× duration) |
| `c2..` | Double-dotted (1.75× duration) |
| `c%<ticks>` | Direct tick specification |
| `c4&c4` | Tie: durations add up, no re-attack |
| `c4&d4` | Legato: pitch changes without key-off |

Default whole note = 192 ticks (configurable via `#zenlen`)

| Duration | Ticks |
|----------|------:|
| `1` (whole) | 192 |
| `2` (half) | 96 |
| `4` (quarter) | 48 |
| `8` (eighth) | 24 |
| `16` | 12 |
| `32` | 6 |

### Octave

| Command | Description |
|---------|-------------|
| `o<-2~10>` | Set octave (default o4) |
| `>` | Octave +1 |
| `<` | Octave -1 |

### Tempo

| Command | Description |
|---------|-------------|
| `t<19-4882>` | BPM tempo |
| `@t<0-255>` | OPM Timer-B value |

### Voice

| Command | Description |
|---------|-------------|
| `@<0-255>` | Switch voice (FM) / PCM bank |
| `SMON` | Enable voice macro expansion (default) |
| `SMOF` | Disable voice macro expansion |

### Volume

| Command | Description |
|---------|-------------|
| `v<0-15>` | Set volume (16 levels) |
| `@v<0-127>` | Set volume (128 levels) |
| `x<0-15>` | Next note only temporary volume |
| `@x<0-127>` | Next note only temporary volume (128 levels) |
| `x+<n>` / `x-<n>` | Next note only relative volume change ★ |
| `(<n>)` | Decrease volume by n (default 1) |
| `)<n>` | Increase volume by n |
| `V<-127~127>` | Relative volume change (writes v/@v command) |
| `VO<-127~127>` | Global offset for all volume commands |

> ★ = mml2mdx extension

### Pan

| Command | Description |
|---------|-------------|
| `p0` | Mute |
| `p1` | Left |
| `p2` | Right |
| `p3` | Center (default) |

### Gate Time

| Command | Description |
|---------|-------------|
| `q<1-8>` | Gate time (8 = 100% sustain) |
| `@q<ticks>` | Tail mute tick count |
| `Q<percent>` | Percentage (Q100 = full sustain) |

### Pitch Control

| Command | Description |
|---------|-------------|
| `TR<-48~48>` | Channel transpose (default TR0) |
| `TTR<value>` | Relative transpose, additive to current TR ★ |
| `D<-32768~32767>` | Detune in 1/64 semitone units (default D0) |
| `$FLAT{notes}` | Always flatten specified notes |
| `$SHARP{notes}` | Always sharpen specified notes |
| `$NORMAL{notes}` | Cancel accidentals |
| `$NATURAL{notes}` | Same as `$NORMAL` |

### Loops

| Command | Description |
|---------|-------------|
| `[...]<count>` | Loop (default 2 if count omitted) |
| `/` | Exit loop on last iteration |
| `L` | Infinite loop start |
| `C` | Pseudo loop point (timing only) |

> [!NOTE]
> **Difference from original NOTE**: The original NOTE compiler restores the octave to its value at `[` when `]` is reached. For example, in `o4[cdefgab>c]2 de`, NOTE would play `de` at o4 after the loop ends. mml2mdx does **not** restore octave state — `>` and `<` changes inside the loop body carry over. In the above example, `de` would play at o5. Add an explicit `o` command after `]` if you need NOTE-compatible behavior.

### Tuplets

| Command | Description |
|---------|-------------|
| `{...}<duration>` | Tuplet: notes split the given duration evenly |

### Chords

| Command | Description |
|---------|-------------|
| `` `...` ``*`<dur>` | Distribution: one note per channel |
| ` `` ... `` `*`<dur>` | Sequential: all notes to current channel |
| `#<duration>` | Repeat previous chord |

> `｢｣` (Shift-JIS 0xA2/0xA3) is equivalent to backticks, kept for compatibility

### Portamento / Glide

| Command | Description |
|---------|-------------|
| `_<note>` | Portamento: slide from previous note to target |
| `_D<-6144~6144>` | Numeric portamento (1/64 semitone units) |
| `GL<speed>,<range>` | Glide processing |
| `GLON` / `GLOF` | Resume / Stop glide |

### Sync

| Command | Description |
|---------|-------------|
| `S<0-15>` or `S<A-W>` | Send sync (A=0, B=1, ..., H=7, P=8, ..., W=15) |
| `W` | Wait for sync signal |

### Misc

| Command | Description |
|---------|-------------|
| `w<0-31>` | OPM noise frequency (omit = stop) |
| `K` | Immediate key-off |
| `y<0-255>,<0-255>` | Direct OPM register write (supports `$FF` / `%11001100`) |
| `F<0-31>` | PCM sample rate (see [PCM Setting.md](PCM%20Setting.md)) |

### Hardware LFO

| Command | Description |
|---------|-------------|
| `MH<wave>,<freq>,<PMD>,<AMD>,<PMS>,<AMS>,<sync>` | 7-parameter setup (see table below) |
| `MHON` / `MHOF` | Resume / Stop |
| `MHR` | Force LFO phase reset |

| Parameter | Description | Range |
|-----------|-------------|-------|
| wave | 0=saw, 1=square, 2=triangle, 3=random | 0-3 |
| freq | LFO frequency | 0-255 |
| PMD | Pitch Modulation Depth | 0-127 |
| AMD | Amplitude Modulation Depth | 0-127 |
| PMS | PMD Sensitivity | 0-7 |
| AMS | AMD Sensitivity | 0-3 |
| sync | 0=free-run, 1=key-on sync | 0-1 |

### Software LFO

| Command | Description |
|---------|-------------|
| `MP<wave>,<period>,<depth>` | Pitch LFO (wave 0-3, +4=256× amplitude) |
| `MPON` / `MPOF` | Resume / Stop |
| `MD<depth>` | Change MP modulation depth |
| `MA<wave>,<period>,<depth>` | Amplitude LFO (same format as MP) |
| `MAON` / `MAOF` | Resume / Stop |

### Wave Effects

Wave effects are an advanced NOTE feature: they use predefined **data sequences** (waveform memory) to automatically control pan, pitch, volume, and other parameters in sync with the beat, enabling vibrato, auto-panning, and similar effects.

How it works: during compilation, the compiler splits long notes into shorter segments and automatically inserts the corresponding control commands (p / D / v / y) between them, simulating continuous parameter changes.

#### Step 1: Define Waveform Memory

Waveform memory is a numbered data table, defined with `*number={...}`:

```mml
*0={ 1, 0,   0, 10, 20, 30, 20, 10, 0, -10, -20, -30, -20, -10 }
;    │  │    └─ all remaining values are waveform data (read sequentially)
;    │  └─ loop start position (0 = loop from the first data point)
;    └─ loop enable (0=play once then stop, 1=loop forever)
```

This defines a 12-step sine wave: `0→10→20→30→20→10→0→-10→-20→-30→-20→-10`, looping.

#### Step 2: Bind Waveform to an Effect

| Effect | Purpose | Commands Inserted |
|--------|---------|-------------------|
| `AP` | Auto-panning | `p` commands |
| `DT` | Pitch vibrato | `D` detune commands |
| `TD` | Second pitch vibrato (stackable with DT) | `D` detune commands |
| `VM` | Volume envelope | `v` / `@v` commands |
| `MV` | Second volume envelope (stackable with VM) | `v` / `@v` commands |
| `KM` | Hardware LFO sensitivity change (requires MH) | PMS/AMS registers |
| `TL` | OP output level change (fine voice control) | OPM TL registers |
| `YA` | Arbitrary register writes | `y` commands |

Command format (AP shown as example; all other effects use the same format):

| Command | Description |
|---------|-------------|
| `AP<wave_num>,<steps>,<sync>` | Bind waveform and start effect |

- **wave_num**: the number from `*N` definition
- **steps**: how many ticks between each waveform data read
- **sync mode**: `0`=free-run (not synced to notes), `1`=restart on each key-on, `2`=free-run but continues after key-off

**Practical example** — auto-panning:

```mml
; Define panning waveform: left→center→right→center, looping
*0={ 1, 0,   1, 2, 3, 2, 1 }
;            p1  p2  p3  p2  p1 (left→right→left loop)

; Apply: switch pan every 12 ticks
A @1v15 AP0,12,1 l1 o4 cdef
; After compilation, equivalent to: auto-insert p1→p2→p3→p2→p1... every 12 ticks
```

#### Effect Sub-Commands

Each effect has matching sub-commands (replace `XX` with AP/DT/TD/VM/MV/KM/TL/YA):

| Command | Description |
|---------|-------------|
| `XXD<value>` | Delay: effect starts after N ticks (negative = count from key-off) |
| `XXS<value>` | Multiplier: waveform data × this value (scale effect intensity) |
| `XXL<value>` | Step change: modify read interval at runtime |
| `XXON` | Resume previously stopped effect |
| `XXOF` | Stop effect |

#### TL-Specific Sub-Commands

| Command | Description |
|---------|-------------|
| `TLM<0-15>` | OP mask (bit0-3=OP1-4, -1=auto-select modulator OPs) |
| `TLT<voice_num>` | Set reference voice (for modulator OP calculation, no actual switch) |

### Key Split

| Command | Description |
|---------|-------------|
| `KS<0-7>` | Auto-switch voice by pitch range |
| `KSON` / `KSOF` | Resume / Stop |

### Macros

| Command | Description |
|---------|-------------|
| `$X={...}` | Define macro ($A through $Z) |
| `$X` | Expand macro |

---

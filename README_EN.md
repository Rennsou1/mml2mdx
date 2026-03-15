[中文](README.md) | **English**

# mml2mdx

**MML compiler for MXDRV2** — compiles MML (Music Macro Language) source files into MDX format, compatible with NOTE v0.85.

MDX is the standard music format for the Sharp X68000 series, driven by the MXDRV/MXDRV2 sound driver.

## Build

Requires **CMake ≥ 3.20** and a **C++17** compiler (GCC/MinGW or MSVC).

```bash
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

The resulting `mml2mdx` executable will be in the `build/` directory.

## Usage

Supports both `.mml` and `.mus` file extensions. The extension can be omitted (auto-detected in `.mml` → `.mus` order).

**Drag & Drop**: Simply drag a `.mml` or `.mus` file onto `mml2mdx.exe` to compile it into a `.mdx` file with the same name.

**Command line**:

```
mml2mdx [switches] <file.mml>
```

### Switches

| Switch | Description |
|--------|-------------|
| `-m<size>` | MDX buffer size in KB (default: 64) |
| `-x` | Reverse octave `<` `>` direction |
| `-p` | Enable PCM8/PCM8++ extended mode |
| `-r` | Delete MDX file on error |
| `-i<channels>` | Channel mask (A-H, P-W) |
| `-b` | Beep on error |
| `-l` | Output PCM usage map (pcmuse.map) |
| `-c[n]` | Duration compression (n: include notes) |
| `-z[dvqpt012]` | Optimization flags |
| `-t[name]` | Save voice data (default: tone.bin) |
| `-w[name]` | Save waveform data (default: wave.bin) |
| `-v[0\|1]` | Verbose mode |
| `-1` | Stop on first error |
| `-e` | Write output even on error |
| `-h` | Show help |

### Quick Start

```mml
#title "Hello MDX"
#pcmfile "drums.pdx"

@1={
 31, 0, 0, 15, 0, 0, 0, 1, 0, 0, 0,
 31, 0, 0, 15, 0, 0, 0, 1, 0, 0, 0,
 31, 0, 0, 15, 0, 0, 0, 1, 0, 0, 0,
 31, 0, 0, 15, 0, 0, 0, 1, 0, 0, 0,
  0, 7, 15
}

A t120 @1 v15 l4 o4 cdefgab>c
```

```bash
mml2mdx hello.mml    # → hello.mdx
```

## Documentation

| Document | Description |
|----------|-------------|
| [Manual (中文)](docs/Manual_CN.md) | Full MML syntax reference (Chinese) |
| [Manual (English)](docs/Manual_EN.md) | Full MML syntax reference (English) |
| [PCM Setting](docs/PCM%20Setting.md) | PCM frequency (F-value) table |

## Project Structure

```
mml2mdx/
├── CMakeLists.txt        # Build configuration
├── README.md
├── docs/
│   ├── Manual_CN.md      # MML syntax manual (Chinese)
│   ├── Manual_EN.md      # MML syntax manual (English)
│   └── PCM Setting.md    # PCM F-value reference table
├── include/
│   └── mml2mdx.h         # Public header / shared types
└── src/
    ├── main.cpp           # Entry point & CLI parsing
    ├── lexer.cpp/h        # MML tokenizer
    ├── parser.cpp/h       # MML→IR parser (core logic)
    ├── compiler.cpp/h     # Compilation orchestrator
    ├── mdx_writer.cpp/h   # MDX binary format writer
    ├── wave_effect.cpp    # Waveform effect note splitting
    ├── optimizer.cpp      # Output optimization pass
    ├── macro.cpp          # Macro expansion
    └── util.cpp/h         # Utility functions
```

## License

MIT License

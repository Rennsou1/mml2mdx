# F 值速查表

---

## 通用（#ex-pcm 0，无驱动，纯 ADPCM）

| F 值 | 格式 | 采样率 | 声道 | 模式码 |
|:----:|------|-------:|------|:------:|
| F0 | ADPCM | 3906 Hz | Mono | `$00` |
| F1 | ADPCM | 5208 Hz | Mono | `$01` |
| F2 | ADPCM | 7812 Hz | Mono | `$02` |
| F3 | ADPCM | 10416 Hz | Mono | `$03` |
| F4 | ADPCM | 15625 Hz | Mono | `$04` |

---

## PCM8A（#ex-pcm 1，philly 1993-1997）

| F 值 | 格式 | 采样率 | 声道 | 模式码 |
|:----:|------|-------:|------|:------:|
| F0 | ADPCM | 3906 Hz | Mono | `$00` |
| F1 | ADPCM | 5208 Hz | Mono | `$01` |
| F2 | ADPCM | 7812 Hz | Mono | `$02` |
| F3 | ADPCM | 10416 Hz | Mono | `$03` |
| F4 | ADPCM | 15625 Hz | Mono | `$04` |
| F5 | 16bit PCM | 15625 Hz | Mono | `$05` |
| F6 | 8bit PCM | 15625 Hz | Mono | `$06` |
| F7 | ADPCM | 20833 Hz | Mono | `$07` |
| F8 | 16bit PCM | 20833 Hz | Mono | `$08` |
| F9 | 8bit PCM | 20833 Hz | Mono | `$09` |
| F10 | ADPCM | 31250 Hz | Mono | `$0A` |
| F11 | 16bit PCM | 31250 Hz | Mono | `$0B` |
| F12 | 8bit PCM | 31250 Hz | Mono | `$0C` |

---

## PCM8++（#ex-pcm 2，たにぃ 1994-1996）

| F 値 | 格式 | 采样率 | 声道 | 模式码 | 备注 |
|:----:|------|-------:|------|:------:|------|
| F0 | ADPCM | 3906 Hz | Mono | `$00` | |
| F1 | ADPCM | 5208 Hz | Mono | `$01` | |
| F2 | ADPCM | 7812 Hz | Mono | `$02` | |
| F3 | ADPCM | 10416 Hz | Mono | `$03` | |
| F4 | ADPCM | 15625 Hz | Mono | `$04` | |
| F5 | 16bit PCM | 15625 Hz | Mono | `$05` | |
| F6 | 8bit PCM | 15625 Hz | Mono | `$06` | |
| F7 | 16bit PCM | 须指定 `,hz` | Mono | `$07` | **可变频率** † |
| F8 | 16bit PCM | 15625 Hz | Mono | `$08` | |
| F9 | 16bit PCM | 16000 Hz | Mono | `$09` | |
| F10 | 16bit PCM | 22050 Hz | Mono | `$0A` | |
| F11 | 16bit PCM | 24000 Hz | Mono | `$0B` | |
| F12 | 16bit PCM | 32000 Hz | Mono | `$0C` | |
| F13 | 16bit PCM | 44100 Hz | Mono | `$0D` | |
| F14 | 16bit PCM | 48000 Hz | Mono | `$0E` | |
| F15 | 16bit PCM | 须指定 `,hz` | Mono | `$0F` | **可变频率** † |
| F16 | 8bit PCM | 15625 Hz | Mono | `$10` | |
| F17 | 8bit PCM | 16000 Hz | Mono | `$11` | |
| F18 | 8bit PCM | 22050 Hz | Mono | `$12` | |
| F19 | 8bit PCM | 24000 Hz | Mono | `$13` | |
| F20 | 8bit PCM | 32000 Hz | Mono | `$14` | |
| F21 | 8bit PCM | 44100 Hz | Mono | `$15` | |
| F22 | 8bit PCM | 48000 Hz | Mono | `$16` | |
| F23 | 8bit PCM | 须指定 `,hz` | Mono | `$17` | **可变频率** † |
| F24 | 16bit PCM | 15625 Hz | **Stereo** | `$18` | |
| F25 | 16bit PCM | 16000 Hz | **Stereo** | `$19` | |
| F26 | 16bit PCM | 22050 Hz | **Stereo** | `$1A` | |
| F27 | 16bit PCM | 24000 Hz | **Stereo** | `$1B` | |
| F28 | 16bit PCM | 32000 Hz | **Stereo** | `$1C` | |
| F29 | 16bit PCM | 44100 Hz | **Stereo** | `$1D` | |
| F30 | 16bit PCM | 48000 Hz | **Stereo** | `$1E` | |
| F31 | 16bit PCM | 须指定 `,hz` | **Stereo** | `$1F` | **可变频率** † |
| F32 | 8bit PCM | 15625 Hz | **Stereo** | `$20` | |
| F33 | 8bit PCM | 16000 Hz | **Stereo** | `$21` | |
| F34 | 8bit PCM | 22050 Hz | **Stereo** | `$22` | |
| F35 | 8bit PCM | 24000 Hz | **Stereo** | `$23` | |
| F36 | 8bit PCM | 32000 Hz | **Stereo** | `$24` | |
| F37 | 8bit PCM | 44100 Hz | **Stereo** | `$25` | |
| F38 | 8bit PCM | 48000 Hz | **Stereo** | `$26` | |
| F39 | 8bit PCM | 须指定 `,hz` | **Stereo** | `$27` | **可变频率** † |
| F40 | ADPCM | 须指定 `,hz` | Mono | `$28` | **可变频率** † |
| F41 | 16bit PCM | 须指定 `,hz` | Mono | `$29` | **可变频率** † |

---

† **可变频率模式（MXDRVp 扩展）**

PCM8++ 的 Variable 模式。必须在 PDL 中指定 `,hz` 参数。

采样会自动按 `rate × 2^(Δnote/12)` 变调进行播放。

原始 MXDRV 不支持此功能（D3 寄存器未传递），仅 MXDRVp 驱动可用。

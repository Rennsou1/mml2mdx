**中文** | [English](README_EN.md)

# mml2mdx

**MXDRV2 用 MML 编译器** — 将 MML（Music Macro Language）源文件编译为 MDX 格式，兼容 NOTE v0.85。

MDX 是 Sharp X68000 系列的标准音乐格式，由 MXDRV/MXDRV2 声音驱动程序驱动。

## 用法

支持 `.mml` 和 `.mus` 两种扩展名，可省略扩展名（自动按 `.mml` → `.mus` 顺序查找）。

**拖放编译**：直接将 `.mml` 或 `.mus` 文件拖到 `mml2mdx.exe` 上即可生成同名 `.mdx` 文件。

**命令行**：

```
mml2mdx [选项] <文件.mml>
```

### 命令行选项

| 选项 | 说明 |
|------|------|
| `-m<大小>` | MDX 缓冲区大小，单位 KB（默认 64） |
| `-x` | 反转八度 `<` `>` 方向 |
| `-p` | 启用 PCM8/PCM8++ 扩展模式 |
| `-r` | 出错时删除 MDX 文件 |
| `-i<通道>` | 通道遮罩（A-H, P-W） |
| `-b` | 出错时蜂鸣 |
| `-l` | 输出 PCM 使用状况文件（pcmuse.map） |
| `-c[n]` | 音长压缩（n: 含音符） |
| `-z[dvqpt012]` | 最适化标志 |
| `-t[名称]` | 保存音色数据（默认 tone.bin） |
| `-w[名称]` | 保存波形数据（默认 wave.bin） |
| `-v[0\|1]` | 详细模式 |
| `-1` | 首个错误即中止 |
| `-e` | 出错时仍写入文件 |
| `-h` | 显示帮助 |

### 快速入门

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

## 使用文档

| 文档 | 说明 |
|------|------|
| [Manual (中文)](docs/Manual_CN.md) | 完整 MML 语法参考 |
| [Manual (English)](docs/Manual_EN.md) | Full MML syntax reference |
| [PCM 频率表](docs/PCM%20Setting.md) | PCM F 值对照表 |

## 构建

需要 **CMake ≥ 3.20** 和 **C++17** 编译器（GCC/MinGW 或 MSVC）。

```bash
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

生成的 `mml2mdx` 可执行文件位于 `build/` 目录。

## 项目结构

```
mml2mdx/
├── CMakeLists.txt        # 构建配置
├── README.md             # 本文件（中文）
├── README_EN.md          # English README
├── docs/
│   ├── Manual_CN.md      # MML 语法手册（中文）
│   ├── Manual_EN.md      # MML 语法手册（英文）
│   └── PCM Setting.md    # PCM F 值参考表
├── include/
│   └── mml2mdx.h         # 公共头文件 / 共享类型
└── src/
    ├── main.cpp           # 入口 & 命令行解析
    ├── lexer.cpp/h        # MML 词法分析器
    ├── parser.cpp/h       # MML→IR 语法解析器（核心逻辑）
    ├── compiler.cpp/h     # 编译流程控制
    ├── mdx_writer.cpp/h   # MDX 二进制格式写入
    ├── wave_effect.cpp    # 波形效果音符拆分
    ├── optimizer.cpp      # 输出优化
    ├── macro.cpp          # 宏展开
    └── util.cpp/h         # 工具函数
```

## 许可证

MIT License

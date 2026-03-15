// mml2mdx — Top-level compiler entry point (compiler.h)
#pragma once
#include "mml2mdx.h"

// 顶层编译函数：读取 MML → 解析 → 编译 → 写入 MDX
bool compile_mml(const mdx::CompilerConfig& config);

// mml2mdx — 工具函数

#pragma once
#include "mml2mdx.h"
#include <string>
#include <vector>
#include <cstdio>

namespace mdx {

// 读取文件全部内容（支持 Shift-JIS 和 UTF-8）
// 返回的字符串以系统默认编码存储（内部处理 BOM）
bool read_file(const std::string& path, std::string& content);

// 将字符串写入文件（二进制模式）
bool write_binary_file(const std::string& path, const std::vector<uint8_t>& data);

// 将 NOTE 47 参数打包为 MDX 27 字节格式
// params: 47 个整数参数, out: 27 字节输出, voice_number: 音色编号
void pack_voice(const int params[VOICE_PARAMS], uint8_t out[VOICE_BINARY_SIZE],
                int voice_number);

// 写入 big-endian 16 位整数到字节流
inline void write_be16(std::vector<uint8_t>& buf, int16_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

// 写入 big-endian 16 位无符号整数到字节流
inline void write_be16u(std::vector<uint8_t>& buf, uint16_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

// 从 tempo BPM 计算 Timer-B 值
// Timer-B = 256 - (60 * 4000000) / (tempo * 48 * 1024)
int tempo_to_timer_b(int bpm);

// 从 v (0-15) 转换为 @v (0-127)
// v=0 → @v=0, v=15 → @v=127
inline int v_to_atv(int v) {
    // NOTE 使用 v*8+4 的近似，但具体映射需要逆向验证
    // MXDRV 实际使用: tl = (15-v) * 8 → @v = 127 - tl
    return (v < 0) ? 0 : (v > 15) ? 127 : v * 8 + 4;
}

// 获取文件所在目录
std::string get_directory(const std::string& path);

// 连接路径
std::string join_path(const std::string& dir, const std::string& file);

// 错误报告
void report_error(const std::string& file, int line, const std::string& msg);
void report_warning(const std::string& file, int line, const std::string& msg);

} // namespace mdx

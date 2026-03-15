// mml2mdx — MDX 二进制文件写入器
// 从编译后的通道 opcode 和音色数据生成最终的 .mdx 二进制文件

#pragma once
#include "mml2mdx.h"
#include <string>
#include <vector>
#include <array>

namespace mdx {

// MDX 二进制文件生成器
class MdxWriter {
public:
    // 从编译数据写入 MDX 文件
    // title: 曲名（将编码为 Shift-JIS）
    // pcm_filename: PDX 文件名引用
    // channels: 每通道编译后的 opcode 流
    // num_channels: 9（标准）或 16（ex-pcm）
    // voices: voice/instrument definitions
    bool write(const std::string& output_path,
               const std::string& title,
               const std::string& pcm_filename,
               const std::array<ChannelState, MAX_CHANNELS>& channels,
               int num_channels,
               const std::vector<VoiceData>& voices);

private:
    // 将 MDX 二进制数据构建到字节向量中
    std::vector<uint8_t> build_mdx(
        const std::string& title,
        const std::string& pcm_filename,
        const std::array<ChannelState, MAX_CHANNELS>& channels,
        int num_channels,
        const std::vector<VoiceData>& voices);
};

} // namespace mdx

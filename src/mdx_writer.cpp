// mml2mdx — MDX 二进制文件写入器 实现

#include "mdx_writer.h"
#include "util.h"
#include <cstring>
#include <set>

namespace mdx {

bool MdxWriter::write(const std::string& output_path,
                      const std::string& title,
                      const std::string& pcm_filename,
                      const std::array<ChannelState, MAX_CHANNELS>& channels,
                      int num_channels,
                      const std::vector<VoiceData>& voices) {
    auto data = build_mdx(title, pcm_filename, channels, num_channels, voices);
    if (data.empty()) return false;
    return write_binary_file(output_path, data);
}

std::vector<uint8_t> MdxWriter::build_mdx(
    const std::string& title,
    const std::string& pcm_filename,
    const std::array<ChannelState, MAX_CHANNELS>& channels,
    int num_channels,
    const std::vector<VoiceData>& voices) {

    std::vector<uint8_t> mdx;

    // ── 文件头 ──
    // 标题（Shift-JIS 编码）+ 0x0D 0x0A 0x1A
    for (char c : title) {
        mdx.push_back(static_cast<uint8_t>(c));
    }
    mdx.push_back(0x0D);  // 回车
    mdx.push_back(0x0A);  // 换行
    mdx.push_back(0x1A);  // 文件结束标记

    // PDX 文件名 + 0x00
    for (char c : pcm_filename) {
        mdx.push_back(static_cast<uint8_t>(c));
    }
    mdx.push_back(0x00);  // 空字符终止符

    // ── 数据体 ──
    // MDX 数据体布局（所有偏移相对于 body_start）:
    //   [2字节 音色数据偏移 (BE)]
    //   [N × 2字节 通道 MML 数据偏移 (BE)]
    //   [通道 MML 数据 ...]
    //   [音色数据 ...]

    size_t body_start = mdx.size();

    // 偏移表大小 = 2 (voice offset) + N * 2 (channel offsets)
    int offset_table_size = 2 + num_channels * 2;

    // 计算各通道 MML 数据大小（空通道需要 2 字节的 F1 00 performance end）
    std::vector<int> ch_sizes(num_channels);
    int total_mml_size = 0;
    for (int i = 0; i < num_channels; i++) {
        ch_sizes[i] = channels[i].opcodes.empty() ? 2 : (int)channels[i].opcodes.size();
        total_mml_size += ch_sizes[i];
    }

    // 音色数据偏移（相对于 body_start）
    int voice_data_offset = offset_table_size + total_mml_size;
    write_be16u(mdx, static_cast<uint16_t>(voice_data_offset));

    // 通道 MML 数据偏移（相对于 body_start）
    int current_offset = offset_table_size;
    for (int i = 0; i < num_channels; i++) {
        write_be16u(mdx, static_cast<uint16_t>(current_offset));
        current_offset += ch_sizes[i];
    }

    // 写入通道 MML 数据
    for (int i = 0; i < num_channels; i++) {
        if (channels[i].opcodes.empty()) {
            // 空通道: 演奏结束 (F1 00)
            mdx.push_back(CMD_PERF_END);
            mdx.push_back(0x00);
        } else {
            for (uint8_t b : channels[i].opcodes) {
                mdx.push_back(b);
            }
        }
    }

    // 写入音色数据（仅输出通道数据中 FD/CMD_VOICE_SET 引用的音色）
    // note.x 兼容: 未使用的音色定义不输出
    std::set<int> used_voices;
    for (int i = 0; i < num_channels; i++) {
        const auto& ops = channels[i].opcodes;
        for (size_t j = 0; j + 1 < ops.size(); j++) {
            if (ops[j] == 0xFD) { // CMD_VOICE_SET
                used_voices.insert(ops[j + 1]);
            }
        }
    }
    for (const auto& voice : voices) {
        if (used_voices.count(voice.number) == 0) continue;
        uint8_t packed[VOICE_BINARY_SIZE];
        pack_voice(voice.params.data(), packed, voice.number);
        for (int i = 0; i < VOICE_BINARY_SIZE; i++) {
            mdx.push_back(packed[i]);
        }
    }

    return mdx;
}

} // namespace mdx

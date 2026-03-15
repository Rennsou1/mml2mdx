// mml2mdx — 解析器
#pragma once
#include "mml2mdx.h"
#include "lexer.h"
#include <string>
#include <vector>
#include <map>

namespace mdx {

// 解析器: 将 MML 源文本解析并编译为每通道的 opcode 流
class Parser {
public:
    // 解析并编译 MML 源文件
    // 成功返回 true
    bool parse(const std::string& input_file, CompilerConfig& config);

    // 获取编译结果
    const std::vector<VoiceData>& voices() const { return voices_; }
    const std::vector<WaveData>& waves() const { return waves_; }
    const std::array<ChannelState, MAX_CHANNELS>& channels() const { return channels_; }

    // 错误数
    int error_count() const { return error_count_; }

private:
    CompilerConfig* config_ = nullptr;
    Lexer lexer_;
    std::array<ChannelState, MAX_CHANNELS> channels_;
    std::vector<VoiceData> voices_;
    std::vector<WaveData> waves_;
    std::vector<KeyMap> keymaps_;
    std::map<std::string, std::string> macros_;      // $varname → content
    std::map<int, std::string> tone_macros_;          // @@N → mml content
    int error_count_ = 0;
    std::string source_dir_;                             // 源文件所在目录

    // 行级处理
    void process_line(const std::string& line, int line_num);
    void process_pseudo_cmd(const std::string& line, int line_num);
    void process_tone_def(const std::string& line, int line_num);
    void process_wave_def(const std::string& line, int line_num);
    void process_keymap_def(const std::string& line, int line_num);
    void process_tone_macro_def(const std::string& line, int line_num);
    void process_macro_def(const std::string& line, int line_num);
    void process_mml_line(const std::string& line, int line_num, 
                          const std::vector<int>& channel_indices);

    // MML 命令解析
    void parse_mml(const std::string& mml, int line_num, int ch_idx,
                   const std::vector<int>& all_channels = {});

    // 波形效果: 解析辅助
    void parse_wave_effect(ChannelState::WaveEffectType we_type);

    // 页面数据: page_channels_[ch_idx][page_num] = ChannelState
    // page 0 存于 channels_[ch_idx]（不在此 map）
    // page 1, 2, ... 存于此 map
    std::map<int, std::map<int, ChannelState>> page_channels_;

    // 和弦上下文
    std::vector<int> chord_channels_;      // 当前 MML 行的通道列表
    struct ChordNote {
        int abs_note;  // 绝对音程 (-1 = 休符)
        int octave_adj; // 八度调整
    };
    std::vector<ChordNote> last_chord_;    // 记忆 (和弦重复 # 用)

    // 工具
    void error(int line, const std::string& msg);
    void warning(int line, const std::string& msg);
};

} // namespace mdx

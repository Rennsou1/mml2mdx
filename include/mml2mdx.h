#pragma once
// mml2mdx — MML→MDX 编译器
// 全局常量、MDX opcode 定义、数据结构

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <optional>

namespace mdx {

// ═══════════════════════════════════════════
// MDX 文件格式常量
// ═══════════════════════════════════════════

// 休符: 0x00-0x7F, 持续时间 = byte + 1
constexpr uint8_t REST_MIN = 0x00;
constexpr uint8_t REST_MAX = 0x7F;

// 音符: 0x80-0xDF, 音程 = byte - 0x80 (范围 0-95)
// 0x80 = o0d+ (最低可播放音), 0xDF = o8d (最高可播放音)
constexpr uint8_t NOTE_MIN    = 0x80;
constexpr uint8_t NOTE_MAX    = 0xDF;
constexpr int     NOTE_OFFSET = 0x80;

// ── 控制命令 opcode（已对照 VGMRips MDX 规格验证）──
constexpr uint8_t CMD_TEMPO        = 0xFF; // @t<n>: set Timer-B tempo value
constexpr uint8_t CMD_OPM_REG      = 0xFE; // y<reg>,<val>: write OPM register
constexpr uint8_t CMD_VOICE        = 0xFD; // @<n>: set voice/PCM bank
constexpr uint8_t CMD_PAN          = 0xFC; // p<n>: set pan (0-3)
constexpr uint8_t CMD_VOLUME       = 0xFB; // @v<n> / v<n>: set volume (0-127)
constexpr uint8_t CMD_VOL_DOWN     = 0xFA; // (: decrease volume (no parameter)
constexpr uint8_t CMD_VOL_UP       = 0xF9; // ): increase volume (no parameter)
constexpr uint8_t CMD_SOUND_LEN    = 0xF8; // q<n>: set sound length (staccato)
constexpr uint8_t CMD_LEGATO       = 0xF7; // &: disable key-off for next note (legato)
constexpr uint8_t CMD_REPEAT_START = 0xF6; // [<n> 0x00: begin repeat n times
constexpr uint8_t CMD_REPEAT_END   = 0xF5; // ]: end repeat, loop back nn (signed word)
constexpr uint8_t CMD_REPEAT_ESC   = 0xF4; // /: repeat escape, skip nn bytes on last iteration
constexpr uint8_t CMD_DETUNE       = 0xF3; // D<nn>: detune nn/64 semitones (signed word)
constexpr uint8_t CMD_PORTAMENTO   = 0xF2; // _: portamento, nn/16384 semitones per clock (signed word)
constexpr uint8_t CMD_PERF_END     = 0xF1; // performance end (0x00) or loop (negative offset)
constexpr uint8_t CMD_KEYON_DELAY  = 0xF0; // k<n>: delay key-on n ticks
constexpr uint8_t CMD_SYNC_SEND    = 0xEF; // S<ch>: sync send on channel n
constexpr uint8_t CMD_SYNC_WAIT    = 0xEE; // W: sync wait (pause until sync send)
constexpr uint8_t CMD_FREQ_MODE    = 0xED; // F<n>: ADPCM/noise frequency mode
constexpr uint8_t CMD_LFO_PITCH    = 0xEC; // MP: pitch LFO (0x80=off, 0x81=on, or m nn aa)
constexpr uint8_t CMD_LFO_VOL      = 0xEB; // MA: volume LFO (0x80=off, 0x81=on, or m nn aa)
constexpr uint8_t CMD_MH_LFO       = 0xEA; // MH: OPM hardware LFO (0x80=off, 0x81=on, or m n o p q)
constexpr uint8_t CMD_LFO_DELAY    = 0xE9; // MD<n>: LFO key-on delay
constexpr uint8_t CMD_PCM8_MODE    = 0xE8; // PCM8 expansion / mode shift
constexpr uint8_t CMD_FADEOUT       = 0xE7; // $FO: fade-out (0x01 n)

// ═══════════════════════════════════════════
// 通道配置
// ═══════════════════════════════════════════
constexpr int MAX_FM_CHANNELS  = 8;   // A-H
constexpr int MAX_PCM_CHANNELS = 8;   // P-W
constexpr int MAX_CHANNELS     = 16;  // 拓展模式

// 标准模式通道数（A-H + P）
constexpr int STD_CHANNELS = 9;

// ═══════════════════════════════════════════
// 音长相关常量
// ═══════════════════════════════════════════
constexpr int STEPS_PER_QUARTER = 48;   // 四分音符 = 48 步
constexpr int STEPS_PER_WHOLE   = 192;  // 全音符 = 192 步
constexpr int MAX_DURATION      = 256;  // 最大音长（步数）
constexpr int MIN_DURATION      = 1;    // 最小音长

// ═══════════════════════════════════════════
// 音程常量
// ═══════════════════════════════════════════
constexpr int NOTE_RANGE_MIN  = -24;  // o-2d+ (n 命令最低)
constexpr int NOTE_RANGE_MAX  = 119;  // o10d  (n 命令最高)
constexpr int PLAYABLE_MIN    = 0;    // o0d+  (实际可播放最低)
constexpr int PLAYABLE_MAX    = 95;   // o8d   (实际可播放最高)
constexpr int DEFAULT_OCTAVE  = 4;    // 初始八度
constexpr int NOTES_PER_OCT   = 12;   // 每八度音数

// ═══════════════════════════════════════════
// 音色相关
// ═══════════════════════════════════════════
constexpr int VOICE_PARAMS       = 47;  // NOTE 格式的参数个数
constexpr int VOICE_BINARY_SIZE  = 27;  // MDX 打包: 26 字节数据 + 1 字节 padding
constexpr int MAX_VOICES         = 256; // 音色编号范围 0-255
constexpr int PARAMS_PER_OP      = 11;  // 每个算子的参数数
constexpr int NUM_OPERATORS      = 4;   // YM2151 算子数

// 算子参数在 47 参数数组中的索引
// OP n (0-3) 的参数 p 位于 [n * 11 + p]
enum OpParam {
    OP_AR  = 0,
    OP_DR  = 1,
    OP_SR  = 2,
    OP_RR  = 3,
    OP_SL  = 4,
    OP_TL  = 5,
    OP_KS  = 6,
    OP_MUL = 7,
    OP_DT1 = 8,
    OP_DT2 = 9,
    OP_AME = 10,
};
// 最后 3 个参数
constexpr int VOICE_ALG     = 44;  // ALG (connection)
constexpr int VOICE_FL      = 45;  // FL (feedback level)
constexpr int VOICE_OP_MASK = 46;  // OP mask

// ═══════════════════════════════════════════
// 波形相关
// ═══════════════════════════════════════════
constexpr int MAX_WAVES      = 128;  // 波形编号 0-127
constexpr int MAX_WAVE_DATA  = 512;  // 波形数据最大个数

// ═══════════════════════════════════════════
// 宏相关
// ═══════════════════════════════════════════
constexpr int MAX_MACRO_VARS  = 91;   // 可用宏变量名数
constexpr int MAX_SUBSCRIPT   = 20;   // 添字 0-19
constexpr int MAX_MACRO_NEST  = 8;    // 宏嵌套上限

// ═══════════════════════════════════════════
// 杂项
// ═══════════════════════════════════════════
constexpr int MAX_INCLUDE_DEPTH = 16;  // #include 嵌套上限
constexpr int MAX_LOOP_NEST     = 16;  // [] 嵌套上限（估计）
constexpr int MAX_CHORD_NOTES   = 32;  // 连符/和弦最大音数
constexpr int KEY_MAP_ENTRIES   = 96;  // @k 映射的音程数 (o0d+~o8d)

// ═══════════════════════════════════════════
// 通道名→索引映射
// ═══════════════════════════════════════════

// 通道字母到索引的映射
// FM: A=0, B=1, ..., H=7
// PCM: P=8, Q=9, ..., W=15
inline int channel_index(char ch) {
    if (ch >= 'A' && ch <= 'H') return ch - 'A';
    if (ch >= 'P' && ch <= 'W') return ch - 'P' + 8;
    if (ch >= 'a' && ch <= 'h') return ch - 'a';
    if (ch >= 'p' && ch <= 'w') return ch - 'p' + 8;
    return -1;
}

inline bool is_fm_channel(int idx) { return idx >= 0 && idx < 8; }
inline bool is_pcm_channel(int idx) { return idx >= 8 && idx < 16; }

// ═══════════════════════════════════════════
// 数据结构
// ═══════════════════════════════════════════

// 音色数据（NOTE 47 参数格式）
struct VoiceData {
    int number = -1;
    std::array<int, VOICE_PARAMS> params = {};
};

// 波形数据
struct WaveData {
    int number = -1;
    int type = 0;           // 0=无循环, 1=有循环
    int loop_point = 0;     // 循环起點
    std::vector<int16_t> data;  // 最大 512 元素
};

// 音色编号映射
struct KeyMap {
    int number = -1;
    std::array<int, KEY_MAP_ENTRIES> map = {};
};

// 循环信息（用于编译器栈）
struct LoopInfo {
    int start_offset = 0;     // 循环开始在 opcode 流中的偏移
    int count = 2;            // 循环次数
    int escape_offset = -1;   // '/' 跳出点的偏移（-1 表示没有）
};

// 每通道的编译状态
struct ChannelState {
    // 当前状态跟踪（用于优化器判断冗余命令）
    int octave = DEFAULT_OCTAVE;
    int base_length = 4;       // l 命令设定（音长分母）
    int volume_v = 8;          // v 音量 (0-15)
    int volume_at_v = -1;      // @v 音量 (0-127)，-1 表示未设定
    int quantize_q = 8;        // q (1-8)
    int quantize_at_q = -1;    // @q 步数，-1 表示未设定
    int pan = 3;               // p (0-3)
    int voice = -1;            // 当前音色号
    int detune = 0;            // D 值
    int keyon_delay = 0;       // k 值
    int transpose = 0;         // TR 值
    int volume_offset = 0;     // VO 值
    bool temp_volume_active = false;  // x/@x 临时音量标志
    uint8_t temp_volume_saved = 0;    // x/@x 恢复用的原音量字节

    // 嵌套循环栈
    std::vector<LoopInfo> loop_stack;

    // 无限循环点
    bool has_infinite_loop = false;
    int  infinite_loop_offset = -1;

    // 已编译的 opcode 流
    std::vector<uint8_t> opcodes;

    // #flat/#sharp/#natural 状態（各音名 c-b）
    // 0=normal, 1=sharp, -1=flat
    std::array<int, 7> note_modifiers = {};

    // Portamento 状态
    bool portamento_pending = false;  // _ 后到下一个音符为止
    int last_abs_note = -1;           // 前一个输出音符的绝对音程
    int last_duration = 0;            // 前一个音符的音长（clock 数）
    int last_note_opcode_pos = -1;    // 前一个音符的 opcode 位置（portamento 插入用）
    bool legato_pending = false;      // 行末的 & 到下行音符的连接

    // 音色宏有效 (SMON/SMOF)
    bool smon = true;

    // 滑音加工 (GL)
    int glide_val = 0;   // glide 音高偏移（半音的 1/64 单位）
    int glide_step = 0;  // glide 步数
    bool glide_on = false;

    // 音色自动切换 (KS)
    int ks_map = -1;     // -1 = 无效
    bool ks_on = false;

    // Q: 波形效果中的量化 (0=普通, 1-256=比率, -256~-1=固定)
    int waveform_quantize = 0;

    // C: 伪循环点
    int pseudo_loop_offset = -1;

    // ═══════════════════════════════════════════
    // 波形效果（编译时音符分割）
    // ═══════════════════════════════════════════
    // 各效果的类型
    enum WaveEffectType {
        WE_AP = 0,  // auto-pan → p command
        WE_DT = 1,  // detune → D command
        WE_TD = 2,  // detune2 → D command
        WE_VM = 3,  // volume → v/@v command
        WE_MV = 4,  // volume2 → v/@v command
        WE_KM = 5,  // PMS/AMS → y command
        WE_TL = 6,  // total level → y command
        WE_YA = 7,  // y-command → y command
        WE_COUNT = 8
    };

    struct WaveEffect {
        bool active = false;     // 效果有效
        int wave_num = -1;       // 波形编号 (@w)
        int step_count = 1;      // 每元素的步数(或音符数)
        int sync_mode = 0;       // 0=非同步1, 1=同步, 2=非同步2
        int delay = 0;           // 延迟（步数）
        int scale = 1;           // 倍率 (DTS/VMS)
        int phase = 0;           // 当前相位（波形数据索引）
        int step_counter = 0;    // 当前步进计数器
    };

    std::array<WaveEffect, WE_COUNT> wave_effects = {};

    // TL 效果用: 算子掩码 (-1=自动调制器, 0-15=位掩码)
    int tl_op_mask = -1;
    int tl_tone_num = -1;
};

// 编译器全局配置（命令行选项 + 伪指令）
struct CompilerConfig {
    // 命令行选项
    int mdx_buffer_size = 65536;  // -m (字节)
    bool octave_rev = false;      // -x
    bool ex_pcm = false;          // -p
    bool remove_on_error = false; // -r
    uint16_t channel_mask = 0;    // -i (位掩码)
    bool beep_on_error = false;   // -b
    bool pcm_list = false;        // -l
    bool pcm_list_or = false;     // -o
    int  compress = -1;           // -c: -1=无, 0=休符, 1=音符+休符
    std::string optimize;         // -z
    std::string tone_save;        // -t
    std::string wave_save;        // -w
    int  verbose = -1;            // -v
    bool stop_on_first_error = false; // -1
    bool write_on_error = false;  // -e

    // 伪指令设定
    std::string title;
    std::string pcm_filename;
    int transpose = 0;           // #tps
    bool tps_all = false;        // #tps-all
    int detune_offset = 0;       // #detune
    int tone_offset = 0;         // #toneofs
    bool overwrite_tone = false; // #overwrite
    bool no_return = false;      // #noreturn
    bool wavemem = false;        // #wavemem
    int  cont_mode = 0;         // #cont (0/1/2)
    int  wcmd_mode = 0;         // #wcmd (0/1/2)
    int  reste_mode = -1;       // #reste/-1=nreste
    bool coder = false;          // #coder
    int  glide_mode = 0;        // #glide

    // #flat/#sharp/#natural 全通道设定
    std::array<int, 7> global_note_modifiers = {};

    // 输入/输出文件
    std::string input_file;
    std::string output_file;
};

// ═══════════════════════════════════════════
// 页面（Page）功能 (page_merge.cpp)
// ═══════════════════════════════════════════

// 页面展平后的段（一个音符或一段休符）
struct PageSegment {
    int start_tick = 0;     // 绝对起始 tick
    int duration = 0;       // 持续 tick 数
    bool is_note = false;   // true=音符, false=休符
    int note_byte = 0;      // 音符字节 (0x80-0xDF), 仅 is_note 时有效

    // 该段开始时的通道状态快照
    int voice = -1;         // @N 音色号
    int volume_byte = -1;   // CMD_VOLUME 参数字节 (-1=未设定)
    int pan = -1;           // p 值 (-1=未设定)

    // 该段开始时需要发出的其他命令 (LFO、detune、sync 等)
    std::vector<uint8_t> prefix_opcodes;
};

// opcode 流展平: 展开循环，输出 tick 段列表
// 返回的 infinite_loop_tick: L 标记位置 (-1=无)
std::vector<PageSegment> flatten_opcodes(
    const std::vector<uint8_t>& opcodes, int& infinite_loop_tick);

// 多页面合并: pages[0] = 最高优先级
// 返回合并后的 opcode 流
std::vector<uint8_t> merge_pages(
    const std::vector<std::vector<PageSegment>>& pages,
    int infinite_loop_tick = -1);

// ═══════════════════════════════════════════
// 波形效果处理函数 (wave_effect.cpp)
// ═══════════════════════════════════════════
bool apply_wave_effects(
    ChannelState& ch, int note_byte, int duration,
    int ch_idx, const std::vector<WaveData>& waves,
    const CompilerConfig& config, bool is_tied = false);
void wave_effects_note_sync(ChannelState& ch, int ch_idx, const std::vector<WaveData>& waves, const CompilerConfig& config);
int get_wave_value(const WaveData& wave, int index);
void emit_wave_effect_cmd(
    std::vector<uint8_t>& opcodes,
    ChannelState::WaveEffectType type,
    int wave_val, int scale,
    int ch_idx, int tl_op_mask, int tl_tone_num,
    int base_volume = 0);

} // namespace mdx

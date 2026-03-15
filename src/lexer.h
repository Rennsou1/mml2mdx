// mml2mdx — 词法分析器
#pragma once
#include "mml2mdx.h"
#include <string>
#include <vector>

namespace mdx {

// Token 类型
enum class TokenType {
    // 行级标识
    LINE_CH_ID,      // A-H, P-W 通道行
    LINE_PSEUDO,     // # 伪指令行
    LINE_TONE_DEF,   // @ 音色定义行
    LINE_WAVE_DEF,   // @w 波形定义行
    LINE_KEYMAP_DEF, // @k 映射定义行
    LINE_TONEMACRO,  // @@ 音色宏定义行
    LINE_MACRO_DEF,  // 宏定义行

    // MML 命令 token
    NOTE,            // a-g 音符
    NOTE_NUM,        // n<音程> 直接指定
    REST,            // r 休符
    TIE,             // & 连音
    PORTAMENTO,      // _ 滑音
    PORTAMENTO_D,    // _D 数值滑音
    OCTAVE,          // o 八度设定
    OCTAVE_UP,       // > 八度 +1
    OCTAVE_DOWN,     // < 八度 -1
    DURATION,        // 数值/% 音长

    VOICE,           // @ 音色
    VOLUME_V,        // v 音量 (16段)
    VOLUME_ATV,      // @v 音量 (128段)
    VOLUME_X,        // x 临时音量 (16段)
    VOLUME_ATX,      // @x 临时音量 (128段)
    VOL_DOWN,        // ( 音量减
    VOL_UP,          // ) 音量增
    VOL_CHANGE,      // V 相对音量
    VOL_OFFSET,      // VO 音量偏移
    PAN,             // p 声道
    TEMPO,           // t 速度 (BPM)
    TEMPO_ATT,       // @t 速度 (Timer-B)
    QUANTIZE,        // q
    QUANTIZE_ATQ,    // @q
    QUANTIZE_WE,     // Q (波形效果中)
    BASE_LENGTH,     // l 基本音长
    DETUNE,          // D 微调
    KEYON_DELAY,     // k 按键延迟
    NOISE,           // w 噪音
    SYNC_SEND,       // S 同步送出
    SYNC_WAIT,       // W 同步等待
    RESIDUE_CUT,     // K 残响切
    FREQ,            // F 采样频率
    TRANSPOSE,       // TR 转调

    // 循环
    LOOP_START,      // [
    LOOP_END,        // ] 
    LOOP_ESCAPE,     // /
    INF_LOOP,        // L 无限循环
    PSEUDO_LOOP,     // C 伪循环

    // LFO
    LFO_MH,          // MH
    LFO_MHON,        // MHON
    LFO_MHOF,        // MHOF
    LFO_MHR,         // MHR
    LFO_MP,          // MP
    LFO_MPON,        // MPON
    LFO_MPOF,        // MPOF
    LFO_MA,          // MA
    LFO_MAON,        // MAON
    LFO_MAOF,        // MAOF
    LFO_MD,          // MD

    // 波形效果
    WE_AP,    WE_APD,  WE_APL,  WE_APON,  WE_APOF,
    WE_DT,    WE_DTD,  WE_DTS,  WE_DTL,   WE_DTON, WE_DTOF,
    WE_TD,    WE_TDD,  WE_TDS,  WE_TDL,   WE_TDON, WE_TDOF,
    WE_VM,    WE_VMD,  WE_VMS,  WE_VML,   WE_VMON, WE_VMOF,
    WE_MV,    WE_MVD,  WE_MVS,  WE_MVL,   WE_MVON, WE_MVOF,
    WE_KM,    WE_KMD,  WE_KML,  WE_KMON,  WE_KMOF,
    WE_TL,    WE_TLD,  WE_TLM,  WE_TLT,   WE_TLL,  WE_TLON, WE_TLOF,
    WE_YA,    WE_YAD,  WE_YAL,  WE_YAON,  WE_YAOF,

    // 格莱德
    GLIDE,           // GL
    GLIDE_ON,        // GLON
    GLIDE_OFF,       // GLOF

    // 调号
    FLAT,            // $FLAT
    SHARP,           // $SHARP
    NATURAL,         // $NATURAL/$NORMAL

    // 音色宏控制
    SMON,            // SMON
    SMOF,            // SMOF

    // 音色映射
    KS,              // KS
    KSON,            // KSON
    KSOF,            // KSOF

    // 和弦
    CHORD_START,     // ｢ 或 ``
    CHORD_END,       // ｣ 或 ``
    CHORD_REPEAT,    // # (和弦重用)

    // 通道别指定
    CH_SPLIT_START,  // |
    CH_SPLIT_SEP,    // :
    CH_SPLIT_END,    // |

    // 连符
    TUPLET_START,    // {
    TUPLET_END,      // }

    // 渐弱
    FADEOUT,         // $FO

    // 制御
    ZC,              // zc
    ZW,              // zw

    // 跳过
    MML_SKIP,        // !
    MML_SKIP_RANGE,  // ?

    // 宏展开
    MACRO,           // $name

    // OPM 寄存器
    OPM_REG,         // y

    // 杂项
    NUMBER,          // 数字
    COMMA,           // ,
    DOT,             // .
    COMMENT,         // 注释
    NEWLINE,         // 换行
    END_OF_FILE,     // 文件结束
    UNKNOWN,         // 未知
};

struct Token {
    TokenType type = TokenType::UNKNOWN;
    int int_value = 0;           // 数值
    std::string str_value;       // 字符串值
    int line = 0;                // 源文件行号
    int column = 0;              // 列号
};

// 词法分析器：将 MML 源文本转换为 token 流
class Lexer {
public:
    // 加载源文件（处理 #include 递归）
    bool load(const std::string& path, const std::string& base_dir);

    // 获取所有源代码行（#include 展开后）
    const std::vector<std::string>& lines() const { return lines_; }

    // 获取行号到原始文件的映射
    struct LineInfo {
        std::string file;
        int original_line;
    };
    const LineInfo& line_info(int line) const { return line_info_[line]; }

private:
    std::vector<std::string> lines_;
    std::vector<LineInfo> line_info_;
    int include_depth_ = 0;

    bool load_recursive(const std::string& path, const std::string& base_dir);
};

// MML 行级 tokenizer：在解析阶段逐行逐字符解析
// （不预先 tokenize，而是在 Parser 中按需解析）
class MMLTokenizer {
public:
    MMLTokenizer(const std::string& line, int line_num);

    // 取下一个 token
    Token next();

    // 预览下一个字符（不消费）
    char peek() const;
    char peek2() const;  // 第二个字符

    // 当前位置
    int pos() const { return pos_; }
    bool at_end() const { return pos_ >= (int)line_.size(); }

    // 跳过空白
    void skip_whitespace();

    // 读取整数
    int read_int();
    bool try_read_int(int& out);

    // 读取音长表达式
    int read_duration(int base_length);

    // 逐字符推进（parser 需要直接访问）
    char advance();
    bool match(char expected);
    bool match_str(const char* str);

private:
    std::string line_;
    int line_num_;
    int pos_ = 0;
};

} // namespace mdx

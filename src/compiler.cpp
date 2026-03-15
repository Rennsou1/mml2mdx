// mml2mdx — 顶层编译器: 连接 解析器 → 优化器 → 写入器 管线
#include "compiler.h"
#include "parser.h"
#include "mdx_writer.h"
#include "util.h"
#include <algorithm>

using namespace mdx;

// ═══════════════════════════════════════════
// 优化遍: 冗余命令去除 (-z / #opt)
// ═══════════════════════════════════════════
static void optimize_channel(std::vector<uint8_t>& ops, const std::string& flags) {
    if (flags.empty()) return;
    
    bool opt_d = flags.find('d') != std::string::npos; // detune
    bool opt_v = flags.find('v') != std::string::npos; // volume
    bool opt_q = flags.find('q') != std::string::npos; // quantize
    bool opt_p = flags.find('p') != std::string::npos; // pan
    bool opt_t = flags.find('t') != std::string::npos; // tempo
    bool opt_0 = flags.find('0') != std::string::npos; // MD (LFO delay)
    bool opt_1 = flags.find('1') != std::string::npos; // MP (pitch LFO)
    bool opt_2 = flags.find('2') != std::string::npos; // MA (vol LFO)
    
    std::vector<uint8_t> out;
    out.reserve(ops.size());
    
    int last_detune = -99999;
    int last_volume = -1;
    int last_quantize = -1;
    int last_pan = -1;
    int last_tempo = -1;
    int last_lfo_delay = -1;
    
    size_t i = 0;
    while (i < ops.size()) {
        uint8_t cmd = ops[i];
        
        // F3 nn nn: detune（3 字节）
        if (cmd == 0xF3 && i + 2 < ops.size() && opt_d) {
            int val = (int16_t)((ops[i+1] << 8) | ops[i+2]);
            if (val == last_detune) { i += 3; continue; }
            last_detune = val;
            out.push_back(ops[i]); out.push_back(ops[i+1]); out.push_back(ops[i+2]);
            i += 3; continue;
        }
        // FB nn: 音量（2 字节）
        if (cmd == 0xFB && i + 1 < ops.size() && opt_v) {
            if ((int)ops[i+1] == last_volume) { i += 2; continue; }
            last_volume = ops[i+1];
            out.push_back(ops[i]); out.push_back(ops[i+1]);
            i += 2; continue;
        }
        // F8 nn: 量化（2 字节）
        if (cmd == 0xF8 && i + 1 < ops.size() && opt_q) {
            if ((int)ops[i+1] == last_quantize) { i += 2; continue; }
            last_quantize = ops[i+1];
            out.push_back(ops[i]); out.push_back(ops[i+1]);
            i += 2; continue;
        }
        // FC nn: 声像（2 字节）
        if (cmd == 0xFC && i + 1 < ops.size() && opt_p) {
            if ((int)ops[i+1] == last_pan) { i += 2; continue; }
            last_pan = ops[i+1];
            out.push_back(ops[i]); out.push_back(ops[i+1]);
            i += 2; continue;
        }
        // FF nn: 速度（2 字节）
        if (cmd == 0xFF && i + 1 < ops.size() && opt_t) {
            if ((int)ops[i+1] == last_tempo) { i += 2; continue; }
            last_tempo = ops[i+1];
            out.push_back(ops[i]); out.push_back(ops[i+1]);
            i += 2; continue;
        }
        // CMD_LFO_DELAY (0xE9) nn: LFO delay (2 bytes) — opt_0
        if (cmd == CMD_LFO_DELAY && i + 1 < ops.size() && opt_0) {
            if ((int)ops[i+1] == last_lfo_delay) { i += 2; continue; }
            last_lfo_delay = ops[i+1];
            out.push_back(ops[i]); out.push_back(ops[i+1]);
            i += 2; continue;
        }
        // CMD_LFO_PITCH (0xEC): pitch LFO (variable) — opt_1
        if (cmd == CMD_LFO_PITCH && i + 1 < ops.size() && opt_1) {
            // ON/OF 为 2 字节, 设定为 6 字节 (waveform + period_be16 + depth_be16)
            int len = (ops[i+1] == 0x80 || ops[i+1] == 0x81) ? 2 : 6;
            for (int j = 0; j < len && (size_t)(i+j) < ops.size(); j++)
                out.push_back(ops[i+j]);
            i += len; continue;
        }
        // CMD_LFO_VOL (0xEB): vol LFO (variable) — opt_2
        if (cmd == CMD_LFO_VOL && i + 1 < ops.size() && opt_2) {
            int len = (ops[i+1] == 0x80 || ops[i+1] == 0x81) ? 2 : 6;
            for (int j = 0; j < len && (size_t)(i+j) < ops.size(); j++)
                out.push_back(ops[i+j]);
            i += len; continue;
        }
        
        // 音符: 2 字节 (0x80-0xDF + duration)
        if (cmd >= 0x80 && cmd <= 0xDF && i + 1 < ops.size()) {
            out.push_back(ops[i]);
            out.push_back(ops[i+1]);
            i += 2; continue;
        }
        
        // 按命令字节长度复制（VGMRips 规格兼容）
        int len = 1;  // 默认 1 字节
        switch (cmd) {
            // 3 字节: opcode + 16 位字
            case 0xF3: // detune (D nn)
            case 0xF2: // portamento
            case 0xFE: // OPM reg write (y reg, val)
            case 0xF6: // repeat start (F6 count 00)
            case 0xF5: // repeat end (+ offset word)
            case 0xF4: // repeat escape (+ offset word)
                len = 3; break;
            // 2 字节: opcode + 1 字节参数
            case 0xFF: // tempo
            case 0xFD: // voice
            case 0xFC: // pan
            case 0xFB: // volume
            case 0xF8: // sound length
            case 0xF0: // keyon delay
            case 0xEF: // sync send
            case 0xED: // freq mode
            case 0xE9: // LFO delay (MD)
                len = 2; break;
            // 1 字节: 仅 opcode
            case 0xFA: // vol down (
            case 0xF9: // vol up )
            case 0xF7: // legato &
            case 0xEE: // sync wait W
            case 0xE8: // PCM8 mode
                len = 1; break;
            // PERF_END: 2 bytes (F1 00=end) or 3 bytes (F1 nn=loop)
            case 0xF1:
                len = (i + 1 < ops.size() && ops[i+1] == 0x00) ? 2 : 3;
                break;
            // FADEOUT: 3 bytes (E7 01 n)
            case 0xE7:
                len = 3; break;
            // LFO 音高/音量: 可变长度
            case 0xEC: // pitch LFO
            case 0xEB: // vol LFO
                if (i + 1 < ops.size() && (ops[i+1] == 0x80 || ops[i+1] == 0x81)) len = 2;
                else len = 6;  // opcode + waveform + period(2) + depth(2)
                break;
            // MH 硬件 LFO: 可变长度
            case 0xEA:
                if (i + 1 < ops.size() && (ops[i+1] == 0x80 || ops[i+1] == 0x81)) len = 2;
                else len = 6;  // opcode + sync_wave + lfrq + pmd_pms + amd_ams + ???
                break;
            default:
                // 0x00-0x7F: 休符 1byte (already handled above)
                // 0x80-0xDF: 音符 2bytes (already handled above)
                len = 1;
                break;
        }
        
        for (int j = 0; j < len && (size_t)(i + j) < ops.size(); j++) {
            out.push_back(ops[i + j]);
        }
        i += len;
    }
    
    ops = std::move(out);
}

// ═══════════════════════════════════════════
// 压缩遍: 连续休符与同音符合并 (-c / #compress)
// ═══════════════════════════════════════════
static void compress_channel(std::vector<uint8_t>& ops, int mode) {
    // mode 0: 仅合并休符
    // mode 1: 合并休符 + 相同音符（用 legato 连接）
    std::vector<uint8_t> out;
    out.reserve(ops.size());
    
    size_t i = 0;
    while (i < ops.size()) {
        uint8_t cmd = ops[i];
        
        // 休符: 0x00-0x7F（持续时间 = byte + 1）
        if (cmd <= 0x7F) {
            int total = cmd + 1;
            size_t j = i + 1;
            while (j < ops.size() && ops[j] <= 0x7F) {
                total += ops[j] + 1;
                j++;
            }
            // 重新编码（最大 128 步/字节）
            while (total > 0) {
                int d = (total > 128) ? 128 : total;
                out.push_back((uint8_t)(d - 1));
                total -= d;
            }
            i = j;
            continue;
        }
        
        // 音符 + 同音合并: 0x80-0xDF nn（音符 + 音长）
        if (cmd >= 0x80 && cmd <= 0xDF && mode >= 1 && i + 1 < ops.size()) {
            uint8_t note = cmd;
            int total = ops[i+1] + 1;
            size_t j = i + 2;
            // 只要 legato(F7) + 相同音符 + duration 连续就合并
            while (j + 2 < ops.size() && ops[j] == 0xF7 && ops[j+1] == note) {
                total += ops[j+2] + 1;
                j += 3;
            }
            // 重新编码
            bool first = true;
            while (total > 0) {
                int d = (total > 256) ? 256 : total;
                if (!first) out.push_back(0xF7); // legato
                out.push_back(note);
                out.push_back((uint8_t)(d - 1));
                total -= d;
                first = false;
            }
            i = j;
            continue;
        }
        
        // 其他命令: 正确判定字节长度后复制
        // ※ 逐字节复制时多字节命令的参数会被误认为休符
        //   这是致命bug，因此需要准确的长度
        int len = 1;
        switch (cmd) {
            case 0xF3: case 0xF2: case 0xFE: case 0xF6:
            case 0xF5: case 0xF4: case 0xE7:
                len = 3; break;
            case 0xFF: case 0xFD: case 0xFC: case 0xFB:
            case 0xF8: case 0xF0: case 0xEF: case 0xED:
            case 0xE9:
                len = 2; break;
            case 0xFA: case 0xF9: case 0xF7: case 0xEE:
            case 0xE8:
                len = 1; break;
            case 0xF1:
                len = (i + 1 < ops.size() && ops[i+1] == 0x00) ? 2 : 3;
                break;
            case 0xEC: case 0xEB:
                if (i + 1 < ops.size() && (ops[i+1] == 0x80 || ops[i+1] == 0x81)) len = 2;
                else len = 6;
                break;
            case 0xEA:
                if (i + 1 < ops.size() && (ops[i+1] == 0x80 || ops[i+1] == 0x81)) len = 2;
                else len = 6;
                break;
            default: len = 1; break;
        }
        for (int j = 0; j < len && (size_t)(i + j) < ops.size(); j++)
            out.push_back(ops[i + j]);
        i += len;
    }
    
    ops = std::move(out);
}

bool compile_mml(const mdx::CompilerConfig& config) {
    // 可变副本（伪指令会修改配置）
    mdx::CompilerConfig cfg = config;

    // 第一阶段：MML 解析 → 每通道 opcode 流
    mdx::Parser parser;
    if (!parser.parse(cfg.input_file, cfg)) {
        if (!cfg.write_on_error) {
            fprintf(stderr, "Compilation failed with %d error(s).\n", parser.error_count());
            return false;
        }
        fprintf(stderr, "Warning: %d error(s), writing output anyway (-e).\n", parser.error_count());
    }

    // 第二阶段：后处理（优化与压缩）
    int num_channels = cfg.ex_pcm ? mdx::MAX_CHANNELS : mdx::STD_CHANNELS;
    
    // 优化遍
    if (!cfg.optimize.empty()) {
        auto& channels = const_cast<std::array<mdx::ChannelState, mdx::MAX_CHANNELS>&>(parser.channels());
        for (int i = 0; i < num_channels; i++) {
            size_t before = channels[i].opcodes.size();
            optimize_channel(channels[i].opcodes, cfg.optimize);
            if (cfg.verbose >= 1 && channels[i].opcodes.size() < before) {
                fprintf(stderr, "  ch%d: optimized %zu -> %zu bytes\n",
                        i, before, channels[i].opcodes.size());
            }
        }
    }
    
    // 压缩遍
    if (cfg.compress >= 0) {
        auto& channels = const_cast<std::array<mdx::ChannelState, mdx::MAX_CHANNELS>&>(parser.channels());
        for (int i = 0; i < num_channels; i++) {
            size_t before = channels[i].opcodes.size();
            compress_channel(channels[i].opcodes, cfg.compress);
            if (cfg.verbose >= 1 && channels[i].opcodes.size() < before) {
                fprintf(stderr, "  ch%d: compressed %zu -> %zu bytes\n",
                        i, before, channels[i].opcodes.size());
            }
        }
    }

    // 第三阶段：MDX 二进制输出
    mdx::MdxWriter writer;
    if (!writer.write(cfg.output_file, cfg.title, cfg.pcm_filename,
                      parser.channels(), num_channels, parser.voices())) {
        fprintf(stderr, "Error: Failed to write output file: %s\n", cfg.output_file.c_str());
        return false;
    }

    fprintf(stderr, "Compiled: %s -> %s (%d channels, %d voices)%s%s\n",
            cfg.input_file.c_str(), cfg.output_file.c_str(),
            num_channels, (int)parser.voices().size(),
            cfg.optimize.empty() ? "" : " [optimized]",
            cfg.compress < 0 ? "" : " [compressed]");

    // 音色数据保存 (-t / #save-tone)
    if (!cfg.tone_save.empty()) {
        std::vector<uint8_t> tone_bin;
        // 256 字节注册位图
        tone_bin.resize(256, 0);
        for (const auto& v : parser.voices()) {
            if (v.number >= 0 && v.number < 256) {
                tone_bin[v.number] = 1;
            }
        }
        // 各音色 27 字节打包数据
        for (int i = 0; i < 256; i++) {
            if (tone_bin[i]) {
                // 搜索对应的音色
                for (const auto& v : parser.voices()) {
                    if (v.number == i) {
                        uint8_t packed[27] = {};
                        mdx::pack_voice(v.params.data(), packed, v.number);
                        tone_bin.insert(tone_bin.end(), packed, packed + 27);
                        break;
                    }
                }
            }
        }
        mdx::write_binary_file(cfg.tone_save, tone_bin);
        fprintf(stderr, "Saved tone data: %s (%zu bytes)\n",
                cfg.tone_save.c_str(), tone_bin.size());
    }

    // 波形数据保存 (-w / #save-wave)
    if (!cfg.wave_save.empty()) {
        std::vector<uint8_t> wave_bin;
        for (const auto& w : parser.waves()) {
            wave_bin.push_back(static_cast<uint8_t>(w.type));
            wave_bin.push_back(static_cast<uint8_t>(w.loop_point));
            int count = (int)w.data.size();
            wave_bin.push_back(static_cast<uint8_t>((count >> 8) & 0xFF));
            wave_bin.push_back(static_cast<uint8_t>(count & 0xFF));
            for (int16_t val : w.data) {
                wave_bin.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
                wave_bin.push_back(static_cast<uint8_t>(val & 0xFF));
            }
        }
        mdx::write_binary_file(cfg.wave_save, wave_bin);
        fprintf(stderr, "Saved wave data: %s (%zu bytes)\n",
                cfg.wave_save.c_str(), wave_bin.size());
    }

    return parser.error_count() == 0;
}

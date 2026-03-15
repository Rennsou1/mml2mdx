// mml2mdx — 页面（Page）合并引擎
// 功能: 将同一通道的多个页面 MML（按优先级）合并为单一 opcode 流
// 实现: 展平各页面 opcode → 按 tick 优先级合并 → 重新生成 opcode
#include "mml2mdx.h"
#include <algorithm>
#include <cstdio>

namespace mdx {

// ═══════════════════════════════════════════
// opcode 字节长度判定（与 compiler.cpp 中的保持一致）
// ═══════════════════════════════════════════
static int opcode_length(const std::vector<uint8_t>& ops, size_t pos) {
    if (pos >= ops.size()) return 1;
    uint8_t cmd = ops[pos];

    // 休符: 0x00-0x7F → 1 字节
    if (cmd <= 0x7F) return 1;
    // 音符: 0x80-0xDF → 2 字节 (note + duration)
    if (cmd >= 0x80 && cmd <= 0xDF) return 2;

    switch (cmd) {
        // 3 字节
        case CMD_DETUNE:       // F3 nn nn
        case CMD_PORTAMENTO:   // F2 nn nn
        case CMD_OPM_REG:      // FE reg val
        case CMD_REPEAT_START: // F6 count 00
        case CMD_REPEAT_END:   // F5 nn nn
        case CMD_REPEAT_ESC:   // F4 nn nn
        case CMD_FADEOUT:      // E7 01 n
            return 3;
        // 2 字节
        case CMD_TEMPO:        // FF nn
        case CMD_VOICE:        // FD nn
        case CMD_PAN:          // FC nn
        case CMD_VOLUME:       // FB nn
        case CMD_SOUND_LEN:    // F8 nn
        case CMD_KEYON_DELAY:  // F0 nn
        case CMD_SYNC_SEND:    // EF nn
        case CMD_FREQ_MODE:    // ED nn
        case CMD_LFO_DELAY:    // E9 nn
            return 2;
        // 1 字节
        case CMD_VOL_DOWN:     // FA
        case CMD_VOL_UP:       // F9
        case CMD_LEGATO:       // F7
        case CMD_SYNC_WAIT:    // EE
        case CMD_PCM8_MODE:    // E8
            return 1;
        // PERF_END: 可变
        case CMD_PERF_END:
            return (pos + 1 < ops.size() && ops[pos + 1] == 0x00) ? 2 : 3;
        // LFO: 可变
        case CMD_LFO_PITCH:
        case CMD_LFO_VOL:
            return (pos + 1 < ops.size() && (ops[pos + 1] == 0x80 || ops[pos + 1] == 0x81)) ? 2 : 6;
        case CMD_MH_LFO:
            return (pos + 1 < ops.size() && (ops[pos + 1] == 0x80 || ops[pos + 1] == 0x81)) ? 2 : 6;
        default:
            return 1;
    }
}

// ═══════════════════════════════════════════
// 展平器: opcode 流 → PageSegment 列表
// 展开所有 [] 循环，跟踪 tick 位置和通道状态
// ═══════════════════════════════════════════

// 循环展开用的栈帧
struct LoopFrame {
    size_t start_ip;    // 循环体开始位置 (F6 之后)
    int count;          // 总循环次数
    int iteration;      // 当前迭代 (0-based)
    size_t escape_ip;   // / 跳出位置 (F4 之后)
    size_t end_ip;      // ] 结束位置 (F5 之后)
};

std::vector<PageSegment> flatten_opcodes(
    const std::vector<uint8_t>& opcodes, int& infinite_loop_tick) {

    std::vector<PageSegment> segments;
    infinite_loop_tick = -1;

    if (opcodes.empty()) return segments;

    // 状态跟踪
    int tick = 0;
    int voice = -1;
    int volume_byte = -1;
    int pan = -1;
    std::vector<uint8_t> pending_opcodes;  // 非音符/休符命令的累积

    // 循环展开栈
    std::vector<LoopFrame> loop_stack;
    constexpr int MAX_TICKS = 192 * 1024;  // 安全上限 (约 1024 全音符)

    size_t ip = 0;
    while (ip < opcodes.size() && tick < MAX_TICKS) {
        uint8_t cmd = opcodes[ip];
        int len = opcode_length(opcodes, ip);

        // ── 音符 (0x80-0xDF) ──
        if (cmd >= NOTE_MIN && cmd <= NOTE_MAX && ip + 1 < opcodes.size()) {
            int dur = opcodes[ip + 1] + 1;
            PageSegment seg;
            seg.start_tick = tick;
            seg.duration = dur;
            seg.is_note = true;
            seg.note_byte = cmd;
            seg.voice = voice;
            seg.volume_byte = volume_byte;
            seg.pan = pan;
            seg.prefix_opcodes = std::move(pending_opcodes);
            pending_opcodes.clear();
            segments.push_back(std::move(seg));
            tick += dur;
            ip += 2;
            continue;
        }

        // ── 休符 (0x00-0x7F) ──
        if (cmd <= REST_MAX) {
            int dur = cmd + 1;
            PageSegment seg;
            seg.start_tick = tick;
            seg.duration = dur;
            seg.is_note = false;
            seg.note_byte = 0;
            seg.voice = voice;
            seg.volume_byte = volume_byte;
            seg.pan = pan;
            seg.prefix_opcodes = std::move(pending_opcodes);
            pending_opcodes.clear();
            segments.push_back(std::move(seg));
            tick += dur;
            ip += 1;
            continue;
        }

        // ── 状态跟踪命令 ──
        if (cmd == CMD_VOICE && ip + 1 < opcodes.size()) {
            voice = opcodes[ip + 1];
            // 也加入 pending（合并时需要完整恢复）
            for (int j = 0; j < len && ip + j < opcodes.size(); j++)
                pending_opcodes.push_back(opcodes[ip + j]);
            ip += len;
            continue;
        }
        if (cmd == CMD_VOLUME && ip + 1 < opcodes.size()) {
            volume_byte = opcodes[ip + 1];
            for (int j = 0; j < len && ip + j < opcodes.size(); j++)
                pending_opcodes.push_back(opcodes[ip + j]);
            ip += len;
            continue;
        }
        if (cmd == CMD_PAN && ip + 1 < opcodes.size()) {
            pan = opcodes[ip + 1];
            for (int j = 0; j < len && ip + j < opcodes.size(); j++)
                pending_opcodes.push_back(opcodes[ip + j]);
            ip += len;
            continue;
        }

        // ── 循环展开 ──
        if (cmd == CMD_REPEAT_START && ip + 2 < opcodes.size()) {
            int count = opcodes[ip + 1];
            if (count < 1) count = 2;
            LoopFrame frame;
            frame.start_ip = ip + 3;  // F6 之后的第一个字节
            frame.count = count;
            frame.iteration = 0;
            frame.escape_ip = 0;
            frame.end_ip = 0;

            // 预扫描找到对应的 F5 和 F4
            int nest = 0;
            size_t scan = ip + 3;
            while (scan < opcodes.size()) {
                uint8_t sc = opcodes[scan];
                if (sc == CMD_REPEAT_START) {
                    nest++;
                    scan += 3;
                } else if (sc == CMD_REPEAT_END && nest > 0) {
                    nest--;
                    scan += 3;
                } else if (sc == CMD_REPEAT_END && nest == 0) {
                    frame.end_ip = scan + 3;
                    break;
                } else if (sc == CMD_REPEAT_ESC && nest == 0) {
                    frame.escape_ip = scan;
                    scan += opcode_length(opcodes, scan);
                } else {
                    scan += opcode_length(opcodes, scan);
                }
            }

            loop_stack.push_back(frame);
            ip += 3;
            continue;
        }

        if (cmd == CMD_REPEAT_ESC && !loop_stack.empty()) {
            auto& frame = loop_stack.back();
            // 最后一次迭代: 跳过到循环结束
            if (frame.iteration == frame.count - 1) {
                ip = frame.end_ip;
                loop_stack.pop_back();
            } else {
                ip += len;
            }
            continue;
        }

        if (cmd == CMD_REPEAT_END && !loop_stack.empty()) {
            auto& frame = loop_stack.back();
            frame.iteration++;
            if (frame.iteration < frame.count) {
                // 回到循环开始
                ip = frame.start_ip;
            } else {
                // 循环完成
                ip += 3;
                loop_stack.pop_back();
            }
            continue;
        }

        // ── 无限循环标记 (CMD_PERF_END 用作循环指示) ──
        // PERF_END 不在展平中处理（由 Parser::parse 末尾处理）
        if (cmd == CMD_PERF_END) {
            // 检查是否是无限循环 (非 0x00 参数)
            if (ip + 1 < opcodes.size() && opcodes[ip + 1] != 0x00) {
                // 这是循环回跳，忽略（不应在页面 MML 中出现）
            }
            // 跳过整个 PERF_END
            ip += len;
            continue;
        }

        // ── 连音 F7 ──
        if (cmd == CMD_LEGATO) {
            pending_opcodes.push_back(cmd);
            ip += 1;
            continue;
        }

        // ── 其他命令: 记入 pending ──
        for (int j = 0; j < len && ip + j < opcodes.size(); j++) {
            pending_opcodes.push_back(opcodes[ip + j]);
        }
        ip += len;
    }

    return segments;
}

// ═══════════════════════════════════════════
// 合并引擎: 多页面段列表 → 单一 opcode 流
// ═══════════════════════════════════════════

// 在 segments 中查找 tick 位置对应的段索引
// 返回 -1 表示 tick 超出范围
static int find_segment_at(const std::vector<PageSegment>& segs, int tick) {
    for (int i = 0; i < (int)segs.size(); i++) {
        if (tick >= segs[i].start_tick &&
            tick < segs[i].start_tick + segs[i].duration) {
            return i;
        }
    }
    return -1;
}

// 发出音符 opcode（处理超过 256 ticks 的分割）
static void emit_note(std::vector<uint8_t>& out, int note_byte, int duration) {
    bool first = true;
    while (duration > 0) {
        int d = (duration > MAX_DURATION) ? MAX_DURATION : duration;
        if (!first) {
            out.push_back(CMD_LEGATO);
        }
        out.push_back(static_cast<uint8_t>(note_byte));
        out.push_back(static_cast<uint8_t>(d - 1));
        duration -= d;
        first = false;
    }
}

// 发出休符 opcode（处理超过 128 ticks 的分割）
static void emit_rest(std::vector<uint8_t>& out, int duration) {
    while (duration > 0) {
        int d = (duration > 128) ? 128 : duration;
        out.push_back(static_cast<uint8_t>(d - 1));
        duration -= d;
    }
}

std::vector<uint8_t> merge_pages(
    const std::vector<std::vector<PageSegment>>& pages,
    int infinite_loop_tick) {

    if (pages.empty()) return {};
    if (pages.size() == 1 && infinite_loop_tick < 0) {
        // 只有一个页面，无需合并 — 但仍需从段重建 opcode
        // (因为循环已被展开)
    }

    // 计算总 tick 数
    int total_ticks = 0;
    for (const auto& page : pages) {
        for (const auto& seg : page) {
            int end = seg.start_tick + seg.duration;
            if (end > total_ticks) total_ticks = end;
        }
    }

    if (total_ticks == 0) return {};

    std::vector<uint8_t> out;
    out.reserve(total_ticks * 2);  // 粗略预分配

    int tick = 0;
    int cur_voice = -1;
    int cur_volume = -1;
    int cur_pan = -1;
    bool l_emitted = false;

    while (tick < total_ticks) {
        // L 标记: 在对应 tick 记录偏移
        if (infinite_loop_tick >= 0 && tick >= infinite_loop_tick && !l_emitted) {
            // merge_pages 不输出 CMD_PERF_END
            // 但需要在正确位置标记 infinite_loop_offset
            // 这里我们记录当前输出位置，调用者会用它设置 infinite_loop_offset
            // 暂时用一个特殊处理: 返回后由调用者扫描
            l_emitted = true;
        }

        // 找出最高优先级的活跃页面
        int active_page = -1;
        int active_seg = -1;
        for (int p = 0; p < (int)pages.size(); p++) {
            int si = find_segment_at(pages[p], tick);
            if (si >= 0 && pages[p][si].is_note) {
                active_page = p;
                active_seg = si;
                break;
            }
        }

        if (active_page >= 0) {
            const auto& seg = pages[active_page][active_seg];

            // 发出页面切换的状态命令（仅在值变化时）
            if (seg.voice >= 0 && seg.voice != cur_voice) {
                out.push_back(CMD_VOICE);
                out.push_back(static_cast<uint8_t>(seg.voice));
                cur_voice = seg.voice;
            }
            if (seg.volume_byte >= 0 && seg.volume_byte != cur_volume) {
                out.push_back(CMD_VOLUME);
                out.push_back(static_cast<uint8_t>(seg.volume_byte));
                cur_volume = seg.volume_byte;
            }
            if (seg.pan >= 0 && seg.pan != cur_pan) {
                out.push_back(CMD_PAN);
                out.push_back(static_cast<uint8_t>(seg.pan));
                cur_pan = seg.pan;
            }

            // 发出该段的前缀命令（LFO, detune 等）
            // 但跳过已输出的 voice/volume/pan 命令
            for (size_t i = 0; i < seg.prefix_opcodes.size(); ) {
                uint8_t pc = seg.prefix_opcodes[i];
                int plen = 1;
                // 跳过 voice/volume/pan（已在上面处理）
                if (pc == CMD_VOICE || pc == CMD_VOLUME || pc == CMD_PAN) {
                    plen = 2;
                    i += plen;
                    continue;
                }
                // 其他命令照搬
                // 判定长度
                if (pc >= NOTE_MIN && pc <= NOTE_MAX) plen = 2;
                else if (pc <= REST_MAX) plen = 1;
                else {
                    // 用简化判定
                    switch (pc) {
                        case CMD_DETUNE: case CMD_PORTAMENTO: case CMD_OPM_REG:
                        case CMD_REPEAT_START: case CMD_REPEAT_END: case CMD_REPEAT_ESC:
                        case CMD_FADEOUT:
                            plen = 3; break;
                        case CMD_TEMPO: case CMD_SOUND_LEN: case CMD_KEYON_DELAY:
                        case CMD_SYNC_SEND: case CMD_FREQ_MODE: case CMD_LFO_DELAY:
                            plen = 2; break;
                        case CMD_VOL_DOWN: case CMD_VOL_UP: case CMD_LEGATO:
                        case CMD_SYNC_WAIT: case CMD_PCM8_MODE:
                            plen = 1; break;
                        case CMD_LFO_PITCH: case CMD_LFO_VOL: case CMD_MH_LFO:
                            plen = (i + 1 < seg.prefix_opcodes.size() &&
                                    (seg.prefix_opcodes[i+1] == 0x80 ||
                                     seg.prefix_opcodes[i+1] == 0x81)) ? 2 : 6;
                            break;
                        default: plen = 1; break;
                    }
                }
                for (int j = 0; j < plen && i + j < seg.prefix_opcodes.size(); j++) {
                    out.push_back(seg.prefix_opcodes[i + j]);
                }
                i += plen;
            }

            // 计算此刻到段结束的剩余 tick
            int seg_remaining = (seg.start_tick + seg.duration) - tick;
            emit_note(out, seg.note_byte, seg_remaining);
            tick += seg_remaining;
        } else {
            // 所有页面都在休符 — 找到下一个音符事件
            int next_note_tick = total_ticks;
            for (const auto& page : pages) {
                for (const auto& seg : page) {
                    if (seg.is_note && seg.start_tick > tick) {
                        if (seg.start_tick < next_note_tick)
                            next_note_tick = seg.start_tick;
                        break;  // 段按时间排序，找到第一个就够
                    }
                }
            }

            int rest_dur = next_note_tick - tick;
            if (rest_dur <= 0) rest_dur = 1;  // 安全护栏
            emit_rest(out, rest_dur);
            tick += rest_dur;
        }
    }

    return out;
}

} // namespace mdx

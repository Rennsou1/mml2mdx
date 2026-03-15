// mml2mdx — 波形效果处理
// 编译时将音符分割并插入命令的系统
#include "mml2mdx.h"
#include <vector>
#include <cstdlib>

namespace mdx {

// ═══════════════════════════════════════════
// 波形效果的音符分割
// AP→p, DT/TD→D, VM/MV→v/@v, KM→y, TL→y, YA→y
// ═══════════════════════════════════════════

// 从波形数据获取值（带循环）
int get_wave_value(const WaveData& wave, int index) {
    if (wave.data.empty()) return 0;
    int len = (int)wave.data.size();
    if (index < len) return wave.data[index];
    // 循环处理
    if (wave.type == 1 && wave.loop_point < len) {
        int loop_len = len - wave.loop_point;
        if (loop_len <= 0) return wave.data.back();
        int loop_idx = (index - len) % loop_len;
        return wave.data[wave.loop_point + loop_idx];
    }
    return wave.data.back(); // 保持最终值
}

// 单个波形效果的命令生成
// wave_val: 波形数据值, scale: 倍率
// base_volume: VM/MV 用的基础音量（波形值作为偏移加算）
void emit_wave_effect_cmd(
    std::vector<uint8_t>& opcodes,
    ChannelState::WaveEffectType type,
    int wave_val,
    int scale,
    int ch_idx,
    int tl_op_mask,
    int tl_tone_num,
    int base_volume)
{
    int val = wave_val * scale;
    
    switch (type) {
    case ChannelState::WE_AP:
        // auto-pan → FC pan (0-3)
        opcodes.push_back(0xFC); // CMD_PAN
        opcodes.push_back(static_cast<uint8_t>(val & 0x03));
        break;
        
    case ChannelState::WE_DT:
    case ChannelState::WE_TD:
        // detune → F3 nn (signed word)
        opcodes.push_back(0xF3); // CMD_DETUNE
        opcodes.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        opcodes.push_back(static_cast<uint8_t>(val & 0xFF));
        break;
        
    case ChannelState::WE_VM:
    case ChannelState::WE_MV: {
        // volume → FB v (base_volume + offset, clamped 0-15)
        int v = base_volume + val;
        if (v < 0) v = 0;
        if (v > 15) v = 15;
        opcodes.push_back(0xFB); // CMD_VOLUME
        opcodes.push_back(static_cast<uint8_t>(v & 0xFF));
        break;
    }
        
    case ChannelState::WE_KM:
        // PMS/AMS → FE $38+ch, val (OPM reg $38 = PMS/AMS)
        opcodes.push_back(0xFE); // CMD_OPM_REG
        opcodes.push_back(static_cast<uint8_t>(0x38 + (ch_idx & 0x07)));
        opcodes.push_back(static_cast<uint8_t>(val & 0xFF));
        break;
        
    case ChannelState::WE_TL: {
        // total level → FE $60+offset, val (per operator)
        // TL 是衰减量: 值越大 = 音量越小，波形值取反并 clamp
        // note.x: TL = clamp(-wave_val * scale, 0, 127)
        int mask = tl_op_mask;
        if (mask < 0) mask = 0x07; // auto: M1+C1+M2 (skip C2)
        // note.x 的算子输出顺序: M1, C1, M2, C2
        static const int op_offset[] = {0x00, 0x10, 0x08, 0x18};
        int tl_val = -val; // 取反: 正的波形值 → TL 减小（音量增）
        if (tl_val < 0) tl_val = 0;
        if (tl_val > 127) tl_val = 127;
        for (int op = 0; op < 4; op++) {
            if (mask & (1 << op)) {
                opcodes.push_back(0xFE); // CMD_OPM_REG
                opcodes.push_back(static_cast<uint8_t>(0x60 + op_offset[op] + (ch_idx & 0x07)));
                opcodes.push_back(static_cast<uint8_t>(tl_val & 0x7F));
            }
        }
        break;
    }
        
    case ChannelState::WE_YA:
        // y-command → FE reg, val
        // wave data format: [reg, val, reg, val, ...]
        // 单个值的情况是 reg=wave_val 直接输出
        opcodes.push_back(0xFE); // CMD_OPM_REG
        opcodes.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        opcodes.push_back(static_cast<uint8_t>(val & 0xFF));
        break;
        
    default:
        break;
    }
}

// 将音符按波形效果分割并输出
// 返回值: true = 已分割（调用方应跳过普通音符输出）
bool apply_wave_effects(
    ChannelState& ch,
    int note_byte,        // 0x80-0xDF (NOTE_MIN + note) or rest (-1)
    int duration,         // 音长（步数）
    int ch_idx,           // 通道索引
    const std::vector<WaveData>& waves,
    const CompilerConfig& config,
    bool is_tied)          // 下一个音符连音
{
    // 确认是否有活动的效果
    bool any_active = false;
    for (int i = 0; i < ChannelState::WE_COUNT; i++) {
        if (ch.wave_effects[i].active) {
            any_active = true;
            break;
        }
    }
    if (!any_active) return false;
    
    // 计算最小步长（所有活动效果的 step_count 的最小值）
    int min_step = duration; // 将整体作为一个区间的回退方案
    for (int i = 0; i < ChannelState::WE_COUNT; i++) {
        auto& we = ch.wave_effects[i];
        if (!we.active || we.wave_num < 0) continue;
        if (we.step_count > 0 && we.step_count < min_step) {
            min_step = we.step_count;
        }
    }
    
    if (min_step <= 0) min_step = 1;
    
    // 分割数
    int segments = duration / min_step;
    int remainder = duration % min_step;
    if (segments <= 1 && remainder == 0) {
        // 无需分割 — 只插入各效果的初始值命令然后正常输出
        for (int i = 0; i < ChannelState::WE_COUNT; i++) {
            auto& we = ch.wave_effects[i];
            if (!we.active || we.wave_num < 0) continue;
            
            // 搜索波形数据
            const WaveData* wd = nullptr;
            for (const auto& w : waves) {
                if (w.number == we.wave_num) { wd = &w; break; }
            }
            if (!wd || wd->data.empty()) continue;
            
            int val = get_wave_value(*wd, we.phase);
            // 波形效果命令输出
            {
                emit_wave_effect_cmd(ch.opcodes, 
                    static_cast<ChannelState::WaveEffectType>(i),
                    val, we.scale, ch_idx, ch.tl_op_mask, ch.tl_tone_num, ch.volume_v);
            }
        }
        return false; // 使用普通音符输出
    }
    
    // 分割并输出 — note.x 兼容算法:
    //   在所有段前插入效果命令
    //   除最后一段外其他都添加 F7 (legato)
    //   最后一段不加 F7（实际的 key-on/key-off）
    int total_segs = segments + (remainder > 0 ? 1 : 0);
    
    for (int seg = 0; seg < total_segs; seg++) {
        int seg_dur = (seg < segments) ? min_step : remainder;
        if (seg_dur <= 0) continue;
        
        bool is_last = (seg == total_segs - 1);
        
        // 各活动效果的命令插入
        for (int i = 0; i < ChannelState::WE_COUNT; i++) {
            auto& we = ch.wave_effects[i];
            if (!we.active || we.wave_num < 0) continue;
            
            // 延迟检查
            int eff_delay = we.delay;
            if (eff_delay < 0 && we.sync_mode == 1) {
                eff_delay = duration + eff_delay;
                if (eff_delay < 0) eff_delay = 0;
            } else if (eff_delay < 0) {
                eff_delay = 0;
            }
            if (eff_delay > 0 && we.step_counter < eff_delay) {
                we.step_counter += seg_dur;
                continue;
            }
            
            // 搜索波形数据
            const WaveData* wd = nullptr;
            for (const auto& w : waves) {
                if (w.number == we.wave_num) { wd = &w; break; }
            }
            if (!wd || wd->data.empty()) continue;
            
            int val = get_wave_value(*wd, we.phase);
            {
                emit_wave_effect_cmd(ch.opcodes,
                    static_cast<ChannelState::WaveEffectType>(i),
                    val, we.scale, ch_idx, ch.tl_op_mask, ch.tl_tone_num, ch.volume_v);
            }
            
            // 步进计数器更新
            we.step_counter += seg_dur;
            if (we.step_counter >= we.step_count) {
                we.step_counter -= we.step_count;
                we.phase++;
            }
        }
        
        // 除最后一段外都添加 legato (F7)
        // 如果 is_tied 为 true 则最后一段也添加 F7
        if (!is_last || is_tied) {
            ch.opcodes.push_back(0xF7); // CMD_LEGATO
        }
        
        // 音符/休符输出
        if (note_byte >= 0) {
            ch.opcodes.push_back(static_cast<uint8_t>(note_byte));
            ch.opcodes.push_back(static_cast<uint8_t>(seg_dur - 1));
        } else {
            // 休符
            ch.opcodes.push_back(static_cast<uint8_t>(seg_dur - 1));
        }
    }
    
    return true; // 已分割所以跳过普通输出
}

// 同步模式1（sync）用: 音符边界处重置相位
// note.x 兼容: 重置前输出当前 phase 0 值的命令（携带传递）
void wave_effects_note_sync(
    ChannelState& ch,
    int ch_idx,
    const std::vector<WaveData>& waves,
    const CompilerConfig& config)
{
    for (int i = 0; i < ChannelState::WE_COUNT; i++) {
        auto& we = ch.wave_effects[i];
        if (we.active && we.sync_mode == 1) {
            // 携带传递: 仅在已使用过（非首次）时输出
            bool has_previous = (we.phase > 0 || we.step_counter > 0);
            if (has_previous && we.wave_num >= 0) {
                const WaveData* wd = nullptr;
                for (const auto& w : waves) {
                    if (w.number == we.wave_num) { wd = &w; break; }
                }
                if (wd && !wd->data.empty()) {
                    int val = get_wave_value(*wd, 0); // phase 0
                    emit_wave_effect_cmd(ch.opcodes,
                        static_cast<ChannelState::WaveEffectType>(i),
                        val, we.scale, ch_idx, ch.tl_op_mask, ch.tl_tone_num, ch.volume_v);
                }
            }
            we.phase = 0;
            we.step_counter = 0;
        }
    }
}

} // namespace mdx

// mml2mdx — 工具函数 实现

#include "util.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace mdx {

bool read_file(const std::string& path, std::string& content) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        fprintf(stderr, "Error: Cannot open file: %s\n", path.c_str());
        return false;
    }
    std::stringstream ss;
    ss << ifs.rdbuf();
    content = ss.str();

    // 跳过 UTF-8 BOM
    if (content.size() >= 3 &&
        (uint8_t)content[0] == 0xEF &&
        (uint8_t)content[1] == 0xBB &&
        (uint8_t)content[2] == 0xBF) {
        content = content.substr(3);
    }

    return true;
}

bool write_binary_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) {
        fprintf(stderr, "Error: Cannot create file: %s\n", path.c_str());
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
    return ofs.good();
}

// 将 NOTE 47 参数打包为 MDX 27 字节格式
// 参照 MXDRV 源码 (mxdrvg_core.h):
//   L0012be: 音色查找 — byte0 为音色编号 (查找键)
//            匹配后 S0004 指向 byte1 (跳过编号)
//            每条记录 27 字节 (0x1a + 1)
//   L000d84: 音色加载 — 从 S0004 (byte1) 开始读 26 字节:
//     byte  1:    (FL << 3) | ALG
//     byte  2:    OP_MASK (raw)
//     byte  3-6:  (DT1_n << 4) | MUL_n   → OPM $40+ch  (slot 0,1,2,3)
//     byte  7-10: TL_n                    → OPM $60+ch
//     byte 11-14: (KS_n << 6) | AR_n      → OPM $80+ch
//     byte 15-18: (AME_n << 7) | DR_n     → OPM $A0+ch
//     byte 19-22: (DT2_n << 6) | SR_n     → OPM $C0+ch
//     byte 23-26: (SL_n << 4) | RR_n      → OPM $E0+ch
//
// NOTE OP1-4 → YM2151 slot 映射: OP1=M1(s0), OP2=C1(s2), OP3=M2(s1), OP4=C2(s3)
// 打包时重排为 slot 顺序: OP1, OP3, OP2, OP4
void pack_voice(const int params[VOICE_PARAMS], uint8_t out[VOICE_BINARY_SIZE],
                int voice_number) {
    for (int i = 0; i < VOICE_BINARY_SIZE; i++) out[i] = 0;

    // byte 0: 音色编号 (MXDRV 查找键)
    out[0] = (uint8_t)(voice_number & 0xFF);

    int alg     = params[VOICE_ALG]     & 0x07;
    int fl      = params[VOICE_FL]      & 0x07;
    int op_mask = params[VOICE_OP_MASK] & 0x0F;

    out[1] = (uint8_t)((fl << 3) | alg);
    out[2] = (uint8_t)(op_mask);

    // slot 映射: {OP1→s0, OP3→s1, OP2→s2, OP4→s3}
    static const int slot_map[4] = {0, 2, 1, 3};

    for (int slot = 0; slot < NUM_OPERATORS; slot++) {
        int op = slot_map[slot];
        const int* p = &params[op * PARAMS_PER_OP];
        int ar  = p[OP_AR]  & 0x1F;
        int dr  = p[OP_DR]  & 0x1F;
        int sr  = p[OP_SR]  & 0x1F;
        int rr  = p[OP_RR]  & 0x0F;
        int sl  = p[OP_SL]  & 0x0F;
        int tl  = p[OP_TL]  & 0x7F;
        int ks  = p[OP_KS]  & 0x03;
        int mul = p[OP_MUL] & 0x0F;
        int dt1 = p[OP_DT1] & 0x07;
        int dt2 = p[OP_DT2] & 0x03;
        int ame = p[OP_AME] & 0x01;

        out[3  + slot] = (uint8_t)((dt1 << 4) | mul);
        out[7  + slot] = (uint8_t)(tl);
        out[11 + slot] = (uint8_t)((ks << 6) | ar);
        out[15 + slot] = (uint8_t)((ame << 7) | dr);
        out[19 + slot] = (uint8_t)((dt2 << 6) | sr);
        out[23 + slot] = (uint8_t)((sl << 4) | rr);
    }
}

int tempo_to_timer_b(int bpm) {
    // Timer-B 周期 = (1024 * (256 - timer_b)) / 4 微秒
    // 1 tick = Timer-B 周期
    // BPM = 60 / (tick * 48) = 60 * 4 / (1024 * (256 - tb) * 48 / 1000000)
    // → tb = 256 - 60000000 / (bpm * 48 * 1024 / 4)
    // → tb = 256 - 60000000 * 4 / (bpm * 48 * 1024)
    // → tb = 256 - 240000000 / (bpm * 49152)
    if (bpm <= 0) return 200; // 默认值
    double tb = 256.0 - 240000000.0 / (bpm * 49152.0);
    int result = static_cast<int>(tb + 0.5);
    if (result < 0) result = 0;
    if (result > 255) result = 255;
    return result;
}

std::string get_directory(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return ".";
    return path.substr(0, pos);
}

std::string join_path(const std::string& dir, const std::string& file) {
    if (dir.empty() || dir == ".") return file;
    // 如果 file 已经是绝对路径，直接返回
    if (file.size() >= 2 && file[1] == ':') return file;  // Windows 盘符
    if (!file.empty() && (file[0] == '/' || file[0] == '\\')) return file;
    char last = dir.back();
    if (last == '/' || last == '\\') return dir + file;
    return dir + "/" + file;
}

void report_error(const std::string& file, int line, const std::string& msg) {
    fprintf(stderr, "%s line %d : Error - %s\n", file.c_str(), line, msg.c_str());
}

void report_warning(const std::string& file, int line, const std::string& msg) {
    fprintf(stderr, "%s line %d : Warning - %s\n", file.c_str(), line, msg.c_str());
}

} // namespace mdx

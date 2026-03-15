// mml2mdx — 解析器 实现（桩）
#include "parser.h"
#include "util.h"
#include <cctype>
#include <cstring>
#include <algorithm>

namespace mdx {

bool Parser::parse(const std::string& input_file, CompilerConfig& config) {
    config_ = &config;
    error_count_ = 0;

    // 重置通道状态
    for (auto& ch : channels_) {
        ch = ChannelState{};
    }
    voices_.clear();
    waves_.clear();

    // 加载源文件（处理 #include）
    std::string base_dir = get_directory(input_file);
    if (!lexer_.load(input_file, base_dir)) {
        return false;
    }

    // 逐行处理
    const auto& lines = lexer_.lines();
    bool in_nlist = false;

    for (int i = 0; i < (int)lines.size(); i++) {
        const std::string& line = lines[i];

        // 跳过空行
        if (line.empty()) continue;

        // 检查 #nlist / #list
        if (in_nlist) {
            // 检查是否遇到 #list
            std::string trimmed = line;
            size_t p = 0;
            while (p < trimmed.size() && (trimmed[p] == ' ' || trimmed[p] == '\t')) p++;
            if (trimmed.compare(p, 5, "#list") == 0) {
                in_nlist = false;
            }
            continue;
        }

        // 检查是否进入 #nlist 区域
        {
            size_t p = 0;
            while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) p++;
            if (line.compare(p, 6, "#nlist") == 0) {
                in_nlist = true;
                continue;
            }
        }

        process_line(line, i + 1);
    }

    // 自动检测 ex-pcm：如果 Q-W 通道有数据，启用 16 通道模式
    if (!config.ex_pcm) {
        for (int i = 9; i < MAX_CHANNELS; i++) {
            if (!channels_[i].opcodes.empty()) {
                config.ex_pcm = true;
                break;
            }
        }
    }

    int num_channels = config.ex_pcm ? MAX_CHANNELS : STD_CHANNELS;

    // 16 通道模式: Ch A 首字节须为 0xE8 (PCM8 expansion marker)
    // note.x 兼容: 即使 ChA 没有数据也插入 E8
    if (config.ex_pcm) {
        channels_[0].opcodes.insert(channels_[0].opcodes.begin(), CMD_PCM8_MODE);
    }

    // 给没有无限循环的通道添加 performance end
    for (int i = 0; i < num_channels; i++) {
        auto& ch = channels_[i];
        if (ch.opcodes.empty()) continue;

        if (ch.has_infinite_loop) {
            // 无限循环: F1 + 负偏移返回循环点 (3 字节)
            int offset = ch.infinite_loop_offset - (int)ch.opcodes.size() - 2;
            ch.opcodes.push_back(CMD_PERF_END);
            write_be16(ch.opcodes, static_cast<int16_t>(offset));
        } else {
            // 演奏结束: F1 00 (2 字节)
            // VGMRips: "0xf1 0x00 • Performance end."
            ch.opcodes.push_back(CMD_PERF_END);
            ch.opcodes.push_back(0x00);
        }
    }

    return error_count_ == 0;
}

void Parser::process_line(const std::string& line, int line_num) {
    // 找到第一个非空字符
    size_t pos = 0;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
    if (pos >= line.size()) return;

    char first = line[pos];

    // 根据行首字符分类
    if (first == '#') {
        process_pseudo_cmd(line, line_num);
        return;
    }

    // 音色定义: @N = { ... }
    if (first == '@') {
        if (pos + 1 < line.size()) {
            char second = line[pos + 1];
            if (second == 'w' || second == 'W') {
                process_wave_def(line, line_num);
                return;
            }
            if (second == 'k' || second == 'K') {
                process_keymap_def(line, line_num);
                return;
            }
            if (second == '@') {
                process_tone_macro_def(line, line_num);
                return;
            }
            // @ 后跟数字 = 音色定义
            size_t p = pos + 1;
            while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) p++;
            if (p < line.size() && (line[p] >= '0' && line[p] <= '9')) {
                // 检查是否有 '=' → 音色定义
                size_t eq = line.find('=', p);
                if (eq != std::string::npos) {
                    process_tone_def(line, line_num);
                    return;
                }
            }
        }
    }

    // MML 行: A-H, P-W 通道标识符
    std::vector<int> channel_indices;
    while (pos < line.size()) {
        int idx = channel_index(line[pos]);
        if (idx >= 0) {
            // 避免重复
            bool found = false;
            for (int ci : channel_indices) {
                if (ci == idx) { found = true; break; }
            }
            if (!found) {
                channel_indices.push_back(idx);
            }
            pos++;
        } else {
            break;
        }
    }

    if (!channel_indices.empty()) {
        // 通道标识符后面是 MML 数据
        std::string mml_content = line.substr(pos);
        process_mml_line(mml_content, line_num, channel_indices);
        return;
    }

    // 宏定义行: 以有效宏变量名开头且包含 '=' 的行
    // 有效变量名: I J K L M N O X Y Z, a-z, 半角片假名
    if ((first >= 'I' && first <= 'Z') || (first >= 'a' && first <= 'z')) {
        // 排除 MML 通道标识符 (A-H, P-W)
        if (channel_index(first) < 0) {
            size_t eq = line.find('=', pos);
            if (eq != std::string::npos) {
                process_macro_def(line, line_num);
                return;
            }
        }
    }
}

void Parser::process_pseudo_cmd(const std::string& line, int line_num) {
    size_t pos = 0;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
    if (line[pos] != '#') return;
    pos++;

    // 提取命令名
    size_t cmd_start = pos;
    while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t' && 
           line[pos] != '"' && line[pos] != '\'') {
        pos++;
    }
    std::string cmd = line.substr(cmd_start, pos - cmd_start);

    // 转小写
    for (auto& c : cmd) c = tolower(c);

    // 跳过空白到参数
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;

    // 提取字符串参数（如果有引号）
    auto read_string_arg = [&]() -> std::string {
        if (pos >= line.size()) return "";
        if (line[pos] == '"' || line[pos] == '\'') {
            char quote = line[pos++];
            std::string result;
            while (pos < line.size() && line[pos] != quote) {
                result += line[pos++];
            }
            if (pos < line.size()) pos++;  // 跳过闭合引号
            return result;
        }
        // 无引号: 取到空白为止
        size_t start = pos;
        while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') {
            pos++;
        }
        return line.substr(start, pos - start);
    };

    // 读取整数参数
    auto read_int_arg = [&]() -> int {
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
        bool neg = false;
        if (pos < line.size() && line[pos] == '-') { neg = true; pos++; }
        int val = 0;
        while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
            val = val * 10 + (line[pos] - '0');
            pos++;
        }
        return neg ? -val : val;
    };

    // 处理各伪指令
    if (cmd == "title") {
        config_->title = read_string_arg();
    } else if (cmd == "pcmfile") {
        config_->pcm_filename = read_string_arg();
    } else if (cmd == "ex-pcm") {
        config_->ex_pcm = true;
    } else if (cmd == "octave-rev") {
        config_->octave_rev = true;
    } else if (cmd == "tps") {
        config_->transpose = read_int_arg();
    } else if (cmd == "tps-all") {
        config_->tps_all = true;
    } else if (cmd == "detune") {
        config_->detune_offset = read_int_arg();
    } else if (cmd == "toneofs") {
        config_->tone_offset = read_int_arg();
    } else if (cmd == "overwrite") {
        config_->overwrite_tone = true;
    } else if (cmd == "noreturn") {
        config_->no_return = true;
    } else if (cmd == "wavemem") {
        config_->wavemem = true;
    } else if (cmd == "remove") {
        config_->remove_on_error = true;
    } else if (cmd == "beep") {
        config_->beep_on_error = true;
    } else if (cmd == "cont") {
        config_->cont_mode = read_int_arg();
    } else if (cmd == "wcmd") {
        config_->wcmd_mode = read_int_arg();
    } else if (cmd == "reste") {
        int val = 0;
        if (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
            val = read_int_arg();
        }
        config_->reste_mode = val;
    } else if (cmd == "nreste") {
        int val = 0;
        if (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
            val = read_int_arg();
        }
        config_->reste_mode = -(val + 1); // 用负值区分
    } else if (cmd == "coder") {
        config_->coder = true;
    } else if (cmd == "ncoder") {
        config_->coder = false;
    } else if (cmd == "glide") {
        config_->glide_mode = read_int_arg();
    } else if (cmd == "flat") {
        std::string notes = read_string_arg();
        for (char c : notes) {
            int ni = -1;
            switch (tolower(c)) {
                case 'c': ni = 0; break; case 'd': ni = 1; break;
                case 'e': ni = 2; break; case 'f': ni = 3; break;
                case 'g': ni = 4; break; case 'a': ni = 5; break;
                case 'b': ni = 6; break;
            }
            if (ni >= 0) config_->global_note_modifiers[ni] = -1;
        }
    } else if (cmd == "sharp") {
        std::string notes = read_string_arg();
        for (char c : notes) {
            int ni = -1;
            switch (tolower(c)) {
                case 'c': ni = 0; break; case 'd': ni = 1; break;
                case 'e': ni = 2; break; case 'f': ni = 3; break;
                case 'g': ni = 4; break; case 'a': ni = 5; break;
                case 'b': ni = 6; break;
            }
            if (ni >= 0) config_->global_note_modifiers[ni] = 1;
        }
    } else if (cmd == "natural" || cmd == "normal") {
        std::string notes = read_string_arg();
        if (notes.empty()) {
            config_->global_note_modifiers.fill(0);
        } else {
            for (char c : notes) {
                int ni = -1;
                switch (tolower(c)) {
                    case 'c': ni = 0; break; case 'd': ni = 1; break;
                    case 'e': ni = 2; break; case 'f': ni = 3; break;
                    case 'g': ni = 4; break; case 'a': ni = 5; break;
                    case 'b': ni = 6; break;
                }
                if (ni >= 0) config_->global_note_modifiers[ni] = 0;
            }
        }
    } else if (cmd == "include") {
        // TODO: #include 处理（已在 Lexer 中初步处理）
    } else if (cmd == "nlist") {
        // TODO: #nlist 处理
    } else if (cmd == "list") {
        // 恢复列表模式
    } else if (cmd == "compress") {
        if (config_->compress < 0) {  // 命令行优先
            int val = 0;
            if (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
                val = read_int_arg();
            }
            config_->compress = val;
        }
    } else if (cmd == "opt") {
        std::string arg = read_string_arg();
        if (arg.empty()) {
            config_->optimize.clear();
        } else if (arg == "*") {
            config_->optimize = "dvqpt012";
        } else {
            config_->optimize = arg;
        }
    } else if (cmd == "play") {
        // 忽略（Windows 下不执行）
    } else if (cmd == "ver") {
        // 详细模式
        if (config_->verbose < 0) {
            config_->verbose = read_int_arg();
        }
    } else if (cmd == "load-tone") {
        // 音色二进制数据加载
        // 格式: 256 字节注册位图 + 各音色 27 字节打包数据
        std::string filename = read_string_arg();
        if (!filename.empty()) {
            std::string content;
            std::string dir = get_directory(config_->input_file);
            if (read_file(join_path(dir, filename), content)) {
                const uint8_t* data = (const uint8_t*)content.data();
                size_t size = content.size();
                if (size >= 256) {
                    // 开头 256 字节: 注册位图 (0=未定义, 非0=已定义)
                    size_t offset = 256;
                    for (int i = 0; i < 256 && offset + 27 <= size; i++) {
                        if (data[i] != 0) {
                            // 将 27 字节的打包音色数据展开
                            VoiceData vd;
                            vd.number = i;
                            // 打包 → 47 参数的逆转换
                            const uint8_t* v = data + offset;
                            for (int op = 0; op < 4; op++) {
                                vd.params[op * PARAMS_PER_OP + OP_DT1] = (v[0 + op] >> 4) & 0x07;
                                vd.params[op * PARAMS_PER_OP + OP_MUL] = v[0 + op] & 0x0F;
                                vd.params[op * PARAMS_PER_OP + OP_TL]  = v[4 + op] & 0x7F;
                                vd.params[op * PARAMS_PER_OP + OP_KS]  = (v[8 + op] >> 6) & 0x03;
                                vd.params[op * PARAMS_PER_OP + OP_AR]  = v[8 + op] & 0x1F;
                                vd.params[op * PARAMS_PER_OP + OP_AME] = (v[12 + op] >> 7) & 0x01;
                                vd.params[op * PARAMS_PER_OP + OP_DR]  = v[12 + op] & 0x1F;
                                vd.params[op * PARAMS_PER_OP + OP_DT2] = (v[16 + op] >> 6) & 0x03;
                                vd.params[op * PARAMS_PER_OP + OP_SR]  = v[16 + op] & 0x1F;
                                vd.params[op * PARAMS_PER_OP + OP_SL]  = (v[20 + op] >> 4) & 0x0F;
                                vd.params[op * PARAMS_PER_OP + OP_RR]  = v[20 + op] & 0x0F;
                            }
                            vd.params[VOICE_ALG]     = v[24] & 0x07;
                            vd.params[VOICE_FL]      = (v[24] >> 3) & 0x07;
                            vd.params[VOICE_OP_MASK] = (v[25] >> 4) & 0x0F;
                            // 注册
                            bool found = false;
                            for (auto& existing : voices_) {
                                if (existing.number == i) {
                                    existing = vd; found = true; break;
                                }
                            }
                            if (!found) voices_.push_back(vd);
                            offset += 27;
                        }
                    }
                }
            }
        }
    } else if (cmd == "save-tone") {
        // 音色二进制数据保存（转换结束时执行）
        std::string filename = read_string_arg();
        if (filename.empty()) filename = "tone.bin";
        if (config_->tone_save.empty()) {  // 命令行优先
            config_->tone_save = filename;
        }
    } else if (cmd == "load-wave") {
        // 波形二进制数据加载
        // 格式: 各波形 = type(1) + loop_point(1) + count_hi(1) + count_lo(1) + data(count*2)
        std::string filename = read_string_arg();
        if (!filename.empty()) {
            std::string content;
            std::string dir = get_directory(config_->input_file);
            if (read_file(join_path(dir, filename), content)) {
                const uint8_t* data = (const uint8_t*)content.data();
                size_t size = content.size();
                size_t offset = 0;
                int wave_num = 0;
                while (offset + 4 <= size && wave_num < 128) {
                    int type = data[offset];
                    int loop_pt = data[offset + 1];
                    int count = (data[offset + 2] << 8) | data[offset + 3];
                    offset += 4;
                    if (count > 512) count = 512;
                    if (offset + count * 2 > size) break;
                    
                    WaveData wd;
                    wd.type = type;
                    wd.loop_point = loop_pt;
                    wd.data.resize(count);
                    for (int j = 0; j < count; j++) {
                        wd.data[j] = (int16_t)((data[offset] << 8) | data[offset + 1]);
                        offset += 2;
                    }
                    // 注册（覆盖已有，不足则追加）
                    while ((int)waves_.size() <= wave_num) {
                        waves_.push_back(WaveData());
                    }
                    waves_[wave_num] = wd;
                    wave_num++;
                }
            }
        }
    } else if (cmd == "save-wave") {
        // 波形二进制数据保存
        std::string filename = read_string_arg();
        if (filename.empty()) filename = "wave.bin";
        if (config_->wave_save.empty()) {
            config_->wave_save = filename;
        }
    } else if (cmd == "pcmlist") {
        // PCM 使用状况文件输出
        int val = 0;
        if (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
            val = read_int_arg();
        }
        config_->pcm_list = true;
        if (val == 1) config_->pcm_list_or = true;
    } else if (cmd == "compress") {
        // #compress [0|1] — 音长压缩（命令行优先）
        if (config_->compress < 0) {  // 仅在命令行未指定时
            int val = 0;
            if (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
                val = read_int_arg();
            }
            config_->compress = val;  // 0=仅休符, 1=音符也压缩
        }
    } else if (cmd == "opt") {
        // #opt ["dvqpt012*"] — 优化（命令行优先）
        if (config_->optimize.empty()) {  // 仅在命令行未指定时
            std::string flags = read_string_arg();
            if (flags.empty()) {
                config_->optimize.clear();  // 无参数→停止优化
            } else if (flags == "*") {
                config_->optimize = "dvqpt012";
            } else {
                config_->optimize = flags;
            }
        }
    } else if (cmd == "ver") {
        // #ver [0|1] — 详细模式
        if (config_->verbose < 0) {
            int val = 0;
            if (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
                val = read_int_arg();
            }
            config_->verbose = val;
        }
    }
    // 未知的伪指令则忽略
}

void Parser::process_tone_def(const std::string& line, int line_num) {
    // @N = { param1, param2, ..., param47 }
    size_t pos = 0;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
    if (line[pos] != '@') return;
    pos++;

    // 读取音色编号
    int tone_num = 0;
    while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
        tone_num = tone_num * 10 + (line[pos] - '0');
        pos++;
    }

    // #toneofs: 音色定义也应用偏移 (@1={...} → slot 1+offset)
    tone_num += config_->tone_offset;

    // 找到 '{'
    while (pos < line.size() && line[pos] != '{') pos++;
    if (pos >= line.size()) {
        error(line_num, "Expected '{' in tone definition");
        return;
    }
    pos++;

    // 读取参数直到 '}'
    // 参数可能跨多行（在 lexer 展开后是同一行的连续行）
    // 先尝试在同一行找到所有参数
    VoiceData voice;
    voice.number = tone_num;
    int param_count = 0;

    // 简化: 收集到 '}' 之间的所有数字
    std::string params_str;
    // 当前行剩余部分
    for (; pos < line.size(); pos++) {
        if (line[pos] == '}') break;
        params_str += line[pos];
    }

    // 解析参数（按逗号和空白分隔，跳过注释）
    size_t p = 0;
    while (p < params_str.size() && param_count < VOICE_PARAMS) {
        // 跳过空白和逗号
        while (p < params_str.size() && 
               (params_str[p] == ' ' || params_str[p] == '\t' || 
                params_str[p] == ',' || params_str[p] == '\n' || params_str[p] == '\r')) {
            p++;
        }

        // 跳过注释 /* ... */
        if (p + 1 < params_str.size() && params_str[p] == '/' && params_str[p+1] == '*') {
            p += 2;
            while (p + 1 < params_str.size()) {
                if (params_str[p] == '*' && params_str[p+1] == '/') {
                    p += 2;
                    break;
                }
                p++;
            }
            continue;
        }

        // 跳过行注释
        if (p < params_str.size() && (params_str[p] == ';' ||
            (p + 1 < params_str.size() && params_str[p] == '/' && params_str[p+1] == '/'))) {
            // 跳到行末
            while (p < params_str.size() && params_str[p] != '\n') p++;
            continue;
        }

        if (p >= params_str.size()) break;

        // 读取数字
        if ((params_str[p] >= '0' && params_str[p] <= '9') || params_str[p] == '-') {
            bool neg = false;
            if (params_str[p] == '-') { neg = true; p++; }
            int val = 0;
            while (p < params_str.size() && params_str[p] >= '0' && params_str[p] <= '9') {
                val = val * 10 + (params_str[p] - '0');
                p++;
            }
            voice.params[param_count++] = neg ? -val : val;
        } else {
            p++;  // 跳过未知字符
        }
    }

    if (param_count != VOICE_PARAMS) {
        warning(line_num, "Tone definition expects " + std::to_string(VOICE_PARAMS) + 
                " params, got " + std::to_string(param_count));
    }

    // 检查是否已存在相同编号的音色
    bool found = false;
    for (auto& v : voices_) {
        if (v.number == tone_num) {
            if (config_->overwrite_tone) {
                v = voice;
            }
            found = true;
            break;
        }
    }
    if (!found) {
        voices_.push_back(voice);
    }
}

void Parser::process_wave_def(const std::string& line, int line_num) {
    // @wN = { type, loop_point, data... }
    size_t pos = 0;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
    if (line[pos] != '@') return;
    pos++; // skip @
    if (pos < line.size() && (line[pos] == 'w' || line[pos] == 'W')) pos++;
    
    // 读取波形编号
    int wave_num = 0;
    while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
        wave_num = wave_num * 10 + (line[pos] - '0');
        pos++;
    }
    
    // 跳过空白和 '='
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t' || line[pos] == '=')) pos++;
    
    // 找 '{'
    while (pos < line.size() && line[pos] != '{') pos++;
    if (pos < line.size()) pos++; // skip '{'
    
    WaveData wave;
    wave.number = wave_num;
    
    // 解析参数（按逗号分隔）
    std::vector<int16_t> values;
    while (pos < line.size() && line[pos] != '}') {
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t' || line[pos] == ',')) pos++;
        if (pos >= line.size() || line[pos] == '}') break;
        
        // 支持 $hex 表示
        int val = 0;
        bool neg = false;
        if (line[pos] == '-') { neg = true; pos++; }
        if (pos < line.size() && line[pos] == '$') {
            pos++;
            while (pos < line.size() && ((line[pos] >= '0' && line[pos] <= '9') || 
                   (line[pos] >= 'a' && line[pos] <= 'f') || (line[pos] >= 'A' && line[pos] <= 'F'))) {
                if (line[pos] >= '0' && line[pos] <= '9') val = val * 16 + (line[pos] - '0');
                else if (line[pos] >= 'a' && line[pos] <= 'f') val = val * 16 + (line[pos] - 'a' + 10);
                else val = val * 16 + (line[pos] - 'A' + 10);
                pos++;
            }
        } else {
            while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
                val = val * 10 + (line[pos] - '0');
                pos++;
            }
        }
        values.push_back(static_cast<int16_t>(neg ? -val : val));
    }
    
    // values[0] = type, values[1] = loop_point, values[2..] = data
    if (values.size() >= 2) {
        wave.type = values[0];
        wave.loop_point = values[1];
        wave.data.assign(values.begin() + 2, values.end());
    }
    
    // 追加或覆盖
    bool found = false;
    for (auto& w : waves_) {
        if (w.number == wave_num) { w = wave; found = true; break; }
    }
    if (!found) waves_.push_back(wave);
}

void Parser::process_keymap_def(const std::string& line, int line_num) {
    // @kN = { 96 values }
    size_t pos = 0;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
    if (line[pos] != '@') return;
    pos++;
    if (pos < line.size() && (line[pos] == 'k' || line[pos] == 'K')) pos++;
    
    int map_num = 0;
    while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
        map_num = map_num * 10 + (line[pos] - '0');
        pos++;
    }
    
    while (pos < line.size() && line[pos] != '{') pos++;
    if (pos < line.size()) pos++;
    
    KeyMap km;
    km.number = map_num;
    int idx = 0;
    
    while (pos < line.size() && line[pos] != '}' && idx < KEY_MAP_ENTRIES) {
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t' || line[pos] == ',')) pos++;
        if (pos >= line.size() || line[pos] == '}') break;
        int val = 0;
        while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
            val = val * 10 + (line[pos] - '0');
            pos++;
        }
        km.map[idx++] = val;
    }
    
    bool found = false;
    for (auto& k : keymaps_) {
        if (k.number == map_num) { k = km; found = true; break; }
    }
    if (!found) keymaps_.push_back(km);
}

void Parser::process_tone_macro_def(const std::string& line, int line_num) {
    // @@N = "mml content"
    size_t pos = 0;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
    if (line[pos] != '@' || pos + 1 >= line.size() || line[pos + 1] != '@') return;
    pos += 2;
    
    int tone_num = 0;
    while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
        tone_num = tone_num * 10 + (line[pos] - '0');
        pos++;
    }
    
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t' || line[pos] == '=')) pos++;
    
    // 读取引号内容
    std::string content;
    if (pos < line.size() && (line[pos] == '"' || line[pos] == '\'')) {
        char quote = line[pos++];
        while (pos < line.size() && line[pos] != quote) {
            content += line[pos++];
        }
    }
    
    tone_macros_[tone_num] = content;
}

void Parser::process_macro_def(const std::string& line, int line_num) {
    // varname[subscript] = "content"
    size_t pos = 0;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
    
    // 读取变量名
    std::string varname;
    varname += line[pos++];
    
    // 读取可选下标
    int subscript = -1;
    if (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
        subscript = 0;
        while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
            subscript = subscript * 10 + (line[pos] - '0');
            pos++;
        }
    }
    
    // 构建键名
    std::string key = varname;
    if (subscript >= 0) key += "_" + std::to_string(subscript);
    
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t' || line[pos] == '=')) pos++;
    
    // 读取引号内容
    std::string content;
    if (pos < line.size() && (line[pos] == '"' || line[pos] == '\'')) {
        char quote = line[pos++];
        while (pos < line.size() && line[pos] != quote) {
            content += line[pos++];
        }
    }
    
    macros_[key] = content;
}

void Parser::process_mml_line(const std::string& mml, int line_num, 
                               const std::vector<int>& channel_indices) {
    // 检查通道別指定 |...:...|
    size_t pipe_pos = mml.find('|');
    if (pipe_pos != std::string::npos && channel_indices.size() > 1) {
        // 找到 |...:...| 结构
        size_t end_pipe = mml.find('|', pipe_pos + 1);
        if (end_pipe != std::string::npos) {
            std::string before = mml.substr(0, pipe_pos);
            std::string inside = mml.substr(pipe_pos + 1, end_pipe - pipe_pos - 1);
            std::string after = mml.substr(end_pipe + 1);
            
            // 按 ':' 分割 inside
            std::vector<std::string> parts;
            size_t p = 0;
            while (p < inside.size()) {
                size_t colon = inside.find(':', p);
                if (colon == std::string::npos) {
                    parts.push_back(inside.substr(p));
                    break;
                }
                parts.push_back(inside.substr(p, colon - p));
                p = colon + 1;
            }
            
            // 按通道顺序分配（注意: 按通道序号从小到大）
            std::vector<int> sorted_chs = channel_indices;
            std::sort(sorted_chs.begin(), sorted_chs.end());
            
            for (size_t i = 0; i < sorted_chs.size(); i++) {
                std::string ch_mml = before;
                if (i < parts.size()) {
                    ch_mml += parts[i];
                }
                ch_mml += after;
                parse_mml(ch_mml, line_num, sorted_chs[i], sorted_chs);
            }
            return;
        }
    }
    
    // 普通模式: 所有通道相同的 MML
    for (int ch_idx : channel_indices) {
        parse_mml(mml, line_num, ch_idx, channel_indices);
    }
}

// 核心 MML 解析：逐字符解析 MML 命令并编译为 opcode
void Parser::parse_mml(const std::string& mml, int line_num, int ch_idx,
                       const std::vector<int>& all_channels) {
    ChannelState& ch = channels_[ch_idx];
    chord_channels_ = all_channels;  // 为和弦保存通道列表
    MMLTokenizer tok(mml, line_num);

    while (!tok.at_end()) {
        tok.skip_whitespace();
        if (tok.at_end()) break;

        char c = tok.peek();

        // 注释处理
        if (c == ';' || c == '*') break;  // 行末注释
        if (c == '/' && tok.peek2() == '/') break;
        if (c == '/' && tok.peek2() == '*') break;  // TODO: 块注释

        // ═══════════════════════════════════════════
        // 通用波形效果解析 lambda (P2)
        // 各 handler (KM/MV/TD/TL/VM/DT/AP/YA) 共用
        // ═══════════════════════════════════════════
        auto parse_wave_effect = [&](ChannelState::WaveEffectType we_type) {
            auto& we = ch.wave_effects[we_type];
            // 检查 ON/OF/D/S/L 后缀
            if (tok.peek() == 'O') {
                tok.advance();
                if (tok.peek() == 'N') { tok.advance(); we.active = true; }
                else if (tok.peek() == 'F') {
                    tok.advance();
                    // note.x 兼容: 效果 OFF 时输出恢复值
                    // 按类型处理:
                    //   AP/DT/TD: val0 + wave[phase-1]（最后使用的值）
                    //   VM/MV/KM/TL: val0 × 2（初始值输出 2 次）
                    //   YA: 仅输出 val0 1 次
                    if (we.active && we.wave_num >= 0) {
                        const WaveData* wd = nullptr;
                        for (const auto& w : waves_) {
                            if (w.number == we.wave_num) { wd = &w; break; }
                        }
                        if (wd && !wd->data.empty()) {
                            int val0 = get_wave_value(*wd, 0);
                            if (we_type == ChannelState::WE_YA) {
                                // YA: 仅输出 val0 1 次
                                emit_wave_effect_cmd(ch.opcodes, we_type,
                                    val0, 1, ch_idx, ch.tl_op_mask, ch.tl_tone_num, ch.volume_v);
                            } else {
                                // 第 1 次: val0
                                emit_wave_effect_cmd(ch.opcodes, we_type,
                                    val0, 1, ch_idx, ch.tl_op_mask, ch.tl_tone_num, ch.volume_v);
                                // 第 2 次: 仅 AP 输出最后的值，其他输出 val0
                                int val2;
                                if (we_type == ChannelState::WE_AP) {
                                    // 仅 AP: 输出最后使用的 pan 值
                                    int last_p = (we.phase > 0) ? we.phase - 1 : 0;
                                    val2 = get_wave_value(*wd, last_p);
                                } else {
                                    // 其他: 再次输出 val0
                                    val2 = val0;
                                }
                                emit_wave_effect_cmd(ch.opcodes, we_type,
                                    val2, 1, ch_idx, ch.tl_op_mask, ch.tl_tone_num, ch.volume_v);
                            }
                        }
                    }
                    we.active = false;
                }
            } else if (tok.peek() == 'D') {
                tok.advance();
                int val; if (tok.try_read_int(val)) we.delay = val;
            } else if (tok.peek() == 'S') {
                tok.advance();
                int val; if (tok.try_read_int(val)) we.scale = val;
            } else if (tok.peek() == 'L') {
                tok.advance();
                int val; if (tok.try_read_int(val)) we.phase = val;
            } else {
                // XX%1,%2,%3: wave_num, step_count, sync_mode
                int w, s, m;
                if (tok.try_read_int(w)) {
                    we.wave_num = w;
                    tok.skip_whitespace();
                    if (tok.peek() == ',') tok.advance();
                    if (tok.try_read_int(s)) {
                        we.step_count = s;
                        tok.skip_whitespace();
                        if (tok.peek() == ',') tok.advance();
                        if (tok.try_read_int(m)) {
                            we.sync_mode = m;
                        }
                    }
                    we.active = true;
                    we.phase = 0;
                    we.step_counter = 0;
                }
            }
        };

        // ═══════════════════════════════════════════
        // 和弦输入 ｢｣ / `` — 将音符分配到各通道
        // ═══════════════════════════════════════════
        // NOTE: ｢ = 0xA2 in Shift-JIS (half-width katakana ｢)
        //       ｣ = 0xA3 in Shift-JIS (half-width katakana ｣)
        //       ` (backtick) = ASCII 0x60
        if (c == '`' || (uint8_t)c == 0xA2) {
            tok.advance();
            char end_char = (c == '`') ? '`' : (char)(uint8_t)0xA3;
            // 双反引号 `` 是单一的和弦分隔符
            bool double_tick = (c == '`' && tok.peek() == '`');
            if (double_tick) tok.advance();
            
            // 收集音符・休符（< > 调整八度）
            struct ChordEntry { int abs_note; bool is_rest; };
            std::vector<ChordEntry> chord_notes;
            int tmp_octave = ch.octave;
            
            while (!tok.at_end() && tok.peek() != end_char) {
                tok.skip_whitespace();
                char cc = tok.peek();
                if (cc == end_char) break;
                
                if (cc >= 'a' && cc <= 'g') {
                    tok.advance();
                    static const int nm[] = {9,11,0,2,4,5,7};
                    int note = nm[cc - 'a'];
                    int mod = ch.note_modifiers[cc-'a'] + config_->global_note_modifiers[cc-'a'];
                    if (tok.peek() == '+') { mod = 1; tok.advance(); }
                    else if (tok.peek() == '-') { mod = -1; tok.advance(); }
                    else if (tok.peek() == '=') { mod = 0; tok.advance(); }
                    note += mod;
                    int abs = tmp_octave * NOTES_PER_OCT + note + ch.transpose + config_->transpose;
                    chord_notes.push_back({abs, false});
                } else if (cc == 'r') {
                    tok.advance();
                    chord_notes.push_back({-1, true});
                } else if (cc == '<') {
                    tok.advance();
                    tmp_octave += config_->octave_rev ? 1 : -1;
                } else if (cc == '>') {
                    tok.advance();
                    tmp_octave += config_->octave_rev ? -1 : 1;
                } else {
                    tok.advance(); // skip unknown
                }
            }
            if (tok.peek() == end_char) {
                tok.advance();
                // 双反引号: 关闭侧也消费 2 个字符
                if (double_tick && tok.peek() == end_char) tok.advance();
            }
            
            // 读取音长
            int dur = tok.read_duration(ch.base_length);
            
            // 保存到记忆 (# 和弦重复用)
            last_chord_.clear();
            for (auto& cn : chord_notes) {
                last_chord_.push_back({cn.abs_note, 0});
            }
            
            if (double_tick) {
                // 双反引号 ``...``: 在当前通道依次输出所有音符
                std::vector<int> sorted_chs = chord_channels_;
                // #coder: 休符填充 (pre) — 最少 1 个休符
                if (config_->coder) {
                    size_t pad = (chord_notes.size() < sorted_chs.size())
                                 ? sorted_chs.size() - chord_notes.size() : 1;
                    for (size_t ni = 0; ni < pad; ni++) {
                        int rem = dur;
                        while (rem > 0) { int d = (rem>128)?128:rem; ch.opcodes.push_back((uint8_t)(d-1)); rem -= d; }
                    }
                }
                // 输出所有音符
                for (size_t ni = 0; ni < chord_notes.size(); ni++) {
                    auto& cn = chord_notes[ni];
                    if (cn.is_rest) {
                        int rem = dur;
                        while (rem > 0) { int d = (rem>128)?128:rem; ch.opcodes.push_back((uint8_t)(d-1)); rem -= d; }
                    } else {
                        int mdx_note = cn.abs_note - 3;
                        if (mdx_note >= 0 && mdx_note <= 95) {
                            int nb = NOTE_MIN + mdx_note;
                            ch.opcodes.push_back((uint8_t)nb);
                            ch.opcodes.push_back((uint8_t)(dur-1));
                            ch.last_abs_note = cn.abs_note;
                            ch.last_duration = dur;
                        }
                    }
                }
                // #coder: 休符填充 (post) — 最少 1 个休符
                if (config_->coder) {
                    size_t pad = (chord_notes.size() < sorted_chs.size())
                                 ? sorted_chs.size() - chord_notes.size() : 1;
                    for (size_t ni = 0; ni < pad; ni++) {
                        int rem = dur;
                        while (rem > 0) { int d = (rem>128)?128:rem; ch.opcodes.push_back((uint8_t)(d-1)); rem -= d; }
                    }
                }
            } else {
                // 单反引号 `...`: 每个通道分配 1 个音符
                std::vector<int> sorted_chs = chord_channels_;
                std::sort(sorted_chs.begin(), sorted_chs.end());
                // 确定当前通道的索引
                int my_slot = -1;
                for (size_t i = 0; i < sorted_chs.size(); i++) {
                    if (sorted_chs[i] == ch_idx) { my_slot = (int)i; break; }
                }
                if (my_slot >= 0 && my_slot < (int)chord_notes.size()) {
                    auto& cn = chord_notes[my_slot];
                    if (cn.is_rest) {
                        int rem = dur;
                        while (rem > 0) { int d = (rem>128)?128:rem; ch.opcodes.push_back((uint8_t)(d-1)); rem -= d; }
                    } else {
                        int mdx_note = cn.abs_note - 3;
                        if (mdx_note >= 0 && mdx_note <= 95) {
                            int nb = NOTE_MIN + mdx_note;
                            ch.opcodes.push_back((uint8_t)nb);
                            ch.opcodes.push_back((uint8_t)(dur-1));
                            ch.last_abs_note = cn.abs_note;
                            ch.last_duration = dur;
                        }
                    }
                }
                // 不足的通道不输出任何内容（通道保持为空）
            }
            continue;
        }
        
        // # (MML 行内): 和弦重复 — 重复之前的和弦
        if (c == '#' && !last_chord_.empty()) {
            tok.advance();
            int dur = tok.read_duration(ch.base_length);
            
            std::vector<int> sorted_chs = chord_channels_;
            std::sort(sorted_chs.begin(), sorted_chs.end());
            
            // 为了写入所有通道，仅在第一个通道时处理
            if (ch_idx != sorted_chs[0]) {
                continue;
            }
            
            // 将所有音符依次输出到所有通道
            for (size_t ci = 0; ci < sorted_chs.size(); ci++) {
                ChannelState& tgt = channels_[sorted_chs[ci]];
                for (size_t ni = 0; ni < last_chord_.size(); ni++) {
                    auto& cn = last_chord_[ni];
                    if (cn.abs_note < 0) {
                        int rem = dur;
                        while (rem > 0) { int d = (rem>128)?128:rem; tgt.opcodes.push_back((uint8_t)(d-1)); rem -= d; }
                    } else {
                        int mdx = cn.abs_note - 3;
                        if (mdx >= 0 && mdx <= 95) {
                            tgt.opcodes.push_back((uint8_t)(NOTE_MIN + mdx));
                            tgt.opcodes.push_back((uint8_t)(dur-1));
                        }
                    }
                }
            }
            continue;
        }

        // 连符 {} — tuplet: 将总音长平均分配给内部音符
        if (c == '{') {
            tok.advance();
            // 第 1 阶段: 收集 {} 内的音符/休符（最多 32 个）
            struct TupletNote { int abs_note; bool is_rest; };
            std::vector<TupletNote> notes;
            int saved_octave = ch.octave;
            
            while (!tok.at_end() && tok.peek() != '}') {
                tok.skip_whitespace();
                char tc = tok.peek();
                if (tc == '}') break;
                
                if (tc >= 'a' && tc <= 'g') {
                    tok.advance();
                    static const int note_map[] = {9, 11, 0, 2, 4, 5, 7};
                    int note = note_map[tc - 'a'];
                    int mod = ch.note_modifiers[tc - 'a'] + config_->global_note_modifiers[tc - 'a'];
                    if (tok.peek() == '+') { mod = 1; tok.advance(); }
                    else if (tok.peek() == '-') { mod = -1; tok.advance(); }
                    else if (tok.peek() == '=') { mod = 0; tok.advance(); }
                    note += mod;
                    int abs = ch.octave * NOTES_PER_OCT + note + ch.transpose + config_->transpose;
                    notes.push_back({abs, false});
                } else if (tc == 'r') {
                    tok.advance();
                    notes.push_back({0, true});
                } else if (tc == 'o') {
                    tok.advance();
                    int val; if (tok.try_read_int(val)) ch.octave = val;
                } else if (tc == '<') {
                    tok.advance();
                    ch.octave += config_->octave_rev ? 1 : -1;
                } else if (tc == '>') {
                    tok.advance();
                    ch.octave += config_->octave_rev ? -1 : 1;
                } else {
                    tok.advance(); // skip unknown
                }
                if (notes.size() >= 32) break;
            }
            if (tok.peek() == '}') tok.advance();
            
            // 第 2 阶段: 读取 } 后的总音长
            int total_dur = tok.read_duration(ch.base_length);
            
            // 第 3 阶段: 分割并输出
            if (!notes.empty()) {
                int per_note = total_dur / (int)notes.size();
                int remainder = total_dur % (int)notes.size();
                for (size_t ni = 0; ni < notes.size(); ni++) {
                    int dur = per_note + (ni < (size_t)remainder ? 1 : 0);
                    if (dur < 1) dur = 1;
                    // note.x 中连符内的每个音是独立的键事件
                    // （不用 legato 连接）
                    if (notes[ni].is_rest) {
                        ch.opcodes.push_back(static_cast<uint8_t>(dur - 1));
                    } else {
                        int mdx_note = notes[ni].abs_note - 3;
                        if (mdx_note >= 0 && mdx_note <= 95) {
                            ch.opcodes.push_back(static_cast<uint8_t>(NOTE_MIN + mdx_note));
                            ch.opcodes.push_back(static_cast<uint8_t>(dur - 1));
                            ch.last_abs_note = notes[ni].abs_note;
                            ch.last_duration = dur;
                        }
                    }
                }
            }
            continue;
        }

        // 音符 a-g
        if (c >= 'a' && c <= 'g') {
            tok.advance();
            // 音名→半音偏移 (c=0, d=2, e=4, f=5, g=7, a=9, b=11)
            static const int note_map[] = {9, 11, 0, 2, 4, 5, 7}; // a,b,c,d,e,f,g
            int note = note_map[c - 'a'];

            // 升降号
            int modifier = ch.note_modifiers[c - 'a'] + config_->global_note_modifiers[c - 'a'];

            if (tok.peek() == '+') { modifier = 1; tok.advance(); }
            else if (tok.peek() == '-') { modifier = -1; tok.advance(); }
            else if (tok.peek() == '=' || tok.peek() == '"') { modifier = 0; tok.advance(); }

            note += modifier;

            // 計算絶対音程番号
            int abs_note = ch.octave * NOTES_PER_OCT + note;
            abs_note += ch.transpose + config_->transpose;

            // 読取音長
            int duration = tok.read_duration(ch.base_length);

            // Portamento pending: 在前一个音符前插入 F2(rate)+F7
            // note.x 方式: c_d → F2(rate) F7 c4 (源音符以滑音播放)
            // 目标音符 d 不输出（因为 pitch bend 会到达）
            if (ch.portamento_pending && ch.last_abs_note >= 0 && ch.last_duration > 0 &&
                ch.last_note_opcode_pos >= 0) {
                int delta_semitones = abs_note - ch.last_abs_note;
                constexpr int PITCH_PER_SEMITONE = 16384;
                int rate = delta_semitones * PITCH_PER_SEMITONE / ch.last_duration;

                // 在源音符前插入 F2(rate)
                // 如果F7 (legato) 已在源音符的正前方则仅插入 F2
                // 否则同时插入 F2 + F7
                int insert_pos = ch.last_note_opcode_pos;
                bool has_legato = (insert_pos > 0 &&
                                   ch.opcodes[insert_pos - 1] == CMD_LEGATO);

                std::vector<uint8_t> insert_bytes;
                insert_bytes.push_back(CMD_PORTAMENTO);
                int16_t rate16 = static_cast<int16_t>(rate);
                insert_bytes.push_back(static_cast<uint8_t>((rate16 >> 8) & 0xFF));
                insert_bytes.push_back(static_cast<uint8_t>(rate16 & 0xFF));
                if (!has_legato) {
                    insert_bytes.push_back(CMD_LEGATO);
                }

                // 如果已有 F7 则在 F7 之前插入 (F2 在前)
                int actual_insert_pos = has_legato ? insert_pos - 1 : insert_pos;
                ch.opcodes.insert(
                    ch.opcodes.begin() + actual_insert_pos,
                    insert_bytes.begin(), insert_bytes.end());

                ch.portamento_pending = false;
                // 目标音符不输出 — 仅更新信息
                ch.last_abs_note = abs_note;
                // last_duration 不变更（滑音以源音符的音长完成）
                ch.last_note_opcode_pos = -1; // 插入后重置
                // 滑音后的 & 是隐式的 (F7 已插入) → 消费
                if (tok.peek() == '&') {
                    tok.advance();
                }
                continue;
            }

            // 編碼音符 (MXDRV: 0x80 = o0d+ = chromatic 3)
            int mdx_note = abs_note - 3;
            if (mdx_note >= 0 && mdx_note <= 95) {
                int note_byte = NOTE_MIN + mdx_note;

                // KS: 音色自动切换（根据音程选择音色）
                if (ch.ks_on && ch.ks_map >= 0 && ch.ks_map < (int)keymaps_.size()) {
                    int ks_voice = keymaps_[ch.ks_map].map[mdx_note];
                    if (ks_voice != ch.voice) {
                        ch.voice = ks_voice;
                        ch.opcodes.push_back(CMD_VOICE);
                        ch.opcodes.push_back(static_cast<uint8_t>(ks_voice & 0xFF));
                    }
                }

                // GL: 滑音加工（音符开头部分的音程变化）
                // #glide 1: & 紧接后的音符不执行滑音加工
                bool glide_suppressed = (config_->glide_mode >= 1 &&
                    !ch.opcodes.empty() && ch.opcodes.back() == CMD_LEGATO);
                if (ch.glide_on && ch.glide_val != 0 && ch.glide_step > 0
                    && duration >= ch.glide_step && !is_pcm_channel(ch_idx)
                    && !glide_suppressed) {
                    // 在步数内从 glide_val→0 线性变化
                    int glide_dur = ch.glide_step;
                    int remain_dur = duration - glide_dur;

                    // Detune 設定: D + glide_val
                    int base_detune = ch.detune + config_->detune_offset;
                    ch.opcodes.push_back(CMD_DETUNE);
                    write_be16(ch.opcodes, static_cast<int16_t>(base_detune + ch.glide_val));

                    // Portamento: rate = -glide_val * 16384 / 64 / glide_dur
                    // = -glide_val * 256 / glide_dur
                    int port_rate = (int)((int64_t)(-ch.glide_val) * 16384 / 64 / glide_dur);
                    ch.opcodes.push_back(CMD_PORTAMENTO);
                    write_be16(ch.opcodes, static_cast<int16_t>(port_rate));

                    // 滑音部分的音符 (F7 置于音符前 — note.x 兼容)
                    ch.opcodes.push_back(CMD_LEGATO);
                    ch.opcodes.push_back(static_cast<uint8_t>(note_byte));
                    ch.opcodes.push_back(static_cast<uint8_t>(glide_dur - 1));

                    // Detune 復元
                    ch.opcodes.push_back(CMD_DETUNE);
                    write_be16(ch.opcodes, static_cast<int16_t>(base_detune));

                    // 剩余的音符
                    if (remain_dur > 0) {
                        ch.opcodes.push_back(static_cast<uint8_t>(note_byte));
                        ch.opcodes.push_back(static_cast<uint8_t>(remain_dur - 1));
                    }

                    ch.last_abs_note = abs_note;
                    ch.last_duration = duration;
                    continue;
                }
                
                // 波形效果分割（apply_wave_effects 返回 true 则分割完成）
                wave_effects_note_sync(ch, ch_idx, waves_, *config_);
                
                // 连音检测（为了传给 apply_wave_effects 而先行判定）
                bool has_tie = ch.legato_pending || (tok.peek() == '&');
                
                if (!apply_wave_effects(ch, note_byte, duration, ch_idx, waves_, *config_, has_tie)) {
                    // note.x 方式: F7 在音符前输出
                    if (tok.peek() == '&') {
                        tok.advance(); // 消费 &
                    }
                    ch.legato_pending = false;
                    if (has_tie) {
                        ch.opcodes.push_back(CMD_LEGATO);
                        // #cont 1/2: & 时也初始化波形相位
                        if (config_->cont_mode >= 1) {
                            wave_effects_note_sync(ch, ch_idx, waves_, *config_);
                        }
                    }
                    // 普通输出（无效果或无需分割）
                    int remaining = duration;
                    bool first = true;
                    while (remaining > 0) {
                        int d = (remaining > MAX_DURATION) ? MAX_DURATION : remaining;
                        if (!first) {
                            ch.opcodes.push_back(CMD_LEGATO);
                        }
                        if (first) {
                            // 记录为 portamento 插入点
                            ch.last_note_opcode_pos = (int)ch.opcodes.size();
                        }
                        ch.opcodes.push_back(static_cast<uint8_t>(note_byte));
                        ch.opcodes.push_back(static_cast<uint8_t>(d - 1));
                        remaining -= d;
                        first = false;
                    }
                } else {
                    // 波形效果分割时的连音处理
                    if (has_tie) {
                        if (tok.peek() == '&') tok.advance();
                        ch.legato_pending = false;
                        if (config_->cont_mode >= 1) {
                            wave_effects_note_sync(ch, ch_idx, waves_, *config_);
                        }
                    }
                }
                ch.last_abs_note = abs_note;
                ch.last_duration = duration;
                // x/@x 临时音量的恢复（如果下一个命令是 x 则跳过恢复）
                if (ch.temp_volume_active) {
                    // 先行读取: 如果下一个命令是 x 则不需要恢复
                    int saved_pos = tok.pos();
                    tok.skip_whitespace();
                    bool next_is_x = (!tok.at_end() && tok.peek() == 'x');
                    bool at_end = tok.at_end();
                    if (!next_is_x && !at_end) {
                        ch.opcodes.push_back(CMD_VOLUME);
                        ch.opcodes.push_back(ch.temp_volume_saved);
                    }
                    ch.temp_volume_active = false;
                }
            } else {
                error(line_num, "Note out of playable range: " + std::to_string(abs_note) +
                      " (mdx_note=" + std::to_string(abs_note - 3) + ")");
            }
            continue;
        }

        // 休符 r
        if (c == 'r') {
            tok.advance();
            int duration = tok.read_duration(ch.base_length);

            // #reste 1: 休符本身也应用波形效果
            // #conte 2: 同步模式下不对休符初始化相位
            if (config_->reste_mode >= 1) {
                // 休符也像音符一样应用波形效果
                if (config_->cont_mode < 2) {
                    wave_effects_note_sync(ch, ch_idx, waves_, *config_);
                }
                if (!apply_wave_effects(ch, -1, duration, -1, waves_, *config_)) {
                    while (duration > 0) {
                        int d = (duration > 128) ? 128 : duration;
                        ch.opcodes.push_back(static_cast<uint8_t>(d - 1));
                        duration -= d;
                    }
                }
            } else if (config_->reste_mode == 0) {
                // #reste 0: 量化产生的休息部分也应用波形效果
                // (休符本身不应用)
                while (duration > 0) {
                    int d = (duration > 128) ? 128 : duration;
                    ch.opcodes.push_back(static_cast<uint8_t>(d - 1));
                    duration -= d;
                }
            } else {
                // #nreste (-1): 休符不应用波形效果（默认）
                while (duration > 0) {
                    int d = (duration > 128) ? 128 : duration;
                    ch.opcodes.push_back(static_cast<uint8_t>(d - 1));
                    duration -= d;
                }
            }
            continue;
        }

        // 连音 & — note.x 方式: F7 在音符输出时已先行输出
        // 这里剩下的 & 仅为跨行连音 (行末的 & → 下行的音符)
        if (c == '&') {
            tok.advance();
            // 下一个音符不在同一行内时的回退处理
            ch.legato_pending = true;
            // #cont 1/2
            if (config_->cont_mode >= 1) {
                wave_effects_note_sync(ch, ch_idx, waves_, *config_);
            }
            continue;
        }

        // 八度设定 o
        if (c == 'o') {
            tok.advance();
            int val;
            if (tok.try_read_int(val)) {
                ch.octave = val;
            }
            continue;
        }

        // 八度上/下 < >
        if (c == '<' || c == '>') {
            tok.advance();
            if (config_->octave_rev) {
                ch.octave += (c == '<') ? 1 : -1;
            } else {
                ch.octave += (c == '<') ? -1 : 1;
            }
            continue;
        }

        // 基本音长 l
        if (c == 'l') {
            tok.advance();
            int val;
            if (tok.try_read_int(val)) {
                ch.base_length = val;
            }
            continue;
        }

        // 速度 t (BPM)
        if (c == 't') {
            tok.advance();
            int bpm;
            if (tok.try_read_int(bpm)) {
                int tb = tempo_to_timer_b(bpm);
                ch.opcodes.push_back(CMD_TEMPO);
                ch.opcodes.push_back(static_cast<uint8_t>(tb));
            }
            continue;
        }

        // 音色 @ (MML 行内)
        if (c == '@') {
            tok.advance();
            // 检查 @t @v @q @x
            char next = tok.peek();
            if (next == 't') {
                tok.advance();
                int val;
                if (tok.try_read_int(val)) {
                    ch.opcodes.push_back(CMD_TEMPO);
                    ch.opcodes.push_back(static_cast<uint8_t>(val & 0xFF));
                }
            } else if (next == 'v') {
                tok.advance();
                int val;
                if (tok.try_read_int(val)) {
                    ch.volume_at_v = val;
                    int out_val = val + ch.volume_offset;
                    if (out_val < 0) out_val = 0;
                    if (out_val > 127) out_val = 127;
                    // MXDRV 用 bit7 区分 v/@v: 0x80=最大, 0xFF=最小
                    // @v N → 0x80 + (127 - N)
                    ch.opcodes.push_back(CMD_VOLUME);
                    ch.opcodes.push_back(static_cast<uint8_t>(0x80 + (127 - out_val)));
                }
            } else if (next == 'q') {
                tok.advance();
                int val;
                if (tok.try_read_int(val)) {
                    ch.quantize_at_q = val;
                    ch.opcodes.push_back(CMD_SOUND_LEN);
                    // @q 以负值存储（门控关闭时间语义）
                    ch.opcodes.push_back(static_cast<uint8_t>((-val) & 0xFF));
                }
            } else if (next == 'x') {
                tok.advance();
                int val;
                if (tok.try_read_int(val)) {
                    // @x<n>: 128 步临时音量 (单个音符) + VO 偏移
                    // 保存当前音量并设置恢复标志
                    int cur_vol = ch.volume_at_v + ch.volume_offset;
                    if (cur_vol < 0) cur_vol = 0;
                    if (cur_vol > 127) cur_vol = 127;
                    ch.temp_volume_saved = static_cast<uint8_t>(cur_vol & 0x7F);
                    ch.temp_volume_active = true;
                    int out_val = val + ch.volume_offset;
                    if (out_val < 0) out_val = 0;
                    if (out_val > 127) out_val = 127;
                    ch.opcodes.push_back(CMD_VOLUME);
                    ch.opcodes.push_back(static_cast<uint8_t>(out_val & 0x7F));
                }
            } else {
                // 音色设定 @N
                int val;
                if (tok.try_read_int(val)) {
                    val += config_->tone_offset;
                    ch.voice = val;
                    ch.opcodes.push_back(CMD_VOICE);
                    ch.opcodes.push_back(static_cast<uint8_t>(val & 0xFF));
                    // 音色宏展开 (@@N): 仅在 SMON 有效时
                    if (ch.smon) {
                        auto it = tone_macros_.find(val);
                        if (it != tone_macros_.end() && !it->second.empty()) {
                            parse_mml(it->second, line_num, ch_idx);
                        }
                    }
                }
            }
            continue;
        }

        // 音量 v — NOTE 的 v 直接设定 @v 值
        if (c == 'v') {
            tok.advance();
            int val;
            if (tok.try_read_int(val)) {
                ch.volume_v = val;
                int out_val = val + ch.volume_offset;
                if (out_val < 0) out_val = 0;
                if (out_val > 255) out_val = 255;
                ch.opcodes.push_back(CMD_VOLUME);
                ch.opcodes.push_back(static_cast<uint8_t>(out_val & 0xFF));
            }
            continue;
        }

        // 音量減 (N → emit N copies of CMD_VOL_DOWN (0xFA)
        if (c == '(') {
            tok.advance();
            int val = 1;
            tok.try_read_int(val);
            for (int i = 0; i < val; i++) {
                ch.opcodes.push_back(CMD_VOL_DOWN);
            }
            continue;
        }

        // 音量增 ) → CMD_VOL_UP (0xF9), no parameter
        // 音量増 )N → emit N copies of CMD_VOL_UP (0xF9)
        if (c == ')') {
            tok.advance();
            int val = 1;
            tok.try_read_int(val);
            for (int i = 0; i < val; i++) {
                ch.opcodes.push_back(CMD_VOL_UP);
            }
            continue;
        }

        // Pan p
        if (c == 'p') {
            tok.advance();
            int val;
            if (tok.try_read_int(val)) {
                // zw1: 波形效果中的显式 p 命令被抑制
                if (config_->wcmd_mode >= 1 &&
                    ch.wave_effects[ChannelState::WE_AP].active) {
                    // 记录值但不输出操作码
                    ch.pan = val;
                } else {
                    ch.pan = val;
                    ch.opcodes.push_back(CMD_PAN);
                    ch.opcodes.push_back(static_cast<uint8_t>(val & 0x03));
                }
            }
            continue;
        }

        // Quantize q → CMD_SOUND_LEN (0xF8 n)
        if (c == 'q') {
            tok.advance();
            int val;
            if (tok.try_read_int(val)) {
                ch.quantize_q = val;
                ch.opcodes.push_back(CMD_SOUND_LEN);
                ch.opcodes.push_back(static_cast<uint8_t>(val & 0xFF));
            }
            continue;
        }

        // Detune D / DT waveform effect
        if (c == 'D') {
            tok.advance();
            if (tok.peek() == 'T') {
                tok.advance();
                // DT: detune waveform effect
                auto& we = ch.wave_effects[ChannelState::WE_DT];
                if (tok.peek() == 'O') {
                    tok.advance();
                    if (tok.peek() == 'N') { tok.advance(); we.active = true; }
                    else if (tok.peek() == 'F') {
                        tok.advance();
                        // note.x 兼容: DTOF 恢复值输出
                        if (we.active && we.wave_num >= 0) {
                            const WaveData* wd = nullptr;
                            for (const auto& w : waves_) {
                                if (w.number == we.wave_num) { wd = &w; break; }
                            }
                            if (wd && !wd->data.empty()) {
                                int val0 = get_wave_value(*wd, 0);
                                emit_wave_effect_cmd(ch.opcodes, ChannelState::WE_DT,
                                    val0, 1, ch_idx, ch.tl_op_mask, ch.tl_tone_num, ch.volume_v);
                                emit_wave_effect_cmd(ch.opcodes, ChannelState::WE_DT,
                                    val0, 1, ch_idx, ch.tl_op_mask, ch.tl_tone_num, ch.volume_v);
                            }
                        }
                        we.active = false;
                    }
                } else if (tok.peek() == 'D') {
                    tok.advance(); int v; if(tok.try_read_int(v)) we.delay = v;
                } else if (tok.peek() == 'S') {
                    tok.advance(); int v; if(tok.try_read_int(v)) we.scale = v;
                } else if (tok.peek() == 'L') {
                    tok.advance(); int v; if(tok.try_read_int(v)) we.phase = v;
                } else {
                    int w; if(tok.try_read_int(w)) {
                        we.wave_num = w; tok.skip_whitespace();
                        if(tok.peek()==',') tok.advance();
                        int s; if(tok.try_read_int(s)) {
                            we.step_count = s; tok.skip_whitespace();
                            if(tok.peek()==',') tok.advance();
                            int m; if(tok.try_read_int(m)) we.sync_mode = m;
                        }
                        we.active = true; we.phase = 0; we.step_counter = 0;
                    }
                }
            } else {
                int val;
                if (tok.try_read_int(val)) {
                    ch.detune = val + config_->detune_offset;
                    ch.opcodes.push_back(CMD_DETUNE);
                    write_be16(ch.opcodes, static_cast<int16_t>(ch.detune));
                }
            }
            continue;
        }

        // Keyon delay k 键开启延迟
        if (c == 'k') {
            tok.advance();
            int val;
            if (tok.try_read_int(val)) {
                ch.keyon_delay = val;
                ch.opcodes.push_back(CMD_KEYON_DELAY);
                ch.opcodes.push_back(static_cast<uint8_t>(val & 0xFF));
            }
            continue;
        }

        // 循环开始 [
        if (c == '[') {
            tok.advance();
            LoopInfo loop;
            loop.start_offset = (int)ch.opcodes.size();
            ch.opcodes.push_back(CMD_REPEAT_START);
            // 循环次数在 ] 之后读取，先占位
            ch.opcodes.push_back(0x02);  // 默认 2 次
            ch.opcodes.push_back(0x00);
            ch.loop_stack.push_back(loop);
            continue;
        }

        // 循环跳出 /
        if (c == '/') {
            tok.advance();
            if (!ch.loop_stack.empty()) {
                auto& loop = ch.loop_stack.back();
                loop.escape_offset = (int)ch.opcodes.size();
                ch.opcodes.push_back(CMD_REPEAT_ESC);
                ch.opcodes.push_back(0x00);  // 偏移占位
                ch.opcodes.push_back(0x00);
            }
            continue;
        }

        // 循环结束 ]
        if (c == ']') {
            tok.advance();
            // 读取循环次数
            int count = 2;
            tok.try_read_int(count);

            if (!ch.loop_stack.empty()) {
                auto loop = ch.loop_stack.back();
                ch.loop_stack.pop_back();

                // 回填循环次数
                ch.opcodes[loop.start_offset + 1] = static_cast<uint8_t>(count);

                // 写循环结束
                // MXDRV L001376: 读 2 字节 offset → negate → A4 -= D0
                // offset 参照点 = F5 opcode 之后 (即 offset word 所在位置)
                // 跳转目标 = repeat_start 之后 (F6 + count + counter = start+3)
                // 所以 offset = start_offset - end_pos (负值, 指向回跳)
                int loop_end_pos = (int)ch.opcodes.size();
                ch.opcodes.push_back(CMD_REPEAT_END);
                int offset = loop.start_offset - loop_end_pos;
                write_be16(ch.opcodes, static_cast<int16_t>(offset));

                // 回填跳出偏移
                // MXDRV L00139a: 读 2 字节 unsigned offset
                // A0 = A4 + D0 → A0 指向 repeat_end 的 offset word
                // 参照点 = escape 指令之后 (F4 + 2 byte = esc+3)
                // 目标 = repeat_end 的 offset word (end_pos + 1)
                // offset = (end_pos + 1) - (esc + 3) = end_pos - esc - 2
                if (loop.escape_offset >= 0) {
                    int esc_offset = loop_end_pos - loop.escape_offset - 2;
                    ch.opcodes[loop.escape_offset + 1] = static_cast<uint8_t>((esc_offset >> 8) & 0xFF);
                    ch.opcodes[loop.escape_offset + 2] = static_cast<uint8_t>(esc_offset & 0xFF);
                }
            }
            continue;
        }

        // 无限循环 L
        if (c == 'L') {
            tok.advance();
            ch.has_infinite_loop = true;
            ch.infinite_loop_offset = (int)ch.opcodes.size();
            continue;
        }

        // OPM 寄存器 y
        if (c == 'y') {
            tok.advance();
            int reg, val;
            if (tok.try_read_int(reg)) {
                tok.skip_whitespace();
                if (tok.peek() == ',') tok.advance();
                if (tok.try_read_int(val)) {
                    ch.opcodes.push_back(CMD_OPM_REG);
                    ch.opcodes.push_back(static_cast<uint8_t>(reg & 0xFF));
                    ch.opcodes.push_back(static_cast<uint8_t>(val & 0xFF));
                }
            }
            continue;
        }

        // 噪音 w — note.x 方式: CMD_FREQ_MODE (0xED) + 1 byte
        if (c == 'w') {
            tok.advance();
            int val = -1;
            tok.try_read_int(val);
            if (val >= 0) {
                // w<n>: 噪音 ON + 频率设定 (0x80 | freq)
                ch.opcodes.push_back(CMD_FREQ_MODE);
                ch.opcodes.push_back(static_cast<uint8_t>(0x80 | (val & 0x1F)));
            } else {
                // w (无参数): 噪音 OFF
                ch.opcodes.push_back(CMD_FREQ_MODE);
                ch.opcodes.push_back(0x80);
            }
            continue;
        }

        // 同期送出 S<ch> / SMON / SMOF
        if (c == 'S') {
            tok.advance();
            // Check for SMON / SMOF
            if (tok.peek() == 'M') {
                tok.advance();
                if (tok.peek() == 'O') {
                    tok.advance();
                    if (tok.peek() == 'N') { tok.advance(); ch.smon = true; }
                    else if (tok.peek() == 'F') { tok.advance(); ch.smon = false; }
                }
            } else {
                // S<ch>: 同步发送 — 0-15 或 A-H/P-W (NOTE 文档: 两种方式都支持)
                int val = -1;
                char lc = tok.peek();
                if (lc >= 'A' && lc <= 'H') { val = lc - 'A'; tok.advance(); }
                else if (lc >= 'P' && lc <= 'W') { val = lc - 'P' + 8; tok.advance(); }
                else { tok.try_read_int(val); }  // 数值 0-15
                if (val >= 0 && val <= 15) {
                    ch.opcodes.push_back(CMD_SYNC_SEND);
                    ch.opcodes.push_back(static_cast<uint8_t>(val & 0xFF));
                }
            }
            continue;
        }

        // 同步等待 W
        if (c == 'W') {
            tok.advance();
            ch.opcodes.push_back(CMD_SYNC_WAIT);
            continue;
        }

        // K: 残响截断 / KS: 音色自动切换 / KM: PMS/AMS 波形效果
        if (c == 'K') {
            tok.advance();
            if (tok.peek() == 'M') {
                // KM: PMS/AMS 波形效果
                tok.advance();
                parse_wave_effect(ChannelState::WE_KM);
            } else if (tok.peek() == 'S') {
                tok.advance();
                if (tok.peek() == 'O') {
                    tok.advance();
                    if (tok.peek() == 'N') { tok.advance(); ch.ks_on = true; }
                    else if (tok.peek() == 'F') { tok.advance(); ch.ks_on = false; }
                } else {
                    // KS<map_number>
                    int val;
                    if (tok.try_read_int(val)) {
                        ch.ks_map = val;
                        ch.ks_on = true;
                    }
                }
            } else {
                // K: 残响截断 → 将所有算子的 D2R/RR 设为最大，立即消音
                // OPM 寄存器 $E0+ch, $E8+ch, $F0+ch, $F8+ch 写入 $FF
                // （CMD_OPM_REG 的通道偏移由 MXDRV 自动加算）
                static const uint8_t rr_regs[] = {0xE0, 0xE8, 0xF0, 0xF8};
                for (int op = 0; op < 4; op++) {
                    ch.opcodes.push_back(CMD_OPM_REG);
                    ch.opcodes.push_back(rr_regs[op]);
                    ch.opcodes.push_back(0xFF);
                }
                // #noreturn 无效的情况下: 重新设定当前音色
                if (!config_->no_return && ch.voice >= 0) {
                    ch.opcodes.push_back(CMD_VOICE);
                    ch.opcodes.push_back(static_cast<uint8_t>(ch.voice & 0xFF));
                }
            }
            continue;
        }

        // GL: glide / GLON / GLOF
        if (c == 'G') {
            tok.advance();
            if (tok.peek() == 'L') {
                tok.advance();
                if (tok.peek() == 'O') {
                    tok.advance();
                    if (tok.peek() == 'N') { tok.advance(); ch.glide_on = true; }
                    else if (tok.peek() == 'F') { tok.advance(); ch.glide_on = false; }
                } else {
                    // GL<val>,<step>
                    int val, step;
                    if (tok.try_read_int(val)) {
                        tok.skip_whitespace();
                        if (tok.peek() == ',') tok.advance();
                        if (tok.try_read_int(step)) {
                            ch.glide_val = val;
                            ch.glide_step = step;
                            ch.glide_on = true;
                        }
                    }
                }
            }
            continue;
        }

        // Q: 波形效果量化
        if (c == 'Q') {
            tok.advance();
            int val;
            if (tok.try_read_int(val)) {
                ch.waveform_quantize = val;
            }
            continue;
        }

        // 采样频率 F
        if (c == 'F') {
            tok.advance();
            int val;
            if (tok.try_read_int(val)) {
                ch.opcodes.push_back(CMD_FREQ_MODE);
                ch.opcodes.push_back(static_cast<uint8_t>(val & 0xFF));
            }
            continue;
        }

        // LFO: M 开头的命令 / MV: volume2 波形效果
        if (c == 'M') {
            tok.advance();
            char next = tok.peek();
            if (next == 'P') {
                tok.advance();
                // 检查 MPON / MPOF
                if (tok.peek() == 'O') {
                    tok.advance();
                    if (tok.peek() == 'N') {
                        tok.advance();
                        ch.opcodes.push_back(CMD_LFO_PITCH);
                        ch.opcodes.push_back(0x81);  // MPON
                    } else if (tok.peek() == 'F') {
                        tok.advance();
                        ch.opcodes.push_back(CMD_LFO_PITCH);
                        ch.opcodes.push_back(0x80);  // MPOF
                    }
                } else {
                    // MP%1,%2,%3
                    int waveform, period, depth;
                    if (tok.try_read_int(waveform)) {
                        tok.skip_whitespace();
                        if (tok.peek() == ',') tok.advance();
                        if (tok.try_read_int(period)) {
                            tok.skip_whitespace();
                            if (tok.peek() == ',') tok.advance();
                            if (tok.try_read_int(depth)) {
                                ch.opcodes.push_back(CMD_LFO_PITCH);
                                ch.opcodes.push_back(static_cast<uint8_t>(waveform & 0xFF));
                                // MP: 每种波形的 period/amplitude 转换方式不同
                                //   wf=0 (锯齿):       period×4, amp = depth×128/period
                                //   wf=1 (方波):     period×2, amp = depth×256
                                //   wf=2 (三角):   period×2, amp = depth×256/period
                                int stored_period, stored_amp;
                                switch (waveform) {
                                case 0:  // 锯齿波
                                    stored_period = period * 4;
                                    stored_amp = depth * 128 / period;
                                    break;
                                case 1:  // 方波
                                    stored_period = period * 2;
                                    stored_amp = depth * 256;
                                    break;
                                default: // 三角波 (wf=2) 及其他
                                    stored_period = period * 2;
                                    stored_amp = depth * 256 / period;
                                    break;
                                }
                                write_be16(ch.opcodes, static_cast<int16_t>(stored_period));
                                write_be16(ch.opcodes, static_cast<int16_t>(stored_amp));
                            }
                        }
                    }
                }
            } else if (next == 'A') {
                tok.advance();
                if (tok.peek() == 'O') {
                    tok.advance();
                    if (tok.peek() == 'N') {
                        tok.advance();
                        ch.opcodes.push_back(CMD_LFO_VOL);
                        ch.opcodes.push_back(0x81);  // MAON
                    } else if (tok.peek() == 'F') {
                        tok.advance();
                        ch.opcodes.push_back(CMD_LFO_VOL);
                        ch.opcodes.push_back(0x80);  // MAOF
                    }
                } else {
                    // MA%1,%2,%3
                    int waveform, period, depth;
                    if (tok.try_read_int(waveform)) {
                        tok.skip_whitespace();
                        if (tok.peek() == ',') tok.advance();
                        if (tok.try_read_int(period)) {
                            tok.skip_whitespace();
                            if (tok.peek() == ',') tok.advance();
                            if (tok.try_read_int(depth)) {
                                ch.opcodes.push_back(CMD_LFO_VOL);
                                ch.opcodes.push_back(static_cast<uint8_t>(waveform & 0xFF));
                                // MA: 每种波形的 period/amplitude 转换方式不同
                                //   wf=0 (锯齿):       period×4, amp = depth×16
                                //   wf=1 (方波):     period×2, amp = depth×256
                                //   wf=2 (三角):   period×2, amp = depth×16
                                int stored_period_ma, stored_amp_ma;
                                switch (waveform) {
                                case 0:  // 锯齿波
                                    stored_period_ma = period * 4;
                                    stored_amp_ma = depth * 16;
                                    break;
                                case 1:  // 方波
                                    stored_period_ma = period * 2;
                                    stored_amp_ma = depth * 256;
                                    break;
                                default: // 三角波 (wf=2) 及其他
                                    stored_period_ma = period * 2;
                                    stored_amp_ma = depth * 16;
                                    break;
                                }
                                write_be16(ch.opcodes, static_cast<int16_t>(stored_period_ma));
                                write_be16(ch.opcodes, static_cast<int16_t>(stored_amp_ma));
                            }
                        }
                    }
                }
            } else if (next == 'V') {
                // MV: volume2 波形效果
                tok.advance();
                parse_wave_effect(ChannelState::WE_MV);
            } else if (next == 'D') {
                tok.advance();
                int val;
                if (tok.try_read_int(val)) {
                    ch.opcodes.push_back(CMD_LFO_DELAY);
                    ch.opcodes.push_back(static_cast<uint8_t>(val & 0xFF));
                }
            } else if (next == 'H') {
                tok.advance();
                // 检查 MHON / MHOF / MHR
                if (tok.peek() == 'O') {
                    tok.advance();
                    if (tok.peek() == 'N') {
                        tok.advance();
                        ch.opcodes.push_back(CMD_MH_LFO);
                        ch.opcodes.push_back(0x81);  // MHON
                    } else if (tok.peek() == 'F') {
                        tok.advance();
                        ch.opcodes.push_back(CMD_MH_LFO);
                        ch.opcodes.push_back(0x80);  // MHOF
                    }
                } else if (tok.peek() == 'R') {
                    tok.advance();
                    // MHR: LFO 相位重置 → y $01,$02
                    ch.opcodes.push_back(CMD_OPM_REG);
                    ch.opcodes.push_back(0x01);
                    ch.opcodes.push_back(0x02);
                } else {
                    // MH %1,%2,%3,%4,%5,%6,%7 → CMD_MH_LFO + 5 param bytes
                    int params[7] = {};
                    int count = 0;
                    for (int pi = 0; pi < 7; pi++) {
                        tok.skip_whitespace();
                        if (pi > 0 && tok.peek() == ',') tok.advance();
                        if (tok.try_read_int(params[pi])) count++;
                        else break;
                    }
                    if (count >= 7) {
                        // VGMRips: 0xea m n o p q (6 bytes total)
                        // m = (sync<<7) | lfo_wave
                        // n = LFRQ
                        // o = PMD
                        // p = AMD
                        // q = (PMS<<4) | AMS
                        ch.opcodes.push_back(CMD_MH_LFO);
                        ch.opcodes.push_back(static_cast<uint8_t>(
                            ((params[6] & 1) << 7) | (params[0] & 0x03)));  // sync|wave
                        ch.opcodes.push_back(static_cast<uint8_t>(params[1] & 0xFF)); // LFRQ
                        ch.opcodes.push_back(static_cast<uint8_t>((params[2] & 0x7F) | 0x80)); // PMD (bit7=PMD select)
                        ch.opcodes.push_back(static_cast<uint8_t>(params[3] & 0x7F)); // AMD
                        ch.opcodes.push_back(static_cast<uint8_t>(
                            ((params[4] & 0x07) << 4) | (params[5] & 0x03))); // PMS|AMS
                    }
                }
            }
            continue;
        }

        // T系列: TR 转调 / TD detune2 波形效果 / TL total level 波形效果
        if (c == 'T') {
            tok.advance();
            if (tok.peek() == 'T') {
                // TT...: TTR<n> 相对移调 (mml2mdx 扩展)
                tok.advance();
                if (tok.peek() == 'R') {
                    tok.advance();
                    int val;
                    if (tok.try_read_int(val)) {
                        ch.transpose += val;
                    }
                }
            } else if (tok.peek() == 'R') {
                // TR<n>: 绝对移调 (note.x 兼容)
                tok.advance();
                int val;
                if (tok.try_read_int(val)) {
                    ch.transpose = val;
                }
            } else if (tok.peek() == 'D') {
                // TD: detune2 波形效果
                tok.advance();
                parse_wave_effect(ChannelState::WE_TD);
            } else if (tok.peek() == 'L') {
                // TL: total level 波形效果
                tok.advance();
                // TL 有额外的子命令: TLM (算子掩码), TLT (音色编号)
                if (tok.peek() == 'M') {
                    tok.advance();
                    int val; if (tok.try_read_int(val)) ch.tl_op_mask = val;
                } else if (tok.peek() == 'T') {
                    tok.advance();
                    int val; if (tok.try_read_int(val)) ch.tl_tone_num = val;
                } else {
                    parse_wave_effect(ChannelState::WE_TL);
                }
            }
            continue;
        }

        // portamento _
        // _[o|<|>]<note>[dur] → 前瞻滑音 (到下一个音符时计算 rate)
        // _D<n> → 原始滑音值 (n = 半音的 1/64 单位, -6144~6144)
        if (c == '_') {
            tok.advance();
            if (tok.peek() == 'D') {
                // _D<n>: 原始 detune 值滑音
                tok.advance();
                int detune_val;
                if (tok.try_read_int(detune_val)) {
                    // rate = detune_val * PITCH_PER_SEMITONE / 64 / duration
                    // 使用 last_duration 计算 rate
                    if (ch.last_duration > 0) {
                        // MXDRV: word = val * 16384 / 64 / duration = val * 256 / duration
                        constexpr int PITCH_PER_SEMITONE = 16384;
                        int rate = (int)((int64_t)detune_val * PITCH_PER_SEMITONE / 64 / ch.last_duration);
                        ch.opcodes.push_back(CMD_PORTAMENTO);
                        write_be16(ch.opcodes, static_cast<int16_t>(rate));
                    }
                }
            } else {
                // _[o|<|>]<note> → 设置 pending, 到下一个音符时计算 rate
                ch.portamento_pending = true;
            }
            continue;
        }

        // 临时音量 x — x<n> 仅对下一个音符设定相当于 v<n> 的音量
        if (c == 'x') {
            tok.advance();
            // x<n>: 绝对临时音量（仅下 1 个音）
            // x+<n> / x-<n>: 从当前音量的相对临时音量 (mml2mdx 扩展)
            bool relative = false;
            int sign = 1;
            if (tok.peek() == '+') { relative = true; sign = 1; tok.advance(); }
            else if (tok.peek() == '-') { relative = true; sign = -1; tok.advance(); }
            int val;
            if (tok.try_read_int(val)) {
                ch.temp_volume_saved = ch.volume_v;
                ch.temp_volume_active = true;
                int out_val;
                if (relative) {
                    // 相对: 当前音量 ± val
                    out_val = ch.volume_v + sign * val;
                } else {
                    // 绝对: val 直接设定
                    out_val = val;
                }
                out_val += ch.volume_offset;
                if (out_val < 0) out_val = 0;
                if (out_val > 255) out_val = 255;
                ch.opcodes.push_back(CMD_VOLUME);
                ch.opcodes.push_back(static_cast<uint8_t>(out_val & 0xFF));
            }
            continue;
        }

        // n 命令（音程编号）
        if (c == 'n') {
            tok.advance();
            int note_num;
            if (tok.try_read_int(note_num)) {
                // n 命令音长的特殊解析 (NOTE doc L1146-1183)
                // ',' → 始终为分隔符，继续读取音长
                // '.' → 如果后面是数字/% 则作为分隔符读取音长
                //        如果后面是 '^' 则为附点+加算
                //        其他情况为附点 (base_length + base_length/2)
                // '^' '‾' '%' → 直接音长解析
                // 无 → 基本音长
                int duration;
                char peek = tok.peek();
                if (peek == ',') {
                    tok.advance();
                    duration = tok.read_duration(ch.base_length);
                } else if (peek == '.') {
                    // '.' 的后面进行确认
                    char peek2 = tok.peek2();
                    if ((peek2 >= '0' && peek2 <= '9') || peek2 == '%') {
                        // 作为分隔符处理
                        tok.advance();
                        duration = tok.read_duration(ch.base_length);
                    } else {
                        // 作为附点处理（read_duration 会处理）
                        duration = tok.read_duration(ch.base_length);
                    }
                } else if (peek == '^' || peek == '%') {
                    // 直接音长解析
                    duration = tok.read_duration(ch.base_length);
                } else {
                    // 音长省略 → 基本音长
                    duration = STEPS_PER_WHOLE / ch.base_length;
                }

                // 转调应用
                note_num += ch.transpose + config_->transpose;

                if (note_num >= PLAYABLE_MIN && note_num <= PLAYABLE_MAX) {
                    int remaining = duration;
                    bool first = true;
                    while (remaining > 0) {
                        int d = (remaining > MAX_DURATION) ? MAX_DURATION : remaining;
                        if (!first) ch.opcodes.push_back(CMD_LEGATO);
                        ch.opcodes.push_back(static_cast<uint8_t>(NOTE_MIN + note_num));
                        ch.opcodes.push_back(static_cast<uint8_t>(d - 1));
                        remaining -= d;
                        first = false;
                    }
                    ch.last_abs_note = note_num + 3; // abs_note
                    ch.last_duration = duration;
                }
            }
            continue;
        }

        // V系列: VM volume 波形效果 / VO volume offset / V<n> 相对音量
        if (c == 'V') {
            tok.advance();
            if (tok.peek() == 'M') {
                // VM: volume 波形效果
                tok.advance();
                parse_wave_effect(ChannelState::WE_VM);
            } else if (tok.peek() == 'O') {
                // VO<n>: volume offset
                tok.advance();
                int val;
                if (tok.try_read_int(val)) {
                    ch.volume_offset = val;
                }
            } else {
                int val;
                if (tok.try_read_int(val)) {
                    // V<n>: 将 val 加到当前音量，发出 v/@v
                    ch.volume_v = std::max(0, std::min(15, ch.volume_v + val));
                    ch.opcodes.push_back(CMD_VOLUME);
                    ch.opcodes.push_back(static_cast<uint8_t>(ch.volume_v & 0xFF));
                }
            }
            continue;
        }

        // 伪循环 C — 伪循环: 从标记位置到末端计算步数
        if (c == 'C') {
            tok.advance();
            ch.pseudo_loop_offset = (int)ch.opcodes.size();
            continue;
        }

        // $ 前缀命令: $FO, $FLAT, $SHARP, $NORMAL/$NATURAL, 宏
        if (c == '$') {
            tok.advance();
            if (tok.peek() == 'F' || tok.peek() == 'f') {
                tok.advance();
                if (tok.peek() == 'O' || tok.peek() == 'o') {
                    tok.advance();
                    int val;
                    if (tok.try_read_int(val)) {
                        // $FO<n>: fade out — VGMRips: 0xe7 0x01 n
                        ch.opcodes.push_back(CMD_FADEOUT);
                        ch.opcodes.push_back(0x01);
                        ch.opcodes.push_back(static_cast<uint8_t>(val & 0xFF));
                    }
                } else if (tok.peek() == 'L' || tok.peek() == 'l') {
                    // $FLAT{notes}
                    tok.advance(); // A
                    if (tok.peek() == 'A' || tok.peek() == 'a') tok.advance();
                    if (tok.peek() == 'T' || tok.peek() == 't') tok.advance();
                    if (tok.peek() == '{') {
                        tok.advance();
                        while (!tok.at_end() && tok.peek() != '}') {
                            char nc = tok.peek();
                            if (nc >= 'a' && nc <= 'g') {
                                ch.note_modifiers[nc - 'a'] = -1;
                            }
                            tok.advance();
                        }
                        if (tok.peek() == '}') tok.advance();
                    }
                }
            } else if (tok.peek() == 'S' || tok.peek() == 's') {
                // $SHARP{notes}
                tok.advance();
                while (!tok.at_end() && tok.peek() != '{' && tok.peek() != ' ') tok.advance();
                if (tok.peek() == '{') {
                    tok.advance();
                    while (!tok.at_end() && tok.peek() != '}') {
                        char nc = tok.peek();
                        if (nc >= 'a' && nc <= 'g') {
                            ch.note_modifiers[nc - 'a'] = 1;
                        }
                        tok.advance();
                    }
                    if (tok.peek() == '}') tok.advance();
                }
            } else if (tok.peek() == 'N' || tok.peek() == 'n') {
                // $NORMAL / $NATURAL
                while (!tok.at_end() && tok.peek() != '{' && tok.peek() != ' ' && tok.peek() != '\t') tok.advance();
                if (tok.peek() == '{') {
                    tok.advance();
                    while (!tok.at_end() && tok.peek() != '}') {
                        char nc = tok.peek();
                        if (nc >= 'a' && nc <= 'g') {
                            ch.note_modifiers[nc - 'a'] = 0;
                        }
                        tok.advance();
                    }
                    if (tok.peek() == '}') tok.advance();
                } else {
                    // 无 {}: 全部重置
                    ch.note_modifiers.fill(0);
                }
            } else {
                // $ 宏展开: 读取变量名，查找并递归解析
                std::string varname;
                varname += tok.peek();
                tok.advance();
                // 读取可选的下标
                int sub = -1;
                if (!tok.at_end() && tok.peek() >= '0' && tok.peek() <= '9') {
                    sub = 0;
                    while (!tok.at_end() && tok.peek() >= '0' && tok.peek() <= '9') {
                        sub = sub * 10 + (tok.peek() - '0');
                        tok.advance();
                    }
                }
                std::string key = varname;
                if (sub >= 0) key += "_" + std::to_string(sub);
                auto it = macros_.find(key);
                if (it != macros_.end() && !it->second.empty()) {
                    parse_mml(it->second, line_num, ch_idx);
                }
            }
            continue;
        }


        // AP: auto-pan waveform
        if (c == 'A') {
            tok.advance();
            if (tok.peek() == 'P') {
                tok.advance();
                parse_wave_effect(ChannelState::WE_AP);
                continue;
            }
            // 非 AP — 可能是未知命令，跳过此字符
            continue;
        }

        // YA: y-command waveform
        if (c == 'Y') {
            tok.advance();
            if (tok.peek() == 'A') {
                tok.advance();
                parse_wave_effect(ChannelState::WE_YA);
                continue;
            }
            continue;
        }

        // zc/zw: MML 行内 #cont/#wcmd 控制
        if (c == 'z') {
            tok.advance();
            if (tok.peek() == 'c') {
                tok.advance();
                int val;
                if (tok.try_read_int(val)) config_->cont_mode = val;
            } else if (tok.peek() == 'w') {
                tok.advance();
                int val;
                if (tok.try_read_int(val)) config_->wcmd_mode = val;
            }
            continue;
        }

        // !: 此后的 MML 全部跳过
        if (c == '!') {
            break; // 忽略该通道的剩余 MML
        }

        // ?: 范围指定 MML 跳过（抑制具有实体命令的数据写出）
        if (c == '?') {
            tok.advance();
            // ? 到下一个 ? 之间全部跳过（状态变更命令则透过）
            // 简化实现: 范围内所有 MML 均忽略
            while (!tok.at_end() && tok.peek() != '?') {
                tok.advance();
            }
            if (tok.peek() == '?') tok.advance();
            continue;
        }

        // 未知字符 → 跳过
        tok.advance();
    }
}

void Parser::error(int line, const std::string& msg) {
    if (!lexer_.lines().empty() && line > 0 && line <= (int)lexer_.lines().size()) {
        const auto& info = lexer_.line_info(line - 1);
        report_error(info.file, info.original_line, msg);
    } else {
        fprintf(stderr, "line %d : Error - %s\n", line, msg.c_str());
    }
    error_count_++;
}

void Parser::warning(int line, const std::string& msg) {
    if (!lexer_.lines().empty() && line > 0 && line <= (int)lexer_.lines().size()) {
        const auto& info = lexer_.line_info(line - 1);
        report_warning(info.file, info.original_line, msg);
    } else {
        fprintf(stderr, "line %d : Warning - %s\n", line, msg.c_str());
    }
}

} // namespace mdx

// mml2mdx — 词法分析器 实现（桩）
#include "lexer.h"
#include "util.h"
#include <cstring>
#include <algorithm>

namespace mdx {

// ═══════════════════════════════════════════
// Lexer: 文件加载与 #include 处理
// ═══════════════════════════════════════════

bool Lexer::load(const std::string& path, const std::string& base_dir) {
    include_depth_ = 0;
    lines_.clear();
    line_info_.clear();
    return load_recursive(path, base_dir);
}

bool Lexer::load_recursive(const std::string& path, const std::string& base_dir) {
    if (include_depth_ >= MAX_INCLUDE_DEPTH) {
        fprintf(stderr, "Error: #include nesting too deep (max %d)\n", MAX_INCLUDE_DEPTH);
        return false;
    }

    std::string content;
    std::string full_path = join_path(base_dir, path);
    if (!read_file(full_path, content)) {
        return false;
    }

    include_depth_++;

    // 按行分割，合并 { } 跨行块（音色/波形定义）
    std::string line;
    int line_num = 0;
    int brace_start_line = 0;
    int brace_depth = 0;
    std::string brace_accum;  // 跨行块累积

    for (size_t i = 0; i <= content.size(); i++) {
        if (i == content.size() || content[i] == '\n') {
            // 去掉末尾 \r
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            line_num++;

            if (brace_depth > 0) {
                // 正在跨行块中，累积到 brace_accum
                brace_accum += " " + line;
                // 检查此行是否有 }
                for (char c : line) {
                    if (c == '{') brace_depth++;
                    else if (c == '}') { brace_depth--; if (brace_depth <= 0) break; }
                }
                if (brace_depth <= 0) {
                    // 块结束，作为一行输出
                    lines_.push_back(brace_accum);
                    line_info_.push_back({full_path, brace_start_line});
                    brace_accum.clear();
                    brace_depth = 0;
                }
            } else {
                // 检查此行是否开始了一个 { 块但没有闭合
                int opens = 0;
                for (char c : line) {
                    if (c == '{') opens++;
                    else if (c == '}') opens--;
                }
                if (opens > 0) {
                    // 开始跨行块
                    brace_depth = opens;
                    brace_start_line = line_num;
                    brace_accum = line;
                } else {
                    // #include 检测: 递归读取文件
                    std::string trimmed = line;
                    size_t sp = 0;
                    while (sp < trimmed.size() && (trimmed[sp] == ' ' || trimmed[sp] == '\t')) sp++;
                    if (trimmed.compare(sp, 8, "#include") == 0) {
                        // 提取文件名 (#include "filename" or #include filename)
                        size_t fp = sp + 8;
                        while (fp < trimmed.size() && (trimmed[fp] == ' ' || trimmed[fp] == '\t')) fp++;
                        std::string inc_file;
                        if (fp < trimmed.size() && trimmed[fp] == '"') {
                            fp++;
                            size_t ep = trimmed.find('"', fp);
                            if (ep != std::string::npos) inc_file = trimmed.substr(fp, ep - fp);
                        } else {
                            size_t ep = fp;
                            while (ep < trimmed.size() && trimmed[ep] != ' ' && trimmed[ep] != '\t') ep++;
                            inc_file = trimmed.substr(fp, ep - fp);
                        }
                        if (!inc_file.empty()) {
                            load_recursive(inc_file, base_dir);
                        }
                    } else {
                        lines_.push_back(line);
                        line_info_.push_back({full_path, line_num});
                    }
                }
            }

            line.clear();
        } else {
            line += content[i];
        }
    }

    // 如果文件结束时块未闭合，也输出
    if (!brace_accum.empty()) {
        lines_.push_back(brace_accum);
        line_info_.push_back({full_path, brace_start_line});
    }

    include_depth_--;
    return true;
}

// ═══════════════════════════════════════════
// MMLTokenizer: 行级 token 化
// ═══════════════════════════════════════════

MMLTokenizer::MMLTokenizer(const std::string& line, int line_num)
    : line_(line), line_num_(line_num) {}

char MMLTokenizer::peek() const {
    if (pos_ >= (int)line_.size()) return '\0';
    return line_[pos_];
}

char MMLTokenizer::peek2() const {
    if (pos_ + 1 >= (int)line_.size()) return '\0';
    return line_[pos_ + 1];
}

char MMLTokenizer::advance() {
    if (pos_ >= (int)line_.size()) return '\0';
    return line_[pos_++];
}

bool MMLTokenizer::match(char expected) {
    if (peek() == expected) {
        pos_++;
        return true;
    }
    return false;
}

bool MMLTokenizer::match_str(const char* str) {
    int len = strlen(str);
    if (pos_ + len > (int)line_.size()) return false;
    if (line_.compare(pos_, len, str) == 0) {
        pos_ += len;
        return true;
    }
    return false;
}

void MMLTokenizer::skip_whitespace() {
    while (pos_ < (int)line_.size() && 
           (line_[pos_] == ' ' || line_[pos_] == '\t')) {
        pos_++;
    }
}

int MMLTokenizer::read_int() {
    skip_whitespace();
    bool negative = false;
    if (peek() == '-') { negative = true; advance(); }
    else if (peek() == '+') { advance(); }

    // 十六进制: $xx
    if (peek() == '$') {
        advance();
        int val = 0;
        int digits = 0;
        // NOTE 惯例: $xx 最多 2 位的 16 进制数 (0x00-0xFF)
        // 不支持 3 位以上（a-f 会与音名冲突）
        while (pos_ < (int)line_.size() && digits < 2) {
            char c = line_[pos_];
            if (c >= '0' && c <= '9') { val = val * 16 + (c - '0'); pos_++; digits++; }
            else if (c >= 'a' && c <= 'f') { val = val * 16 + (c - 'a' + 10); pos_++; digits++; }
            else if (c >= 'A' && c <= 'F') { val = val * 16 + (c - 'A' + 10); pos_++; digits++; }
            else break;
        }
        return negative ? -val : val;
    }

    // 二进制: %<bits>（y 命令的值部分等使用）
    // 注意: read_int 在音长以外的上下文中被调用
    //       音长由 read_duration 另行处理
    if (peek() == '%') {
        // 先行查看: % 后面是 0/1 则为二进制数
        if (pos_ + 1 < (int)line_.size() && 
            (line_[pos_ + 1] == '0' || line_[pos_ + 1] == '1')) {
            advance(); // skip '%'
            int val = 0;
            int bits = 0;
            while (pos_ < (int)line_.size() && bits < 8) {
                char c = line_[pos_];
                if (c == '0' || c == '1') { val = val * 2 + (c - '0'); pos_++; bits++; }
                else if (c == '_') { pos_++; } // 分隔字符 (例: %11_000_101)
                else break;
            }
            return negative ? -val : val;
        }
    }

    // 十进制
    int val = 0;
    while (pos_ < (int)line_.size() && line_[pos_] >= '0' && line_[pos_] <= '9') {
        val = val * 10 + (line_[pos_] - '0');
        pos_++;
    }
    return negative ? -val : val;
}

bool MMLTokenizer::try_read_int(int& out) {
    skip_whitespace();
    int save_pos = pos_;
    char c = peek();
    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '$') {
        out = read_int();
        return true;
    }
    pos_ = save_pos;
    return false;
}

// 读取音长表达式: [%N | N][.][^expr | ~expr]
int MMLTokenizer::read_duration(int base_length) {
    skip_whitespace();

    int duration = -1;  // -1 表示使用默认

    // 先检查 % 步数表示
    if (peek() == '%') {
        advance();
        duration = read_int();
    } else if (peek() >= '0' && peek() <= '9') {
        int n = read_int();
        if (n > 0 && STEPS_PER_WHOLE % n == 0) {
            duration = STEPS_PER_WHOLE / n;
        } else if (n > 0) {
            // NOTE 允许任何可以被 192 整除的值
            duration = STEPS_PER_WHOLE / n;
        } else {
            duration = base_length > 0 ? (STEPS_PER_WHOLE / base_length) : STEPS_PER_QUARTER;
        }
    }

    if (duration < 0) {
        // 使用基本音长
        duration = base_length > 0 ? (STEPS_PER_WHOLE / base_length) : STEPS_PER_QUARTER;
    }

    // 处理符点 '.'
    int dot_add = duration;
    while (peek() == '.') {
        advance();
        dot_add /= 2;
        duration += dot_add;
    }

    // 处理加减 '^' '~'
    while (peek() == '^' || peek() == '~') {
        char op = advance();
        int add_dur;
        if (peek() == '%') {
            advance();
            add_dur = read_int();
        } else if (peek() >= '0' && peek() <= '9') {
            int n = read_int();
            add_dur = (n > 0) ? (STEPS_PER_WHOLE / n) : 0;
            // 符点处理
            int da = add_dur;
            while (peek() == '.') {
                advance();
                da /= 2;
                add_dur += da;
            }
        } else {
            // 没数字跟随，使用基本音长
            add_dur = base_length > 0 ? (STEPS_PER_WHOLE / base_length) : STEPS_PER_QUARTER;
        }
        if (op == '^') duration += add_dur;
        else duration -= add_dur;
    }

    return duration;
}

Token MMLTokenizer::next() {
    // TODO: 完整实现
    Token tok;
    tok.line = line_num_;
    tok.column = pos_;
    tok.type = TokenType::END_OF_FILE;
    return tok;
}

} // namespace mdx

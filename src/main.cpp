// mml2mdx — MML→MDX 编译器 主入口
// 完全参考 NOTE v0.8.5 编译器，将 MML 编译成 MDX
//
// 用法: mml2mdx [switches] <mml file[.mml]>

#include "mml2mdx.h"
#include "compiler.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#ifdef _WIN32
#include <direct.h>   // _chdir
#else
#include <unistd.h>   // chdir
#endif

static void print_version() {
    printf("mml2mdx version 0.1.0\n");
    printf("MML file converter for MXDRV2 (NOTE v0.8.5 compatible)\n");
}

static void print_help() {
    printf(
        "Usage: mml2mdx [switches] <mml file[.mml]>\n"
        "\n"
        "Switches:\n"
        "  -m<size>       MDX buffer size (KB, default 64)\n"
        "  -x             Reverse octave commands < >\n"
        "  -p             ex-pcm mode (enable Q-W channels)\n"
        "  -r             Remove MDX file on error\n"
        "  -i<channels>   Channel mask (A-H, P-W)\n"
        "  -b             Beep on error\n"
        "  -l             Output PCM usage file (pcmuse.map)\n"
        "  -o             pcmuse.map logical OR\n"
        "  -c[n]          Duration compression (n: include notes)\n"
        "  -z[dvqpt012]   Optimization\n"
        "  -t[name]       Save tone data\n"
        "  -w[name]       Save wave data\n"
        "  -v[0|1]        Verbose mode\n"
        "  -1             Stop on first error\n"
        "  -e             Write output even on error\n"
        "  -h             Show help\n"
    );
}

// 从文件名推导输出 .mdx 路径
static std::string make_output_path(const std::string& input) {
    std::string out = input;
    // 找最后一个 '.' 
    auto dot = out.rfind('.');
    if (dot != std::string::npos) {
        out = out.substr(0, dot);
    }
    out += ".mdx";
    return out;
}

// 确保输入文件有扩展名（默认 .mml）
static std::string resolve_input_path(const std::string& input) {
    // 如果已有扩展名，直接使用
    auto dot = input.rfind('.');
    auto sep = input.find_last_of("/\\");
    if (dot != std::string::npos && (sep == std::string::npos || dot > sep)) {
        return input;
    }
    // 尝试 .mml 优先, 然后 .mus
    std::string mml_path = input + ".mml";
    FILE* f = fopen(mml_path.c_str(), "r");
    if (f) { fclose(f); return mml_path; }
    
    std::string mus_path = input + ".mus";
    f = fopen(mus_path.c_str(), "r");
    if (f) { fclose(f); return mus_path; }
    
    // 都找不到，返回 .mml 让后面报错
    return mml_path;
}

int main(int argc, char* argv[]) {
    mdx::CompilerConfig config;

    // 解析命令行参数
    int file_arg_index = -1;
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        
        if (arg[0] == '-') {
            switch (arg[1]) {
            case 'm':
                config.mdx_buffer_size = atoi(arg + 2) * 1024;
                if (config.mdx_buffer_size <= 0) config.mdx_buffer_size = 65536;
                break;
            case 'x':
                config.octave_rev = true;
                break;
            case 'p':
                config.ex_pcm = true;
                break;
            case 'r':
                config.remove_on_error = true;
                break;
            case 'i':
                // 解析通道遮罩: -iABP 等
                for (const char* p = arg + 2; *p; p++) {
                    int idx = mdx::channel_index(*p);
                    if (idx >= 0) {
                        config.channel_mask |= (1 << idx);
                    }
                }
                break;
            case 'b':
                config.beep_on_error = true;
                break;
            case 'l':
                config.pcm_list = true;
                break;
            case 'o':
                config.pcm_list_or = true;
                break;
            case 'c':
                config.compress = (arg[2] == 'n') ? 1 : 0;
                break;
            case 'z':
                if (arg[2]) {
                    config.optimize = arg + 2;
                } else {
                    config.optimize = "dvqpt012";
                }
                break;
            case 't':
                config.tone_save = (arg[2]) ? (arg + 2) : "tone.bin";
                break;
            case 'w':
                config.wave_save = (arg[2]) ? (arg + 2) : "wave.bin";
                break;
            case 'v':
                config.verbose = (arg[2] == '1') ? 1 : 0;
                break;
            case '1':
                config.stop_on_first_error = true;
                break;
            case 'e':
                config.write_on_error = true;
                break;
            case 'h':
            case '?':
                print_help();
                return 0;
            default:
                fprintf(stderr, "Unknown switch: %s\n", arg);
                return 1;
            }
        } else {
            file_arg_index = i;
        }
    }

    if (file_arg_index < 0) {
        print_version();
        printf("\n");
        print_help();
        return 0;
    }

    // 解析输入/输出文件路径
    std::string full_input = resolve_input_path(argv[file_arg_index]);

    // 切换工作目录到输入文件所在目录
    // 这样 #pcmfile 等相对路径引用能正确解析（拖放到 exe 时尤其重要）
    {
        auto sep = full_input.find_last_of("/\\");
        if (sep != std::string::npos) {
            std::string dir = full_input.substr(0, sep);
            std::string basename = full_input.substr(sep + 1);
#ifdef _WIN32
            _chdir(dir.c_str());
#else
            chdir(dir.c_str());
#endif
            config.input_file = basename;
        } else {
            config.input_file = full_input;
        }
    }
    config.output_file = make_output_path(config.input_file);

    if (config.verbose >= 0) {
        fprintf(stderr, "Input:  %s\n", config.input_file.c_str());
        fprintf(stderr, "Output: %s\n", config.output_file.c_str());
    }

    // 执行编译
    bool success = compile_mml(config);

    if (!success) {
        if (config.remove_on_error) {
            remove(config.output_file.c_str());
        }
        if (config.beep_on_error) {
            printf("\a");  // 蜂鸣音
        }
        return 1;
    }

    return 0;
}

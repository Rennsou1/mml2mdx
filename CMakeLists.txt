cmake_minimum_required(VERSION 3.20)
project(mml2mdx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 源文件列表
set(MML2MDX_SOURCES
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/compiler.cpp
    src/mdx_writer.cpp
    src/optimizer.cpp
    src/macro.cpp
    src/wave_effect.cpp
    src/util.cpp
)

add_executable(mml2mdx ${MML2MDX_SOURCES})
target_include_directories(mml2mdx PRIVATE include src)

# Windows 特定设定：使用 Shift-JIS 作为执行字符集
if(MSVC)
    target_compile_options(mml2mdx PRIVATE /utf-8)
elseif(MINGW)
    target_compile_options(mml2mdx PRIVATE -finput-charset=UTF-8)
endif()

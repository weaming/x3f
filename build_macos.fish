#!/usr/bin/env fish
# macOS 本地编译脚本

set -l SCRIPT_DIR (dirname (status -f))
cd $SCRIPT_DIR

# 解析命令行参数
set -l BUILD_UNIVERSAL 0
if test (count $argv) -gt 0
    if test "$argv[1]" = "--universal"
        set BUILD_UNIVERSAL 1
    else if test "$argv[1]" = "--help" -o "$argv[1]" = "-h"
        echo "用法: ./build_macos.fish [选项]"
        echo ""
        echo "选项:"
        echo "  --universal    编译通用二进制 (支持 ARM64 和 x86_64)"
        echo "  --help, -h     显示此帮助信息"
        echo ""
        echo "默认行为: 编译当前架构的二进制"
        exit 0
    else
        echo "错误: 未知参数 '$argv[1]'"
        echo "使用 --help 查看可用选项"
        exit 1
    end
end

if test $BUILD_UNIVERSAL -eq 1
    echo "=== 编译 x3f_tools (macOS Universal Binary) ==="
else
    echo "=== 编译 x3f_tools (macOS) ==="
end
echo ""

# 检查是否在 macOS 系统
if test (uname -s) != "Darwin"
    echo "错误: 此脚本只能在 macOS 系统上运行"
    echo "如需在 Linux 上交叉编译,请使用 setup_osx_cross_compiler.fish"
    exit 1
end

# 检查是否安装了 Xcode Command Line Tools
if not test -d /Library/Developer/CommandLineTools
    echo "错误: 未检测到 Xcode Command Line Tools"
    echo "请运行: xcode-select --install"
    exit 1
end

if test $BUILD_UNIVERSAL -eq 1
    echo "步骤 1/5: 清理旧的编译产物..."
    make clean

    echo ""
    echo "步骤 2/5: 编译 ARM64 版本的 OpenCV..."
    echo "提示: 这可能需要 30-60 分钟"
    make TARGET=osx-arm64 deps/lib/osx-arm64/opencv/.success
    if test $status -ne 0
        echo ""
        echo "错误: ARM64 OpenCV 编译失败"
        exit 1
    end

    echo ""
    echo "步骤 3/5: 编译 x86_64 版本的 OpenCV..."
    echo "提示: 这可能需要 30-60 分钟"
    make TARGET=osx-x86_64 deps/lib/osx-x86_64/opencv/.success
    if test $status -ne 0
        echo ""
        echo "错误: x86_64 OpenCV 编译失败"
        exit 1
    end

    echo ""
    echo "步骤 4/5: 构建通用二进制文件..."
    make TARGET=osx-universal all
    if test $status -ne 0
        echo ""
        echo "错误: 通用二进制编译失败"
        exit 1
    end

    echo ""
    echo "步骤 5/5: 创建发行版..."
    make TARGET=osx-universal dist
else
    echo "步骤 1/3: 清理旧的编译产物..."
    make clean

    echo ""
    echo "步骤 2/3: 构建可执行文件..."
    make all

    if test $status -ne 0
        echo ""
        echo "错误: 编译失败"
        exit 1
    end

    echo ""
    echo "步骤 3/3: 创建发行版..."
    make dist
end

if test $status -ne 0
    echo ""
    echo "错误: 创建发行版失败"
    exit 1
end

echo ""
echo "=== 编译完成 ==="
echo ""
echo "可执行文件位置:"
find bin -name "x3f_extract" -type f | while read -l file
    echo "  - $file"
end

if test $BUILD_UNIVERSAL -eq 1
    echo ""
    echo "验证通用二进制:"
    set -l universal_bin (find bin/osx-universal -name "x3f_extract" -type f | head -n 1)
    if test -n "$universal_bin"
        lipo -info $universal_bin
    end
end

echo ""
echo "发行版位置:"
find dist -name "x3f_tools-*.tar.gz" -o -name "x3f_tools-*.zip" | while read -l file
    echo "  - $file"
end

echo ""
echo "运行示例:"
set -l exe_path (find bin -name "x3f_extract" -type f | head -n 1)
if test -n "$exe_path"
    echo "  $exe_path --help"
end

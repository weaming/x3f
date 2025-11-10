#!/usr/bin/env fish
# macOS 本地编译脚本

set -l SCRIPT_DIR (dirname (status -f))
cd $SCRIPT_DIR

echo "=== 编译 x3f_tools (macOS) ==="
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

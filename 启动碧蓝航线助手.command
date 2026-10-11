#!/bin/zsh

# 双击启动碧蓝航线助手。每次只做增量编译，并只监听本机地址。
set -u

PROJECT_DIR="${0:A:h}"
BUILD_DIR="${PROJECT_DIR}/build"
GUI_PROGRAM="${BUILD_DIR}/azur_lane_gui"

pause_and_exit() {
    local exit_code="$1"
    echo
    read -r "?按回车键关闭此窗口…"
    exit "$exit_code"
}

find_cmake() {
    if [[ -n "${GAME_GUI_CMAKE_BIN:-}" ]]; then
        [[ -x "${GAME_GUI_CMAKE_BIN}" ]] && print -r -- "${GAME_GUI_CMAKE_BIN}" && return 0
        echo "* GAME_GUI_CMAKE_BIN 指定的 CMake 不存在或不可执行：${GAME_GUI_CMAKE_BIN}" >&2
        return 1
    fi
    command -v cmake >/dev/null 2>&1 && command -v cmake && return 0
    local candidate
    for candidate in \
        "/Applications/CLion.app/Contents/bin/cmake/mac/x64/bin/cmake" \
        "/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake"; do
        [[ -x "$candidate" ]] && print -r -- "$candidate" && return 0
    done
    return 1
}

echo "========================================"
echo "  碧蓝航线助手"
echo "========================================"
echo "项目目录：${PROJECT_DIR}"

CMAKE_BIN="$(find_cmake)"
if [[ -z "$CMAKE_BIN" ]]; then
    echo "* 找不到 CMake。请安装 CMake，或确认 CLion 位于 /Applications/CLion.app。"
    pause_and_exit 1
fi

cd "$PROJECT_DIR" || { echo "* 无法进入项目目录。"; pause_and_exit 1; }

# 文件夹移动后，旧 CMake 缓存仍记录原路径；保留它并重新构建。
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    CACHED_SOURCE="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt")"
    if [[ "$CACHED_SOURCE" != "$PROJECT_DIR" ]]; then
        SAVED_BUILD="${BUILD_DIR}.previous.$(date +%Y%m%d-%H%M%S).$$"
        mv "$BUILD_DIR" "$SAVED_BUILD" || { echo "* 无法保留旧构建目录。"; pause_and_exit 1; }
        echo "已保留旧构建目录：${SAVED_BUILD}"
    fi
fi

echo
echo "[1/3] 重新加载项目配置…"
if ! "$CMAKE_BIN" -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug; then
    echo "* 项目配置失败，未运行旧程序。"
    pause_and_exit 1
fi

JOBS="$(sysctl -n hw.logicalcpu 2>/dev/null || print 4)"
echo
echo "[2/3] 增量编译碧蓝航线助手…"
if ! "$CMAKE_BIN" --build "$BUILD_DIR" --target azur_lane_gui --parallel "$JOBS"; then
    echo "* 编译失败，未运行旧程序。请查看上方错误。"
    pause_and_exit 1
fi

if [[ ! -x "$GUI_PROGRAM" ]]; then
    echo "* 没有找到生成的程序：${GUI_PROGRAM}"
    pause_and_exit 1
fi

echo
echo "[3/3] 启动本地网页…"
echo "浏览器会自动打开；关闭本窗口或按 Ctrl+C 可停止服务。"
echo
"$GUI_PROGRAM"
PROGRAM_STATUS=$?

echo
if [[ "$PROGRAM_STATUS" -eq 0 || "$PROGRAM_STATUS" -eq 130 || "$PROGRAM_STATUS" -eq 143 ]]; then
    echo "游戏助手已停止。"
    pause_and_exit 0
fi
echo "* 游戏助手异常退出，状态码：${PROGRAM_STATUS}"
pause_and_exit "$PROGRAM_STATUS"

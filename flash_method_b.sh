#!/usr/bin/env bash
# ================================================================
# FoloToy AI Passport —— 方案 B：本机一键烧录脚本 (esptool write_flash)
#
# 用法:
#   1) Ubuntu / WSL / mac / Git Bash :  chmod +x flash_method_b.sh  &&  ./flash_method_b.sh [PORT]
#      例: ./flash_method_b.sh /dev/ttyACM0
#   2) 不给 PORT 时自动找第一个 ttyACM*, 找不到就报错并列出所有候选
#   3) 烧录完成后自动打开 monitor,按 Ctrl+] 退出。
#
# 本脚本要求:本机已按 AI_HARDWARE_DEVELOPMENT_GUIDE §12.2 装 ESP-IDF 5.5.3
#   (即 $HOME/esp/esp-idf-v5.5.3/export.sh 存在;若不在该路径,请修改下一行)
IDF_EXPORT="${IDF_EXPORT:-$HOME/esp/esp-idf-v5.5.3/export.sh}"
PYENV_VERSION="${PYENV_VERSION:-3.11.15}"           # 已验证 3.11 对 IDF 5.5.3 最稳

# --- 颜色/失败退出 ---------------------------------------------------------
set -euo pipefail
R=$'\e[31m' G=$'\e[32m' Y=$'\e[33m' B=$'\e[1m' N=$'\e[0m'
die()  { echo "${R}${B}ERROR: $*${N}" >&2; exit 1; }
info() { echo "${G}>>>${N} $*"; }
warn() { echo "${Y}!!!${N} $*"; }

# --- 环境 ------------------------------------------------------------------
if [[ -n "${PYENV_VERSION:-}" ]] && command -v pyenv >/dev/null 2>&1; then
    export PYENV_VERSION
    info "Using pyenv python: $(python3 --version 2>&1)"
fi
if [[ ! -f "$IDF_EXPORT" ]]; then
    die "找不到 ESP-IDF export.sh: $IDF_EXPORT
请先装 IDF 5.5.3 (见 AI_HARDWARE_DEVELOPMENT_GUIDE §12.2), 或:
    IDF_EXPORT=/你的路径/esp-idf-v5.5.3/export.sh  $0  $*"
fi

# shellcheck disable=SC1090
source "$IDF_EXPORT" >/dev/null
command -v esptool.py >/dev/null || die "source IDF 后仍找不到 esptool.py"
command -v idf.py    >/dev/null || die "source IDF 后仍找不到 idf.py"
info "ESP-IDF: $(idf.py --version 2>&1 | head -1)"

# --- bin 文件检查 ----------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${SCRIPT_DIR}"
APP_BIN="${ROOT}/build/FoloToy-AI-Passport.bin"
BLD_BIN="${ROOT}/build/bootloader/bootloader.bin"
PT_BIN="${ROOT}/build/partition_table/partition-table.bin"
for f in "$APP_BIN" "$BLD_BIN" "$PT_BIN"; do
    [[ -f "$f" ]] || die "缺少 $f —— 请先在 $ROOT 执行 idf.py build"
done
info "App bin     : $APP_BIN  ($(stat -c %s "$APP_BIN") bytes, md5 $(md5sum "$APP_BIN" | cut -c1-10))"
info "Bootloader  : $BLD_BIN  ($(stat -c %s "$BLD_BIN") bytes)"
info "Part table  : $PT_BIN   ($(stat -c %s "$PT_BIN") bytes)"

# --- 端口探测 --------------------------------------------------------------
if   [[ $# -ge 1 && -n "$1" ]];     then PORT="$1"
elif ls /dev/ttyACM* >/dev/null 2>&1; then PORT="$(ls /dev/ttyACM* | head -1)"
elif ls /dev/ttyUSB* >/dev/null 2>&1; then warn "FoloToy 应走 USB Serial/JTAG 即 /dev/ttyACM*,但只看到 ttyUSB*(可能是外扩 USB-UART)"; PORT="$(ls /dev/ttyUSB* | head -1)"
else
    echo "枚举到的串口:"; ls /dev/ttyACM* /dev/ttyUSB* 2>&1 | sed 's/^/   /'
    die "请把 FoloToy AI Passport 用可传数据的 USB 线插上,然后用: $0 /dev/ttyACM0"
fi
[[ -r "$PORT" && -w "$PORT" ]] || die "端口 $PORT 不可读写。Ubuntu 解决: sudo usermod -aG dialout \$USER  然后注销/重登"

info "Target port : ${B}${PORT}${N}"

# --- 执行 esptool.py (方案 B, 原生 write_flash) ---------------------------
echo
info "开始烧录...若板子不自动进入下载模式,请按住 BOOT 键再按一下 RST 键"
set -x
python3 -m esptool \
    --chip esp32c3 -p "$PORT" -b 460800 \
    --before default_reset --after hard_reset \
    write_flash \
      --flash_mode dio --flash_size detect --flash_freq 80m \
      0x0      "$BLD_BIN" \
      0x8000   "$PT_BIN" \
      0x10000  "$APP_BIN"
FLASH_RC=$?
set +x

if [[ $FLASH_RC -ne 0 ]]; then
    die "esptool.py 退出码 $FLASH_RC。常见原因: 选错端口 / USB 线只充电不传数 / 未进下载模式 / 波特率过高(改 -b 115200 再试)"
fi
echo
info "${G}烧录成功。${N} 接下来打开 monitor 捕获启动日志(按 Ctrl+] 退出)..."
info "成功判据:启动日志里必须看到:  FoloToy-Card BSP demo 启动  +  就绪:Display=1 Button=? Audio=? Battery=? PunchCard=1"
echo

# 捕获日志到文件方便回传
LOG="${ROOT}/flash_and_monitor_$(date +%Y%m%d_%H%M%S).log"
info "日志同步写入: $LOG"
# 用 script 保留颜色; tee 同时存盘;idf monitor 退出码不稳定,忽略 rc
( script -q -c "idf.py -p $PORT monitor" /dev/null ) 2>&1 | tee "$LOG" || true
warn "monitor 已退出。若日志中出现 PunchCard=1 且无重启循环,则烧录+运行双成功。请把 $LOG 回传。"

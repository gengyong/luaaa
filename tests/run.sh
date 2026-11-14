#!/usr/bin/env bash
# 编译并运行 luaaa 全部测试: Lua 版本 × C++ 标准 × sanitizer 模式。
#
# 用法:
#   bash tests/run.sh                # 单一配置: 探测到的最新 Lua, c++11, ASan+UBSan
#   bash tests/run.sh all            # 矩阵: 探测到的全部 Lua × c++11/14/17/20, ASan+UBSan
#   SAN_MODE=tsan bash tests/run.sh  # 只跑并发测试, 用 TSan
#   LUA_TARGETS="luajit 5.5" bash tests/run.sh all   # 自选 Lua 版本
#   STDS="c++11 c++23" bash tests/run.sh all         # 自选 C++ 标准
#   CXX=clang++ STD=c++14 bash tests/run.sh          # 单一配置下自选编译器 / 标准
#
# 环境变量:
#   CXX         编译器 (默认 g++)
#   STD         单一配置下的 C++ 标准 (默认 c++11)
#   STDS        all 模式下的 C++ 标准列表 (默认 "c++11 c++14 c++17 c++20")
#   SAN_MODE    asan (默认) | tsan | none
#   LUA_TARGETS 空格分隔的 Lua 版本 (默认: 探测到的全部; 可选 luajit 5.1 5.2 5.3 5.4 5.5)
#   JOBS        并行任务数 (默认: CPU 核数)
#   LUA_CFLAGS / LUA_LIBS  pkg-config 找不到 Lua 时手工指定 (此时只跑这一个 Lua)
#
# Lua 通过 pkg-config 查找; 装了 Homebrew 时会自动把各 lua@x.y 的 pkgconfig 目录加进搜索路径。
#
# 退出码: 0 全部通过, 非 0 有失败
set -u

# --- 内部: 运行单个任务 (由下面的 xargs 并行调用) --------------------------
if [ "${1:-}" = "__job" ]; then
  # shellcheck disable=SC1090
  . "$2"
  label="[$J_LUA $J_STD] $(basename "$J_SRC")"
  {
    echo "############################################################"
    echo "# $label"
    echo "############################################################"
    # shellcheck disable=SC2086
    if ! $J_CXX -std=$J_STD -g $J_SAN -I. $J_CFLAGS $J_EXTRA "$J_SRC" $J_LIBS -o "$J_BIN" 2>"$J_LOG.cc"; then
      echo "  [COMPILE FAIL] $label"
      sed 's/^/    /' "$J_LOG.cc"
    else
      "$J_BIN"
      echo "  (exit=$?)"
    fi
  } > "$J_LOG" 2>&1
  rm -f "$J_LOG.cc" "$J_BIN"
  rm -rf "$J_BIN.dSYM"
  exit 0
fi

cd "$(dirname "$0")/.."

MODE="${1:-default}"
CXX="${CXX:-g++}"
STD="${STD:-c++11}"
SAN_MODE="${SAN_MODE:-asan}"
RESULTS="tests/results.txt"
JOBS="${JOBS:-$( (getconf _NPROCESSORS_ONLN || sysctl -n hw.ncpu || echo 4) 2>/dev/null | head -1)}"

# --- Lua 版本探测 ---------------------------------------------------------
# Homebrew 的带版本号的 keg (lua@5.4 等) 不在默认的 pkg-config 搜索路径里。
if command -v brew >/dev/null 2>&1; then
  BREW_PREFIX="$(brew --prefix 2>/dev/null)"
  for d in "$BREW_PREFIX"/opt/lua*/lib/pkgconfig "$BREW_PREFIX"/opt/luajit*/lib/pkgconfig; do
    [ -d "$d" ] && PKG_CONFIG_PATH="$d${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
  done
  export PKG_CONFIG_PATH
fi

# 每个 target: "name|cflags|libs"
ALL_TARGETS=()
have_target() {
  local t
  for t in ${ALL_TARGETS[@]+"${ALL_TARGETS[@]}"}; do [ "${t%%|*}" = "$1" ] && return 0; done
  return 1
}
add_target() {   # add_target <name> <pkg-config 模块名候选...>
  local name="$1" pc cflags libs; shift
  have_target "$name" && return 0
  for pc in "$@"; do
    if pkg-config --exists "$pc" 2>/dev/null; then
      cflags="$(pkg-config --cflags "$pc")"; libs="$(pkg-config --libs "$pc")"
      ALL_TARGETS+=("$name|$cflags|$libs")
      return 0
    fi
  done
  return 1
}

if [ -n "${LUA_CFLAGS:-}" ] || [ -n "${LUA_LIBS:-}" ]; then
  ALL_TARGETS=("custom|${LUA_CFLAGS:-}|${LUA_LIBS:--llua -lm}")
else
  add_target luajit luajit
  for v in 5.1 5.2 5.3 5.4 5.5; do
    add_target "$v" "lua$v" "lua-$v" "lua${v/./}"
  done
  # 不带版本号的 "lua" 模块: 按它报告的版本归类 (该版本还没找到时才用)
  if pkg-config --exists lua 2>/dev/null; then
    v="$(pkg-config --modversion lua | cut -d. -f1,2)"
    add_target "$v" lua
  fi
fi
if [ ${#ALL_TARGETS[@]} -eq 0 ]; then
  echo "[FATAL] pkg-config 找不到任何 Lua。请安装 Lua 的开发包, 或设置 LUA_CFLAGS / LUA_LIBS。" >&2
  exit 2
fi

if [ "$MODE" = "all" ]; then
  STD_LIST="${STDS:-c++11 c++14 c++17 c++20}"
  if [ -z "${LUA_TARGETS:-}" ]; then
    LUA_TARGETS=""
    for t in "${ALL_TARGETS[@]}"; do LUA_TARGETS+=" ${t%%|*}"; done
  fi
else
  STD_LIST="$STD"
  # 单一配置: 探测到的最新 PUC Lua, 没有则 LuaJIT
  if [ -z "${LUA_TARGETS:-}" ]; then
    LUA_TARGETS="${ALL_TARGETS[${#ALL_TARGETS[@]}-1]%%|*}"
  fi
fi

SELECTED=()
for want in $LUA_TARGETS; do
  for t in "${ALL_TARGETS[@]}"; do
    [ "${t%%|*}" = "$want" ] && SELECTED+=("$t")
  done
done
if [ ${#SELECTED[@]} -eq 0 ]; then
  echo "[FATAL] 选中的 LUA_TARGETS ($LUA_TARGETS) 无一可用; 探测到: $(for t in "${ALL_TARGETS[@]}"; do printf '%s ' "${t%%|*}"; done)" >&2
  exit 2
fi

# --- sanitizer 配置 -------------------------------------------------------
case "$SAN_MODE" in
  asan)  SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
         export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
         export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=0}" ;;
  tsan)  SAN="-fsanitize=thread -fno-omit-frame-pointer"
         export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=0}" ;;
  none)  SAN="" ;;
  *) echo "[FATAL] 未知 SAN_MODE=$SAN_MODE (asan|tsan|none)" >&2; exit 2 ;;
esac

# --- 测试文件清单 ---------------------------------------------------------
# 格式: "文件名|是否并发测试"
ALL_TESTS=(
  "tests/test_functional.cpp|0"
  "tests/test_fixes.cpp|0"
  "tests/test_callbacks.cpp|0"
  "tests/test_bugs.cpp|0"
  "tests/test_edge.cpp|0"
  "tests/test_extend.cpp|0"
  "tests/test_numeric.cpp|0"
  "tests/test_containers.cpp|0"
  "tests/test_def_singleton.cpp|0"
  "tests/test_cast.cpp|0"
  "tests/test_property.cpp|0"
  "tests/test_lifetime.cpp|0"
  "tests/test_callbacks_ptr.cpp|0"
  "tests/test_features.cpp|0"
  "tests/test_features_no_property.cpp|0"
  "tests/test_features_no_extend.cpp|0"
  "tests/test_features_no_conflict.cpp|0"
  "tests/test_features_no_intrange.cpp|0"
  "tests/test_features_nil_nullptr.cpp|0"
  "tests/test_overload.cpp|0"
  "tests/test_metamethod.cpp|0"
  "tests/test_float_edge.cpp|0"
  "tests/test_containers_edge.cpp|0"
  "tests/test_exception.cpp|0"
  "tests/test_strings.cpp|0"
  "tests/test_static_const.cpp|0"
  "tests/test_forward_list.cpp|0"
  "tests/test_property_except.cpp|0"
  "tests/test_gc_except.cpp|0"
  "tests/test_numeric_edge2.cpp|0"
  "tests/test_review3.cpp|0"
  "tests/test_review7.cpp|0"
  "tests/test_review8.cpp|0"
  "tests/test_review9.cpp|0"
  "tests/test_review10.cpp|0"
  "tests/test_concurrency.cpp|1"
)

# --- 生成任务并并行运行 ---------------------------------------------------
WORK="$(mktemp -d "${TMPDIR:-/tmp}/luaaa_run.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
q() { printf '%q' "$1"; }

n=0
for target in "${SELECTED[@]}"; do
  lua_name="${target%%|*}"; rest="${target#*|}"
  cflags="${rest%%|*}"; libs="${rest#*|}"
  for std in $STD_LIST; do
    for entry in "${ALL_TESTS[@]}"; do
      src="${entry%%|*}"; is_concurrent="${entry#*|}"
      # TSan 构建开销大, 对非并发测试没有收益: 只跑并发测试
      if [ "$SAN_MODE" = "tsan" ] && [ "$is_concurrent" != "1" ]; then continue; fi
      extra=""
      [ "$is_concurrent" = "1" ] && extra="-pthread"
      n=$((n + 1))
      id="$(printf '%05d' "$n")"
      {
        echo "J_CXX=$(q "$CXX")";       echo "J_STD=$(q "$std")"
        echo "J_SAN=$(q "$SAN")";       echo "J_LUA=$(q "$lua_name")"
        echo "J_CFLAGS=$(q "$cflags")"; echo "J_LIBS=$(q "$libs")"
        echo "J_EXTRA=$(q "$extra")";   echo "J_SRC=$(q "$src")"
        echo "J_BIN=$(q "$WORK/$id.bin")"; echo "J_LOG=$(q "$WORK/$id.log")"
      } > "$WORK/$id.job"
    done
  done
done

echo "luaaa tests: $n 个任务, $JOBS 路并行  (CXX=$CXX SAN=$SAN_MODE)"
echo "  Lua : $(for t in "${SELECTED[@]}"; do printf '%s ' "${t%%|*}"; done)"
echo "  C++ : $STD_LIST"
ls "$WORK"/*.job | xargs -P "$JOBS" -n 1 bash "$0" __job

# --- 汇总 -----------------------------------------------------------------
# 每个测试最后一行形如 "=== name: N passed, M failed ==="
TOTAL_PASS=0
TOTAL_FAIL=0
FAIL_RECORDS=""
COMPILE_FAILS=""
NO_SUMMARY=""
: > "$RESULTS"
for job in "$WORK"/*.job; do
  # shellcheck disable=SC1090
  . "$job"
  tag="$J_LUA/$J_STD:$(basename "$J_SRC")"
  cat "$J_LOG" >> "$RESULTS"
  if grep -q "\[COMPILE FAIL\]" "$J_LOG"; then
    COMPILE_FAILS="$COMPILE_FAILS $tag"
    cat "$J_LOG"
    continue
  fi
  summary="$(grep -E "^=== .* [0-9]+ passed, [0-9]+ failed ===" "$J_LOG" | tail -1)"
  if [ -z "$summary" ]; then
    # 没有汇总行: 退出码为 0 说明该测试本来就不输出汇总 (只打印信息);
    # 非 0 则是崩溃或被 sanitizer 中止。
    if ! grep -q "^  (exit=0)$" "$J_LOG"; then
      NO_SUMMARY="$NO_SUMMARY $tag"
      cat "$J_LOG"
    fi
    continue
  fi
  p="$(echo "$summary" | sed -nE 's/.*: ([0-9]+) passed.*/\1/p')"
  f="$(echo "$summary" | sed -nE 's/.*[^0-9]([0-9]+) failed ===/\1/p')"
  TOTAL_PASS=$((TOTAL_PASS + ${p:-0}))
  TOTAL_FAIL=$((TOTAL_FAIL + ${f:-0}))
  if [ "${f:-0}" -gt 0 ]; then
    FAIL_RECORDS="$FAIL_RECORDS $tag"
    cat "$J_LOG"
  fi
done
SAN_REPORTS="$(grep -cE "ERROR: (Address|Thread|Leak)Sanitizer|runtime error:|WARNING: ThreadSanitizer" "$RESULTS")"

echo
echo "================================================================"
echo "MATRIX SUMMARY   (CXX=$CXX SAN=$SAN_MODE)"
echo "================================================================"
echo "  Lua targets : $(for t in "${SELECTED[@]}"; do printf '%s ' "${t%%|*}"; done)"
echo "  C++ stds    : $STD_LIST"
echo "  Total PASS  : $TOTAL_PASS"
echo "  Total FAIL  : $TOTAL_FAIL"
echo "  Sanitizer   : $SAN_REPORTS 条报告"
[ -n "$COMPILE_FAILS" ] && echo "  COMPILE FAIL:$COMPILE_FAILS"
[ -n "$NO_SUMMARY" ]    && echo "  CRASHED     :$NO_SUMMARY"
[ -n "$FAIL_RECORDS" ]  && echo "  FAIL cases  :$FAIL_RECORDS"
echo "  Details     : $RESULTS"
echo

if [ "$TOTAL_FAIL" -gt 0 ] || [ -n "$COMPILE_FAILS" ] || [ -n "$NO_SUMMARY" ] || [ "$SAN_REPORTS" -gt 0 ]; then
  exit 1
fi
exit 0

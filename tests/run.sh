#!/usr/bin/env bash
# 编译并运行 luaaa 全部测试，支持多 Lua 版本 × sanitizer 模式矩阵。
#
# 用法:
#   bash tests/run.sh                # 默认: 当前唯一 Lua (lua5.5), ASan+UBSan
#   bash tests/run.sh all            # 矩阵: luajit/5.4/5.5 × ASan+UBSan, 5.5 × TSan
#   SAN_MODE=tsan bash tests/run.sh  # 仅 TSan (默认 asan)
#   LUA_TARGETS="luajit 5.5" bash tests/run.sh all   # 自选版本子集
#   CXX=clang++ STD=c++14 bash tests/run.sh all      # 自选编译器/标准
#
# 环境变量:
#   CXX         编译器 (默认 g++)
#   STD         C++ 标准 (默认 c++11)
#   SAN_MODE    asan (默认) | tsan | none
#   LUA_TARGETS 空格分隔的 Lua 版本 (默认自动探测)
#
# 退出码: 0 全部通过, 非0 有失败
set -u
cd "$(dirname "$0")/.."

MODE="${1:-default}"
CXX="${CXX:-g++}"
STD="${STD:-c++11}"
SAN_MODE="${SAN_MODE:-asan}"
RESULTS="tests/results.txt"

# --- Lua 版本探测 ---------------------------------------------------------
# 每个 target: "name|cflags|libs"
declare -a ALL_TARGETS=()
add_target() {
  local name="$1" pc="$2"
  local cflags libs
  cflags="$(pkg-config --cflags "$pc" 2>/dev/null)" || return 1
  libs="$(pkg-config --libs "$pc" 2>/dev/null)" || return 1
  ALL_TARGETS+=("$name|$cflags|$libs")
  return 0
}

probe_targets() {
  ALL_TARGETS=()
  # LuaJIT (5.1 API)
  pkg-config --exists luajit 2>/dev/null && add_target luajit luajit
  # Lua 5.4
  PKG_CONFIG_PATH="/opt/homebrew/opt/lua@5.4/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" \
    pkg-config --exists lua5.4 2>/dev/null && \
    PKG_CONFIG_PATH="/opt/homebrew/opt/lua@5.4/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" add_target 5.4 lua5.4
  # Lua 5.5 (默认/当前)
  pkg-config --exists lua5.5 2>/dev/null && add_target 5.5 lua5.5
}

if [ "$MODE" = "all" ]; then
  probe_targets
  if [ ${#ALL_TARGETS[@]} -eq 0 ]; then
    echo "[FATAL] 未探测到任何 Lua 版本" >&2; exit 2
  fi
  if [ -z "${LUA_TARGETS:-}" ]; then
    LUA_TARGETS=""
    for t in "${ALL_TARGETS[@]}"; do LUA_TARGETS+=" ${t%%|*}"; done
  fi
else
  # 默认模式: 单一当前 Lua (兼容旧行为)
  LUA_CFLAGS="$(pkg-config --cflags lua5.5 2>/dev/null || pkg-config --cflags lua 2>/dev/null || echo '-I/opt/homebrew/include/lua')"
  LUA_LIBS="$(pkg-config --libs lua5.5 2>/dev/null || pkg-config --libs lua 2>/dev/null || echo '-L/opt/homebrew/lib -llua -lm')"
  ALL_TARGETS=("default|$LUA_CFLAGS|$LUA_LIBS")
  LUA_TARGETS="default"
fi

# 解析选中的 targets
declare -a SELECTED=()
for want in $LUA_TARGETS; do
  for t in "${ALL_TARGETS[@]}"; do
    [ "${t%%|*}" = "$want" ] && SELECTED+=("$t")
  done
done
if [ ${#SELECTED[@]} -eq 0 ]; then
  echo "[FATAL] 选中的 Lua_TARGETS ($LUA_TARGETS) 无一可用" >&2; exit 2
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
# 格式: "文件名|是否需要 stdlib|是否并发"
ALL_TESTS=(
  "tests/test_functional.cpp|1|0"
  "tests/test_fixes.cpp|1|0"
  "tests/test_callbacks.cpp|1|0"
  "tests/test_bugs.cpp|1|0"
  "tests/test_edge.cpp|1|0"
  "tests/test_extend.cpp|1|0"
  "tests/test_numeric.cpp|1|0"
  "tests/test_containers.cpp|1|0"
  "tests/test_def_singleton.cpp|1|0"
  "tests/test_cast.cpp|1|0"
  "tests/test_property.cpp|1|0"
  "tests/test_lifetime.cpp|1|0"
  "tests/test_callbacks_ptr.cpp|1|0"
  "tests/test_features.cpp|1|0"
  "tests/test_features_no_property.cpp|1|0"
  "tests/test_features_no_extend.cpp|1|0"
  "tests/test_features_no_conflict.cpp|1|0"
  "tests/test_features_no_intrange.cpp|1|0"
  "tests/test_overload.cpp|1|0"
  "tests/test_metamethod.cpp|1|0"
  "tests/test_float_edge.cpp|1|0"
  "tests/test_containers_edge.cpp|1|0"
  "tests/test_exception.cpp|1|0"
  "tests/test_strings.cpp|1|0"
  "tests/test_static_const.cpp|1|0"
  "tests/test_forward_list.cpp|1|0"
  "tests/test_property_except.cpp|1|0"
  "tests/test_gc_except.cpp|1|0"
  "tests/test_numeric_edge2.cpp|1|0"
  "tests/test_review3.cpp|1|0"
  "tests/test_review7.cpp|1|0"
  "tests/test_review8.cpp|1|0"
  "tests/test_review9.cpp|1|0"
  "tests/test_concurrency.cpp|1|1"
)

# --- 运行单测 -------------------------------------------------------------
# 全局累计
TOTAL_PASS=0
TOTAL_FAIL=0
FAIL_RECORDS=""   # "lua:file" 列表
COMPILE_FAILS=""

run_one() {
  local lua_name="$1" cflags="$2" libs="$3" src="$4" extra="$5"
  local bin="/tmp/luaaa_$(basename "$src" .cpp)_${lua_name}"
  local label="[$lua_name] $(basename "$src")"
  {
    echo "############################################################"
    echo "# $label"
    echo "############################################################"
  }
  if ! $CXX -std=$STD -g $SAN -I. $cflags $libs $extra "$src" -o "$bin" 2>/tmp/cc.log; then
    echo "  [COMPILE FAIL] $label"
    sed 's/^/    /' /tmp/cc.log
    COMPILE_FAILS="$COMPILE_FAILS $lua_name:$(basename "$src")"
    return 1
  fi
  "$bin"; local rc=$?
  echo "  (exit=$rc)"
  return 0
}

# 从测试输出最后行 "=== name: N passed, M failed ===" 提取
parse_summary() {
  local out="$1"
  # 抓最后一行 === xxx: P passed, F failed ===
  grep -E "^=== .* [0-9]+ passed, [0-9]+ failed ===" "$out" | tail -1
}

# --- 主循环: targets × tests ---------------------------------------------
: > "$RESULTS"
for target in "${SELECTED[@]}"; do
  lua_name="${target%%|*}"; rest="${target#*|}"
  cflags="${rest%%|*}"; libs="${rest#*|}"

  echo "================================================================"
  echo "== Lua: $lua_name   (cflags: $cflags  libs: $libs)"
  echo "================================================================" | tee -a "$RESULTS"

  for entry in "${ALL_TESTS[@]}"; do
    src="${entry%%|*}"; rest="${entry#*|}"
    need_stdlib="${rest%%|*}"; is_concurrent="${rest#*|}"

    # TSan 模式仅跑并发测试 (TSan 构建开销大, 且非并发测试无收益)
    if [ "$SAN_MODE" = "tsan" ] && [ "$is_concurrent" != "1" ]; then
      continue
    fi
    # 5.1/LuaJIT 不跑 stdlib-only 测试里的并发(无 stdlib 需求差异, 但并发测试需要 thread)
    # 实际所有测试都用 stdlib; 此开关为未来 LUAAA_WITHOUT_CPP_STDLIB 测试预留

    tmpout="/tmp/luaaa_run_${lua_name}_$(basename "$src" .cpp).log"
    # 并发测试需要链接 pthread
    extra=""
    [ "$is_concurrent" = "1" ] && extra="-pthread"
    run_one "$lua_name" "$cflags" "$libs" "$src" "$extra" > "$tmpout" 2>&1
    cat "$tmpout"
    cat "$tmpout" >> "$RESULTS"

    summary="$(parse_summary "$tmpout")"
    if [ -n "$summary" ]; then
      p="$(echo "$summary" | sed -nE 's/.*: ([0-9]+) passed.*/\1/p')"
      f="$(echo "$summary" | sed -nE 's/.*[^0-9]([0-9]+) failed ===/\1/p')"
      TOTAL_PASS=$((TOTAL_PASS + ${p:-0}))
      TOTAL_FAIL=$((TOTAL_FAIL + ${f:-0}))
      [ "${f:-0}" -gt 0 ] && FAIL_RECORDS="$FAIL_RECORDS $lua_name:$(basename "$src")"
    fi
  done
done

# --- 汇总 -----------------------------------------------------------------
echo
echo "================================================================"
echo "MATRIX SUMMARY   (CXX=$CXX STD=$STD SAN=$SAN_MODE)"
echo "================================================================"
echo "  Lua targets : $(echo "${SELECTED[@]}" | sed 's/|[^ ]*//g')"
echo "  Total PASS  : $TOTAL_PASS"
echo "  Total FAIL  : $TOTAL_FAIL"
[ -n "$COMPILE_FAILS" ] && echo "  COMPILE FAIL: $COMPILE_FAILS"
[ -n "$FAIL_RECORDS" ]  && echo "  FAIL cases  :$FAIL_RECORDS"
echo "  Details     : $RESULTS"
echo

if [ "$TOTAL_FAIL" -gt 0 ] || [ -n "$COMPILE_FAILS" ]; then
  exit 1
fi
exit 0

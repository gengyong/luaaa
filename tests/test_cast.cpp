// 类型转换边界测试：系统检查 LuaStack<T> 各特化在错误/边界输入下的行为。
// 设计原则：断言「正确/合理」的行为；若当前实现有 bug，对应断言 FAIL，成为实证。
//
// 探测辅助 raises(L, code): 执行 code, 返回 code 是否抛错 (pcall 内层判定, 与 assert 解耦).
//   code 应是单条函数调用表达式; 探测会把它的 pcall 结果存入全局 g_raises.
//
// 重点排查:
//   - 整数 get 静默错误 (luaaa.hpp:550-561)  — 非数字字符串/浮点字符串返回 0
//   - 整数 put 无溢出检测                    — 与浮点特化的溢出报错不对称
//   - 浮点 get 溢出/NaN/inf 边界             — luaaa.hpp:486-503
//   - bool 严格类型拒绝强转                  — luaaa.hpp:519
//   - const char* 静默强转 bool/number       — luaaa.hpp:572
//   - T* 传 nil 静默返回 nullptr             — luaaa.hpp:437 (C++ 侧崩溃风险)
//   - std::pair 短表                         — 不崩溃
//   - char 类型字符串输入
#include "luaaa.hpp"

#include <cstdio>
#include <cfloat>
#include <climits>
#include <cmath>
#include <string>
#include <utility>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

// 探测「调用 expr 是否抛错」. 用 pcall 内层判定, 不与 assert 混淆.
// 返回 true=抛错, false=成功.
static bool raises(lua_State* L, const char* expr) {
    // 把 expr 包成 function 并 pcall; 把是否成功存入 g_ok
    char buf[512];
    snprintf(buf, sizeof(buf),
             "local ok = pcall(function() return %s end); g_ok = ok", expr);
    if (luaL_dostring(L, buf) != 0) {
        // 包装代码本身有语法错 (不应发生)
        lua_pop(L, 1);
        return true;
    }
    lua_getglobal(L, "g_ok");
    bool ok = lua_isboolean(L, -1) && lua_toboolean(L, -1);
    lua_pop(L, 1);
    return !ok;  // ok=false 表示抛错 -> raises=true
}

// 取 Lua 全局 number 值 (探测返回值用)
static lua_Number get_num(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    lua_Number v = lua_tonumber(L, -1); lua_pop(L, 1);
    return v;
}

// ---- 被测函数 ----
static long long          echo_ll  (long long v)          { return v; }
static unsigned long long echo_ull (unsigned long long v) { return v; }
static int                echo_int (int v)                { return v; }
static char               echo_char(char v)               { return v; }
static float              echo_f   (float v)              { return v; }
static double             echo_d   (double v)             { return v; }
static bool               echo_bool(bool v)               { return v; }
static std::string        echo_str (const std::string& s) { return s; }
static const char*        echo_cstr(const char* s)        { return s; }
static std::pair<int,int> echo_pair(std::pair<int,int> p) { return p; }
static long ptr_addr(int* p) { return reinterpret_cast<long>(p); }

int main() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    LuaModule(L, "c")
        .fun("ll", echo_ll)
        .fun("ull", echo_ull)
        .fun("int", echo_int)
        .fun("char", echo_char)
        .fun("f", echo_f)
        .fun("d", echo_d)
        .fun("b", echo_bool)
        .fun("str", echo_str)
        .fun("cstr", echo_cstr)
        .fun("pair", echo_pair)
        .fun("ptr", ptr_addr);

    // ===== 整数 get: 非数字字符串 (M1 修复 — 应报错) =====
    printf("[整数 get 非数字字符串 (M1 修复: 应报错)]\n");
    {
        bool raises_hello = raises(L, "c.ll('hello')");
        printf("    (信息) echo_ll('hello') %s\n", raises_hello ? "报错(好)" : "静默返回0 (bug)");
        check(raises_hello, "echo_ll('hello') 应报错而非静默返回0 (M1 修复)");
    }

    // ===== 整数 get: 浮点格式字符串 "1e30" (M1 修复 — 5.3+ 应报错) =====
    // 注: Lua 5.1/LuaJIT 无整型子类型, '1e30' 是合法 number, lua_tointegerx 截断为 LLONG_MAX
    //     (isnum=1) — 这是 Lua/LuaJIT 固有行为, 非 luaaa bug. 仅 5.3+ 报错.
    printf("[整数 get 浮点格式字符串 '1e30' (M1 修复, 版本相关)]\n");
    {
        bool r = raises(L, "c.ll('1e30')");
#if !defined(LUA_VERSION_NUM) || LUA_VERSION_NUM <= 501
        // LuaJIT/5.1: '1e30' 截断为 LLONG_MAX, 不报错 (Lua 固有行为)
        printf("    (信息) echo_ll('1e30') raises=%d (5.1/LuaJIT: 截断, 非bug)\n", (int)r);
        check(!r, "echo_ll('1e30') 5.1/LuaJIT 截断为 LLONG_MAX (Lua 固有, 非 bug)");
#else
        printf("    (信息) echo_ll('1e30') %s\n", r ? "报错(好)" : "静默返回0 (5.3+ bug)");
        check(r, "echo_ll('1e30') 应报错 (M1 修复, 5.3+ 静默返回0 是 bug)");
#endif
    }

    // ===== 整数 get: 正常数字字符串仍应工作 (回归保护) =====
    printf("[整数 get 正常数字字符串 (回归保护)]\n");
    {
        bool r = raises(L, "assert(c.ll('42') == 42)");
        check(!r, "echo_ll('42') == 42 (字符串数字仍可转换)");
    }

    // ===== 整数 get: 浮点数 1.5 (跨版本一致性, 仅记录) =====
    printf("[整数 get 浮点数 1.5 (跨版本一致性, 仅记录)]\n");
    {
        luaL_dostring(L, "g_v = c.ll(1.5)");
        lua_Number v = get_num(L, "g_v");
        printf("    (信息) echo_ll(1.5) = %g\n", (double)v);
        check(v == 0 || v == 1, "echo_ll(1.5) = 截断(1) 或 拒绝(0) — 记录当前值");
    }

    // ===== 整数 get: 整数 3 vs 浮点 3.0 (5.3+ 子类型) =====
    printf("[整数 get 整数 vs 浮点 (5.3+ 子类型)]\n");
    {
        luaL_dostring(L, "g_v3 = c.int(3); g_v30 = c.int(3.0)");
        lua_Number v3 = get_num(L, "g_v3");
        lua_Number v30 = get_num(L, "g_v30");
        printf("    (信息) echo_int(3)=%g  echo_int(3.0)=%g\n", (double)v3, (double)v30);
        // 期望两者都返回 3. 当前 5.3+: echo_int(3.0) 可能返回 0
        check(v3 == 3, "echo_int(3) = 3");
        // echo_int(3.0) 在 5.3+ 若返回 0, 记录为不一致
        check(v30 == 3, "echo_int(3.0) 应 = 3 (5.3+ 整浮子类型可能致 0)");
    }

    // ===== 整数 put: 64位无符号溢出 (M2 修复 — 5.3+ 应报错) =====
    // 注: LuaJIT/5.1 上字面量 18446744073709551615 先解析为 double, get 方向截断为 LLONG_MAX,
    //     C++ 收到的值在范围内, put 不报错 — 这是 Lua 固有行为. 仅 5.3+ (整型子类型) 能保留原值并触发 put 报错.
    printf("[整数 put 64位无符号 — M2 修复 (版本相关)]\n");
    {
        bool r = raises(L, "c.ull(18446744073709551615)");
#if !defined(LUA_VERSION_NUM) || LUA_VERSION_NUM <= 501
        printf("    (信息) echo_ull(ULLONG_MAX) raises=%d (5.1/LuaJIT: get 截断, put 不报错)\n", (int)r);
        check(!r, "echo_ull(ULLONG_MAX) 5.1/LuaJIT: get 截断为 LLONG_MAX (Lua 固有)");
#else
        printf("    (信息) echo_ull(ULLONG_MAX): raises=%d\n", (int)r);
        check(r, "echo_ull(ULLONG_MAX) 应报溢出 (M2 修复, 与浮点特化对称)");
#endif
    }

    // ===== 整数 put: 在范围内的负值仍正常 (回归保护) =====
    printf("[整数 put 负值正常 (回归保护)]\n");
    check(!raises(L, "assert(c.ll(-2147483648) == -2147483648)"), "echo_ll(-2^31) 正常往返");

    // ===== char 类型字符串 (M1 修复 — 'A' 非数字, 应报错而非返回0) =====
    printf("[char 类型字符串输入 (M1 修复: 非数字应报错)]\n");
    {
        bool r = raises(L, "c.char('A')");
        printf("    (信息) echo_char('A') raises=%d (期望 true: 报错)\n", (int)r);
        check(r, "echo_char('A') 应报错 (M1 修复, 字符非数字不再静默返回0)");
    }

    // ===== 浮点 get 正常往返 =====
    printf("[浮点 get 正常往返]\n");
    check(!raises(L, "c.f(1.5)"), "float 1.5 往返");
    check(!raises(L, "c.d(1e300)"), "double 1e300 往返");

    // ===== 浮点 get 溢出应报错 (luaaa.hpp:492-494) =====
    printf("[浮点 get 溢出报错]\n");
    {
        bool r = raises(L, "c.f(1e300)");
        printf("    (信息) echo_f(1e300) raises=%d\n", (int)r);
        check(r, "float get 1e300 溢出应报错");
    }

    // ===== 浮点 get: NaN (luaaa.hpp:492 NaN 不触发 v>hi/v<-hi) =====
    printf("[浮点 get NaN 处理]\n");
    {
        luaL_dostring(L, "g_nan = c.f(0/0)");
        lua_Number nv = get_num(L, "g_nan");
        printf("    (信息) echo_f(NaN) = %g (isnan=%d)\n", (double)nv, std::isnan((float)nv));
        check(std::isnan((float)nv), "float get NaN 透传 (文档化: 不触发溢出检查)");
    }

    // ===== 浮点 get: +inf 透传 (第九轮 R9-1 起: 无穷大不算溢出) =====
    // 原先这里断言「float get +inf 应报错 (inf > FLT_MAX)」。
    printf("[浮点 get +inf 处理]\n");
    {
        bool r = raises(L, "assert(c.f(math.huge) == math.huge)");
        printf("    (信息) echo_f(+inf) raises=%d (期望 false: inf 透传)\n", (int)r);
        check(!r, "float get +inf 透传 (不再报错)");
    }

    // ===== 浮点 get: 近 FLT_MAX 边界 (3.4e38 应正常, 不误报) =====
    printf("[浮点 get 近 FLT_MAX 边界]\n");
    {
        bool r = raises(L, "c.f(3.4e38)");
        printf("    (信息) echo_f(3.4e38) raises=%d (期望 false)\n", (int)r);
        check(!r, "float get 3.4e38 (近FLT_MAX) 不应误报溢出");
    }

    // ===== bool: 严格 Lua 类型 (L1/L2: 只接受 true/false, 拒绝 number/string/nil) =====
    // 决策: 采用严格派. Lua 的 truthiness (if x then) 虽接受一切, 但把任意值悄悄变 bool
    //       会掩盖脚本侧的类型错误. 严格匹配 Lua `boolean` 类型, 务实明确.
    printf("[bool — 严格 Lua 类型: 只接受 true/false]\n");
    {
        // true/false 正常
        check(!raises(L, "c.b(true)"),  "bool(true)  接受");
        check(!raises(L, "c.b(false)"), "bool(false) 接受");
        // number/string/nil 一律报错
        check(raises(L, "c.b(1)"),      "bool(1) 报错 (拒绝 number, 严格派)");
        check(raises(L, "c.b(0)"),      "bool(0) 报错 (拒绝 number)");
        check(raises(L, "c.b('true')"), "bool('true') 报错 (拒绝 string)");
        check(raises(L, "c.b('')"),     "bool('') 报错 (拒绝 string)");
        check(raises(L, "c.b(nil)"),    "bool(nil) 报错 (拒绝 nil)");
    }

    // ===== int: 拒绝 bool (L1: 对齐 Lua 算术 — true+1 会报错, int(true) 也报错) =====
    printf("[int — 拒绝 bool (对齐 Lua 算术: true+1 报错)]\n");
    {
        check(raises(L, "c.int(true)"),  "int(true)  报错 (对齐 Lua: true+1 报错)");
        check(raises(L, "c.int(false)"), "int(false) 报错");
        // 数字 / 数字字符串仍正常 (Lua 算术也接受)
        check(!raises(L, "assert(c.int(42) == 42)"),   "int(42) 正常");
        check(!raises(L, "assert(c.int('42') == 42)"), "int('42') 正常 (数字字符串)");
    }

    // ===== const char* / string: Lua tostring() 语义 (接受一切可 stringify 的值) =====
    // 注: 这等于 Lua 的 tostring() — bool/number 都会被 stringify, 属既定语义非 bug.
    printf("[const char* — Lua tostring() 语义]\n");
    {
        auto sv = [&](const char* expr) -> std::string {
            char buf[128];
            snprintf(buf, sizeof(buf), "g_sv = c.cstr(%s)", expr);
            if (luaL_dostring(L, buf) != 0) { lua_pop(L, 1); return "(err)"; }
            lua_getglobal(L, "g_sv");
            const char* s = lua_tostring(L, -1); lua_pop(L, 1);
            return s ? std::string(s) : std::string("(null)");
        };
        check(sv("true")  == "true",  "cstr(true)  = 'true'  (Lua tostring)");
        check(sv("false") == "false", "cstr(false) = 'false' (Lua tostring)");
        check(sv("123")   == "123",   "cstr(123)   = '123'   (Lua tostring)");
        check(sv("'hi'")  == "hi",    "cstr('hi')  = 'hi'");
    }

    // ===== T* 传 nil (M4 修复 — 应报错而非返回 nullptr) =====
    printf("[T* 传 nil (M4 修复: 应报错而非返回 nullptr)]\n");
    {
        bool r = raises(L, "c.ptr(nil)");
        printf("    (信息) ptr_addr(nil) raises=%d (期望 true: 报错)\n", (int)r);
        // 修复后: luaL_argcheck 报错, C++ 不会收到 nullptr. 消除解引用崩溃风险.
        check(r, "ptr_addr(nil) 应报错 (M4 修复, 不再静默返回 nullptr)");
    }

    // ===== std::pair 短表 (1元素, 应报错不崩溃) =====
    printf("[std::pair 短表不崩溃]\n");
    {
        bool r = raises(L, "c.pair({10})");
        printf("    (信息) echo_pair({10}) raises=%d (期望 true: 报错不崩溃)\n", (int)r);
        check(r, "echo_pair({10}) 短表应报错而非崩溃");
    }

    lua_close(L);
    printf("\n=== cast: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

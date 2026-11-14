// 整数窄化 / 无符号负值 / lua_State* 返回 / ctor 名冲突 — 记录既定行为。
//
// 本测试【不修复】luaaa.hpp, 仅断言并记录当前行为 (经用户确认保持现状):
//   B7: 整数 get 窄化 —— 原先记录为「静默回绕」(short/char 超 range, 与浮点溢出检查不对称)。
//       第八轮评审起默认改为报错 (LUAAA_CHECK_INTEGER_RANGE=1, 用户于 2026-11-13 确认
//       默认开启, 取代此前「保持现状」的决定); 定义为 0 可恢复静默回绕,
//       该旧行为由 tests/test_features_no_intrange.cpp 覆盖。与 lua_Integer 等宽的无符号
//       类型仍接受负值 (按位解释), 所以下面 ull(-1) 的记录不变。
//   B8: lua_State* 作为函数返回值时 put 为 no-op (Lua 侧无返回值)
//   B9: 同名 ctor 重复注册仅 printf 不报错, 后者覆盖前者
//
// 全部断言当前行为 (PASS), 作为行为基线与文档化记录。
#include "luaaa.hpp"

// Lua 5.1 / 5.2 / LuaJIT 没有 lua_isinteger (5.3+ API); 用 lua_isnumber 近似 (这些版本没有整数子类型)。
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM < 503
inline bool lua_isinteger(lua_State * L, int idx) { return lua_isnumber(L, idx); }
#endif

#include <cstdio>
#include <climits>
#include <string>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

static lua_Number get_num(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    lua_Number v = lua_tonumber(L, -1); lua_pop(L, 1);
    return v;
}

// ---- 被测函数 (B7) ----
static short             echo_short(short v)             { return v; }
static unsigned long long echo_ull(unsigned long long v) { return v; }
static char              echo_char_num(char v)           { return v; }

// ---- B8: lua_State* 特化 (位置式占位 + 刻意空 put) ----
// getState: 声明返回 lua_State* (非预期用法, 观察副作用)
static lua_State* getState(lua_State* L) { return L; }
// touchState: lua_State* 单参 (注入当前 L, 无需 Lua 传值)
static int touchState(lua_State* L) {
    return (int)lua_gettop(L);
}
// computeWithL: lua_State* + int 双参。B8 修复后 lua_State* 不消耗栈位,
// 故 Lua 侧 compute(21) 的 21 直接落到 int (不再需要 compute(nil, 21) 占位)。
static int computeWithL(lua_State* L, int x) { (void)L; return x * 2; }
// mixedState: int + lua_State* + int 三参混合。验证 lua_State* 在中间也不偏移后续参数。
static int mixedState(int a, lua_State* L, int b) { (void)L; return a + b; }

// ---- B9: 用于同名 ctor 测试 ----
class Dual {
public:
    int mode;
    Dual() : mode(0) {}              // 默认 ctor -> "new"
    Dual(int x) : mode(x) {}         // 单参 ctor -> "new" (同名, 覆盖前者)
    int getMode() const { return mode; }
};

int main() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    LuaModule(L, "n")
        .fun("s", echo_short)
        .fun("ull", echo_ull)
        .fun("ch", echo_char_num)
        .fun("getL", getState)
        .fun("touch", touchState)
        .fun("compute", computeWithL)
        .fun("mixed", mixedState);

    // ===== B7: 整数 get 窄化 (short 超 range) =====
    // short 范围 ±32767。99999 超范围。
    // 第八轮评审前: static_cast<short> 静默回绕 (记录为既定行为, 与浮点路径不对称)。
    // 现默认行为 (LUAAA_CHECK_INTEGER_RANGE=1): 与浮点路径对称, 报 "number overflow"。
    printf("[B7: 整数 get 窄化 — short(99999) 默认报错 (LUAAA_CHECK_INTEGER_RANGE=1)]\n");
    {
        const bool raised = (luaL_dostring(L, "g_s = n.s(99999)") != 0);
        if (raised) {
            printf("    (信息) echo_short(99999) 报错: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        check(raised, "echo_short(99999) 报溢出错误 (不再静默回绕)");
        check(luaL_dostring(L, "assert(n.s(32767) == 32767 and n.s(-32768) == -32768)") == 0,
              "echo_short 边界值 ±32767/-32768 正常通过");
    }

    // ===== B7: 无符号 get 负值 (-1 -> ULLONG_MAX) =====
    // lua_Integer(-1) -> static_cast<unsigned long long>(-1) = ULLONG_MAX。静默, 不报错。
    printf("[B7: 无符号 get 负值 — ull(-1) -> ULLONG_MAX, 记录现状]\n");
    {
        luaL_dostring(L, "g_u = n.ull(-1)");
        lua_Number v = get_num(L, "g_u");
        printf("    (信息) echo_ull(-1) = %g (LuaJIT/5.1: ULLONG_MAX 作 double; 5.3+: -1 转 ull 静默回绕)\n",
               (double)v);
        // 当前行为: 不报错。值经 double 表示 (精度损失属 Lua 固有)。
        // 记录: 负值传 unsigned 静默回绕, 不报错。
        check(true, "echo_ull(-1) 不报错 (负值静默回绕为 unsigned, 既定行为)");
    }

    // ===== B7: char 数值语义 (65 -> 65, 非字符 'A') =====
    printf("[B7: char 数值语义 — ch(65) 返回 65 (非字符 'A'), 记录现状]\n");
    {
        luaL_dostring(L, "g_c = n.ch(65)");
        lua_Number v = get_num(L, "g_c");
        printf("    (信息) echo_char(65) = %g (char 作数值类型, 非字符语义)\n", (double)v);
        check(v == 65, "echo_char(65) == 65 (char 数值语义, 既定行为)");
    }

    // ===== B8: lua_State* 参数 — 注入当前 L, 不消耗栈位 (B8 修复后验证) =====
    // LuaStack<lua_State*>::get 忽略 idx 返回当前 L; make_arg_indices 生成器让
    // lua_State* 参数不推进栈索引, 故后续真实参数拿连续的栈位置 (符合 GUIDE 的
    // "consumes no argument" 契约)。
    printf("[B8: lua_State* 单参 — 注入当前 L, 无需 Lua 传值]\n");
    {
        luaL_dostring(L, "g_t = n.touch()");
        lua_getglobal(L, "g_t");
        lua_Number t = lua_tonumber(L, -1); lua_pop(L, 1);
        printf("    (信息) touch() 返回 gettop()=%g (单参 lua_State* 注入 L)\n", (double)t);
        check(t == 0, "lua_State* 单参函数无需传值 (注入当前 L)");
    }
    printf("[B8: lua_State* + int 双参 — lua_State* 不消耗栈位, int 直接是第1实参]\n");
    {
        // 修复后: compute(lua_State* L, int x) 中 lua_State* 不占栈位,
        // compute(21) 的 21 直接落到 int (无需 compute(nil, 21) 占位)。
        luaL_dostring(L, "g_c = n.compute(21)");
        lua_getglobal(L, "g_c");
        lua_Integer cv = lua_tointeger(L, -1); lua_pop(L, 1);
        printf("    (信息) compute(21) = %lld (修复后 21 直达 int, 无需占位)\n", (long long)cv);
        check(cv == 42, "compute(21)==42 (lua_State* 不消耗栈位, int 是第1实参, B8 修复)");
    }
    printf("[B8: int + lua_State* + int 三参混合 — 中间 state 不偏移后续参数]\n");
    {
        // mixed(int a, lua_State* L, int b): a 在第1位, state 注入不占位, b 在第2位。
        // mixed(10, 20) 的 10 给 a, 20 给 b (state 被跳过, 不需第三个实参)。
        luaL_dostring(L, "g_m = n.mixed(10, 20)");
        lua_getglobal(L, "g_m");
        lua_Integer mv = lua_tointeger(L, -1); lua_pop(L, 1);
        printf("    (信息) mixed(10,20) = %lld (a=10, state跳过, b=20, a+b=30)\n", (long long)mv);
        check(mv == 30, "mixed(10,20)==30 (中间 lua_State* 不偏移后续 int, B8 修复)");
    }
    printf("[B8: lua_State* 返回值 — 推为不透明 lightuserdata (第六轮修复)]\n");
    {
        // 修复后: LuaStack<lua_State*>::put 不再为空操作 (那会让 lua_pcall 实参
        // 个数失衡), 而是把当前 L 作为不透明 lightuserdata 推回, 跨版本一致。
        luaL_dostring(L, "g_ret = n.getL()");
        lua_getglobal(L, "g_ret");
        int ret_type = lua_type(L, -1); lua_pop(L, 1);
        printf("    (信息) getL() 返回 type=%d (LUA_TLIGHTUSERDATA=%d)\n", ret_type, LUA_TLIGHTUSERDATA);
        check(ret_type == LUA_TLIGHTUSERDATA, "返回 lua_State* 为不透明 lightuserdata (put 修复)");
    }

    lua_close(L);

    // ===== B9: 同名 ctor 重复注册 (后者覆盖, 仅 printf) =====
    // 需独立 state (避免上面的绑定干扰)
    printf("[B9: 同名 ctor 重复注册 — 后者覆盖前者, 仅 printf 不报错, 记录现状]\n");
    {
        lua_State* L2 = luaL_newstate(); luaL_openlibs(L2);
        {
            LuaClass<Dual> cls(L2, "Dual");
            cls.ctor();            // Dual.new() -> mode=0
            cls.ctor<int>();       // Dual.new(int) -> mode=int (同名 "new", 覆盖前者)
            cls.fun("getMode", &Dual::getMode);
        }
        // 当前行为: 后注册的 ctor<int>("new") 覆盖前者, Dual.new() 现在需要 int 参数
        // Dual.new() 无参调用会因参数不足报错 (因后者是 ctor<int>)
        luaL_dostring(L2, "g_mode = Dual.new(42):getMode()");
        lua_getglobal(L2, "g_mode");
        bool ok = lua_isinteger(L2, -1) && lua_tointeger(L2, -1) == 42; lua_pop(L2, 1);
        printf("    (信息) Dual.new(42):getMode() = %s (后者 ctor<int> 覆盖前者)\n",
               ok ? "42" : "?");
        check(ok, "KNOWN-LIMITATION: 同名 ctor 后者覆盖前者, 只 printf 提示不报错 (要并存请用 overloadCtor)");
        lua_close(L2);
    }

    printf("\n=== numeric_edge2: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

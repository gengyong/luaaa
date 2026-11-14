// 重载/分发/原生 C 函数/state 注入/多返回值语义测试。
//
// 覆盖未测分支:
//   1. 同名 fun 两次注册 (不同签名) 的覆盖语义 — 后注册者胜出, 无分发
//   2. fun(name, lua_CFunction) 原生 C 函数注册 (类侧 + 模块侧)
//   3. 绑定函数声明 lua_State* 参数 (注入式, LuaStack<lua_State*> 忽略 idx 直接返回 L)
//   4. std::tuple 返回值 — 实证不支持多返回值, 产生单个 Lua table
//
// 设计原则: 断言「既定/合理」行为; 若行为不符则 FAIL, 揭示 bug 或文档缺口。
#include "luaaa.hpp"

// Lua 5.1 / 5.2 / LuaJIT 没有 lua_isinteger (5.3+ API); 用 lua_isnumber 近似 (这些版本没有整数子类型)。
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM < 503
inline bool lua_isinteger(lua_State * L, int idx) { return lua_isnumber(L, idx); }
#endif

#include <cstdio>
#include <string>
#include <tuple>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

// 探测 expr 是否抛错 (true=抛错)
static bool raises(lua_State* L, const char* expr) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "g_ok = pcall(function() return %s end)", expr);
    if (luaL_dostring(L, buf) != 0) { lua_pop(L, 1); return true; }
    lua_getglobal(L, "g_ok");
    bool ok = lua_isboolean(L, -1) && lua_toboolean(L, -1);
    lua_pop(L, 1);
    return !ok;
}

// ====== 1. 同名 fun 覆盖语义 ======
// 记录最后一次被调用的函数签名类型
static int g_lastCall = 0;   // 1=int 版, 2=string 版
static void dup_int(int x)    { (void)x; g_lastCall = 1; }
static void dup_str(const std::string& s) { (void)s; g_lastCall = 2; }

// ====== 2. lua_CFunction 原生注册 ======
// 模块侧: 参数从 idx 1 开始。
// 类侧 (作方法调用): luaaa 不跳过 self, 故 self 在 idx 1, 实参从 idx 2 开始。
static int nativeCFun(lua_State* L) {
    int top = lua_gettop(L);
    int argIdx = (top >= 2) ? 2 : 1;  // 类方法调用时 idx1=self, 取 idx2
    lua_Integer v = luaL_checkinteger(L, argIdx);
    lua_pushinteger(L, v * 2);
    return 1;   // 原生函数自己控制返回值数
}

// ====== 3. lua_State* 注入参数 ======
static int stateDepth(lua_State* L) {
    // 不消耗 Lua 参数: L 是注入的。返回当前栈顶 (调用前应为 0, Lua 侧无实参)
    (void)L;
    return (int)lua_gettop(L);
}

// ====== 4. tuple 返回 ======
static std::tuple<int, std::string> retTuple() {
    return std::make_tuple(42, std::string("hi"));
}

int main() {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    // 模块绑定
    LuaModule M(L, "ov");
    M.fun("dup", dup_int);
    M.fun("dup", dup_str);          // 同名二次注册: 覆盖前者
    M.fun("nat", nativeCFun);       // lua_CFunction 模块侧
    M.fun("depth", stateDepth);     // lua_State* 注入
    M.fun("rtuple", retTuple);      // tuple 返回

    // ===== 1. 同名 fun 覆盖: 后者胜出 =====
    printf("[同名 fun 覆盖语义 — 后注册者胜出, 无分发]\n");
    {
        g_lastCall = 0;
        luaL_dostring(L, "ov.dup(123)");          // 若分发存在, 应命中 int 版
        int c1 = g_lastCall;
        printf("    (信息) ov.dup(123) 命中分支=%d (1=int,2=string)\n", c1);
        // 既定行为: 后注册的 string 版覆盖, 无论传什么都命中 string 版
        check(c1 == 2, "ov.dup(123) 命中后注册的 string 版 (覆盖, 非分发)");
    }
    {
        g_lastCall = 0;
        luaL_dostring(L, "ov.dup('hello')");
        int c2 = g_lastCall;
        printf("    (信息) ov.dup('hello') 命中分支=%d\n", c2);
        check(c2 == 2, "ov.dup('hello') 同样命中 string 版");
    }
    {
        // 传 string 给已被覆盖的 int 版: 不应崩溃 (既定: 后者接管, string 版接受 string)
        check(!raises(L, "ov.dup('x')"), "ov.dup('x') 不报错 (string 版接管)");
    }

    // ===== 2. lua_CFunction 原生注册 (模块侧) =====
    printf("[lua_CFunction 原生注册 — 模块侧]\n");
    {
        luaL_dostring(L, "g_nat = ov.nat(21)");
        lua_getglobal(L, "g_nat");
        bool ok = lua_isinteger(L, -1) && lua_tointeger(L, -1) == 42;
        lua_pop(L, 1);
        check(ok, "ov.nat(21)==42 (原生 C 函数模块侧注册)");
    }

    // ===== 2b. lua_CFunction 原生注册 (类侧) =====
    printf("[lua_CFunction 原生注册 — 类侧]\n");
    {
        struct Tag { int x; Tag() : x(0) {} int get() const { return x; } };
        LuaClass<Tag> cls(L, "Tag");
        cls.ctor();
        cls.fun("raw", nativeCFun);   // 类侧注册原生 C 函数
        // 注: 类侧原生 C 函数不自动传 self, 用户需自行处理
        luaL_dostring(L, "g_tag = Tag.new():raw(50)");
        lua_getglobal(L, "g_tag");
        lua_Integer tv = lua_tointeger(L, -1);
        bool ok = lua_isinteger(L, -1) && tv == 100;
        lua_pop(L, 1);
        printf("    (信息) Tag.new():raw(50) = %lld\n", (long long)tv);
        check(ok, "Tag.new():raw(50)==100 (原生 C 函数类侧注册, self@idx1 实参@idx2)");
    }

    // ===== 3. lua_State* 参数 (注入当前 L, 不消耗栈位 — B8 修复后) =====
    // LuaStack<lua_State*>::get 忽略 idx 返回当前 L; make_arg_indices 让 lua_State*
    // 参数不推进栈索引, 故后续真实参数拿连续栈位置 (符合 GUIDE 的 consumes no argument)。
    printf("[lua_State* 参数 (注入当前 L, 不消耗栈位)]\n");
    {
        luaL_dostring(L, "g_d = ov.depth()");
        lua_getglobal(L, "g_d");
        // depth(lua_State* L): 单参, 注入当前 L, 调用前栈顶应为 0
        lua_Integer d = lua_tointeger(L, -1); lua_pop(L, 1);
        printf("    (信息) ov.depth() 返回栈顶=%lld (lua_State* 注入 L, 无需传值)\n", (long long)d);
        check(d == 0, "lua_State* 单参函数注入当前 L, 无需传值 (B8 修复: 不消耗栈位)");
    }

    // ===== 4. tuple 返回: 单个 table, 非多返回值 =====
    printf("[tuple 返回 — 实证: 单个 Lua table, 非多返回值]\n");
    {
        luaL_dostring(L,
            "local a, b = ov.rtuple()\n"   // 若多返回值, a=42,b='hi'
            "g_r1 = a\n"
            "g_r2 = b\n");
        lua_getglobal(L, "g_r1");
        bool r1_is_table = lua_istable(L, -1);
        lua_pop(L, 1);
        lua_getglobal(L, "g_r2");
        bool r2_is_nil = lua_isnil(L, -1);
        lua_pop(L, 1);
        printf("    (信息) ov.rtuple() 第一接收者 is_table=%d, 第二接收者 is_nil=%d\n",
               (int)r1_is_table, (int)r2_is_nil);
        // 既定语义: tuple 打包成单个 Lua table (LuaStackReturn 恒返回 1)
        check(r1_is_table && r2_is_nil, "tuple 返回单个 table (非多返回值, 第二接收者 nil)");
    }
    {
        // 验证 table 内容: r[1]=42, r[2]='hi' (C++14 快速路径 / C++11 回退)
        luaL_dostring(L,
            "local r = ov.rtuple()\n"
            "g_t1 = r[1]\n"
            "g_t2 = r[2]\n");
        lua_getglobal(L, "g_t1");
        bool t1_ok = lua_isinteger(L, -1) && lua_tointeger(L, -1) == 42; lua_pop(L, 1);
        lua_getglobal(L, "g_t2");
        bool t2_ok = lua_isstring(L, -1);
        std::string s2 = t2_ok ? lua_tostring(L, -1) : ""; lua_pop(L, 1);
        printf("    (信息) tuple table: r[1]=%lld r[2]='%s'\n",
               (long long)(t1_ok ? 42 : -1), s2.c_str());
        check(t1_ok && t2_ok && s2 == "hi", "tuple table 内容 r[1]=42 r[2]='hi'");
    }

    lua_close(L);
    printf("\n=== overload: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

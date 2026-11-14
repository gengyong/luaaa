// 函数指针回调测试 — 实证 H2 已知限制 (luaaa.hpp:655-736 IMPLEMENT_CALLBACK_INVOKER).
//
// 已知限制 (luaaa.hpp:650-654 注释):
//   函数指针回调用 per-signature static 槽 (cacheLuaState/cacheLuaFuncId),
//   同签名多回调互相踩踏, 不可重入. 建议改用 std::function.
//
// 本测试实证该限制, 并验证 H1 部分修复 (单回调可多次调用).
#include "luaaa.hpp"

#include <cstdio>
#include <functional>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

// C++ 函数接收函数指针回调
static int call_cb_ptr(int(*cb)(int), int x) {
    return cb(x);
}

// 同签名的「调用两个回调」— 实证 H2 踩踏
static int call_two_cbs(int(*cb1)(int), int(*cb2)(int), int x) {
    int r1 = cb1(x);
    int r2 = cb2(x);
    return r1 * 100 + r2;  // 区分两个结果
}

// 对照组: std::function 回调
static int call_cb_fn(std::function<int(int)> cb, int x) {
    return cb(x);
}

static int call_two_fns(std::function<int(int)> cb1, std::function<int(int)> cb2, int x) {
    int r1 = cb1(x);
    int r2 = cb2(x);
    return r1 * 100 + r2;
}

int main() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    LuaModule m(L, "cb");
    m.fun("call", call_cb_ptr);
    m.fun("calltwo", call_two_cbs);
    m.fun("callfn", call_cb_fn);
    m.fun("calltwofn", call_two_fns);

    // ===== H1 修复验证: 单回调多次调用 =====
    printf("[函数指针回调 — H1 修复: 多次调用]\n");
    check(luaL_dostring(L, "local f = function(x) return x+1 end; "
                           "assert(cb.call(f, 10) == 11)") == 0,
          "函数指针回调首次调用");
    check(luaL_dostring(L, "local f = function(x) return x+1 end; "
                           "assert(cb.call(f, 20) == 21)") == 0,
          "函数指针回调二次调用 (H1 已修复)");

    // ===== H2 限制: 同签名两回调并存 — 踩踏 =====
    printf("[函数指针回调 — H2 限制: 同签名多回调踩踏]\n");
    {
        // f1 = x*10, f2 = x*100. 期望结果 10*5*100 + 100*5 = 5500 (正确)
        // H2 限制: cb1/cb2 共用 static 槽, 第二个 luaL_ref 覆盖第一个 -> cb1 实际调用 f2
        // 结果可能是 100*5*100 + 100*5 = 50500 (错误) 或其它
        luaL_dostring(L, "g_ptr_two = cb.calltwo(function(x) return x*10 end, "
                         "function(x) return x*100 end, 5) or 0");
        lua_getglobal(L, "g_ptr_two");
        lua_Integer got = lua_tointeger(L, -1); lua_pop(L, 1);
        printf("    (信息) calltwo(f1,f2,5) = %lld (正确值应为 5500)\n", (long long)got);
        // 文档化 H2: 当前实现下两回调踩踏, 结果 != 5500
        check(got != 5500, "KNOWN-LIMITATION: 函数指针回调同签名只有一个槽位, 两个同签名回调互相踩踏");
    }

    // ===== 对照: std::function 同签名两回调并存正常 =====
    printf("[std::function 回调 — 对照: 同签名多回调正常]\n");
    {
        luaL_dostring(L, "g_fn_two = cb.calltwofn(function(x) return x*10 end, "
                         "function(x) return x*100 end, 5) or 0");
        lua_getglobal(L, "g_fn_two");
        lua_Integer got = lua_tointeger(L, -1); lua_pop(L, 1);
        printf("    (信息) calltwofn(f1,f2,5) = %lld (正确值应为 5500)\n", (long long)got);
        check(got == 5500, "std::function 同签名多回调并存正常 (对照)");
    }

    // ===== 函数指针回调连调栈平衡 (H1 修复的栈方面) =====
    printf("[函数指针回调连调栈平衡]\n");
    {
        bool ok = (luaL_dostring(L,
            "local f = function(x) return x end; "
            "for i = 1, 50 do if cb.call(f, i) ~= i then error('mismatch at '..i) end end"
        ) == 0);
        check(ok, "连调 50 次栈平衡且结果正确");
    }

    lua_close(L);
    printf("\n=== callbacks_ptr: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

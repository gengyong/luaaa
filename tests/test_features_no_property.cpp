// 子测试: LUAAA_FEATURE_PROPERTY=0 时的行为.
// 2.0 之前: 关闭 property 后用户的 fun("__index", f) 会顶掉整个方法查找, 连方法也取不到。
// 2.0 起: 方法存在独立的方法表里, 用户的 __index 只在方法找不到时兜底, 与开启
//   property 时的查找顺序一致 (方法 -> 用户 __index)。
#define LUAAA_FEATURE_PROPERTY 0
#include "luaaa.hpp"
#include <cstdio>
#include <string>

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

static int g_idx_calls = 0;
static int customIndex(lua_State* L) {
    ++g_idx_calls;
    lua_pushinteger(L, 42);
    return 1;
}

class P2 {
public:
    int v;
    P2() : v(0) {}
    int method(int x) { return x; }
};

int main() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    {
        luaaa::LuaClass<P2> cls(L, "P2");
        cls.ctor();
        cls.fun("method", &P2::method);
        cls.fun("__index", customIndex);  // 用户 __index
    }
    printf("[PROPERTY=0 + fun('__index') 交互]\n");
    {
        // 在 PROPERTY=0 下: luaL_setfuncs 注册的 __index 函数覆盖了
        // luaaa.hpp:1494 的 "__index = metatable 自身". 于是 metatable.__index = customIndex.
        // 访问任何属性都会调用 customIndex 返回 42.
        bool method_ok = (luaL_dostring(L, "g_r = P2.new().method") == 0);
        if (!method_ok) { lua_pop(L, 1); }
        lua_getglobal(L, "g_r");
        bool is_42 = lua_isnumber(L, -1) && lua_tointeger(L, -1) == 42;
        lua_pop(L, 1);
        printf("    (信息) P2.new().method -> customIndex_calls=%d is_42=%d\n",
               g_idx_calls, (int)is_42);
        check(!is_42, "PROPERTY=0 + fun('__index'): 方法仍能取到 (不再被用户 __index 顶掉)");
        check(luaL_dostring(L, "local o = P2.new(); assert(o:method(7) == 7); assert(o.nosuch == 42)") == 0,
              "PROPERTY=0 + fun('__index'): 方法可调用, 不存在的键才走用户 __index");
    }

    printf("[PROPERTY=0 无 __index — 方法应正常]\n");
    {
        lua_State* L2 = luaL_newstate(); luaL_openlibs(L2);
        {
            luaaa::LuaClass<P2> cls(L2, "P2b");
            cls.ctor();
            cls.fun("method", &P2::method);
            // 不注册 __index
        }
        bool ok = (luaL_dostring(L2, "local w=P2b.new(); assert(w:method(5)==5)") == 0);
        check(ok, "PROPERTY=0 且无 __index 时方法访问正常");
        if (!ok) { lua_pop(L2, 1); }
        lua_close(L2);
    }

    lua_close(L);
    printf("\n=== features_no_property: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

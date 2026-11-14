// property 与元方法语义测试。
//
// 重点排查:
//   【高危 已确认】类 __index 返回值被丢弃 (luaaa.hpp:1388 nresults=0)
//                 —— 对照模块版本 (luaaa.hpp:2388 正确 nresults=1)
//   【中危】 __index/__newindex 契约: 非字符串 key (table) 报错 (luaaa.hpp:1355)
//   【中危】 类与模块 property 行为对称性
//   栈深度平衡
#include "luaaa.hpp"

#include <cstdio>
#include <string>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

// 探测: 执行 Lua 代码, 返回是否抛错
static bool raises(lua_State* L, const char* code) {
    char buf[1024];
    snprintf(buf, sizeof(buf),
             "g_r = pcall(function() %s end)", code);
    if (luaL_dostring(L, buf) != 0) { lua_pop(L, 1); return true; }
    lua_getglobal(L, "g_r");
    bool ok = lua_isboolean(L, -1) && lua_toboolean(L, -1);
    lua_pop(L, 1);
    return !ok;
}

// ====== 类 A: 测 __index 返回值丢弃 bug (luaaa.hpp:1388) ======
// __index 注册为成员函数返回 int. 访问不存在的属性应得到该 int.
class IdxBug {
public:
    int v;
    IdxBug() : v(0) {}
    int method(int x) { return x * 2; }
    int customIndex(lua_State* L) const {
        // 作为 __index 调用: 应返回 999 给 Lua
        (void)L;
        return 999;
    }
};

// ====== 类 B: 干净的 property 测试 (无 __index 干扰) ======
class Widget {
public:
    int v;
    Widget() : v(0) {}
    int getV() const { return v; }
    void setV(int x) { v = x; }
    int method(int x) { return x * 2; }
};

// 模块级 __index (走 fun, 存 "!__index", luaaa.hpp:2388 正确)
static int g_mod_idx_calls = 0;
static int modCustomIndex() { ++g_mod_idx_calls; return 777; }

int main() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);

    // 绑定 IdxBug (含 __index)
    {
        LuaClass<IdxBug> cls(L, "IdxBug");
        cls.ctor();
        cls.fun("method", &IdxBug::method);
        cls.fun("__index", &IdxBug::customIndex);
    }
    // 绑定 Widget (干净 property)
    {
        LuaClass<Widget> cls(L, "Widget");
        cls.ctor();
        cls.fun("method", &Widget::method);
        cls.get("ro", &Widget::getV);
        cls.set("wo", &Widget::setV);
        cls.get("rw", &Widget::getV);
        cls.set("rw", &Widget::setV);
    }
    // 模块 M (模块级 __index 对照)
    {
        LuaModule m(L, "M");
        m.fun("__index", modCustomIndex);
    }

    // ===== 【高危】类 __index 返回值被丢弃 (luaaa.hpp:1388 nresults=0) =====
    // customIndex 返回 999; 访问任意不存在属性应得到 999.
    // 当前: f_internal_index 用 lua_pcall(...,0,0) 调用 !__index -> 返回值被丢 -> nil
    printf("[类 __index 返回值丢弃 — 已确认 bug luaaa.hpp:1388]\n");
    {
        luaL_dostring(L, "g_idx = IdxBug.new().nonexistent");
        lua_getglobal(L, "g_idx");
        bool is_num = lua_isnumber(L, -1);
        lua_Integer val = lua_tointeger(L, -1); lua_pop(L, 1);
        printf("    (信息) IdxBug.new().nonexistent -> is_number=%d val=%lld\n",
               is_num, (long long)val);
        check(is_num && val == 999, "类 __index 返回值应能取到 (当前 nresults=0 bug 致 nil)");
    }

    // ===== 对照: 模块 __index (luaaa.hpp:2388 nresults=1, 正确) =====
    // 注: 2.0 起模块表统一显式建表, 5.1/LuaJIT 与 5.2+ 行为一致 (此前 5.1 路径不触发模块 __index)。
    printf("[模块 __index 返回值 — 对照组 luaaa.hpp:2388]\n");
    {
        luaL_dostring(L, "g_midx = M.nonexistent");
        lua_getglobal(L, "g_midx");
        bool is_num = lua_isnumber(L, -1);
        lua_Integer val = lua_tointeger(L, -1); lua_pop(L, 1);
        printf("    (信息) M.nonexistent -> is_number=%d val=%lld (calls=%d)\n",
               is_num, (long long)val, g_mod_idx_calls);
        check(is_num && val == 777, "模块 __index 返回值能取到 (所有 Lua 版本)");
    }

    // ===== __index 契约: 非字符串 key (table) 应返回 nil 而非报错 (M3 修复) =====
    printf("[__index 非字符串 key (M3 修复: table key 应返回 nil)]\n");
    {
        // w[{}] — table key. 修复后: 返回 nil (符合 Lua __index 契约), 不再抛错.
        bool r_table = raises(L, "local _ = Widget.new()[{}]");
        printf("    (信息) Widget.new()[{}] raises=%d (期望 false: 返回 nil)\n", (int)r_table);
        check(!r_table, "Widget[{}] table key 应返回 nil 而非报错 (M3 修复)");
    }

    // ===== 类 property 对称性 (干净 Widget) =====
    printf("[类 property 读写对称性]\n");
    check(!raises(L, "local w=Widget.new(); w.rw=42; assert(w.rw==42)"), "类 rw 读写一致");
    check(!raises(L, "local w=Widget.new(); assert(w.ro==0)"), "类 ro 只读读出");
    check(raises(L, "local _ = Widget.new().wo"), "类 读只写属性应报错");
    check(raises(L, "local w=Widget.new(); w.ro=1"), "类 写只读属性应报错");
    check(!raises(L, "local w=Widget.new(); w.wo=5"), "类 写只写属性成功");

    // ===== 方法访问 =====
    printf("[方法访问]\n");
    check(!raises(L, "local w=Widget.new(); assert(w:method(21)==42)"), "类方法访问正常");

    // ===== 写不存在属性 (Widget 无 __index, 走 rawset 扩展或 Non-existing 报错) =====
    printf("[类写不存在属性 — 行为观测]\n");
    {
        bool r = raises(L, "Widget.new().unknown = 1");
        printf("    (信息) Widget.unknown=1 raises=%d (luaaa 对纯 userdata 报 Non-existing)\n", (int)r);
        // 当前行为: 报 "Non-existing property" 错误. 合理.
        check(r, "类写不存在属性: luaaa 报 Non-existing (合理行为)");
    }

    lua_close(L);
    printf("\n=== property: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

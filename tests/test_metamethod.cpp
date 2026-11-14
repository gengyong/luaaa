// 元方法 (metamethod) / 运算符重载测试。
//
// 覆盖未测分支:
//   - 类侧 fun("__add"/"__eq"/"__lt"/"__le"/"__len"/"__concat"/"__tostring"/"__call")
//     经 _registerClassFunction 以字面名注册进 metatable, Lua 运算符应生效。
//   - 模块侧 fun("__add") 不进 metatable (进模块表), 运算符应【不】生效 (实证既定限制)。
//
// 设计原则: 断言 Lua 运算符语法能触发绑定的 C++ 方法; FAIL 即 bug。
#include "luaaa.hpp"

#include <cstdio>
#include <string>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

// 探测 expr (单个表达式) 是否抛错
static bool raises(lua_State* L, const char* expr) {
    char buf[512];
    snprintf(buf, sizeof(buf), "g_ok = pcall(function() return %s end)", expr);
    if (luaL_dostring(L, buf) != 0) { lua_pop(L, 1); return true; }
    lua_getglobal(L, "g_ok");
    bool ok = lua_isboolean(L, -1) && lua_toboolean(L, -1);
    lua_pop(L, 1);
    return !ok;
}

// 探测一段 Lua 语句块 stmts 是否抛错 (支持 local/多语句)
static bool raisesStmt(lua_State* L, const char* stmts) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "g_ok = pcall(function() %s end)", stmts);
    if (luaL_dostring(L, buf) != 0) { lua_pop(L, 1); return true; }
    lua_getglobal(L, "g_ok");
    bool ok = lua_isboolean(L, -1) && lua_toboolean(L, -1);
    lua_pop(L, 1);
    return !ok;
}

// 取全局 number
static lua_Number get_num(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    lua_Number v = lua_tonumber(L, -1); lua_pop(L, 1);
    return v;
}
static std::string get_str(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    const char* s = lua_tostring(L, -1);
    std::string r = s ? s : ""; lua_pop(L, 1);
    return r;
}

// ====== Vec: 二维向量, 测算术与比较元方法 ======
class Vec {
public:
    int x, y;
    Vec() : x(0), y(0) {}
    Vec add(const Vec& o) const { Vec r; r.x = x + o.x; r.y = y + o.y; return r; }
    // __add 返回 Vec: Lua 侧无法直接构造 Vec, 故用 int 简化断言
    int addInt(const Vec& o) const { return x + o.x; }
    int sumComponents() const { return x + y; }
    bool equalTo(const Vec& o) const { return x == o.x && y == o.y; }
    bool lessThan(const Vec& o) const { return (x + y) < (o.x + o.y); }
    bool lessEqual(const Vec& o) const { return (x + y) <= (o.x + o.y); }
    std::string describe() const { return "(" + std::to_string(x) + "," + std::to_string(y) + ")"; }
};

// 带构造参数的 Vec (用于 __len/__call/__concat)
class Box {
public:
    int n;
    Box() : n(0) {}
    int lenSelf() const { return n; }
    std::string concatWith(const std::string& s) const { return std::to_string(n) + s; }
    int callSelf(int v) const { return v * 10; }
    std::string describe() const { return "Box(" + std::to_string(n) + ")"; }
};

// 模块级 __add (实证: 不进 metatable)
static int modAdd(int a) { return a + 100; }

int main() {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    // 绑定 Vec (算术/比较元方法)
    {
        LuaClass<Vec> cls(L, "Vec");
        cls.ctor();
        cls.fun("__add", &Vec::addInt);        // v1 + v2 -> int(x 之和)
        cls.fun("__eq", &Vec::equalTo);
        cls.fun("__lt", &Vec::lessThan);
        cls.fun("__le", &Vec::lessEqual);
        cls.fun("sum", &Vec::sumComponents);    // 普通方法对照
    }
    // 绑定 Box (__len/__call/__concat/__tostring)
    {
        LuaClass<Box> cls(L, "Box");
        cls.ctor();
        cls.fun("__len", &Box::lenSelf);
        cls.fun("__call", &Box::callSelf);
        cls.fun("__concat", &Box::concatWith);
        cls.fun("__tostring", &Box::describe);
    }
    // 绑定模块 MM (模块级 __add, 实证不生效)
    {
        LuaModule m(L, "MM");
        m.fun("__add", modAdd);
    }

    // ===== __add (类侧) =====
    printf("[__add 类侧 — v1 + v2 运算符触发]\n");
    {
        bool r = raisesStmt(L, "local a=Vec.new(); local b=Vec.new(); g_add = a + b");
        printf("    (信息) Vec + Vec raises=%d\n", (int)r);
        check(!r, "Vec + Vec 不抛错 (运算符触发绑定的 __add)");
        lua_Number v = get_num(L, "g_add");
        printf("    (信息) Vec(0,0)+Vec(0,0).__add = %g (期望 0)\n", (double)v);
        check(v == 0, "__add 返回值正确 (0+0=0)");
    }

    // ===== __eq (类侧) =====
    printf("[__eq 类侧 — v1 == v2]\n");
    {
        bool r = raisesStmt(L, "local a=Vec.new(); local b=Vec.new(); g_eq = (a == b)");
        printf("    (信息) Vec == Vec raises=%d\n", (int)r);
        check(!r, "Vec == Vec 不抛错");
        lua_getglobal(L, "g_eq");
        bool eq = lua_toboolean(L, -1); lua_pop(L, 1);
        printf("    (信息) Vec(0,0)==Vec(0,0) = %d (期望 true)\n", (int)eq);
        check(eq, "__eq 相等判定为 true");
    }

    // ===== __lt / __le (类侧) =====
    printf("[__lt / __le 类侧 — 比较运算符]\n");
    {
        bool r = raisesStmt(L, "local a=Vec.new(); local b=Vec.new(); g_lt = (a < b)");
        printf("    (信息) Vec < Vec raises=%d\n", (int)r);
        check(!r, "Vec < Vec 不抛错");
        lua_getglobal(L, "g_lt");
        bool lt = lua_toboolean(L, -1); lua_pop(L, 1);
        printf("    (信息) Vec(0,0) < Vec(0,0) = %d (期望 false, 和相等)\n", (int)lt);
        check(!lt, "__lt 相等时为 false");
    }
    {
        bool r = raisesStmt(L, "local a=Vec.new(); local b=Vec.new(); g_le = (a <= b)");
        check(!r, "Vec <= Vec 不抛错");
        lua_getglobal(L, "g_le");
        bool le = lua_toboolean(L, -1); lua_pop(L, 1);
        check(le, "__le 相等时为 true");
    }

    // ===== __len (类侧) =====
    printf("[__len 类侧 — #obj]\n");
    {
        bool r = raisesStmt(L, "g_len = #Box.new()");
        printf("    (信息) #Box.new() raises=%d\n", (int)r);
        check(!r, "#Box.new() 不抛错");
        lua_Number v = get_num(L, "g_len");
        printf("    (信息) #Box.new() = %g (期望 0)\n", (double)v);
        check(v == 0, "__len 返回 n (默认 0)");
    }

    // ===== __call (类侧) =====
    printf("[__call 类侧 — obj(args)]\n");
    {
        bool r = raisesStmt(L, "g_call = Box.new()(5)");
        printf("    (信息) Box.new()(5) raises=%d\n", (int)r);
        check(!r, "Box.new()(5) 不抛错");
        lua_Number v = get_num(L, "g_call");
        printf("    (信息) Box.new()(5) = %g (期望 50)\n", (double)v);
        check(v == 50, "__call 传入 5 返回 50");
    }

    // ===== __concat (类侧) =====
    printf("[__concat 类侧 — obj .. str]\n");
    {
        bool r = raisesStmt(L, "g_cat = Box.new() .. '!'");
        printf("    (信息) Box.new()..'!' raises=%d\n", (int)r);
        check(!r, "Box.new()..'!' 不抛错");
        std::string s = get_str(L, "g_cat");
        printf("    (信息) Box.new()..'!' = '%s' (期望 '0!')\n", s.c_str());
        check(s == "0!", "__concat 拼接结果 '0!'");
    }

    // ===== __tostring (类侧) =====
    printf("[__tostring 类侧 — tostring(obj)]\n");
    {
        bool r = raisesStmt(L, "g_ts = tostring(Box.new())");
        printf("    (信息) tostring(Box.new()) raises=%d\n", (int)r);
        check(!r, "tostring(Box.new()) 不抛错");
        std::string s = get_str(L, "g_ts");
        printf("    (信息) tostring(Box.new()) = '%s'\n", s.c_str());
        check(!s.empty(), "__tostring 返回非空字符串");
    }

    // ===== 模块级 __add (实证: 不进 metatable, 运算符不生效) =====
    printf("[模块级 __add — 实证不生效 (仅可 M:__add 调用)]\n");
    {
        bool r = raises(L, "g_madd = MM + 5");
        printf("    (信息) MM + 5 raises=%d (期望 true: 模块运算符不触发)\n", (int)r);
        // 模块 __add 存于模块表而非 metatable, Lua 不会用它做 + 运算 -> 报错
        check(r, "KNOWN-LIMITATION: MM + 5 报错 (模块的 __add 存在模块表里, 不进 metatable, 运算符不生效)");
    }
    {
        // 但作为普通字段可调用 (实证: 仅函数调用形式可用)
        bool r = raises(L, "assert(MM.__add(5) == 105)");
        printf("    (信息) MM.__add(5) raises=%d\n", (int)r);
        check(!r, "MM.__add(5)==105 可作为普通函数调用 (存于模块表)");
    }

    lua_close(L);
    printf("\n=== metamethod: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

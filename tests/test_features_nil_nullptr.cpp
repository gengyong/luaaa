// 子测试: LUAAA_NIL_AS_NULLPTR=1 (R9-3)。
// 开启后 nil / 缺省实参传给 T* 参数得到 nullptr, C++ 返回的 nullptr (nil) 可以原样传回。
// 默认关闭时的行为 (报错) 见 test_cast / test_review9 / test_review10。
#define LUAAA_NIL_AS_NULLPTR 1
#include "luaaa.hpp"
#include <cstdio>
#include <string>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}
static bool lua_ok(lua_State* L, const char* code) {
    if (luaL_dostring(L, code) != 0) { printf("    (lua error) %s\n", lua_tostring(L, -1)); lua_pop(L, 1); return false; }
    return true;
}

struct Cat { int v; explicit Cat(int x) : v(x) {} int get() const { return v; } };
static Cat* g_cat = nullptr;
static Cat* current() { return g_cat; }
static bool isNull(Cat* c) { return c == nullptr; }
static int valueOr(const Cat* c, int d) { return c ? c->v : d; }
static int byRef(const Cat& c) { return c.v; }
static std::string pick(Cat* c) { return c ? "cat" : "null"; }
static std::string pickInt(int) { return "int"; }

int main() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    LuaClass<Cat>(L, "Cat").ctor<int>().fun("get", &Cat::get);
    LuaModule(L, "m").fun("current", current).fun("isNull", isNull).fun("valueOr", valueOr).fun("byRef", byRef)
        .overload("pick", pickInt).overload("pick", pick);

    printf("[LUAAA_NIL_AS_NULLPTR=1]\n");
    check(lua_ok(L, "assert(m.isNull(nil) == true)"), "nil 传给 T* -> nullptr");
    check(lua_ok(L, "assert(m.isNull() == true)"), "缺省实参传给 T* -> nullptr");
    check(lua_ok(L, "assert(m.isNull(Cat.new(1)) == false and m.valueOr(Cat.new(4), 0) == 4)"), "对象照常传入");
    check(lua_ok(L, "assert(m.current() == nil and m.isNull(m.current()) and m.valueOr(m.current(), 9) == 9)"), "C++ 返回的 nullptr (nil) 可以原样传回");
    check(lua_ok(L, "assert(not pcall(m.isNull, 5) and not pcall(m.isNull, 'x') and not pcall(m.isNull, {}))"), "其他类型的值仍然报错");
    check(lua_ok(L, "assert(not pcall(m.byRef, nil))"), "T& / const T& 参数不受影响: nil 仍报错");
    check(lua_ok(L, "assert(m.pick(1) == 'int' and m.pick(nil) == 'null' and m.pick(Cat.new(1)) == 'cat')"), "重载分发: nil 能匹配 T* 参数");
    lua_close(L);
    printf("\n=== features_nil_nullptr: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

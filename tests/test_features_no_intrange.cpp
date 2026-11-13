// LUAAA_CHECK_INTEGER_RANGE=0: 关闭整数入参范围检查, 恢复第八轮评审前的行为 ——
// 超出目标整数类型范围的 lua 数值按 static_cast 静默回绕 (原 test_numeric_edge2 B7 的记录)。
#define LUAAA_CHECK_INTEGER_RANGE 0
#include "luaaa.hpp"

#include <cstdio>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

static short echo_short(short v) { return v; }
static unsigned char echo_u8(unsigned char v) { return v; }

int main() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    LuaModule(L, "n").fun("s", echo_short).fun("u8", echo_u8);

    printf("[LUAAA_CHECK_INTEGER_RANGE=0: 窄化静默回绕]\n");
    const short expected = static_cast<short>(99999);
    lua_pushinteger(L, expected); lua_setglobal(L, "expected");
    check(luaL_dostring(L, "assert(n.s(99999) == expected)") == 0, "echo_short(99999) 静默回绕, 不报错");
    check(luaL_dostring(L, "assert(n.u8(300) == 44)") == 0, "echo_u8(300) 静默回绕为 44");

    lua_close(L);
    printf("\n=== features_no_intrange: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

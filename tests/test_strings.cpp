// 字符串类型边界测试：const char* / char* / std::string 在各种输入下的行为。
//
// 覆盖未测分支:
//   - const char* 单次调用内有效 (L3 文档化行为正向断言)
//   - std::string 嵌入 NUL — 双向保留 (R9-9 修复); const char* 仍在 NUL 处截断 (KNOWN-LIMITATION)
//   - Lua 二进制安全字符串 -> const char* 丢失 NUL 后内容
//   - 空串 "" 往返
//   - UTF-8 多字节 ("中文") 往返字节一致
//   - char* 特化 echo 往返
//   - const char* 对 number/bool 的 tostring 强转 (L2 既定语义回归)
//
// 设计原则: 断言「正确/合理」行为; 把错误行为写成预期的断言一律以 KNOWN-LIMITATION 标出。
#include "luaaa.hpp"

// Lua 5.1/LuaJIT 没有 lua_rawlen (5.2+ API); 用 lua_objlen 提供 polyfill,
// 与 test_functional.cpp 顶部 polyfill 风格一致。
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM <= 501
inline int lua_rawlen(lua_State * L, int idx) {
    return (int)lua_objlen(L, idx);
}
#endif

#include <cstdio>
#include <cstring>
#include <string>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

static bool raises(lua_State* L, const char* code) {
    char buf[512];
    snprintf(buf, sizeof(buf), "g_ok = pcall(function() %s end)", code);
    if (luaL_dostring(L, buf) != 0) { lua_pop(L, 1); return true; }
    lua_getglobal(L, "g_ok");
    bool ok = lua_isboolean(L, -1) && lua_toboolean(L, -1);
    lua_pop(L, 1);
    return !ok;
}

// ---- 被测函数 ----
static const char*        echo_cstr(const char* s)   { return s; }
static char*              echo_cstr2(char* s)        { return s; }
static std::string        echo_str(const std::string& s) { return s; }
static std::size_t        str_len(const std::string& s) { return s.size(); }

int main() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    LuaModule(L, "s")
        .fun("cstr", echo_cstr)
        .fun("cstr2", echo_cstr2)
        .fun("str", echo_str)
        .fun("slen", str_len);

    // ===== const char* 普通字符串往返 =====
    printf("[const char* 普通字符串往返]\n");
    check(!raises(L, "assert(s.cstr('hello') == 'hello')"), "const char* echo 'hello'");

    // ===== std::string 普通字符串往返 =====
    printf("[std::string 普通字符串往返]\n");
    check(!raises(L, "assert(s.str('world') == 'world')"), "std::string echo 'world'");

    // ===== char* 特化往返 =====
    printf("[char* 特化往返]\n");
    check(!raises(L, "assert(s.cstr2('data') == 'data')"), "char* echo 'data'");

    // ===== 空串往返 =====
    printf("[空串 '' 往返]\n");
    check(!raises(L, "assert(s.cstr('') == '' and s.str('') == '')"), "空串 const char*/std::string 往返");

    // ===== UTF-8 多字节往返 (字节一致) =====
    printf("[UTF-8 多字节往返]\n");
    check(!raises(L, "assert(s.cstr('中文') == '中文')"), "const char* UTF-8 '中文' 往返");
    check(!raises(L, "assert(s.str('日本語') == '日本語')"), "std::string UTF-8 往返");

    // ===== std::string 嵌入 NUL: 双向保留 (R9-9) =====
    // put 用 lua_pushlstring, get 用 lua_tolstring, 长度随字符串一起走。
    printf("[std::string 嵌入 NUL — 双向保留]\n");
    {
        // 构造含嵌入 NUL 的 std::string (size=3, 内容 'a' \0 'b')
        std::string embedded("a\0b", 3);
        printf("    (信息) C++ 侧 std::string size=%zu (期望3, 含嵌入NUL)\n", embedded.size());
        // 手动 put 到 Lua, 观察 Lua 侧 #s
        LuaStack<std::string>::put(L, embedded);
        lua_pushvalue(L, -1);  // 复制供 slen 取长度
        lua_setglobal(L, "g_emb");
        // Lua 侧 #s 在 NUL 后停止 (lua_pushstring 以 C 串语义)
        lua_getglobal(L, "g_emb");
        lua_Integer luallen = lua_rawlen(L, -1); lua_pop(L, 1);
        lua_pop(L, 1);  // 清理之前的 put
        printf("    (信息) Lua 侧 #string = %lld (期望3)\n", (long long)luallen);
        check(luallen == 3, "C++ std::string(\"a\\0b\", 3) 到 Lua 后长度仍为 3");
        check(!raises(L, "local s3 = 'a' .. string.char(0) .. 'b'; local r = s.str(s3); assert(#r == 3 and r == s3)"),
              "Lua 的 'a\\0b' 经 std::string 参数往返后长度仍为 3");
    }

    // ===== Lua 二进制安全字符串 -> const char* (lua_tostring 丢失长度信息) =====
    printf("[Lua 二进制安全串 -> const char* — 长度信息丢失]\n");
    {
        // Lua 字符串支持嵌入 NUL (二进制安全)
        luaL_dostring(L, "g_bin = 'a' .. string.char(0) .. 'b'");  // "a\0b", #g_bin=3
        lua_getglobal(L, "g_bin");
        lua_Integer binlen = lua_rawlen(L, -1); lua_pop(L, 1);
        printf("    (信息) Lua 侧 g_bin 长度=%lld (二进制安全, 期望3)\n", (long long)binlen);
        check(binlen == 3, "Lua 字符串二进制安全, 可含嵌入 NUL");
        // 但传给 const char* 参数后, C 侧无法获知 NUL 后内容 (无长度输出)
        luaL_dostring(L, "g_echo = s.cstr(g_bin)");
        lua_getglobal(L, "g_echo");
        lua_Integer echolen = lua_rawlen(L, -1); lua_pop(L, 1);
        printf("    (信息) echo_cstr(g_bin) 返回串长度=%lld (期望1, const char* 无长度信息)\n",
               (long long)echolen);
        // KNOWN-LIMITATION: const char* 是 C 字符串, 在第一个 NUL 处结束; 要保留请用 std::string
        check(echolen == 1, "KNOWN-LIMITATION: const char* 往返丢失 NUL 之后的内容");
    }

    // ===== const char* 对 number/bool 的 tostring 强转 (L2 回归) =====
    printf("[const char* tostring 强转 (L2 既定语义回归)]\n");
    check(!raises(L, "assert(s.cstr(123) == '123')"), "cstr(123)=='123' (number tostring)");
    check(!raises(L, "assert(s.cstr(true) == 'true')"), "cstr(true)=='true' (bool tostring)");
    check(!raises(L, "assert(s.cstr(false) == 'false')"), "cstr(false)=='false' (bool tostring)");

    // ===== const char* 对 nil 报错 (无法 tostring) =====
    printf("[const char* 对 nil 报错]\n");
    check(raises(L, "s.cstr(nil)"), "cstr(nil) 应报错 (nil 无法 tostring)");

    // ===== const char* 对 table 报错 =====
    printf("[const char* 对 table 报错]\n");
    check(raises(L, "s.cstr({})"), "cstr({}) 应报错 (table 无法 tostring)");

    lua_close(L);
    printf("\n=== strings: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

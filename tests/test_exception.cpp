// 非构造函数抛 C++ 异常的转换测试。
//
// 覆盖未测分支 + 发现新 bug:
//   ⭐【实证 bug — 高危】成员函数抛 C++ 异常致进程崩溃 (std::terminate / SIGABRT)
//      根因: LuaInvokeInstanceMemberImpl (luaaa.hpp:1090-1093) 调用成员函数时【无 try/catch】,
//      不同于非成员函数的 LuaInvokeImpl (luaaa.hpp:977 有 try/catch)。
//      H2 修复 (TranslateCppException) 只覆盖了构造函数路径 (PlacementConstructorCaller)
//      和非成员函数路径, 【漏掉了成员函数路径】。
//      影响: 任何绑定的成员方法 (const/非const) 抛异常都会杀死整个宿主进程, 非 lua_error。
//      复现: 本测试 [成员函数抛异常] 子进程退出码 -6 (SIGABRT)。
//
//   - 绑定自由函数 (非成员) 抛 std::logic_error -> 应正常翻译为 lua_error (LuaInvokeImpl 有保护)
//   - 绑定函数抛非 std::exception (throw 42) -> 成员路径崩溃, 自由函数路径捕获
//
// 验证方式: 异常转换不应 std::terminate 致进程崩溃。
//   所有有抛异常风险的调用都在 fork() 子进程中执行 (避免主进程崩溃中断测试),
//   父进程观测退出码 (正常=0, SIGABRT=134/-6 表示崩溃)。
#include "luaaa.hpp"
#include "child_process.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

// ====== 被测: 会抛异常的成员函数 ======
class Boom {
public:
    int throwRuntime(int x) const {
        if (x < 0) throw std::runtime_error("negative input");
        return x * 2;
    }
    int throwUnknown(int x) const {
        if (x < 0) throw 42;   // 非 std::exception
        return x;
    }
};

// ====== 被测: 会抛异常的自由函数 ======
static int freeThrowLogic(int x) {
    if (x == 0) throw std::logic_error("zero not allowed");
    return 100 / x;
}

// ====== fork 子进程执行场景, 返回退出码 (0=正常, -sig=被信号杀) ======

// 场景: 成员函数抛 std::runtime_error (无 pcall, 直接调用)
static int scenario_member_throw() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    { LuaClass<Boom> cls(L, "Boom"); cls.ctor(); cls.fun("calc", &Boom::throwRuntime); }
    // 抛异常的调用: 若未翻译为 lua_error, 会 std::terminate -> 退出码 134
    luaL_dostring(L, "Boom.new():calc(-1)");
    lua_close(L);
    return 0;
}

// 场景: 成员函数抛 std::runtime_error (有 pcall 包裹)
// 即使 pcall, C++ 异常仍会穿过边界先于 pcall 触发 terminate
static int scenario_member_pcall() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    { LuaClass<Boom> cls(L, "Boom"); cls.ctor(); cls.fun("calc", &Boom::throwRuntime); }
    luaL_dostring(L, "pcall(function() Boom.new():calc(-5) end)");
    lua_close(L);
    return 0;
}

// 场景: 自由函数抛 std::logic_error (LuaInvokeImpl 有 try/catch, 应不崩溃)
static int scenario_free_throw() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    LuaModule(L).fun("div100", freeThrowLogic);
    luaL_dostring(L, "div100(0)");
    lua_close(L);
    return 0;
}

// 场景: 抛非 std::exception (成员路径)
static int scenario_unknown_throw() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    { LuaClass<Boom> cls(L, "Boom"); cls.ctor(); cls.fun("unk", &Boom::throwUnknown); }
    luaL_dostring(L, "Boom.new():unk(-1)");
    lua_close(L);
    return 0;
}

int main() {
    // ===== 1. ⭐【实证 bug】成员函数抛 std::runtime_error -> 进程崩溃 (SIGABRT) =====
    // 根因: LuaInvokeInstanceMemberImpl (luaaa.hpp:1090) 无 try/catch, 异常穿过 extern "C" 边界
    printf("[成员函数抛 std::runtime_error — 实证 bug: 应翻译为 lua_error 但当前崩溃]\n");
    {
        int rc = run_in_child(scenario_member_throw);
        printf("    (信息) 子进程退出码=%d (期望 0; -6=SIGABRT 崩溃)\n", rc);
        // 期望不崩溃 (rc==0); 当前实现崩溃 (rc==-6) -> 此断言 FAIL = bug 实证
        check(rc == 0, "成员函数抛异常应翻译为 lua_error 不致崩溃 (当前: LuaInvokeInstanceMemberImpl:1090 缺 try/catch, bug)");
    }
    // 即使用 pcall 包裹, C++ 异常仍穿过边界先于 pcall terminate
    printf("[成员函数抛异常 (pcall 包裹) — 仍崩溃 (C++ 异常先于 pcall)]\n");
    {
        int rc = run_in_child(scenario_member_pcall);
        printf("    (信息) 子进程退出码=%d (期望 0)\n", rc);
        check(rc == 0, "pcall 包裹的成员函数异常应能捕获 (当前: 仍崩溃, 同 bug)");
    }
    // 正常路径回归 (不抛异常的调用应正常工作)
    printf("[成员函数正常路径回归 — calc(5)==10]\n");
    {
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        { LuaClass<Boom> cls(L, "Boom"); cls.ctor(); cls.fun("calc", &Boom::throwRuntime); }
        bool ok = luaL_dostring(L, "assert(Boom.new():calc(5) == 10)") == 0;
        if (!ok) printf("    lua-err: %s\n", lua_tostring(L, -1));
        check(ok, "成员函数正常路径 calc(5)==10");
        lua_close(L);
    }

    // ===== 2. 对照: 自由函数 (非成员) 抛 std::logic_error -> 不崩溃 (LuaInvokeImpl 有保护) =====
    printf("[自由函数抛 std::logic_error — 对照: 应翻译为 lua_error, 不崩溃]\n");
    {
        int rc = run_in_child(scenario_free_throw);
        printf("    (信息) 子进程退出码=%d (期望 0)\n", rc);
        check(rc == 0, "自由函数抛异常不致进程崩溃 (LuaInvokeImpl:977 有 try/catch, 对照组)");
    }
    // 自由函数异常可在主进程内验证 pcall 捕获 (安全, 不会崩溃)
    printf("[自由函数抛异常 — pcall 捕获 + 消息含 e.what()]\n");
    {
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        LuaModule(L).fun("div100", freeThrowLogic);
        luaL_dostring(L,
            "local ok, err = pcall(function() div100(0) end)\n"
            "g_err = err\n");
        lua_getglobal(L, "g_err");
        std::string err = lua_isstring(L, -1) ? lua_tostring(L, -1) : ""; lua_pop(L, 1);
        printf("    (信息) err='%s'\n", err.c_str());
        check(err.find("zero not allowed") != std::string::npos,
              "自由函数错误消息含 e.what() ('zero not allowed')");
        lua_close(L);
    }
    // 自由函数正常路径回归
    printf("[自由函数正常路径回归 — div100(4)==25]\n");
    {
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        LuaModule(L).fun("div100", freeThrowLogic);
        bool ok = luaL_dostring(L, "assert(div100(4) == 25)") == 0;
        if (!ok) printf("    lua-err: %s\n", lua_tostring(L, -1));
        check(ok, "自由函数正常路径 div100(4)==25");
        lua_close(L);
    }

    // ===== 3. 抛非 std::exception (成员路径) -> 同样崩溃 =====
    printf("[成员函数抛非 std::exception (throw 42) — 同 bug: 崩溃]\n");
    {
        int rc = run_in_child(scenario_unknown_throw);
        printf("    (信息) 子进程退出码=%d (期望 0; -6=SIGABRT)\n", rc);
        check(rc == 0, "成员函数抛非 std::exception 应翻译为 lua_error (当前: 同 bug 崩溃)");
    }

    printf("\n=== exception: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

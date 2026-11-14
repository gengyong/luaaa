// 属性 getter/setter 抛 C++ 异常的转换测试 (H5)。
//
// 覆盖未测分支 + 发现新 bug:
//   ⭐【实证 bug — 高危】属性 getter/setter 抛 C++ 异常致进程崩溃 (SIGABRT)
//      根因: getter/setter 的 Invoke lambda (luaaa.hpp:2262/2283/2305/... 类侧,
//      3118/3139/3160/3195 模块侧) 直接调函数指针【无 try/catch】。
//      H4 修复只覆盖了 LuaInvokeInstanceMemberImpl (普通成员函数), 漏掉了 property 路径。
//      抛异常的 getter (如校验失败、资源获取失败) 会穿过 extern "C" 边界 -> terminate。
//
//   - 类 getter 抛 std::runtime_error -> 应翻译为 lua_error (不崩溃)
//   - 类 setter 抛异常 -> 同上
//   - 模块 getter/setter 抛异常 -> 同上
//   - 对照: 正常 getter/setter 路径 (不抛异常) 仍工作
//
// 验证方式: 抛异常风险的调用在 fork() 子进程执行 (避免主进程崩溃),
//   父进程观测退出码 (0=正常, -6=SIGABRT 崩溃)。修复后主进程内验证 pcall 捕获。
#include "luaaa.hpp"
#include "child_process.h"

#include <cstdio>
#include <stdexcept>
#include <string>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}


// ====== 会抛异常的类 ======
class Prop {
public:
    int v;
    Prop() : v(0) {}
    int getThrow() const { throw std::runtime_error("getter boom"); }
    void setThrow(int x) { (void)x; throw std::runtime_error("setter boom"); }
    int getOk() const { return v; }
    void setOk(int x) { v = x; }
};

// ====== 模块级会抛异常的 getter/setter ======
static int modGetThrow() { throw std::runtime_error("mod getter boom"); }
static void modSetThrow(const std::string& s) { (void)s; throw std::runtime_error("mod setter boom"); }
static int g_modVal = 7;
static int modGetOk() { return g_modVal; }
static void modSetOk(int x) { g_modVal = x; }

// ====== 场景函数 ======
static int scenario_class_getter() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    { LuaClass<Prop> cls(L, "Prop"); cls.ctor(); cls.get("x", &Prop::getThrow); }
    luaL_dostring(L, "local p = Prop.new(); local _ = p.x");
    lua_close(L);
    return 0;
}
static int scenario_class_setter() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    { LuaClass<Prop> cls(L, "Prop"); cls.ctor(); cls.set("x", &Prop::setThrow); }
    luaL_dostring(L, "local p = Prop.new(); p.x = 1");
    lua_close(L);
    return 0;
}
static int scenario_mod_getter() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    { LuaModule m(L, "M"); m.get("x", modGetThrow); }
    luaL_dostring(L, "local _ = M.x");
    lua_close(L);
    return 0;
}
static int scenario_mod_setter() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    { LuaModule m(L, "M"); m.set("x", modSetThrow); }
    luaL_dostring(L, "M.x = 'test'");
    lua_close(L);
    return 0;
}

int main() {
    // ===== 1. 类 getter 抛异常 -> 不崩溃 (子进程退出码 0) =====
    printf("[类 getter 抛异常 — 应翻译为 lua_error, 不崩溃]\n");
    {
        int rc = run_in_child(scenario_class_getter);
        printf("    (信息) 子进程退出码=%d (期望 0; -6=SIGABRT 崩溃)\n", rc);
        check(rc == 0, "类 getter 抛异常不致崩溃 (property Invoke 缺 try/catch, H5)");
    }
    // ===== 2. 类 setter 抛异常 -> 不崩溃 =====
    printf("[类 setter 抛异常 — 应翻译为 lua_error, 不崩溃]\n");
    {
        int rc = run_in_child(scenario_class_setter);
        printf("    (信息) 子进程退出码=%d (期望 0)\n", rc);
        check(rc == 0, "类 setter 抛异常不致崩溃 (H5)");
    }
    // ===== 3. 模块 getter 抛异常 -> 不崩溃 =====
    printf("[模块 getter 抛异常 — 应翻译为 lua_error, 不崩溃]\n");
    {
        int rc = run_in_child(scenario_mod_getter);
        printf("    (信息) 子进程退出码=%d (期望 0)\n", rc);
        check(rc == 0, "模块 getter 抛异常不致崩溃 (H5)");
    }
    // ===== 4. 模块 setter 抛异常 -> 不崩溃 =====
    printf("[模块 setter 抛异常 — 应翻译为 lua_error, 不崩溃]\n");
    {
        int rc = run_in_child(scenario_mod_setter);
        printf("    (信息) 子进程退出码=%d (期望 0)\n", rc);
        check(rc == 0, "模块 setter 抛异常不致崩溃 (H5)");
    }

    // ===== 5. 对照: 正常 getter/setter 路径 (不抛异常) 仍工作 =====
    printf("[正常 getter/setter 路径回归 (不抛异常)]\n");
    {
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        {
            LuaClass<Prop> cls(L, "Prop"); cls.ctor();
            cls.get("ro", &Prop::getOk);
            cls.set("wo", &Prop::setOk);
            cls.get("rw", &Prop::getOk);
            cls.set("rw", &Prop::setOk);
        }
        bool ok = luaL_dostring(L,
            "local p = Prop.new()\n"
            "p.wo = 42\n"
            "assert(p.ro == 42, 'getter via wo set')\n"
            "p.rw = 99\n"
            "assert(p.rw == 99, 'rw roundtrip')\n") == 0;
        if (!ok) printf("    lua-err: %s\n", lua_tostring(L, -1));
        check(ok, "正常类 getter/setter 路径回归");
        lua_close(L);
    }
    // 模块正常路径回归
    // 注: 模块 property 机制依赖 luaL_setfuncs (Lua 5.2+, USE_NEW_MODULE_REGISTRY)。
    //     Lua 5.1/LuaJIT 走 luaL_openlib 旧路径, 不挂模块 property 元方法 — 既有限制,
    //     与 test_functional.cpp 的模块 property 跳过逻辑一致。
    printf("[正常模块 getter/setter 路径回归]\n");
    {
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        {
            LuaModule m(L, "M");
            m.get("v", modGetOk);
            m.set("v", modSetOk);
        }
#if !defined(LUA_VERSION_NUM) || LUA_VERSION_NUM <= 501
        // 5.1/LuaJIT: 模块 property 不支持, 跳过 (既有限制)
        check(true, "模块 property 在 5.1/LuaJIT 不支持 (跳过, 既有限制)");
#else
        bool ok = luaL_dostring(L,
            "assert(M.v == 7, 'mod getter initial')\n"
            "M.v = 100\n"
            "assert(M.v == 100, 'mod setter roundtrip')\n") == 0;
        if (!ok) printf("    lua-err: %s\n", lua_tostring(L, -1));
        check(ok, "正常模块 getter/setter 路径回归");
#endif
        lua_close(L);
    }

    printf("\n=== property_except: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

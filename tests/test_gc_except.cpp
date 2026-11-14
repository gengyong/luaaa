// GC 期间 deleter/析构函数抛 C++ 异常测试 (H6)。
//
// 覆盖未测分支 + 发现新 bug:
//   ⭐【实证 bug — 高危】GC 期间 deleter/析构抛 C++ 异常致进程崩溃
//      根因: f__objgc (luaaa.hpp:1585-1587) 调用 uData->dtor(uData) 【无 try/catch】。
//      Lua 要求 __gc 不得抛异常 (lua_pcall 不捕获 C++ 异常, lua_error 在 __gc 内未定义)。
//      用户 deleter 或析构函数抛异常 -> 穿过 extern "C" 边界 -> terminate -> SIGABRT。
//
//   - 模块 def 单例 + deleter 抛异常 -> lua_close 触发 __gc -> 应不崩溃
//   - 析构函数抛异常的类 -> GC -> 应不崩溃
//   - 对照: 正常 deleter/析构 (不抛异常) 仍正确触发
//
// 验证方式: 在 fork() 子进程执行 (lua_close 触发崩溃会中断测试),
//   父进程观测退出码 (0=正常, -6=SIGABRT 崩溃)。
#include "luaaa.hpp"
#include "child_process.h"

#include <cstdio>
#include <stdexcept>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}


// ====== 会抛异常的 deleter ======
struct Thing { int v; Thing() : v(0) {} int get() const { return v; } };
static void throwingDeleter(Thing* p) {
    (void)p;
    throw std::runtime_error("deleter boom");
}

// ====== 析构函数抛异常的类 ======
class ThrowDtor {
public:
    static int& counter() { static int c = 0; return c; }
    ThrowDtor() { ++counter(); }
    ~ThrowDtor() noexcept(false) { --counter(); throw std::runtime_error("dtor boom"); }
    int val() const { return 42; }
};

// ====== 场景: 单例 deleter 抛异常, lua_close 触发 GC ======
static int scenario_singleton_throwing_deleter() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    {
        LuaClass<Thing> cls(L, "Thing"); cls.fun("get", &Thing::get);
        Thing* obj = new Thing();
        LuaModule(L, "M").def("inst", cls, obj, throwingDeleter);
    }
    luaL_dostring(L, "assert(M.inst:get() == 0, 'access before close')");
    lua_close(L);   // 触发 __gc -> f_dtor -> throw -> 若无保护则 terminate
    return 0;
}

// ====== 场景: 析构函数抛异常, GC 触发 ======
static int scenario_throwing_dtor() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    { LuaClass<ThrowDtor> cls(L, "ThrowDtor"); cls.ctor(); cls.fun("val", &ThrowDtor::val); }
    // 创建对象, 显式 GC 触发析构 -> throw -> 若无保护则 terminate
    luaL_dostring(L, "local _ = ThrowDtor.new(); collectgarbage('collect')");
    lua_close(L);
    return 0;
}

// ====== 对照: 正常 deleter (不抛异常) 仍正确触发 ======
static int g_normal_deleted = 0;
struct NormalThing { int v; NormalThing() : v(5) {} int get() const { return v; } };
static void normalDeleter(NormalThing* p) { delete p; ++g_normal_deleted; }

int main() {
    // ===== 1. 单例 deleter 抛异常 -> lua_close 不崩溃 =====
    printf("[单例 deleter 抛异常 — lua_close 触发 GC 应不崩溃]\n");
    {
        int rc = run_in_child(scenario_singleton_throwing_deleter);
        printf("    (信息) 子进程退出码=%d (期望 0; -6=SIGABRT 崩溃)\n", rc);
        check(rc == 0, "deleter 抛异常的 __gc 不致崩溃 (f__objgc dtor 缺 try/catch, H6)");
    }

    // ===== 2. 析构函数抛异常 -> GC 不崩溃 =====
    printf("[析构函数抛异常 — GC 应不崩溃]\n");
    {
        int rc = run_in_child(scenario_throwing_dtor);
        printf("    (信息) 子进程退出码=%d (期望 0)\n", rc);
        check(rc == 0, "析构函数抛异常的 GC 不致崩溃 (H6)");
    }

    // ===== 3. 对照: 正常 deleter (不抛异常) 正确触发 =====
    printf("[正常 deleter 回归 (不抛异常) — lua_close 触发]\n");
    {
        g_normal_deleted = 0;
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        {
            LuaClass<NormalThing> cls(L, "NormalThing"); cls.fun("get", &NormalThing::get);
            NormalThing* obj = new NormalThing();
            LuaModule(L, "N").def("inst", cls, obj, normalDeleter);
        }
        bool ok = luaL_dostring(L, "assert(N.inst:get() == 5, 'access')") == 0;
        if (!ok) printf("    lua-err: %s\n", lua_tostring(L, -1));
        check(ok, "正常单例访问");
        lua_close(L);
        printf("    (信息) lua_close 后 g_normal_deleted=%d (期望1)\n", g_normal_deleted);
        check(g_normal_deleted == 1, "正常 deleter 在 lua_close 触发 (回归保护)");
    }

    // ===== 4. 对照: 正常析构 (不抛异常) GC 平衡 =====
    printf("[正常析构 GC 平衡回归]\n");
    {
        ThrowDtor::counter() = 0;
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        // 注: 用一个不抛析构的普通类验证 GC 平衡
        struct Tracked {
            static int& c() { static int x = 0; return x; }
            Tracked() { ++c(); }
            ~Tracked() { --c(); }
            int val() const { return 1; }
        };
        { LuaClass<Tracked> cls(L, "Tracked"); cls.ctor(); cls.fun("val", &Tracked::val); }
        luaL_dostring(L, "for i=1,5 do local _ = Tracked.new() end");
        luaL_dostring(L, "collectgarbage('collect')");
        lua_close(L);
        printf("    (信息) Tracked 计数器=%d (期望0, 析构平衡)\n", Tracked::c());
        check(Tracked::c() == 0, "正常析构 GC 平衡 (5 构造全析构)");
    }

    printf("\n=== gc_except: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

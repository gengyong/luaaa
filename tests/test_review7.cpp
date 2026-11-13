// 第七轮评审修复回归测试 (高危 4 项):
//   H1  __gc 幂等: 脚本手动调用 obj:__gc() 后 GC 不再二次析构; 之后调方法报错而非
//       访问已析构对象; 用户 __gc 只跑一次; 对外来 userdata 调 __gc 被忽略
//   H2  ctor(name, spawner, deleter): 类表(构造闭包)先于对象被回收后, 对象回收时
//       deleter 仍可安全调用 (修复前: 读取已释放的 deleter holder -> use-after-free)
//   H3  回调内的类型转换错误不再 luaL_error 逃出回调 (修复前: 纯 C++ 调用时 PANIC/abort)
//   H4  在协程里注册的回调运行在主线程 state 上 (修复前: 持有已回收协程的 lua_State)
//
// 用法: 不带参数跑全部; 带用例名 (h1|h2|h3|h4) 只跑该组 (便于单独观察崩溃类缺陷)。
#include "luaaa.hpp"

#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <functional>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

static bool lua_ok(lua_State* L, const char* code) {
    if (luaL_dostring(L, code) != 0) {
        printf("    (lua err) %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

// 无保护调用里的 lua 错误会触发 panic -> abort。测试里用 longjmp 逃出 panic
// 以便把「发生了 panic」记为一次失败, 而不是让整个测试进程崩掉。
static jmp_buf g_panicJmp;
static int panicHandler(lua_State*) { longjmp(g_panicJmp, 1); return 0; }

// ===================== 被测类型与函数 =====================
struct Tracker {
    static int s_ctor, s_dtor, s_userGc, s_destroy;
    int v;
    explicit Tracker(int x = 0) : v(x) { ++s_ctor; }
    Tracker(const Tracker& o) : v(o.v) { ++s_ctor; }
    ~Tracker() { ++s_dtor; }
    int get() const { return v; }
    void userGc() { ++s_userGc; }
    static Tracker* make() { return new Tracker(7); }
    static void destroy(Tracker* t) { ++s_destroy; delete t; }
    static void reset() { s_ctor = s_dtor = s_userGc = s_destroy = 0; }
};
int Tracker::s_ctor = 0, Tracker::s_dtor = 0, Tracker::s_userGc = 0, Tracker::s_destroy = 0;

struct Unbound { int x; };   // 故意不绑定: 按值 put 会报 "not export"

typedef int (*IntFn)();
static std::function<int()> g_cb;
static std::function<void(Unbound)> g_cbUnbound;
static IntFn g_fp = nullptr;
static void keep(std::function<int()> f) { g_cb = f; }
static void keepUnbound(std::function<void(Unbound)> f) { g_cbUnbound = f; }
static void keepFp(IntFn f) { g_fp = f; }

static lua_State* g_main = nullptr;
static bool onMain(lua_State* L) { return L == g_main; }

// ===================== H1 =====================
static void test_h1() {
    printf("[H1: __gc 幂等]\n");
    {
        Tracker::reset();
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        LuaClass<Tracker>(L, "Tracker").ctor<int>().fun("get", &Tracker::get);
        check(lua_ok(L, "local o = Tracker.new(5); o:__gc(); o = nil; collectgarbage(); collectgarbage()"),
              "脚本手动 o:__gc() 后再 GC 不报错");
        lua_close(L);
        check(Tracker::s_ctor == 1 && Tracker::s_dtor == 1, "手动 __gc + GC: 析构恰好一次 (ctor=1, dtor=1)");
    }
    {
        Tracker::reset();
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        LuaClass<Tracker>(L, "Tracker").ctor<int>().fun("get", &Tracker::get);
        check(lua_ok(L, "local o = Tracker.new(5); o:__gc(); local ok = pcall(o.get, o); assert(ok == false, 'method on finalized object must raise')"),
              "已终结对象上调方法 -> lua 错误 (而非访问已析构对象)");
        lua_close(L);
    }
    {
        Tracker::reset();
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        LuaClass<Tracker>(L, "Tracker").ctor<int>().fun("__gc", &Tracker::userGc);
        lua_ok(L, "local o = Tracker.new(5); o:__gc(); o:__gc(); o = nil; collectgarbage(); collectgarbage()");
        lua_close(L);
        check(Tracker::s_userGc == 1, "用户 __gc 只运行一次 (手动两次 + GC 一次)");
        check(Tracker::s_ctor == 1 && Tracker::s_dtor == 1, "带用户 __gc 时析构仍恰好一次");
    }
    {
        Tracker::reset();
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        LuaClass<Tracker>(L, "Tracker").ctor<int>().fun("get", &Tracker::get);
        // 外来 userdata (io.stdout) 不是本类对象: 不得被当成 UserDataDetail 解释
        check(lua_ok(L, "local o = Tracker.new(5); getmetatable(o).__gc(io.stdout); assert(io.stdout:write('') ~= nil); assert(o:get() == 5)"),
              "对外来 userdata 调 __gc 被忽略, 两个对象都完好");
        lua_close(L);
        check(Tracker::s_ctor == 1 && Tracker::s_dtor == 1, "外来 userdata 场景析构计数平衡");
    }
}

// ===================== H2 =====================
static void test_h2() {
    printf("[H2: 类表先被回收, deleter 仍可安全调用]\n");
    Tracker::reset();
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    LuaClass<Tracker>(L, "Tracker").ctor("make", &Tracker::make, &Tracker::destroy);
    check(lua_ok(L, "local o = Tracker.make(); Tracker = nil; collectgarbage(); collectgarbage(); o = nil; collectgarbage(); collectgarbage()"),
          "Tracker=nil 回收构造闭包后再回收对象不报错");
    check(Tracker::s_destroy == 1, "deleter 恰好调用一次");
    lua_close(L);
    check(Tracker::s_ctor == 1 && Tracker::s_dtor == 1, "对象恰好析构一次");
}

// ===================== H3 =====================
static void test_h3() {
    printf("[H3: 回调内转换错误不逃出回调]\n");
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    lua_atpanic(L, panicHandler);
    LuaModule(L, "m").fun("keep", keep).fun("keepUnbound", keepUnbound).fun("keepFp", keepFp);

    // 返回值类型不符 (std::function)
    lua_ok(L, "m.keep(function() return 'not a number' end)");
    {
        volatile bool panicked = false; volatile int r = -1;
        const int top = lua_gettop(L);
        if (setjmp(g_panicJmp) == 0) { r = g_cb(); } else { panicked = true; }
        check(!panicked, "std::function 回调返回类型不符: 不 panic");
        check(!panicked && r == 0, "  ...返回默认值 RET()");
        if (!panicked) {
            for (int i = 0; i < 50; ++i) g_cb();
            check(lua_gettop(L) == top, "  ...连续 50 次失败调用后栈平衡");
        }
    }
    // 实参无法转换 (未绑定类按值传入 lua)
    lua_ok(L, "m.keepUnbound(function(u) end)");
    {
        volatile bool panicked = false;
        const int top = lua_gettop(L);
        if (setjmp(g_panicJmp) == 0) { g_cbUnbound(Unbound{1}); } else { panicked = true; }
        check(!panicked, "回调实参无法转换 (未绑定类): 不 panic");
        check(!panicked && lua_gettop(L) == top, "  ...栈平衡");
    }
    // 返回值类型不符 (裸函数指针)
    lua_ok(L, "m.keepFp(function() return {} end)");
    {
        volatile bool panicked = false; volatile int r = -1;
        const int top = lua_gettop(L);
        if (setjmp(g_panicJmp) == 0) { r = g_fp(); } else { panicked = true; }
        check(!panicked, "函数指针回调返回类型不符: 不 panic");
        check(!panicked && r == 0 && lua_gettop(L) == top, "  ...返回默认值且栈平衡");
    }
    // 回归保护: 正常回调仍然工作
    lua_ok(L, "local n = 0; m.keep(function() n = n + 1; return n end)");
    check(g_cb() == 1 && g_cb() == 2, "正常回调可重复调用且返回值正确");
    lua_ok(L, "m.keepFp(function() return 42 end)");
    check(g_fp() == 42, "正常函数指针回调返回值正确");

    g_cb = nullptr; g_cbUnbound = nullptr; g_fp = nullptr;
    lua_close(L);
}

// ===================== H4 =====================
static void test_h4() {
    printf("[H4: 协程里注册的回调运行在主线程]\n");
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    g_main = L;
    LuaModule(L, "m").fun("keep", keep).fun("keepFp", keepFp).fun("onMain", onMain);

    check(lua_ok(L,
        "do local co = coroutine.create(function() m.keep(function() return m.onMain() and 1 or 0 end) end) "
        "   assert(coroutine.resume(co)) end "
        "collectgarbage(); collectgarbage()"), "在协程内注册 std::function 回调, 随后回收协程");
    check(g_cb() == 1, "std::function 回调在主线程 state 上运行");

    check(lua_ok(L,
        "do local co = coroutine.create(function() m.keepFp(function() return m.onMain() and 1 or 0 end) end) "
        "   assert(coroutine.resume(co)) end "
        "collectgarbage(); collectgarbage()"), "在协程内注册函数指针回调, 随后回收协程");
    check(g_fp() == 1, "函数指针回调在主线程 state 上运行");

    g_cb = nullptr; g_fp = nullptr;
    lua_close(L);
    g_main = nullptr;
}

int main(int argc, char** argv) {
    const char* only = argc > 1 ? argv[1] : nullptr;
    if (!only || !strcmp(only, "h1")) test_h1();
    if (!only || !strcmp(only, "h2")) test_h2();
    if (!only || !strcmp(only, "h3")) test_h3();
    if (!only || !strcmp(only, "h4")) test_h4();
    printf("\n=== review7: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

// GC / 异常 / 生命周期测试。
//
// 重点排查:
//   【高危】构造函数抛异常 — C++ 异常未翻译为 lua_error, 导致 std::terminate 进程崩溃
//           (luaaa.hpp 边界无 try/catch). 这是比「userdata 字段安全」更严重的问题.
//   【中危】用户 __gc 抛错 → C++ 析构仍执行 (luaaa.hpp:1334-1345)
//   【文档化】std::function 回调跨 lua_close 析构 → use-after-free (luaaa.hpp:743)
//   【文档化】const char* 悬垂 (luaaa.hpp:663)
//   GC 生命周期平衡 (ctor/dtor 计数对齐)
//
// 注: 「构造函数抛异常」会触发 std::terminate. 为不让该崩溃中断整个测试套件,
//     该场景用 fork() 在子进程中执行, 父进程观测子进程退出码 (SIGABRT=134).
#include "luaaa.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <functional>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

// ====== GC 生命周期计数器 ======
static int g_ctor = 0, g_dtor = 0;
static void reset_counts() { g_ctor = 0; g_dtor = 0; }

class Tracked {
public:
    int v;
    Tracked() : v(0) { ++g_ctor; }
    Tracked(int x) : v(x) { ++g_ctor; }
    ~Tracked() { ++g_dtor; }
};

// ====== 构造抛异常类 ======
static int g_throw_ctor_calls = 0;
static int g_throw_dtor_calls = 0;
class ThrowCtor {
public:
    ThrowCtor() {
        ++g_throw_ctor_calls;
        throw std::runtime_error("ctor throws");
    }
    ~ThrowCtor() { ++g_throw_dtor_calls; }
};

// ====== 用户 __gc 探测 ======
static int g_user_gc_calls = 0;
static int g_real_dtor_calls = 0;
class GcProbe {
public:
    GcProbe() {}
    ~GcProbe() { ++g_real_dtor_calls; }
    int userGc() { ++g_user_gc_calls; return 0; }
};

// ====== const char* 悬垂探测 ======
static const char* g_stored_cstr = nullptr;
static void store_cstr(const char* s) { g_stored_cstr = s; }
static const char* read_stored_cstr() { return g_stored_cstr ? g_stored_cstr : "(null)"; }

// 在子进程中运行「构造函数抛异常」场景 (会 std::terminate, 退出码 134)
// 返回子进程退出状态码 (WIFEXITED) 或 -1 (被信号终止, 如 SIGABRT)
static int run_ctor_throw_in_child() {
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程: 重定向 stdout 到 /dev/null (我们只关心退出方式)
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, 1); dup2(devnull, 2); close(devnull); }
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        {
            LuaClass<ThrowCtor> cls(L, "ThrowCtor");
            cls.ctor();
        }
        // 调用会抛的 ctor. C++ 异常穿过 luaaa 边界 (无 try/catch) -> std::terminate
        luaL_dostring(L, "ThrowCtor.new()");
        lua_close(L);
        _exit(0);  // 若未 terminate, 正常退出
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return -WTERMSIG(status);  // 负数表示被信号杀
    return -999;
}

#include <fcntl.h>  // 已在文件顶部包含

int main() {
    // ========================================================================
    // 测试 1: GC 生命周期平衡
    // ========================================================================
    printf("[GC 生命周期平衡]\n");
    {
        reset_counts();
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        { LuaClass<Tracked> cls(L, "Tracked"); cls.ctor(); cls.ctor<int>("fromInt"); }
        check(luaL_dostring(L, "for i=1,10 do local _ = Tracked.new() end") == 0,
              "创建 10 个对象成功");
        luaL_dostring(L, "collectgarbage('collect')");
        luaL_dostring(L, "collectgarbage('collect')");
        printf("    (信息) ctor=%d dtor=%d\n", g_ctor, g_dtor);
        check(g_ctor == 10, "10 个对象被构造");
        check(g_dtor == 10, "10 个对象被析构 (生命周期平衡)");
        lua_close(L);
    }

    // ========================================================================
    // 测试 2: 构造函数抛 C++ 异常 → 应翻译为 lua_error (H2 修复)
    // ========================================================================
    printf("[构造函数抛异常 — H2 修复: 应翻译为 lua_error (pcall 捕获)]\n");
    {
        int rc = run_ctor_throw_in_child();
        printf("    (信息) 子进程退出码 = %d (0=正常退出, 134/-6=SIGABRT崩溃)\n", rc);
        bool crashed = (rc == 134 || rc == -6 || rc == -11);  // SIGABRT or SIGSEGV
        bool clean = (rc == 0);   // 异常被翻译为 lua_error, pcall 捕获, 进程正常退出
        if (crashed) {
            printf("    [实证] 构造函数抛 C++ 异常仍导致 std::terminate/SIGABRT — 进程崩溃 (H2 未修复)\n");
        } else if (clean) {
            printf("    [验证] 构造函数抛 C++ 异常被翻译为 lua_error, pcall 捕获, 进程正常退出\n");
        }
        // H2 修复: C++ 异常翻译为 lua_error, 宿主进程不再崩溃
        check(clean, "构造函数抛异常应翻译为 lua_error (H2 修复, 进程不再崩溃)");
    }

    // ========================================================================
    // 测试 3: 用户 __gc 与 C++ 析构
    // ========================================================================
    printf("[用户 __gc 与 C++ 析构 — luaaa.hpp:1334-1345]\n");
    {
        g_user_gc_calls = 0;
        g_real_dtor_calls = 0;
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        { LuaClass<GcProbe> cls(L, "GcProbe"); cls.ctor(); cls.fun("__gc", &GcProbe::userGc); }
        check(luaL_dostring(L, "local _ = GcProbe.new()") == 0, "创建 GcProbe");
        check(luaL_dostring(L, "collectgarbage('collect')") == 0, "GC 触发");
        check(luaL_dostring(L, "collectgarbage('collect')") == 0, "再次 GC");
        printf("    (信息) user_gc=%d real_dtor=%d\n", g_user_gc_calls, g_real_dtor_calls);
        check(g_real_dtor_calls >= 1, "C++ 析构在 GC 时执行");
        lua_close(L);
    }

    // ========================================================================
    // 测试 4: GC 重复调用安全 (无 double-free)
    // ========================================================================
    printf("[GC 重复调用安全 — 无 double-free]\n");
    {
        reset_counts();
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        { LuaClass<Tracked> cls(L, "Tracked2"); cls.ctor(); }
        luaL_dostring(L, "for i=1,5 do local _ = Tracked2.new() end");
        for (int i = 0; i < 3; ++i) luaL_dostring(L, "collectgarbage('collect')");
        printf("    (信息) ctor=%d dtor=%d (3次GC后)\n", g_ctor, g_dtor);
        check(g_ctor == 5 && g_dtor == 5, "5 对象 ctor=dtor, 无 double-free");
        lua_close(L);
    }

    // ========================================================================
    // 测试 5: const char* 悬垂 (文档化)
    // ========================================================================
    printf("[const char* 悬垂行为 (文档化) — luaaa.hpp:663]\n");
    {
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        { LuaModule m(L, "S"); m.fun("store", store_cstr); m.fun("read", read_stored_cstr); }
        check(luaL_dostring(L, "S.store('hello')") == 0, "存储 const char*");
        check(luaL_dostring(L, "assert(S.read() == 'hello')") == 0, "立即读回有效");
        printf("    (信息) const char* 单次调用内安全, 跨调用存储有悬垂风险\n");
        check(true, "KNOWN-LIMITATION: const char* 参数不能跨调用保存 (仅文档约定, 此处不作实测)");
        lua_close(L);
    }

    // ========================================================================
    // 测试 6: std::function 回调生命周期 (单 state 内)
    // ========================================================================
    printf("[std::function 回调生命周期 — 单 state 内安全]\n");
    {
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        {
            LuaModule m(L, "CB");
            m.fun("call", [](std::function<int(int)> cb, int x) -> int { return cb(x); });
        }
        check(luaL_dostring(L, "local f=function(x)return x*2 end; assert(CB.call(f,5)==10)") == 0,
              "回调首次调用");
        check(luaL_dostring(L, "local f=function(x)return x*2 end; assert(CB.call(f,6)==12)") == 0,
              "回调二次调用 (H1 修复验证)");
        check(luaL_dostring(L, "collectgarbage('collect')") == 0, "GC 期间回调安全");
        lua_close(L);
    }

    printf("\n=== lifetime: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

// 静态常量 / 数组 def / 类重复绑定 / 跨 state registry 测试。
//
// 覆盖未测分支:
//   - 模块 def(name, val[], length) 数组导出
//   - 类 def 常量 (const char* / std::string / 数值)
//   - 同 state 重复 LuaClass<SameType>("Name") 绑定 — 不崩溃、方法可达
//   - 跨两个 state 各自绑定同名类 — registry 独立
//   - const 成员函数对照 (已有覆盖, 补 std::string 返回)
//
// 设计原则: 断言「正确/合理」行为; FAIL 即 bug。
#include "luaaa.hpp"

#include <cstdio>
#include <string>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

static bool run(lua_State* L, const char* tag, const char* code) {
    printf("[%s]\n", tag);
    if (luaL_dostring(L, code)) {
        printf("  FAIL (lua-err: %s)\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        ++F;
        return false;
    }
    printf("  ok\n");
    ++P;
    return true;
}

// 被测类
class Config {
public:
    int version;
    Config() : version(1) {}
    int getVersion() const { return version; }
    std::string label() const { return "cfg"; }
};

int main() {
    // ========================================================================
    // 测试 1: 模块数组 def
    // ========================================================================
    printf("[模块数组 def(name, val[], length)]\n");
    {
        static const int arr[] = {10, 20, 30, 40};
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        LuaModule(L, "data").def("nums", arr, 4);
        bool ok = luaL_dostring(L,
            "assert(#data.nums == 4, 'len')\n"
            "assert(data.nums[1]==10 and data.nums[4]==40, 'values')\n") == 0;
        if (!ok) printf("    lua-err: %s\n", lua_tostring(L, -1));
        check(ok, "数组 def 导出 4 元素, 值正确");
        lua_close(L);
    }

    // 空数组 def
    printf("[模块空数组 def]\n");
    {
        // 零长数组 `int a[] = {}` 是 GNU 扩展，MSVC 报 C2466；用长度 1 的数组、传长度 0
        static const int empty[1] = { 0 };
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        LuaModule(L, "e").def("arr", empty, 0);
        bool ok = luaL_dostring(L, "assert(#e.arr == 0, 'empty')") == 0;
        if (!ok) printf("    lua-err: %s\n", lua_tostring(L, -1));
        check(ok, "空数组 def 导出 0 元素");
        lua_close(L);
    }

    // ========================================================================
    // 测试 2: 类 def 常量
    // ========================================================================
    printf("[类 def 常量 — 存于 metatable, 实例级访问]\n");
    {
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        {
            LuaClass<Config> cls(L, "Config");
            cls.ctor();
            cls.def("MAX", 100);
            cls.def("name", std::string("myconfig"));
            cls.def("tag", "tagval");   // const char*
            cls.fun("ver", &Config::getVersion);
            cls.fun("label", &Config::label);
        }
        // 类 def 存于 metatable (luaaa.hpp:2181 luaL_getmetatable), 故实例可访问,
        // 但类表 Config.MAX 直接访问不可达 (非静态常量语义)。
        bool ok = luaL_dostring(L,
            "local c = Config.new()\n"
            "assert(c.MAX == 100, 'int const via instance')\n"
            "assert(c.name == 'myconfig', 'string const via instance')\n"
            "assert(c.tag == 'tagval', 'cstr const via instance')\n"
            "assert(c:ver() == 1, 'method still works')\n"
            "assert(c:label() == 'cfg', 'string return')\n") == 0;
        if (!ok) printf("    lua-err: %s\n", lua_tostring(L, -1));
        check(ok, "类 def 常量经实例访问 (int/string/cstr) + 方法共存");
        lua_close(L);
    }
    // 实证: 类表直接访问 Config.MAX 不可达 (非静态常量)
    printf("[类 def 常量 — 类表直接访问不可达 (实例级, 非静态)]\n");
    {
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        {
            LuaClass<Config> cls(L, "Config");
            cls.ctor();
            cls.def("MAX", 100);
        }
        lua_getglobal(L, "Config");
        lua_getfield(L, -1, "MAX");
        bool reachable = !lua_isnil(L, -1);
        lua_pop(L, 2);
        printf("    (信息) Config.MAX 类表直接访问 is_nil=%d (期望 nil: 非静态)\n", !reachable);
        check(!reachable, "类 def 常量非静态, 类表直接访问 Config.MAX 不可达 (存于 metatable)");
        lua_close(L);
    }

    // ========================================================================
    // 测试 3: 同 state 重复绑定同名类
    // ========================================================================
    printf("[同 state 重复 LuaClass<SameType>('Name') 绑定]\n");
    {
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        {
            LuaClass<Config> cls(L, "Config");
            cls.ctor();
            cls.fun("ver", &Config::getVersion);
        }
        // 再次绑定同名类 (补加方法)
        {
            LuaClass<Config> cls(L, "Config");
            cls.fun("label", &Config::label);
        }
        bool ok = luaL_dostring(L,
            "assert(Config.new():ver() == 1, 'first bind method')\n"
            "assert(Config.new():label() == 'cfg', 'second bind method')\n") == 0;
        if (!ok) printf("    lua-err: %s\n", lua_tostring(L, -1));
        check(ok, "同 state 重复绑定同名类, 两次的方法都可达");
        lua_close(L);
    }

    // ========================================================================
    // 测试 4: 跨两个 state 各自绑定同名类 (registry 独立)
    // ========================================================================
    printf("[跨 state registry 独立 — 同名类互不干扰]\n");
    {
        lua_State* L1 = luaL_newstate(); luaL_openlibs(L1);
        lua_State* L2 = luaL_newstate(); luaL_openlibs(L2);
        {
            LuaClass<Config> c1(L1, "Config"); c1.ctor(); c1.fun("ver", &Config::getVersion);
            LuaClass<Config> c2(L2, "Config"); c2.ctor(); c2.fun("ver", &Config::getVersion);
        }
        // L1 设 version, L2 独立
        luaL_dostring(L1, "g1 = Config.new():ver()");
        luaL_dostring(L2, "g2 = Config.new():ver()");
        lua_getglobal(L1, "g1"); lua_Integer v1 = lua_tointeger(L1, -1); lua_pop(L1, 1);
        lua_getglobal(L2, "g2"); lua_Integer v2 = lua_tointeger(L2, -1); lua_pop(L2, 1);
        printf("    (信息) L1 Config.ver=%lld, L2 Config.ver=%lld\n", (long long)v1, (long long)v2);
        check(v1 == 1 && v2 == 1, "两个 state 各自的 Config 独立工作");
        lua_close(L1);
        lua_close(L2);
    }

    // ========================================================================
    // 测试 5: 模块 def 单例 + deleter (对象生命周期)
    // ========================================================================
    printf("[模块 def 单例 + deleter — 析构触发]\n");
    {
        static int g_deleted = 0;
        struct Single {
            int v;
            Single() : v(42) {}
            int get() const { return v; }
        };
        struct SingleDel { static void del(Single* p) { delete p; ++g_deleted; } };

        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        {
            LuaClass<Single> cls(L, "Single");
            cls.fun("get", &Single::get);
            Single* obj = new Single();
            LuaModule(L, "sys").def("inst", cls, obj, SingleDel::del);
        }
        bool ok = luaL_dostring(L, "assert(sys.inst:get() == 42, 'singleton')") == 0;
        if (!ok) printf("    lua-err: %s\n", lua_tostring(L, -1));
        check(ok, "模块 def 单例 + deleter 可访问");
        lua_close(L);
        printf("    (信息) lua_close 后 g_deleted=%d (期望1, deleter 触发)\n", g_deleted);
        check(g_deleted == 1, "lua_close 触发 deleter 析构单例");
    }

    printf("\n=== static_const: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

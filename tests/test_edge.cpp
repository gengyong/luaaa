// 边界/多 state 测试：
//   M3 修复后 —— 同一 C++ 类型可在多个并存 lua_State 中以【相同】名绑定并各自使用；
//               用【不同】名绑定同一类型仍应冲突报错。
//   继承   —— luaaa:extend + override + base 调用。
// 下面手写的 extend 辅助函数是旧文档里的写法, 靠 getmetatable(o) 取类的 metatable。
// 2.0 起默认隐藏 metatable (getmetatable 返回 false), 这种脚本要关掉该保护。
#define LUAAA_PROTECT_METATABLE 0
#include "luaaa.hpp"
#include <string>
#include <cstdio>
using namespace luaaa;

struct Animal {
    std::string n;
    Animal(const std::string& s) : n(s) {}
    std::string name() const { return n; }
    void speak() const { printf("  [C++ base speak] %s\n", n.c_str()); }
};

static const char* EXTEND = R"(
luaaa = {}
function luaaa:extend(base, obj)
  local d = obj or {}
  d.new = function(self, ...) local o=base.new(...); setmetatable(self, getmetatable(o)); self["@"]=o; return self end
  return d
end
function luaaa:base(obj) if type(obj)=="table" then return obj["@"] end return nil end
)";

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* name) {
    if (ok) { ++g_pass; printf("  ok   %s\n", name); }
    else    { ++g_fail; printf("  FAIL %s\n", name); }
}

// 在受保护环境中尝试用给定名字绑定 Animal（构造里的 luaL_argcheck 失败会 longjmp）
static bool tryBind(lua_State* s, const char* name) {
    lua_pushcfunction(s, [](lua_State* st)->int {
        const char* nm = lua_tostring(st, 1);
        LuaClass<Animal> c(st, nm);
        c.ctor<std::string>();
        c.fun("name", &Animal::name);
        return 0;
    });
    lua_pushstring(s, name);
    return lua_pcall(s, 1, 0, 0) == 0;
}

int main() {
    printf("=== M3: 并存 lua_State 同名绑定同一 C++ 类 ===\n");
    {
        lua_State* L1 = luaL_newstate(); luaL_openlibs(L1);
        lua_State* L2 = luaL_newstate(); luaL_openlibs(L2);

        check(tryBind(L1, "Animal"), "L1 绑定 Animal");
        check(tryBind(L2, "Animal"), "L2 绑定同名 Animal(并存, 不冲突)");

        check(luaL_dostring(L1, "local a=Animal.new('rex'); assert(a:name()=='rex')") == 0,
              "L1 使用 Animal 正常");
        check(luaL_dostring(L2, "local a=Animal.new('max'); assert(a:name()=='max')") == 0,
              "L2 使用 Animal 正常");

        lua_close(L1);
        check(luaL_dostring(L2, "local a=Animal.new('leo'); assert(a:name()=='leo')") == 0,
              "L1 关闭后 L2 仍可用 Animal");
        lua_close(L2);
    }

    printf("=== M3: 并存 lua_State 以【不同】名绑定同一 C++ 类(per-state) ===\n");
    {
        lua_State* La = luaL_newstate(); luaL_openlibs(La);
        lua_State* Lb = luaL_newstate(); luaL_openlibs(Lb);

        check(tryBind(La, "Animal"), "La 绑定为 Animal");
        check(tryBind(Lb, "Beast"),  "Lb 绑定同一类型为 Beast(不同名并存)");

        check(luaL_dostring(La, "local a=Animal.new('rex'); assert(a:name()=='rex')") == 0,
              "La 用 Animal 名调用");
        check(luaL_dostring(Lb, "local b=Beast.new('max');  assert(b:name()=='max')") == 0,
              "Lb 用 Beast 名调用");
        // 各 state 只认自己那个名字
        check(luaL_dostring(La, "assert(Beast==nil)") == 0, "La 中不存在 Beast 名");
        lua_close(La); lua_close(Lb);
    }

    printf("=== M3: 同一 state 内用【不同】名绑定同类型应冲突 ===\n");
    {
        lua_State* Ls = luaL_newstate(); luaL_openlibs(Ls);
        check(tryBind(Ls, "Cat"),   "Ls 首次绑定为 Cat");
        check(!tryBind(Ls, "Kitty"), "Ls 再用 Kitty 绑同类型被拒绝(同一 state 冲突)");
        lua_close(Ls);
    }

    printf("=== 继承: luaaa:extend + override + base 调用 ===\n");
    {
        lua_State* L = luaL_newstate(); luaL_openlibs(L);
        check(tryBind(L, "Animal"), "绑定 Animal");
        lua_pushcfunction(L, [](lua_State* s)->int {
            LuaClass<Animal> c(s, "Animal"); c.fun("speak", &Animal::speak); return 0;
        });
        lua_pcall(L, 0, 0, 0);
        if (luaL_dostring(L, EXTEND)) { printf("  extend helper err: %s\n", lua_tostring(L, -1)); }
        const char* prog = R"(
          Dog = luaaa:extend(Animal, {kind="dog"})
          function Dog:bark() return self:name() .. " barks" end
          local d = Dog:new("Rex")
          assert(d:name()=="Rex", "inherited method failed")
          assert(d:bark()=="Rex barks", "subclass method failed")
          assert(d.kind=="dog", "subclass field failed")
          -- object metatable / "@" 机制: 通过 luaaa:base 取回 C++ 对象并调其方法
          assert(luaaa:base(d):name()=="Rex", "base C++ method via @ failed")
        )";
        check(luaL_dostring(L, prog) == 0, "继承/子类方法/字段");
        lua_close(L);
    }

    printf("\n=== edge: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

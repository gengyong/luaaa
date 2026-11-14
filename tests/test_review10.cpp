// 第十轮: 遗留清单 R9-2 / R9-3 / R9-4 / R9-6 / R9-7 / R9-8 / R9-9 与功能类建议的回归测试。
//   R9-2  等宽无符号整数出参按位传递, 与入参对称 (不再经浮点比较)
//   R9-3  默认 nil 传给 T* 仍报错 (开启 LUAAA_NIL_AS_NULLPTR 的行为见 test_features_nil_nullptr)
//   R9-4  luaaa:extend 的实例可以传给 T* 参数
//   R9-6  回调出错时调用 setCallbackErrorHandler 注册的钩子
//   R9-7  LuaModule(L) 不再改动 _G; 模块名支持 "a.b"; 写入 package.loaded
//   R9-8  metatable 对脚本隐藏; 方法在独立的方法表里
//   R9-9  std::string 保留嵌入的 NUL (另见 test_strings)
//   功能  C++ 继承 (inherit<Base>) / 智能指针 / 只能移动的返回类型 / C++17 类型 /
//         重载分发 (overload, overloadCtor) / staticFun, constant
#include "luaaa.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}
static bool lua_ok(lua_State* L, const char* code) {
    if (luaL_dostring(L, code) != 0) {
        printf("    (lua error) %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}
static std::string lua_err(lua_State* L, const char* code) {
    if (luaL_dostring(L, code) != 0) {
        const char* m = lua_tostring(L, -1);
        std::string s = m ? m : "<non-string error>";
        lua_pop(L, 1);
        return s;
    }
    return "<no error>";
}
static bool has(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }
static lua_State* newState() { lua_State* L = luaL_newstate(); luaL_openlibs(L); return L; }

// ===================== R9-2 =====================
static uint64_t u64max() { return UINT64_MAX; }
static uint64_t u64hi() { return uint64_t(1) << 63; }
static uint64_t u64small() { return 12345; }
static bool isU64Max(uint64_t v) { return v == UINT64_MAX; }
static bool isU64Hi(uint64_t v) { return v == (uint64_t(1) << 63); }
static uint64_t u64echo(uint64_t v) { return v; }
static uint32_t u32max() { return UINT32_MAX; }
static int64_t i64min() { return INT64_MIN; }
static int64_t i64max() { return INT64_MAX; }

static void test_r9_2() {
    printf("[R9-2: 等宽无符号整数出参]\n");
    lua_State* L = newState();
    LuaModule(L, "n").fun("u64max", u64max).fun("u64hi", u64hi).fun("u64small", u64small)
        .fun("isU64Max", isU64Max).fun("isU64Hi", isU64Hi).fun("u64echo", u64echo)
        .fun("u32max", u32max).fun("i64min", i64min).fun("i64max", i64max);
    check(lua_ok(L, "assert(n.u64max() == -1)"), "UINT64_MAX 出参按位传递, Lua 侧为 -1 (不再报错)");
    check(lua_ok(L, "assert(n.u64hi() < 0)"), "uint64(1)<<63 出参按位传递, Lua 侧为负数");
    check(lua_ok(L, "assert(n.isU64Max(n.u64max()))"), "UINT64_MAX 往返后仍是 UINT64_MAX");
    check(lua_ok(L, "assert(n.isU64Hi(n.u64hi()))"), "uint64(1)<<63 往返后值不变");
    check(lua_ok(L, "assert(n.isU64Max(-1) and n.u64echo(-1) == -1)"), "入参 -1 -> UINT64_MAX, 原样返回仍是 -1 (往返对称)");
    check(lua_ok(L, "assert(n.u64small() == 12345 and n.u32max() == 4294967295)"), "范围内的无符号值不受影响");
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM >= 503
    check(lua_ok(L, "assert(n.i64min() == math.mininteger and n.i64max() == math.maxinteger)"), "INT64_MIN / INT64_MAX 出参精确");
#else
    // 5.1 / 5.2 / LuaJIT 的数字是 double, 64 位整数只能近似表示
    check(lua_ok(L, "assert(n.i64min() < 0 and n.i64max() > 0)"), "INT64_MIN / INT64_MAX 出参不报错 (double 近似)");
#endif
    lua_close(L);
}

// ===================== 被测类型 =====================
struct Animal {
    std::string n;
    int legs;
    explicit Animal(const std::string& s) : n(s), legs(4) {}
    virtual ~Animal() {}
    std::string name() const { return n; }
    virtual std::string speak() const { return "..."; }
    int getLegs() const { return legs; }
    void setLegs(int v) { legs = v; }
};
struct Dog : Animal {
    int tricks;
    explicit Dog(const std::string& s) : Animal(s), tricks(0) {}
    std::string speak() const override { return "woof"; }
    void learn() { ++tricks; }
    int count() const { return tricks; }
};
struct Puppy : Dog {
    explicit Puppy(const std::string& s) : Dog(s) {}
    std::string speak() const override { return "yip"; }
};
struct Rock { int w; Rock() : w(1) {} int weight() const { return w; } };

static std::string nameOf(const Animal& a) { return a.name(); }
static std::string speakOf(Animal* a) { return a->speak(); }
static int tricksOf(const Dog& d) { return d.tricks; }
static int dogPtr(Dog* d) { return d->tricks; }
static Animal* asAnimal(Dog* d) { return d; }

// 多重继承: 第二个基类子对象的地址与对象起始地址不同, 向上转换必须调整指针
struct PartA { int a; PartA() : a(11) {} int getA() const { return a; } };
struct PartB { int b; PartB() : b(22) {} int getB() const { return b; } };
struct Both : PartA, PartB { int c; Both() : c(33) {} int getC() const { return c; } };
static int readB(const PartB& b) { return b.b; }
static int readBPtr(PartB* b) { return b->b; }
static int readA(const PartA& a) { return a.a; }

static void bindAnimals(lua_State* L) {
    LuaClass<Animal>(L, "Animal").ctor<std::string>()
        .fun("name", &Animal::name).fun("speak", &Animal::speak)
        .get("legs", &Animal::getLegs).set("legs", &Animal::setLegs)
        .def("KINGDOM", "animalia");
    LuaClass<Dog>(L, "Dog").inherit<Animal>().ctor<std::string>()
        .fun("learn", &Dog::learn).fun("count", &Dog::count);
    LuaClass<Puppy>(L, "Puppy").inherit<Dog>().ctor<std::string>();
    LuaClass<Rock>(L, "Rock").ctor<>().fun("weight", &Rock::weight);
    LuaModule(L, "m").fun("nameOf", nameOf).fun("speakOf", speakOf).fun("tricksOf", tricksOf)
        .fun("dogPtr", dogPtr).fun("asAnimal", asAnimal);
}

// ===================== C++ 继承 =====================
static void test_inherit() {
    printf("[C++ 继承: inherit<Base>()]\n");
    lua_State* L = newState();
    bindAnimals(L);
    check(lua_ok(L, "local d = Dog.new('rex'); assert(d:name() == 'rex')"), "派生类对象能调用基类的方法");
    check(lua_ok(L, "local d = Dog.new('rex'); assert(d:speak() == 'woof')"), "经基类方法调用虚函数, 走派生类的实现");
    check(lua_ok(L, "local d = Dog.new('rex'); d:learn(); d:learn(); assert(d:count() == 2)"), "派生类自己的方法照常工作");
    check(lua_ok(L, "local d = Dog.new('rex'); assert(d.legs == 4); d.legs = 3; assert(d.legs == 3)"), "基类的属性在派生类对象上可读可写");
    check(lua_ok(L, "assert(Dog.new('rex').KINGDOM == 'animalia')"), "基类的常量在派生类对象上可见");
    check(lua_ok(L, "assert(m.nameOf(Dog.new('rex')) == 'rex')"), "派生类对象传给 const Base& 参数");
    check(lua_ok(L, "assert(m.speakOf(Dog.new('rex')) == 'woof')"), "派生类对象传给 Base* 参数");
    check(lua_ok(L, "local p = Puppy.new('bit'); assert(p:name() == 'bit' and p:speak() == 'yip' and p:count() == 0)"), "两级继承: 方法逐级可见");
    check(lua_ok(L, "local p = Puppy.new('bit'); assert(m.nameOf(p) == 'bit' and m.tricksOf(p) == 0 and m.dogPtr(p) == 0)"), "两级继承: 可传给各级基类的参数");
    check(lua_ok(L, "local a = m.asAnimal(Dog.new('rex')); assert(a:name() == 'rex' and a.learn == nil)"), "以 Base* 返回的对象按 Base 包装, 看不到派生类方法");

    std::string e = lua_err(L, "return m.tricksOf(Animal.new('cat'))");
    check(has(e, "Dog expected"), "基类对象不能传给派生类参数: 「Dog expected」");
    e = lua_err(L, "return m.nameOf(Rock.new())");
    check(has(e, "Animal expected"), "无关的类仍被拒绝: 「Animal expected」");
    e = lua_err(L, "local d = Dog.new('rex'); d.nosuch = 1");
    check(has(e, "Non-existing property 'nosuch' of 'Dog'"), "派生类对象写不存在的属性: 报错里是派生类的名字");

    check(lua_ok(L, "local D = luaaa:extend(Dog, {}); local d = D:new('ext'); assert(m.nameOf(d) == 'ext' and d:name() == 'ext' and d.legs == 4)"),
          "派生类的 luaaa:extend 实例: 可传给基类参数, 可调基类方法与属性");
    lua_close(L);

    // 多重继承: 指针需要调整
    L = newState();
    LuaClass<PartA>(L, "PartA").fun("getA", &PartA::getA);
    LuaClass<PartB>(L, "PartB").fun("getB", &PartB::getB);
    LuaClass<Both>(L, "Both").inherit<PartA>().inherit<PartB>().ctor<>().fun("getC", &Both::getC);
    LuaModule(L, "m").fun("readB", readB).fun("readBPtr", readBPtr).fun("readA", readA);
    check(lua_ok(L, "local o = Both.new(); assert(o:getA() == 11 and o:getB() == 22 and o:getC() == 33)"), "多重继承: 两个基类的方法都拿到正确的 this");
    check(lua_ok(L, "local o = Both.new(); assert(m.readA(o) == 11 and m.readB(o) == 22 and m.readBPtr(o) == 22)"), "多重继承: 传给第二个基类的参数时指针已调整");
    lua_close(L);

    // 绑定继承来的成员函数, 而基类本身没有绑定
    L = newState();
    LuaClass<Dog>(L, "Dog").ctor<std::string>().fun("name", &Dog::name).get("legs", &Dog::getLegs).set("legs", &Dog::setLegs);
    check(lua_ok(L, "local d = Dog.new('solo'); assert(d:name() == 'solo'); d.legs = 2; assert(d.legs == 2)"),
          "基类未绑定时, 派生类上直接绑定继承来的成员函数 / 属性可用");
    lua_close(L);

    // 基类未绑定时 inherit 报错
    L = newState();
    lua_pushcfunction(L, [](lua_State* s) -> int { LuaClass<Dog>(s, "Dog").inherit<Animal>(); return 0; });
    const bool failed = lua_pcall(L, 0, 0, 0) != 0;
    check(failed && has(lua_tostring(L, -1), "not bound"), "基类未绑定就 inherit: 绑定时报错");
    lua_close(L);
}

// ===================== R9-4 / R9-3 =====================
static void test_r9_4() {
    printf("[R9-4: extend 实例传给 T* 参数; R9-3 默认行为]\n");
    lua_State* L = newState();
    bindAnimals(L);
    check(lua_ok(L, "local D = luaaa:extend(Dog, {}); local d = D:new('ext'); d:learn(); assert(m.dogPtr(d) == 1)"),
          "luaaa:extend 的实例可直接传给 T* 参数 (不必 luaaa:base)");
    check(lua_ok(L, "local D = luaaa:extend(Dog, {}); local d = D:new('ext'); assert(m.speakOf(d) == 'woof')"),
          "luaaa:extend 的实例可传给基类的 T* 参数");
    std::string e = lua_err(L, "return m.dogPtr(nil)");
    check(has(e, "Dog expected, got nil"), "默认 (LUAAA_NIL_AS_NULLPTR=0): nil 传给 T* 仍报错");
    e = lua_err(L, "return m.dogPtr({})");
    check(has(e, "Dog expected"), "不带对象的普通表传给 T* 报错");
    lua_close(L);
}

// ===================== R9-6 =====================
static int g_hookCalls = 0;
static std::string g_hookMsg;
static void* g_hookUd = nullptr;
static void onCallbackError(lua_State*, const char* msg, void* ud) { ++g_hookCalls; g_hookMsg = msg; g_hookUd = ud; }
static void throwingHook(lua_State*, const char* msg, void*) { throw std::runtime_error(std::string("callback failed: ") + msg); }
static int callIt(std::function<int(int)> f) { return f(7); }
static std::string callGuarded(std::function<int(int)> f) {
    try { f(7); } catch (const std::exception& e) { return e.what(); }
    return "no exception";
}
typedef int (*IntFn)(int);
static int callPtr(IntFn f) { return f ? f(7) : -1; }

static void test_r9_6() {
    printf("[R9-6: 回调错误钩子]\n");
    lua_State* L = newState();
    LuaModule(L, "m").fun("callIt", callIt).fun("callGuarded", callGuarded).fun("callPtr", callPtr);
    static int marker = 0;

    g_hookCalls = 0;
    check(lua_ok(L, "assert(m.callIt(function(x) error('cb boom') end) == 0)"), "未注册钩子: 回调出错返回 RET(), 行为不变");
    check(g_hookCalls == 0, "未注册钩子: 不调用任何钩子");

    setCallbackErrorHandler(L, onCallbackError, &marker);
    check(lua_ok(L, "assert(m.callIt(function(x) error('cb boom') end) == 0)"), "注册钩子后: 回调仍返回 RET()");
    check(g_hookCalls == 1 && has(g_hookMsg, "cb boom") && g_hookUd == &marker, "钩子收到错误信息与 userdata");
    check(lua_ok(L, "assert(m.callIt(function(x) return 'not a number' end) == 0)"), "回调返回值无法转换: 返回 RET()");
    check(g_hookCalls == 2 && has(g_hookMsg, "number expected"), "返回值转换失败也经过钩子");
    check(lua_ok(L, "assert(m.callPtr(function(x) error('ptr boom') end) == 0)"), "函数指针回调出错: 返回 RET()");
    check(g_hookCalls == 3 && has(g_hookMsg, "ptr boom"), "函数指针回调也经过钩子");
    check(lua_ok(L, "assert(m.callIt(function(x) return x + 1 end) == 8)"), "正常回调不触发钩子");
    check(g_hookCalls == 3, "钩子调用次数未变");
    check(lua_gettop(L) == 0, "多次失败后栈平衡");

    setCallbackErrorHandler(L, nullptr);
    check(lua_ok(L, "assert(m.callIt(function(x) error('again') end) == 0)") && g_hookCalls == 3, "传 nullptr 可移除钩子");

#if LUAAA_HAS_EXCEPTIONS
    setCallbackErrorHandler(L, throwingHook);
    check(lua_ok(L, "local s = m.callGuarded(function(x) error('to cpp') end); assert(s:find('callback failed') and s:find('to cpp'), s)"),
          "钩子里抛 C++ 异常: 调用回调的 C++ 代码能 catch 到");
    check(lua_gettop(L) == 0, "钩子抛异常后栈仍平衡");
#endif
    lua_close(L);
}

// ===================== R9-7 =====================
static int g_modVal = 5;
static int modGet() { return g_modVal; }
static void modSet(int v) { g_modVal = v; }
static int addOne(int x) { return x + 1; }

static void test_r9_7() {
    printf("[R9-7: LuaModule 不改动 _G; 模块名]\n");
    lua_State* L = newState();
    LuaModule(L).fun("addOne", addOne).def("ANSWER", 42);
    check(lua_ok(L, "assert(addOne(1) == 2 and ANSWER == 42)"), "默认模块: 函数与常量注册为全局");
    check(lua_ok(L, "assert(getmetatable(_G) == nil)"), "默认模块: _G 没有被挂 metatable");
    check(lua_ok(L, "assert(rawget(_G, '__name') == nil)"), "默认模块: 没有多出全局 __name");

    LuaModule(L, "cfg").fun("addOne", addOne);
    check(lua_ok(L, "assert(getmetatable(cfg) == nil and cfg.__name == nil and cfg.addOne(2) == 3)"), "只有函数的模块: 没有 metatable, 没有 __name 字段");
    LuaModule(L, "cfg").get("val", modGet).set("val", modSet);
    check(lua_ok(L, "assert(getmetatable(cfg) ~= nil); assert(cfg.val == 5); cfg.val = 9; assert(cfg.val == 9)"), "注册 property 后才挂 metatable, 属性可读写");
    check(lua_ok(L, "for k in pairs(cfg) do assert(k == 'addOne', k) end"), "属性的 getter / setter 不出现在模块表里");
    LuaModule(L, "cfg").get("ro", modGet);
    std::string e = lua_err(L, "cfg.ro = 1");
    check(has(e, "Read-Only property 'ro' of 'cfg'"), "只读属性的报错里带模块名 (名字不再存模块表)");

    LuaModule(L, "game.util").fun("addOne", addOne);
    check(lua_ok(L, "assert(type(game) == 'table' and game.util.addOne(4) == 5)"), "模块名 \"a.b\": 建成嵌套表 _G.a.b");
    check(lua_ok(L, "assert(rawget(_G, 'game.util') == nil)"), "不再产生名为 \"a.b\" 的全局");
    check(lua_ok(L, "assert(package.loaded['game.util'] == game.util and require('game.util') == game.util)"), "模块写入 package.loaded, 可以 require");
    check(lua_ok(L, "assert(package.loaded.cfg == cfg and package.loaded._G == _G)"), "顶层模块同样写入 package.loaded; _G 不受影响");

    lua_pushcfunction(L, [](lua_State* s) -> int { LuaModule(s, "ANSWER.sub").fun("f", addOne); return 0; });
    const bool failed = lua_pcall(L, 0, 0, 0) != 0;
    check(failed && has(lua_tostring(L, -1), "name conflict"), "模块名路径上已有非表的值: 报 name conflict");
    lua_close(L);
}

// ===================== R9-8 =====================
struct Box {
    int v;
    explicit Box(int x) : v(x) {}
    int get() const { return v; }
    void set(int x) { v = x; }
    std::string str() const { return "Box(" + std::to_string(v) + ")"; }
    static Box* create(int x) { return new Box(x * 2); }
    static int twice(int x) { return x * 2; }
};

static void test_r9_8() {
    printf("[R9-8: metatable 对脚本隐藏; 静态函数与类常量]\n");
    lua_State* L = newState();
    LuaClass<Box>(L, "Box").ctor<int>().fun("get", &Box::get).fun("__tostring", &Box::str)
        .staticFun("twice", &Box::twice).staticFun("sum", [](int a, int b) { return a + b; })
        .constant("MAX", 99).constant("KIND", "box").def("TAG", 7);
    check(lua_ok(L, "assert(getmetatable(Box.new(1)) == false)"), "getmetatable(obj) 返回 false");
    check(lua_ok(L, "local o = Box.new(1); assert(o.__gc == nil and o.__index == nil and o.__newindex == nil)"), "obj.__gc / __index / __newindex 取不到");
    check(lua_ok(L, "local o = Box.new(1); assert(not pcall(setmetatable, o, {}))"), "脚本不能给对象换 metatable");
    check(lua_ok(L, "local o = Box.new(3); assert(o:get() == 3 and tostring(o) == 'Box(3)' and o.TAG == 7)"), "方法 / 元方法 / 实例常量照常工作");
    std::string e = lua_err(L, "local o = Box.new(1); o.get = function() return 0 end");
    check(has(e, "Non-existing property 'get'"), "脚本不能在对象上改写方法");
    check(lua_ok(L, "assert(Box.twice(4) == 8 and Box.sum(2, 3) == 5 and Box.MAX == 99 and Box.KIND == 'box')"), "staticFun / constant 挂在类表上");
    check(lua_ok(L, "local o = Box.new(1); assert(o.twice == nil and o.MAX == nil)"), "类表上的静态成员不出现在对象上");

    // extend 实例在创建之后, C++ 侧再给类加 property: 旧实例也要看得到
    check(lua_ok(L, "D = luaaa:extend(Box, {}); d1 = D:new(5); assert(d1:get() == 5)"), "luaaa:extend 的实例照常工作");
    LuaClass<Box>(L, "Box").get("v", &Box::get).set("v", &Box::set);
    check(lua_ok(L, "assert(d1.v == 5); d1.v = 6; assert(d1.v == 6 and d1:get() == 6)"), "类在之后才注册 property: 已有的 extend 实例同样生效");
    check(lua_ok(L, "assert(getmetatable(d1) ~= false)"), "luaaa:extend 的实例是 lua 表, metatable 不隐藏");
    lua_close(L);
}

// ===================== 智能指针 =====================
struct Res {
    static int alive;
    int id;
    explicit Res(int i) : id(i) { ++alive; }
    virtual ~Res() { --alive; }
    int getId() const { return id; }
};
int Res::alive = 0;
struct SubRes : Res { explicit SubRes(int i) : Res(i) {} int extra() const { return id * 10; } };

static std::shared_ptr<Res> g_shared;
static std::shared_ptr<Res> makeShared(int id) { return std::make_shared<Res>(id); }
static std::shared_ptr<Res> getGlobal() { return g_shared; }
static std::shared_ptr<Res> noShared() { return std::shared_ptr<Res>(); }
static long useCount(std::shared_ptr<Res> p) { return p ? p.use_count() : 0; }
static void keep(std::shared_ptr<Res> p) { g_shared = p; }
static int idOfRef(const Res& r) { return r.id; }
static std::shared_ptr<SubRes> makeSub(int id) { return std::make_shared<SubRes>(id); }
static bool isEmptyShared(std::shared_ptr<Res> p) { return !p; }

static std::unique_ptr<Res> g_unique;
static std::unique_ptr<Res> makeUnique(int id) { return std::unique_ptr<Res>(new Res(id)); }
static std::unique_ptr<Res> noUnique() { return std::unique_ptr<Res>(); }
static int takeUnique(std::unique_ptr<Res> p) { g_unique = std::move(p); return g_unique ? g_unique->id : -1; }

static void test_smart() {
    printf("[智能指针: std::shared_ptr / std::unique_ptr]\n");
    lua_State* L = newState();
    LuaClass<Res>(L, "Res").ctor<int>().ctor("shared", &makeShared).fun("getId", &Res::getId);
    LuaClass<SubRes>(L, "SubRes").inherit<Res>().fun("extra", &SubRes::extra);
    LuaModule(L, "m").fun("makeShared", makeShared).fun("getGlobal", getGlobal).fun("noShared", noShared)
        .fun("useCount", useCount).fun("keep", keep).fun("idOfRef", idOfRef).fun("makeSub", makeSub)
        .fun("isEmptyShared", isEmptyShared)
        .fun("makeUnique", makeUnique).fun("noUnique", noUnique).fun("takeUnique", takeUnique);

    Res::alive = 0;
    check(lua_ok(L, "s = m.makeShared(7); assert(s:getId() == 7 and m.idOfRef(s) == 7)"), "返回 shared_ptr: 对象可调方法, 可传给 T& 参数");
    check(Res::alive == 1, "shared_ptr 返回的对象存活");
    check(lua_ok(L, "assert(m.useCount(s) == 2)"), "传给 shared_ptr 参数: 与 lua 共享所有权 (use_count=2)");
    check(lua_ok(L, "m.keep(s); s = nil; collectgarbage(); collectgarbage()"), "C++ 保留一份后 lua 侧回收");
    check(Res::alive == 1 && g_shared && g_shared.use_count() == 1 && g_shared->id == 7, "lua 回收只释放自己那份引用, 对象仍由 C++ 持有");
    check(lua_ok(L, "local g = m.getGlobal(); assert(g:getId() == 7 and m.useCount(g) == 3)"), "同一对象再次返回给 lua: 引用计数累加");
    g_shared.reset();
    lua_ok(L, "collectgarbage(); collectgarbage()");
    check(Res::alive == 0, "最后一份引用释放后对象析构");

    check(lua_ok(L, "assert(m.noShared() == nil and m.isEmptyShared(nil) and m.isEmptyShared())"), "空 shared_ptr <-> nil (含缺省实参)");
    std::string e = lua_err(L, "return m.useCount(Res.new(1))");
    check(has(e, "not held by a shared_ptr"), "不是由 shared_ptr 持有的对象传给 shared_ptr 参数: 报错");
    check(lua_ok(L, "local r = Res.shared(9); assert(r:getId() == 9 and m.useCount(r) == 2)"), "ctor 接受返回 shared_ptr 的工厂函数");
    check(lua_ok(L, "local d = m.makeSub(4); assert(d:extra() == 40 and d:getId() == 4 and m.useCount(d) == 2)"),
          "shared_ptr<Derived> 的对象可传给 shared_ptr<Base> 参数");
    lua_ok(L, "collectgarbage(); collectgarbage()");
    check(Res::alive == 0, "shared 对象全部回收");

    check(lua_ok(L, "u = m.makeUnique(3); assert(u:getId() == 3)") && Res::alive == 1, "返回 unique_ptr: 所有权交给 lua");
    check(lua_ok(L, "u = nil; collectgarbage(); collectgarbage()") && Res::alive == 0, "lua 回收时 unique_ptr 的对象被删除");
    check(lua_ok(L, "u = m.makeUnique(5); assert(m.takeUnique(u) == 5)") && g_unique && Res::alive == 1, "传给 unique_ptr 参数: 所有权交回 C++");
    e = lua_err(L, "return u:getId()");
    check(has(e, "invalid user data"), "所有权取走后 lua 侧对象失效");
    check(lua_ok(L, "u = nil; collectgarbage(); collectgarbage()") && Res::alive == 1, "失效对象被回收时不会二次删除");
    g_unique.reset();
    check(Res::alive == 0, "C++ 侧释放后对象析构");
    check(lua_ok(L, "assert(m.noUnique() == nil and m.takeUnique(nil) == -1)"), "空 unique_ptr <-> nil");
    e = lua_err(L, "return m.takeUnique(Res.new(1))");
    check(has(e, "not owned by a matching unique_ptr"), "不是 unique_ptr 持有的对象不能被取走");
    g_unique.reset();
    lua_close(L);
    check(Res::alive == 0, "关闭 state 后无泄漏");
}

// ===================== 只能移动的返回类型 =====================
struct MoveOnly {
    static int alive;
    int v;
    explicit MoveOnly(int x) : v(x) { ++alive; }
    MoveOnly(MoveOnly&& o) noexcept : v(o.v) { o.v = -1; ++alive; }
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    ~MoveOnly() { --alive; }
    int get() const { return v; }
};
int MoveOnly::alive = 0;
static MoveOnly makeMoveOnly(int x) { return MoveOnly(x); }

static void test_move_only() {
    printf("[只能移动的返回类型]\n");
    lua_State* L = newState();
    LuaClass<MoveOnly>(L, "MoveOnly").fun("get", &MoveOnly::get).staticFun("make", makeMoveOnly);
    LuaModule(L, "m").fun("make", makeMoveOnly).fun("makeLambda", [](int x) { return MoveOnly(x + 1); });
    check(lua_ok(L, "local o = m.make(5); assert(o:get() == 5)"), "按值返回拷贝构造被删除的类 (移动进 lua)");
    check(lua_ok(L, "assert(m.makeLambda(5):get() == 6 and MoveOnly.make(8):get() == 8)"), "lambda / staticFun 返回只能移动的类");
    lua_close(L);
    check(MoveOnly::alive == 0, "移动进 lua 的对象在回收时析构, 计数平衡");
}

// ===================== 重载 =====================
struct Calc {
    int base;
    Calc() : base(0) {}
    explicit Calc(int b) : base(b) {}
    Calc(int a, int b) : base(a + b) {}
    explicit Calc(const std::string& s) : base((int)s.size()) {}
    int add(int x) { return base + x; }
    int add2(int x, int y) { return base + x + y; }
    std::string addStr(const std::string& s) { return s + "!"; }
    double addF(double x) { return base + x; }
    int getBase() const { return base; }
};
static std::string kindInt(int) { return "int"; }
static std::string kindDouble(double) { return "double"; }
static std::string kindStr(const std::string&) { return "string"; }
static std::string kindBool(bool) { return "bool"; }
static std::string kindVec(std::vector<int> v) { return "table" + std::to_string(v.size()); }
static std::string kindCalc(const Calc&) { return "Calc"; }
static std::string kindBox(Box*) { return "Box"; }
static std::string kindFn(std::function<int(int)> f) { return "function" + std::to_string(f(1)); }
static std::string kindNone() { return "none"; }
static std::string kindTwo(int, int) { return "two"; }

static void test_overload() {
    printf("[重载分发: overload / overloadCtor]\n");
    lua_State* L = newState();
    LuaClass<Calc>(L, "Calc").overloadCtor<>().overloadCtor<int>().overloadCtor<int, int>().overloadCtor<std::string>()
        .overload("add", &Calc::add).overload("add", &Calc::add2).overload("add", &Calc::addStr).overload("add", &Calc::addF)
        .fun("getBase", &Calc::getBase);
    LuaClass<Box>(L, "Box").ctor<int>();
    LuaModule(L, "m").overload("kind", kindInt).overload("kind", kindDouble).overload("kind", kindStr)
        .overload("kind", kindBool).overload("kind", kindVec).overload("kind", kindCalc).overload("kind", kindBox)
        .overload("kind", kindFn).overload("kind", kindNone).overload("kind", kindTwo)
        .overload("kind", [](const std::string& a, int b) { return a + std::to_string(b); });

    check(lua_ok(L, "assert(Calc.new():getBase() == 0 and Calc.new(3):getBase() == 3 and Calc.new(3, 4):getBase() == 7 and Calc.new('abcd'):getBase() == 4)"),
          "构造函数按参数个数与类型分发");
    check(lua_ok(L, "local c = Calc.new(10); assert(c:add(1) == 11 and c:add(1, 2) == 13 and c:add('x') == 'x!' and c:add(0.5) == 10.5)"),
          "成员函数按参数个数与类型分发 (含 int / double 区分)");
    check(lua_ok(L, "assert(m.kind(1) == 'int' and m.kind(1.5) == 'double' and m.kind('s') == 'string' and m.kind(true) == 'bool')"),
          "模块函数按 number / string / boolean 分发");
    check(lua_ok(L, "assert(m.kind({1, 2, 3}) == 'table3' and m.kind(Calc.new()) == 'Calc' and m.kind(Box.new(1)) == 'Box')"),
          "按 table / 不同的绑定类分发");
    check(lua_ok(L, "assert(m.kind(function(x) return x + 4 end) == 'function5' and m.kind() == 'none' and m.kind(1, 2) == 'two' and m.kind('n', 3) == 'n3')"),
          "按 function / 零参数 / 两个参数分发 (含 lambda)");
    std::string e = lua_err(L, "return m.kind(1, 2, 3)");
    check(has(e, "no overload of 'kind' matches"), "没有匹配的重载: 报错带函数名");
    e = lua_err(L, "return Calc.new(true)");
    check(has(e, "no overload of 'new' matches"), "构造函数没有匹配的重载: 报错");
    lua_close(L);
}

// ===================== C++17 =====================
#if __cplusplus >= 201703L
static std::optional<int> optHalf(int x) { return (x % 2 == 0) ? std::optional<int>(x / 2) : std::nullopt; }
static int optOr(std::optional<int> v, int d) { return v.value_or(d); }
static std::string optName(std::optional<std::string> s) { return s ? *s : "<none>"; }
static int optTail(int a, std::optional<int> b) { return a + b.value_or(100); }
static size_t svLen(std::string_view s) { return s.size(); }
static std::string_view svHello() { return std::string_view("he\0llo", 6); }
static std::string optKindA(std::optional<int>) { return "optint"; }
static std::string optKindB(std::string_view) { return "sv"; }

static void test_cpp17() {
    printf("[C++17: std::optional / std::string_view]\n");
    lua_State* L = newState();
    LuaModule(L, "m").fun("optHalf", optHalf).fun("optOr", optOr).fun("optName", optName).fun("optTail", optTail)
        .fun("svLen", svLen).fun("svHello", svHello).overload("k", optKindA).overload("k", optKindB);
    check(lua_ok(L, "assert(m.optHalf(8) == 4 and m.optHalf(7) == nil)"), "optional 返回: 有值 -> 值, nullopt -> nil");
    check(lua_ok(L, "assert(m.optOr(5, 9) == 5 and m.optOr(nil, 9) == 9)"), "optional 参数: nil -> nullopt");
    check(lua_ok(L, "assert(m.optName('x') == 'x' and m.optName(nil) == '<none>' and m.optName() == '<none>')"), "optional<string> 参数, 缺省实参 -> nullopt");
    check(lua_ok(L, "assert(m.optTail(1, 2) == 3 and m.optTail(1) == 101)"), "末尾的 optional 参数可以省略");
    std::string e = lua_err(L, "return m.optOr('abc', 1)");
    check(has(e, "number expected"), "optional<int> 收到非数字: 报类型错误 (不当成 nullopt)");
    check(lua_ok(L, "assert(m.svLen('a' .. string.char(0) .. 'bc') == 4 and #m.svHello() == 6)"), "string_view 双向保留嵌入的 NUL");
    e = lua_err(L, "return m.svLen(12)");
    check(has(e, "string expected"), "string_view 只接受字符串");
    check(lua_ok(L, "assert(m.k(1) == 'optint' and m.k() == 'optint' and m.k('s') == 'sv')"), "重载分发识别 optional / string_view");
    lua_close(L);
}
#endif

int main() {
    test_r9_2();
    test_inherit();
    test_r9_4();
    test_r9_6();
    test_r9_7();
    test_r9_8();
    test_smart();
    test_move_only();
    test_overload();
#if __cplusplus >= 201703L
    test_cpp17();
#endif
    printf("\n=== review10: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

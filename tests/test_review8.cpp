// 第八轮评审修复回归测试 (中危 8 项):
//   M1  T& / const T& 返回值是别名而非拷贝 (put_ref 的 remove_cv 漏 ::type);
//       const bool& / const std::string& 仍按值; tuple<类,...> 元素是拥有拷贝
//   M2  luaaa:extend: 每次 new 得到独立实例; 多级 extend; 原型链; 属性/元方法/GC
//   M3  与类同名的 LuaModule 不再破坏类的 metatable
//   M4  顺序容器/集合按数组下标读取 (顺序确定, 不混入哈希键的值)
//   M5  整数入参范围检查 (窄化不再静默回绕)
//   M6  __index/__newindex 不再把数字键原地转成字符串
//   M7  参数转换失败时已构造的实参被正常析构 (不再被 longjmp 跳过)
//   M8  C++ 异常翻译为 lua 错误后不残留「已捕获异常」
//
// 用法: 不带参数跑全部; 带用例名 (m1..m8) 只跑该组。
#include "luaaa.hpp"

#include <cstdio>
#include <cstring>
#include <deque>
#include <exception>
#include <forward_list>
#include <functional>
#include <list>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
    fflush(stdout);
}

static bool lua_ok(lua_State* L, const char* code) {
    if (luaL_dostring(L, code) != 0) {
        printf("    (lua err) %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

// 期望这段代码抛 lua 错误
static bool lua_raises(lua_State* L, const char* code) {
    if (luaL_dostring(L, code) != 0) { lua_pop(L, 1); return true; }
    return false;
}

static lua_State* newState() { lua_State* L = luaL_newstate(); luaL_openlibs(L); return L; }

// ===================== 被测类型 =====================
struct Box {
    static int s_ctor, s_dtor, s_copies;
    int v;
    Box() : v(0) { ++s_ctor; }
    explicit Box(int x) : v(x) { ++s_ctor; }
    Box(const Box& o) : v(o.v) { ++s_ctor; ++s_copies; }
    ~Box() { ++s_dtor; v = -999; }
    int get() const { return v; }
    void set(int x) { v = x; }
    Box& self() { return *this; }
    std::string str() const { return "Box(" + std::to_string(v) + ")"; }
    static void reset() { s_ctor = s_dtor = s_copies = 0; }
};
int Box::s_ctor = 0, Box::s_dtor = 0, Box::s_copies = 0;

struct Holder {   // ctor<Box, int>: 构造函数路径的实参泄漏探测
    int v;
    Holder(Box b, int n) : v(b.v + n) {}
    int get() const { return v; }
    int add(Box b, int n) { return v + b.v + n; }
};

static Box g_box(5);
static bool g_flag = false;
static std::string g_name = "luaaa";
static Box& refBox() { return g_box; }
static const Box& crefBox() { return g_box; }
static const bool& flagRef() { return g_flag; }
static const std::string& nameRef() { return g_name; }
static std::tuple<Box, int> makeTuple() { return std::tuple<Box, int>(Box(42), 1); }
static int boxGet(const Box& b) { return b.get(); }
static void mutateVia(std::function<void(Box&)> f) { f(g_box); }

static std::string joinVec(std::vector<std::string> v) { std::string s; for (auto& x : v) s += x; return s; }
static std::string joinList(std::list<std::string> v) { std::string s; for (auto& x : v) s += x; return s; }
static std::string joinDeque(std::deque<std::string> v) { std::string s; for (auto& x : v) s += x; return s; }
static std::string joinFwd(std::forward_list<std::string> v) { std::string s; for (auto& x : v) s += x; return s; }
static int setSize(std::set<std::string> v) { return (int)v.size(); }
static int sumNested(std::vector<std::vector<int>> v) { int s = 0; for (auto& r : v) for (int x : r) s += x; return s; }

static unsigned char u8(unsigned char v) { return v; }
static signed char i8(signed char v) { return v; }
static int i32(int v) { return v; }
static unsigned int u32(unsigned int v) { return v; }
static unsigned long long u64(unsigned long long v) { return v; }
static long long i64(long long v) { return v; }
static bool u64IsMax(unsigned long long v) { return v == ~0ULL; }

static int keyTypeIndex(lua_State* L) { lua_pushstring(L, lua_typename(L, lua_type(L, 2))); return 1; }
static int g_lastNewindexKeyType = LUA_TNONE;
static int keyTypeNewindex(lua_State* L) { g_lastNewindexKeyType = lua_type(L, 2); return 0; }

static void takeBoxAndInt(Box b, int n) { (void)b; (void)n; }
static int takeVecAndInt(std::vector<Box> v, int n) { return (int)v.size() + n; }
static int rawTakesInt(lua_State* L) { lua_pushinteger(L, LuaStack<int>::get(L, 1) + 1); return 1; }
static void raisesLuaError(lua_State* L) { luaL_error(L, "raised from inside a bound function"); }

static void throwStd() { throw std::runtime_error("boom"); }
static void throwInt() { throw 42; }
struct Thrower {
    explicit Thrower(int x) { if (x < 0) throw std::invalid_argument("negative"); }
    int badGet() const { throw std::runtime_error("getter boom"); }
    void badSet(int) { throw std::runtime_error("setter boom"); }
};

// ===================== M1 =====================
static void test_m1() {
    printf("[M1: 引用返回值是别名]\n");
    lua_State* L = newState();
    LuaClass<Box>(L, "Box").ctor<int>().fun("get", &Box::get).fun("set", &Box::set).fun("self", &Box::self);
    LuaModule(L, "m").fun("refBox", refBox).fun("crefBox", crefBox).fun("flagRef", flagRef)
        .fun("nameRef", nameRef).fun("makeTuple", makeTuple).fun("mutateVia", mutateVia);

    g_box.v = 5; Box::reset();
    lua_ok(L, "m.refBox():set(100)");
    check(g_box.v == 100 && Box::s_copies == 0, "Box& 返回: 经 lua 修改作用到原对象, 无拷贝");

    g_box.v = 5; Box::reset();
    lua_ok(L, "m.crefBox():set(77)");
    check(g_box.v == 77 && Box::s_copies == 0, "const Box& 返回: 同为别名, 无拷贝");

    check(lua_ok(L, "local b = Box.new(1); b:self():set(9); assert(b:get() == 9)"), "成员函数返回 Box&: 别名到自身");

    g_flag = false;
    check(lua_ok(L, "assert(m.flagRef() == false)"), "const bool& 返回 false 仍是 false (不被当作指针)");
    g_flag = true;
    check(lua_ok(L, "assert(m.flagRef() == true)"), "const bool& 返回 true");
    check(lua_ok(L, "assert(m.nameRef() == 'luaaa')"), "const std::string& 返回仍是字符串");

    check(lua_ok(L, "local t = m.makeTuple(); collectgarbage(); assert(t[1]:get() == 42 and t[2] == 1)"),
          "tuple<Box,int> 返回: 元素是拥有拷贝 (不是指向已销毁临时量的别名)");

    g_box.v = 5;
    lua_ok(L, "m.mutateVia(function(b) b:set(31) end)");
    check(g_box.v == 31, "回调的 Box& 实参是别名: lua 里的修改回到 C++");
    lua_close(L);
}

// ===================== M2 =====================
static void test_m2() {
    printf("[M2: luaaa:extend 实例模型]\n");
    Box::reset();
    lua_State* L = newState();
    LuaClass<Box>(L, "Box").ctor<int>().fun("get", &Box::get).fun("set", &Box::set)
        .fun("__tostring", &Box::str).get("v", &Box::get).set("v", &Box::set);
    LuaModule(L, "m").fun("boxGet", boxGet);

    check(lua_ok(L, "D = luaaa:extend(Box, { n = 0 }); a = D:new(1); b = D:new(2); "
                    "assert(a ~= b, 'distinct instances'); assert(a ~= D and b ~= D, 'instance is not the class table'); "
                    "assert(a:get() == 1 and b:get() == 2)"), "每次 new 得到独立实例, 各自持有自己的 C++ 对象");
    check(lua_ok(L, "a.n = 5; assert(a.n == 5 and b.n == 0 and D.n == 0)"), "类表字段是默认值, 写入只影响该实例");
    check(lua_ok(L, "function D:double() return self:get() * 2 end; assert(a:double() == 2 and b:double() == 4)"),
          "实例创建后才加到类表的 lua 方法也可见 (原型链)");
    check(lua_ok(L, "function D:get() return luaaa:base(self):get() + 1000 end; assert(a:get() == 1001); D.get = nil; assert(a:get() == 1)"),
          "lua 侧覆盖 C++ 方法, 并经 luaaa:base 调回 C++");
    check(lua_ok(L, "assert(a.v == 1); a.v = 11; assert(a.v == 11 and a:get() == 11 and b.v == 2)"), "实例上的 C++ property 读写正常");
    check(lua_ok(L, "assert(m.boxGet(b) == 2)"), "实例可作为 const Box& 实参传给 C++");
    check(lua_ok(L, "assert(tostring(b) == 'Box(2)')"), "类的 __tostring 元方法对实例生效");
    check(lua_ok(L, "E = luaaa:extend(D, { tag = 'e' }); function E:triple() return self:get() * 3 end; "
                    "e = E:new(7); assert(e:get() == 7 and e:triple() == 21 and e:double() == 14 and e.tag == 'e' and e.n == 0)"),
          "两级 extend: 构造、本级方法、上级 lua 方法与字段都可用");
    check(lua_ok(L, "assert(m.boxGet(e) == 7 and tostring(e) == 'Box(7)')"), "两级 extend 实例同样可传给 C++ / 走元方法");
    check(lua_ok(L, "a, b, e = nil, nil, nil; collectgarbage(); collectgarbage()"), "回收实例");
    lua_close(L);
    check(Box::s_ctor == 3 && Box::s_dtor == 3, "三个实例的 C++ 对象各析构一次");
}

// ===================== M3 =====================
static void test_m3() {
    printf("[M3: 与类同名的 LuaModule]\n");
    {
        lua_State* L = newState();
        LuaClass<Box>(L, "Box").ctor<int>().fun("get", &Box::get).get("v", &Box::get).set("v", &Box::set);
        LuaModule(L, "Box").fun("make7", []() { return 7; }).def("MAX", 9);
        check(lua_ok(L, "assert(Box.make7() == 7 and Box.MAX == 9)"), "类表上的模块函数/常量可用 (Box.make7 / Box.MAX)");
        check(lua_ok(L, "local o = Box.new(5); assert(o:get() == 5); o.v = 6; assert(o.v == 6)"), "类的方法与 property 不受影响");
        lua_close(L);
    }
    {
        lua_State* L = newState();
        LuaModule(L, "Box").def("MAX", 9);
        LuaClass<Box>(L, "Box").ctor<int>().fun("get", &Box::get);
        check(lua_ok(L, "assert(Box.MAX == 9 and Box.new(3):get() == 3)"), "反过来先模块后类也可用");
        lua_close(L);
    }
    {
        lua_State* L = newState();
        LuaModule(L, "A").get("x", []() { return 1; });
        LuaModule(L, "B").get("x", []() { return 2; });
        check(lua_ok(L, "assert(A.x == 1 and B.x == 2)"), "不同模块的同名 property 互不串扰");
        check(lua_raises(L, "A.x = 3"), "模块只读 property 写入仍报错");
        lua_close(L);
    }
}

// ===================== M4 =====================
static void test_m4() {
    printf("[M4: 顺序容器按数组下标读取]\n");
    lua_State* L = newState();
    LuaModule(L, "m").fun("joinVec", joinVec).fun("joinList", joinList).fun("joinDeque", joinDeque)
        .fun("joinFwd", joinFwd).fun("setSize", setSize).fun("sumNested", sumNested);
    check(lua_ok(L, "assert(m.joinVec({[1]='a',[2]='b',[3]='c',[4]='d'}) == 'abcd')"), "vector: 显式下标表按 1..n 顺序");
    check(lua_ok(L, "assert(m.joinList({[1]='a',[2]='b',[3]='c',[4]='d'}) == 'abcd')"), "list: 同上");
    check(lua_ok(L, "assert(m.joinDeque({[1]='a',[2]='b',[3]='c',[4]='d'}) == 'abcd')"), "deque: 同上");
    check(lua_ok(L, "assert(m.joinFwd({[1]='a',[2]='b',[3]='c',[4]='d'}) == 'abcd')"), "forward_list: 同上");
    check(lua_ok(L, "assert(m.joinVec({'a','b',x='HASH',y='KEYS'}) == 'ab')"), "vector: 不混入哈希键的值");
    check(lua_ok(L, "assert(m.setSize({'a','b',x='c'}) == 2)"), "set: 不混入哈希键的值");
    check(lua_ok(L, "assert(m.joinVec({}) == '' and m.sumNested({{1,2},{3},{}}) == 6)"), "空表 / 嵌套 vector 回归");
    lua_close(L);
}

// ===================== M5 =====================
static void test_m5() {
    printf("[M5: 整数入参范围检查]\n");
    lua_State* L = newState();
    LuaModule(L, "m").fun("u8", u8).fun("i8", i8).fun("i32", i32).fun("u32", u32).fun("u64", u64).fun("i64", i64).fun("u64IsMax", u64IsMax);
    check(lua_raises(L, "return m.u8(300)"), "u8(300) 报错 (修复前静默得到 44)");
    check(lua_raises(L, "return m.u8(-1)"), "u8(-1) 报错");
    check(lua_raises(L, "return m.i8(-129)"), "i8(-129) 报错");
    check(lua_raises(L, "return m.i32(1099511627776)"), "i32(2^40) 报错 (修复前静默得到 0)");
    check(lua_raises(L, "return m.u32(-1)"), "u32(-1) 报错");
    check(lua_ok(L, "assert(m.u8(0) == 0 and m.u8(255) == 255 and m.i8(-128) == -128 and m.i8(127) == 127)"), "边界值正常通过");
    check(lua_ok(L, "assert(m.i32(-2147483648) == -2147483648 and m.i32(2147483647) == 2147483647 and m.u32(4294967295) == 4294967295)"),
          "32 位边界值正常通过");
    check(lua_ok(L, "assert(m.i64(1099511627776) == 1099511627776)"), "64 位有符号正常通过");
    check(lua_ok(L, "assert(m.u64IsMax(-1) == true)"), "u64 参数收到 -1 -> 按位解释为 2^64-1 (64 位无符号保留此语义, 文档化)");
    check(lua_ok(L, "assert(m.u64(5) == 5)"), "u64 普通值往返");
    lua_close(L);
}

// ===================== M6 =====================
static void test_m6() {
    printf("[M6: 数字键不被转成字符串]\n");
    lua_State* L = newState();
    LuaModule(L).def("answer", 42);
    check(lua_ok(L, "_G[1] = 'x'; assert(rawget(_G, 1) == 'x' and rawget(_G, '1') == nil); _G[1] = nil"), "_G[1]='x' 存到数字键 1");
    check(lua_ok(L, "_G[true] = 't'; assert(rawget(_G, true) == 't'); _G[true] = nil"), "_G[true]='t' 不被静默丢弃");
    check(lua_ok(L, "assert(answer == 42); newGlobal = 3; assert(newGlobal == 3)"), "字符串键 (普通全局变量) 回归");

    LuaModule(L, "M").def("k", 1).get("ro", []() { return 7; });
    check(lua_ok(L, "M[1] = 'a'; M[2] = 'b'; assert(#M == 2 and rawget(M, 1) == 'a' and M[2] == 'b')"), "模块表的数组部分可正常使用");
    check(lua_ok(L, "assert(M.ro == 7 and M.k == 1)") && lua_raises(L, "M.ro = 1"), "模块 property 回归 (只读仍报错)");

    LuaClass<Box>(L, "Box").ctor<int>().fun("get", &Box::get).get("v", &Box::get)
        .fun("__index", keyTypeIndex).fun("__newindex", keyTypeNewindex);
    check(lua_ok(L, "local o = Box.new(1); assert(o[2] == 'number', 'got ' .. tostring(o[2])); assert(o.nokey == 'string'); assert(o.v == 1 and o:get() == 1)"),
          "用户 __index 收到原始的数字键 (字符串键与 property 回归)");
    g_lastNewindexKeyType = LUA_TNONE;
    lua_ok(L, "local o = Box.new(1); o[3] = 'z'");
    check(g_lastNewindexKeyType == LUA_TNUMBER, "用户 __newindex 收到原始的数字键");
    check(lua_ok(L, "local o = Box.new(1); assert(o[{}] == nil)"), "非字符串/数字键读取仍得 nil");

    check(lua_ok(L, "local D = luaaa:extend(Box, {}); local d = D:new(3); d[1] = 'first'; "
                    "assert(d[1] == 'first' and rawget(d, 1) == 'first' and rawget(d, '1') == nil)"), "extend 实例的数组部分可正常使用");
    lua_close(L);
}

// ===================== M7 =====================
static void test_m7() {
    printf("[M7: 参数转换失败时实参正常析构]\n");
    {
        Box::reset();
        lua_State* L = newState();
        LuaClass<Box>(L, "Box").ctor<int>().fun("get", &Box::get);
        LuaClass<Holder>(L, "Holder").ctor<Box, int>().fun("get", &Holder::get).fun("add", &Holder::add);
        LuaModule(L, "m").fun("takeBoxAndInt", takeBoxAndInt).fun("takeVecAndInt", takeVecAndInt)
            .fun("rawTakesInt", rawTakesInt).fun("raisesLuaError", raisesLuaError);

        check(lua_gettop(L) == 0, "绑定 (含各类构造函数) 后 lua 栈平衡");

        check(lua_ok(L, "o = Box.new(1); for i = 1, 5 do assert(pcall(m.takeBoxAndInt, o, 'not-a-number') == false) end"),
              "自由函数: 第 2 个实参类型错误 -> lua 错误");
        check(Box::s_ctor - Box::s_dtor == 1, "  ...按值实参 Box 已析构 (只剩 o 存活)");

        check(lua_ok(L, "for i = 1, 5 do assert(pcall(m.takeVecAndInt, {o, o, o}, {}) == false) end"), "vector<Box> + 错误的 int");
        check(Box::s_ctor - Box::s_dtor == 1, "  ...vector 里的拷贝已析构");

        check(lua_ok(L, "for i = 1, 5 do assert(pcall(m.takeVecAndInt, {o, o, 5}, 1) == false) end"), "vector<Box> 第 3 个元素类型错误");
        check(Box::s_ctor - Box::s_dtor == 1, "  ...已转换的前 2 个元素已析构");

        check(lua_ok(L, "for i = 1, 5 do assert(pcall(Holder.new, o, 'bad') == false) end"), "构造函数: 第 2 个实参类型错误");
        check(Box::s_ctor - Box::s_dtor == 1, "  ...构造函数路径的按值实参已析构");

        check(lua_ok(L, "h = Holder.new(o, 1); for i = 1, 5 do assert(pcall(h.add, h, o, 'bad') == false) end; assert(h:add(o, 1) == 4)"),
              "成员函数: 第 2 个实参类型错误; 之后正常调用仍可用");
        check(Box::s_ctor - Box::s_dtor == 1, "  ...成员函数路径的按值实参已析构");

        check(lua_ok(L, "local ok, err = pcall(m.takeBoxAndInt, o, 'x'); assert(not ok and tostring(err):find('number expected', 1, true), tostring(err))"),
              "错误信息仍是原生的 'number expected' 形式");
        check(lua_ok(L, "local ok, err = pcall(m.takeBoxAndInt, 5, 1); assert(not ok and tostring(err):find('Box', 1, true), tostring(err))"),
              "类型不符的 userdata 实参: 错误信息仍带类名");

        // 裸 lua_CFunction 里直接用 LuaStack::get: 转换失败必须仍是 lua 错误 (不能变成逃逸的 C++ 异常)
        check(lua_ok(L, "assert(m.rawTakesInt(1) == 2); assert(pcall(m.rawTakesInt, 'x') == false)"), "裸 lua_CFunction 内的 LuaStack::get 失败仍是 lua 错误");
        check(lua_ok(L, "assert(pcall(m.raisesLuaError) == false); assert(pcall(m.rawTakesInt, {}) == false); assert(m.rawTakesInt(2) == 3)"),
              "绑定函数内部 longjmp 之后, 裸函数的转换失败仍是 lua 错误");
        lua_close(L);
        check(Box::s_ctor == Box::s_dtor, "关闭后 Box 构造/析构计数平衡");
    }
}

// ===================== M8 =====================
static void test_m8() {
    printf("[M8: 异常翻译后不残留已捕获异常]\n");
    lua_State* L = newState();
    LuaClass<Thrower>(L, "Thrower").ctor<int>().get("bad", &Thrower::badGet).set("bad", &Thrower::badSet);
    LuaModule(L, "m").fun("throwStd", throwStd).fun("throwInt", throwInt);

    check(lua_ok(L, "for i = 1, 3 do local ok, err = pcall(m.throwStd); assert(not ok and tostring(err):find('boom', 1, true) and tostring(err):find('function call failed', 1, true), tostring(err)) end"),
          "std::exception -> lua 错误, 信息含 what()");
    check(!std::current_exception(), "  ...之后 std::current_exception() 为空");
    check(lua_ok(L, "local ok, err = pcall(m.throwInt); assert(not ok and tostring(err):find('unknown C++ exception', 1, true), tostring(err))"),
          "非 std 异常 -> 'unknown C++ exception'");
    check(!std::current_exception(), "  ...之后 std::current_exception() 为空");
    check(lua_ok(L, "local ok, err = pcall(Thrower.new, -1); assert(not ok and tostring(err):find('negative', 1, true), tostring(err))"),
          "构造函数抛异常 -> lua 错误");
    check(!std::current_exception(), "  ...之后 std::current_exception() 为空");
    check(lua_ok(L, "local t = Thrower.new(1); "
                    "local ok, err = pcall(function() return t.bad end); assert(not ok and tostring(err):find('getter boom', 1, true), tostring(err)); "
                    "ok, err = pcall(function() t.bad = 1 end); assert(not ok and tostring(err):find('setter boom', 1, true), tostring(err))"),
          "property getter/setter 抛异常 -> lua 错误");
    check(!std::current_exception(), "  ...之后 std::current_exception() 为空");
    lua_close(L);
}

int main(int argc, char** argv) {
    const char* only = argc > 1 ? argv[1] : nullptr;
    if (!only || !strcmp(only, "m1")) test_m1();
    if (!only || !strcmp(only, "m2")) test_m2();
    if (!only || !strcmp(only, "m3")) test_m3();
    if (!only || !strcmp(only, "m4")) test_m4();
    if (!only || !strcmp(only, "m5")) test_m5();
    if (!only || !strcmp(only, "m6")) test_m6();
    if (!only || !strcmp(only, "m7")) test_m7();
    if (!only || !strcmp(only, "m8")) test_m8();
    printf("\n=== review8: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

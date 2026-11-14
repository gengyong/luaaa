// 第九轮评审修复回归测试 (第一批低危):
//   L1   (R9-1)  无穷大是合法浮点值: math.huge 可传给 double/float 参数, C++ 的 inf 可返回;
//                有限但超出目标类型范围的值仍报溢出
//   L5   (R9-5)  构造函数缺参时不再把对象自身的 userdata 当成实参读取
//   L10  (R9-10) T* 参数类型不符时, 报错用绑定的 lua 类名和实参序号, 不再是 mangled 名
//   ENUM         enum / enum class 自动按底层整数类型转换 (参数/返回值/容器/常量)
//   MACRO        头文件的内部宏不泄漏到包含它的翻译单元
//
// 用法: 不带参数跑全部; 带用例名 (l1 / l5 / l10 / enum / macro) 只跑该组。
#include "luaaa.hpp"

// ---- MACRO: 必须紧跟在 #include 之后求值 ----
#if defined(USE_NEW_MODULE_REGISTRY) || defined(RTTI_CLASS_NAME) || defined(IMPLEMENT_CALLBACK_INVOKER) \
    || defined(IMPLEMENT_FUNCTION_CALLER) || defined(luaaa_check_constructor_name_conflict) || defined(_NOTHING)
static const bool kOldMacroLeaks = true;
#else
static const bool kOldMacroLeaks = false;
#endif
#if defined(LUAAA_USE_NEW_MODULE_REGISTRY) || defined(LUAAA_RTTI_CLASS_NAME) || defined(LUAAA_IMPLEMENT_CALLBACK_INVOKER) \
    || defined(LUAAA_IMPLEMENT_FUNCTION_CALLER) || defined(LUAAA_REPORT_CTOR_NAME_CONFLICT) || defined(LUAAA_NOTHING) \
    || defined(LUAAA_TRY) || defined(LUAAA_CATCH) || defined(LUAAA_BOUNDARY_TRY) || defined(LUAAA_BOUNDARY_CATCH)
static const bool kInternalMacroLeaks = true;
#else
static const bool kInternalMacroLeaks = false;
#endif
// 配置宏是公开接口, 包含之后应当仍可见
#if defined(LUAAA_FEATURE_PROPERTY) && defined(LUAAA_FEATURE_EXTEND) && defined(LUAAA_CHECK_INTEGER_RANGE) \
    && defined(LUAAA_HAS_EXCEPTIONS) && defined(LUAAA_NS)
static const bool kConfigMacrosVisible = true;
#else
static const bool kConfigMacrosVisible = false;
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
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

// 跑一段应当报错的代码, 返回错误信息 (没报错返回 "<no error>")
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
static bool want(int argc, char** argv, const char* name) { return argc < 2 || strcmp(argv[1], name) == 0; }

// ===================== 被测类型 =====================
struct Cat {
    int v;
    Cat() : v(0) {}
    explicit Cat(int x) : v(x) {}
    Cat(int x, int y) : v(x + y) {}
    Cat(lua_State*, int x) : v(x * 10) {}
    int get() const { return v; }
    static Cat* make(int x) { return new Cat(x + 100); }
};
struct Unbound { int x; };

static double dbl(double d) { return d; }
static float flt(float d) { return d; }
static double retInf() { return std::numeric_limits<double>::infinity(); }
static double retNegInf() { return -std::numeric_limits<double>::infinity(); }
static float retFltInf() { return std::numeric_limits<float>::infinity(); }

static int ptrGet(Cat* c) { return c->v; }
static int ptrGet2(int n, const Cat* c) { return n + c->v; }
static int unboundGet(Unbound* u) { return u ? u->x : -1; }

enum Color { Red, Green, Blue };
enum class Mode : uint8_t { Off = 0, On = 1, Auto = 200 };
enum class Signed : int { Neg = -5, Zero = 0, Pos = 7 };

static Color nextColor(Color c) { return c == Blue ? Red : static_cast<Color>(c + 1); }
static Mode echoMode(Mode m) { return m; }
static bool isAuto(Mode m) { return m == Mode::Auto; }
static Signed echoSigned(Signed s) { return s; }
static const Color& colorRef() { static Color c = Green; return c; }
static std::vector<Color> reverseColors(std::vector<Color> v) { return std::vector<Color>(v.rbegin(), v.rend()); }
struct Lamp {
    Mode mode;
    Lamp() : mode(Mode::Off) {}
    void set(Mode m) { mode = m; }
    Mode get() const { return mode; }
};

int main(int argc, char** argv) {
    // ---------------- L1: 无穷大 ----------------
    if (want(argc, argv, "l1")) {
        printf("[L1 (R9-1): 无穷大可以进出浮点参数]\n");
        lua_State* L = newState();
        LuaModule(L, "m").fun("dbl", dbl).fun("flt", flt).fun("retInf", retInf).fun("retNegInf", retNegInf).fun("retFltInf", retFltInf);
        check(lua_ok(L, "assert(m.dbl(math.huge) == math.huge)"), "double 参数接受 math.huge");
        check(lua_ok(L, "assert(m.dbl(-math.huge) == -math.huge)"), "double 参数接受 -math.huge");
        check(lua_ok(L, "assert(m.flt(math.huge) == math.huge)"), "float 参数接受 math.huge");
        check(lua_ok(L, "assert(m.flt(-math.huge) == -math.huge)"), "float 参数接受 -math.huge");
        check(lua_ok(L, "assert(m.retInf() == math.huge)"), "C++ 返回 +inf 得到 math.huge");
        check(lua_ok(L, "assert(m.retNegInf() == -math.huge)"), "C++ 返回 -inf 得到 -math.huge");
        check(lua_ok(L, "assert(m.retFltInf() == math.huge)"), "C++ 返回 float 的 +inf 得到 math.huge");
        check(has(lua_err(L, "return m.flt(1e300)"), "number overflow"), "有限但超出 float 范围的值仍报溢出");
        check(has(lua_err(L, "return m.flt(-1e300)"), "number overflow"), "负方向同样报溢出");
        check(lua_ok(L, "local n = m.dbl(0/0); assert(n ~= n)"), "NaN 照常通过");
        check(lua_ok(L, "assert(m.dbl(1.5) == 1.5 and m.flt(0.25) == 0.25)"), "普通值不受影响");
        lua_close(L);
    }

    // ---------------- L5: 构造函数缺参 ----------------
    if (want(argc, argv, "l5")) {
        printf("[L5 (R9-5): 构造函数缺参的报错不再提到 userdata]\n");
        lua_State* L = newState();
        LuaClass<Cat>(L, "Cat").ctor<int>().ctor<int, int>("new2").ctor<lua_State*, int>("newS").ctor("make", &Cat::make).fun("get", &Cat::get);
        std::string e = lua_err(L, "return Cat.new()");
        check(!has(e, "userdata"), "单参构造缺参: 报错不含 userdata");
        check(has(e, "#1") && has(e, "number expected"), "单参构造缺参: 指向第 1 个参数, 期望 number");
        e = lua_err(L, "return Cat.new2(1)");
        check(!has(e, "userdata"), "双参构造少传一个: 报错不含 userdata");
        check(has(e, "#2") && has(e, "number expected"), "双参构造少传一个: 指向第 2 个参数");
        e = lua_err(L, "return Cat.newS()");
        check(!has(e, "userdata") && has(e, "#1"), "带 lua_State* 形参的构造缺参: 指向第 1 个 lua 实参");
        e = lua_err(L, "return Cat.make()");
        check(!has(e, "userdata") && has(e, "#1"), "spawner 构造缺参: 同样不提 userdata");
        check(has(lua_err(L, "return Cat.new('x')"), "number expected, got string"), "类型不符的报错不变");
        check(lua_ok(L, "assert(Cat.new(3):get() == 3)"), "单参构造正常");
        check(lua_ok(L, "assert(Cat.new2(3, 4):get() == 7)"), "双参构造正常");
        check(lua_ok(L, "assert(Cat.newS(3):get() == 30)"), "带 lua_State* 形参的构造正常");
        check(lua_ok(L, "assert(Cat.make(1):get() == 101)"), "spawner 构造正常");
        check(lua_ok(L, "assert(Cat.new(3, 'extra', {}):get() == 3)"), "多余的实参被忽略");
        check(lua_ok(L, "for i = 1, 200 do pcall(Cat.new) end; collectgarbage(); assert(Cat.new(9):get() == 9)"), "连续缺参失败后仍可正常构造");
        lua_close(L);
    }

    // ---------------- L10: T* 参数的报错 ----------------
    if (want(argc, argv, "l10")) {
        printf("[L10 (R9-10): T* 参数类型不符的报错用 lua 类名]\n");
        lua_State* L = newState();
        LuaClass<Cat>(L, "Kitty").ctor<int>().fun("get", &Cat::get);   // lua 名与 C++ 名不同
        LuaModule(L, "m").fun("ptrGet", ptrGet).fun("ptrGet2", ptrGet2).fun("unboundGet", unboundGet);
        std::string e = lua_err(L, "return m.ptrGet(nil)");
        check(has(e, "Kitty expected, got nil"), "nil 传给 Cat*: 「Kitty expected, got nil」");
        check(!has(e, "P3Cat") && !has(e, "cpp pointer"), "报错里没有 mangled 名");
        check(has(lua_err(L, "return m.ptrGet(5)"), "Kitty expected, got number"), "数字传给 Cat*: 「got number」");
        check(has(lua_err(L, "return m.ptrGet('s')"), "Kitty expected, got string"), "字符串传给 Cat*: 「got string」");
        check(has(lua_err(L, "return m.ptrGet({})"), "Kitty expected, got table"), "普通表传给 Cat*: 「got table」");
        e = lua_err(L, "return m.ptrGet2(1, nil)");
        check(has(e, "#2") && has(e, "Kitty expected, got nil"), "第 2 个参数出错时报 #2");
        check(has(lua_err(L, "return m.ptrGet()"), "Kitty expected, got no value"), "缺参: 「got no value」");
        check(has(lua_err(L, "return m.unboundGet(nil)"), "cpp pointer"), "未绑定的指针类型仍报 cpp pointer");
        check(lua_ok(L, "assert(m.ptrGet(Kitty.new(4)) == 4 and m.ptrGet2(1, Kitty.new(4)) == 5)"), "正常传对象不受影响");
        lua_close(L);
    }

    // ---------------- ENUM ----------------
    if (want(argc, argv, "enum")) {
        printf("[ENUM: enum / enum class 自动转换]\n");
        lua_State* L = newState();
        LuaClass<Lamp>(L, "Lamp").ctor().fun("set", &Lamp::set).fun("get", &Lamp::get);
        LuaModule m(L, "m");
        m.fun("nextColor", nextColor).fun("echoMode", echoMode).fun("isAuto", isAuto).fun("echoSigned", echoSigned)
         .fun("colorRef", colorRef).fun("reverseColors", reverseColors);
        m.def("RED", Red);
        m.def("AUTO", Mode::Auto);
        check(lua_ok(L, "assert(m.nextColor(0) == 1 and m.nextColor(2) == 0)"), "普通 enum 作参数和返回值");
        check(lua_ok(L, "assert(m.echoMode(200) == 200)"), "enum class (底层 uint8_t) 往返");
        check(lua_ok(L, "assert(m.isAuto(200) == true and m.isAuto(1) == false)"), "enum class 参数的值正确");
        check(lua_ok(L, "assert(m.echoSigned(-5) == -5 and m.echoSigned(7) == 7)"), "带负值的 enum class 往返");
        check(lua_ok(L, "assert(m.RED == 0 and m.AUTO == 200)"), "def 可以把枚举值导出为常量");
        check(lua_ok(L, "assert(m.nextColor(m.RED) == 1)"), "导出的常量可以直接传回");
        check(lua_ok(L, "assert(m.colorRef() == 1)"), "const enum& 返回按值转换");
        check(lua_ok(L, "local r = m.reverseColors({0, 1, 2}); assert(#r == 3 and r[1] == 2 and r[3] == 0)"), "vector<enum> 往返");
        check(lua_ok(L, "local l = Lamp.new(); l:set(1); assert(l:get() == 1)"), "成员函数的 enum 参数和返回值");
        check(has(lua_err(L, "return m.echoMode(300)"), "number overflow"), "超出底层类型范围的值报溢出");
        check(has(lua_err(L, "return m.nextColor('red')"), "number expected"), "非数字传给 enum 参数报类型错误");
        check(has(lua_err(L, "return m.nextColor({})"), "number expected"), "表传给 enum 参数报类型错误 (不再是 not export)");
        lua_close(L);
    }

    // ---------------- MACRO ----------------
    if (want(argc, argv, "macro")) {
        printf("[MACRO: 内部宏不泄漏]\n");
        check(!kOldMacroLeaks, "未加前缀的旧宏名在包含后不存在");
        check(!kInternalMacroLeaks, "内部辅助宏在包含后已 #undef");
        check(kConfigMacrosVisible, "公开的配置宏仍然可见");
    }

    printf("\n=== review9: %d passed, %d failed ===\n", P, F);
    return F ? 1 : 0;
}

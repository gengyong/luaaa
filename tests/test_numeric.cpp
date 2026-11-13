// 数值类型支持：除 int/float/double/bool 外的算术类型（long/unsigned/long long/
// unsigned long long/short/size_t 等）应能在 C++ 与 Lua 之间往返（get + put）。
// 关键跨平台点：size_t / int64_t 等是别名，必须命中同一条特化，不得重定义。
#include "luaaa.hpp"
#include <cstdio>
#include <cstdint>
#include <limits>
using namespace luaaa;

static int P = 0, F = 0;
static void check(bool ok, const char* n) {
    if (ok) { ++P; printf("  ok   %s\n", n); }
    else    { ++F; printf("  FAIL %s\n", n); }
}

static long               echo_l  (long v)               { return v; }
static unsigned int       echo_u  (unsigned int v)       { return v; }
static long long          echo_ll (long long v)          { return v; }
static unsigned long long echo_ull(unsigned long long v) { return v; }
static short              echo_s  (short v)              { return v; }
static size_t             echo_sz (size_t v)             { return v; }   // 别名
static int64_t            echo_i64(int64_t v)            { return v; }   // 别名

static float              echo_f  (float v)              { return v; }
static double             echo_d  (double v)             { return v; }
static long double        echo_ld (long double v)        { return v; }   // 新纳入
// long double 的最大有限值。x86 上 long double 比 lua_Number(double) 宽, 这个值放不进去;
// arm64 / MSVC 上 long double 就是 double, 它正好等于 DBL_MAX, 可以原样往返。
// (原先用 1e300L * 1e300L: 在等宽平台上它其实是 +inf, 靠「inf 也算溢出」才通过;
//  第九轮 R9-1 起 inf 不再算溢出, 所以改成一个确定有限的值。)
static long double        big_ld  ()                     { return std::numeric_limits<long double>::max(); }

int main() {
    lua_State* L = luaL_newstate(); luaL_openlibs(L);
    LuaModule(L, "num")
        .fun("l",   echo_l)
        .fun("u",   echo_u)
        .fun("ll",  echo_ll)
        .fun("ull", echo_ull)
        .fun("s",   echo_s)
        .fun("sz",  echo_sz)
        .fun("i64", echo_i64)
        .fun("f",   echo_f)
        .fun("d",   echo_d)
        .fun("ld",  echo_ld)
        .fun("bigld", big_ld);

    check(luaL_dostring(L, "assert(num.l(-2147483648) == -2147483648)") == 0,
          "long 往返 (-2^31)");
    check(luaL_dostring(L, "assert(num.u(4000000000) == 4000000000)") == 0,
          "unsigned 往返 (>2^31)");
    check(luaL_dostring(L, "assert(num.s(-12345) == -12345)") == 0,
          "short 往返");
    check(luaL_dostring(L, "assert(num.sz(123456789) == 123456789)") == 0,
          "size_t 往返 (别名)");

    if (sizeof(lua_Integer) >= 8) {
        check(luaL_dostring(L, "assert(num.ll(9000000000) == 9000000000)") == 0,
              "long long 往返 (>2^32)");
        check(luaL_dostring(L, "assert(num.ull(9000000000) == 9000000000)") == 0,
              "unsigned long long 往返 (>2^32)");
        check(luaL_dostring(L, "assert(num.i64(-9000000000) == -9000000000)") == 0,
              "int64_t 往返 (别名, <-2^32)");
    } else {
        printf("  skip 64-bit cases (lua_Integer=%zu bytes)\n", sizeof(lua_Integer));
    }

    // 浮点：float/double 往返，long double 现已可用
    check(luaL_dostring(L, "assert(num.f(1.5) == 1.5)") == 0,   "float 往返");
    check(luaL_dostring(L, "assert(num.d(1e300) == 1e300)") == 0, "double 往返 (大值不误判溢出)");
    check(luaL_dostring(L, "assert(num.ld(2.5) == 2.5)") == 0,  "long double 往返 (新纳入)");

    // 溢出检查：get 方向 lua_Number(double) -> float 收窄溢出应报错
    check(luaL_dostring(L, "assert(pcall(num.f, 1e300) == false)") == 0,
          "float get 溢出报错 (1e300 不入 float)");
    // 溢出检查：put 方向 long double -> lua_Number, 有限但放不下的值应报错
    if (std::numeric_limits<long double>::max_exponent > std::numeric_limits<lua_Number>::max_exponent) {
        check(luaL_dostring(L, "assert(pcall(num.bigld) == false)") == 0,
              "long double put 溢出报错 (>lua_Number 范围)");
    } else {
        check(luaL_dostring(L, "local v = num.bigld(); assert(v > 1e308 and v < math.huge)") == 0,
              "long double 最大值可返回 (本平台 long double 与 lua_Number 等宽)");
    }

    printf("\n=== numeric: %d passed, %d failed ===\n", P, F);
    return F > 0 ? 1 : 0;
}

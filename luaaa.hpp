
/*
 Copyright (c) 2019 gengyong
 https://github.com/gengyong/luaaa
 licensed under MIT License.
*/

#ifndef HEADER_LUAAA_HPP
#define HEADER_LUAAA_HPP

#define LUAAA_NS luaaa
#define LUAAA_VER_MAJOR (2)
#define LUAAA_VER_MINOR (0)

/// if you want to disable C++ std libs, set to 1
#ifndef LUAAA_WITHOUT_CPP_STDLIB
#define LUAAA_WITHOUT_CPP_STDLIB 0
#endif

#ifndef LUAAA_DEBUG
#define LUAAA_DEBUG 0
#endif

/// enable to check LuaClass constructor name conflict
/// exposed constructor should have unique name, otherwise the early one will be overwriten. 
#ifndef LUAAA_CHECK_CONSTRUCTOR_NAME_CONFLICT
#define LUAAA_CHECK_CONSTRUCTOR_NAME_CONFLICT 1
#endif

#ifndef LUAAA_FEATURE_PROPERTY
#define LUAAA_FEATURE_PROPERTY 1
#endif

/// reject a lua number that does not fit the C++ integer parameter it is passed to
/// (e.g. 300 -> uint8_t, 2^40 -> int, -1 -> unsigned) instead of wrapping silently.
/// set to 0 to restore the old static_cast wrap-around.
#ifndef LUAAA_CHECK_INTEGER_RANGE
#define LUAAA_CHECK_INTEGER_RANGE 1
#endif

/// enable built-in lua-side inheritance helpers (luaaa:extend / luaaa:base).
/// any lua_State that binds a LuaClass gets them injected automatically (once).
#ifndef LUAAA_FEATURE_EXTEND
#define LUAAA_FEATURE_EXTEND 1
#endif

/// let a lua `nil` (or a missing argument) reach a `T*` parameter as nullptr.
/// OFF by default: with it on, a script that passes nil where an object was
/// expected no longer gets an argument error -- the bound function receives
/// nullptr and MUST check for it. (std::optional<T>, std::shared_ptr<T> and
/// std::unique_ptr<T> parameters always accept nil.)
#ifndef LUAAA_NIL_AS_NULLPTR
#define LUAAA_NIL_AS_NULLPTR 0
#endif

/// hide the metatable of bound objects from scripts: getmetatable(obj) returns
/// false, so a script cannot reach or replace __gc / __index / the methods.
/// set to 0 if your scripts rely on getmetatable(obj) of a bound object.
#ifndef LUAAA_PROTECT_METATABLE
#define LUAAA_PROTECT_METATABLE 1
#endif

/// also record every class / module table in package.loaded under its name,
/// so `require "name"` returns it (skipped when the state has no package.loaded).
#ifndef LUAAA_REGISTER_IN_PACKAGE_LOADED
#define LUAAA_REGISTER_IN_PACKAGE_LOADED 1
#endif

extern "C"
{
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

// Compatibility shims. They are LUAAA_-prefixed functions of our own rather
// than definitions of the 5.2+ API names (lua_rawgetp, lua_rawsetp, ...): a
// header that defines those names collides with every other library that
// polyfills them too (lua-compat-5.3, LuaJIT's own 5.2 extensions, ...).
// t[p] with a light userdata key, raw. `idx` may be relative or a pseudo-index.
inline void LUAAA_rawgetp(lua_State * L, int idx, const void * p) {
#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM <= 501
    if (idx < 0 && idx > LUA_REGISTRYINDEX) idx = lua_gettop(L) + idx + 1;
    lua_pushlightuserdata(L, const_cast<void*>(p));
    lua_rawget(L, idx);
#else
    lua_rawgetp(L, idx, p);
#endif
}

// t[p] = <top of stack> with a light userdata key, raw; pops the value.
inline void LUAAA_rawsetp(lua_State * L, int idx, const void * p) {
#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM <= 501
    if (idx < 0 && idx > LUA_REGISTRYINDEX) idx = lua_gettop(L) + idx + 1;
    lua_pushlightuserdata(L, const_cast<void*>(p)); // stack: ..., value, p
    lua_insert(L, -2);                              // stack: ..., p, value
    lua_rawset(L, idx);
#else
    lua_rawsetp(L, idx, p);
#endif
}

inline void LUAAA_pushglobaltable(lua_State * L) {
#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM <= 501
    lua_pushvalue(L, LUA_GLOBALSINDEX);
#else
    lua_pushglobaltable(L);
#endif
}

// The language level, also where MSVC keeps __cplusplus at 199711.
#if defined(_MSVC_LANG) && _MSVC_LANG > __cplusplus
#   define LUAAA_CPLUSPLUS _MSVC_LANG
#else
#   define LUAAA_CPLUSPLUS __cplusplus
#endif

// Portable thread_local. The raw function-pointer callback path stores its
// (lua_State*, ref) in per-signature slots; making them thread_local gives each
// thread its own slots so concurrent "one lua_State per thread" usage is race-free.
#ifndef LUAAA_THREAD_LOCAL
#  if defined(__cplusplus) && __cplusplus >= 201103L
#    define LUAAA_THREAD_LOCAL thread_local
#  elif defined(_MSC_VER) && _MSC_VER >= 1900   // MSVC often reports __cplusplus as 199711
#    define LUAAA_THREAD_LOCAL thread_local
#  else
#    define LUAAA_THREAD_LOCAL static   // no thread_local on this toolchain: falls back to old behavior (function-pointer callbacks not thread-safe)
#  endif
#endif

// Exception support. When the host is compiled WITH exceptions (the normal case),
// a C++ exception thrown out of a bound function/ctor must be translated into a
// lua_error() -- otherwise it crosses the extern "C" boundary, hits std::terminate,
// and kills the whole process (turning a script bug into a host crash). When the
// host is compiled -fno-exceptions (e.g. the embedded/freestanding dialect), there
// is nothing to catch and nothing to translate, so the guards compile away to nothing.
#ifndef LUAAA_HAS_EXCEPTIONS
#  if defined(__cpp_exceptions) && __cpp_exceptions >= 199711L
#    define LUAAA_HAS_EXCEPTIONS 1
#  elif defined(_MSC_VER)   // MSVC enables /EH by default unless /EHs-c- is passed
#    define LUAAA_HAS_EXCEPTIONS 1
#  else
#    define LUAAA_HAS_EXCEPTIONS 0
#  endif
#endif

#include <cassert>
#include <cstdio>       // snprintf (property-name builder, debug dumps)
#include <cstdlib>      // std::abort (unreachable tails after lua_error)
#include <cstdint>      // uintptr_t (alignment fix-up for in-place construction)
#include <cstring>      // memset / memcpy / strcmp / strlen
#include <typeinfo>
#include <type_traits>
#include <utility>
#include <new>          // placement new for storing callables in holder userdata

// LuaJIT raises lua errors by unwinding the C++ stack with a FOREIGN exception,
// which `catch (...)` intercepts. Where the C++ ABI lets us tell such an
// exception from a real C++ one, keep the lua error as it is (see PushCppException).
// (lua.h does not say whether it is LuaJIT's, so this is keyed on the 5.1 API;
// PUC Lua 5.1 never throws a foreign exception, so the check is inert there.)
#if (!defined(LUA_VERSION_NUM) || LUA_VERSION_NUM <= 501) && LUAAA_HAS_EXCEPTIONS && (defined(__GNUC__) || defined(__clang__)) && !defined(_MSC_VER)
#   include <cxxabi.h>
#   define LUAAA_FOREIGN_EXCEPTION() (abi::__cxa_current_exception_type() == nullptr)
#else
#   define LUAAA_FOREIGN_EXCEPTION() (false)
#endif

#if defined(_MSC_VER)
#   define LUAAA_RTTI_CLASS_NAME(a) typeid(a).name() //vc always has this operator even if RTTI was disabled.
#elif __GXX_RTTI
#   define LUAAA_RTTI_CLASS_NAME(a) typeid(a).name()
#elif _HAS_STATIC_RTTI
#   define LUAAA_RTTI_CLASS_NAME(a) typeid(a).name()
#else
#   define LUAAA_RTTI_CLASS_NAME(a) "?"
#endif

#if LUAAA_WITHOUT_CPP_STDLIB
#   include <limits>       // numeric_limits: overflow checks for numeric types
#else
#   include <limits>       // numeric_limits: overflow checks for numeric types
#   include <exception>    // std::exception (translated into lua errors)
#   include <string>
#   include <functional>
#   include <memory>
#endif

// lua_tonumberx / lua_tointegerx (5.2+) are used by the numeric converters.
// PUC Lua 5.1 lacks them (LuaJIT provides its own, so the test matrix never
// noticed), and defining identically-named polyfills would collide with
// LuaJIT's declarations -- route through these wrappers instead. Before 5.3
// (no integer subtype: every number is a double) the integer wrapper clamps
// to lua_Integer's range (like LuaJIT does) instead of relying on the
// double->integer cast, which is undefined for a value out of range -- and
// PUC 5.2's own lua_tointegerx does exactly that cast.
inline lua_Number LUAAA_tonumberx(lua_State * L, int idx, int * isnum) {
#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM <= 501
    const lua_Number r = lua_tonumber(L, idx);
    if (isnum) *isnum = lua_isnumber(L, idx);
    return r;
#else
    return lua_tonumberx(L, idx, isnum);
#endif
}

inline lua_Integer LUAAA_tointegerx(lua_State * L, int idx, int * isnum) {
#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM < 503
    const lua_Number n = LUAAA_tonumberx(L, idx, isnum);
    const lua_Number hi = (lua_Number)(std::numeric_limits<lua_Integer>::max)();
    const lua_Number lo = (lua_Number)(std::numeric_limits<lua_Integer>::min)();
    if (n != n) return 0;                       // NaN: the cast below would be undefined
    if (n >= hi) return (std::numeric_limits<lua_Integer>::max)();
    if (n <= lo) return (std::numeric_limits<lua_Integer>::min)();
    return (lua_Integer)n;
#else
    return lua_tointegerx(L, idx, isnum);
#endif
}

// lua_rawlen (5.2+) is lua_objlen on 5.1 / LuaJIT.
inline size_t LUAAA_rawlen(lua_State * L, int idx) {
#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM <= 501
    return (size_t)lua_objlen(L, idx);
#else
    return (size_t)lua_rawlen(L, idx);
#endif
}

#if LUAAA_DEBUG
inline size_t LUAAA_DUMP_OBJECT(char* buffer, size_t buflen, lua_State * L, int idx, int indent=0) {
    char prefix[32] = {0};
    if (indent > sizeof(prefix) - 1) {
        indent = sizeof(prefix) - 1;
    }
    for (int i = 0; i < indent; i++) {
        prefix[i] = '\t';
    }
    switch (lua_type(L, idx)) {
    default:
        return snprintf(buffer, buflen, "<%s>(%p)", luaL_typename(L, idx), lua_topointer(L, idx));
    case LUA_TNUMBER:
    case LUA_TSTRING:
        return snprintf(buffer, buflen, "\"%s\"", lua_tostring(L, idx));
    case LUA_TBOOLEAN:
        return snprintf(buffer, buflen, "%s", (lua_toboolean(L, idx) ? "true" : "false"));
    case LUA_TNIL:
        return snprintf(buffer, buflen, "%s", "nil");
    case LUA_TTABLE:
        int ret = 0;
        if (luaL_getmetafield(L, idx, "__name")) {
            ret = snprintf(buffer, buflen, "<%s>(%p):", lua_tostring(L, -1), lua_topointer(L, idx));
            lua_pop(L, 1);
        } else {
            ret = snprintf(buffer, buflen, "<%s>(%p):", luaL_typename(L, idx), lua_topointer(L, idx));
        }
        if (ret > 0) {
            size_t wrote = ret;
            lua_pushnil(L);
            while (lua_next(L, idx)) {
                const int top = lua_gettop(L);
                if (buflen > wrote) {
                    ret = snprintf(buffer + wrote, buflen - wrote, "\n%s", prefix);
                    if (ret > 0) {
                        wrote += ret;
                    }
                }

                if (buflen > wrote) {
                    lua_pushvalue(L, top - 1);
                    ret = snprintf(buffer + wrote, buflen - wrote, "\"%s\":", lua_tostring(L, -1));
                    lua_pop(L, 1);
                    if (ret > 0) {
                        wrote += ret;
                    }
                }

                if (buflen > wrote) {
                    if (lua_topointer(L, top) == lua_topointer(L, idx)) {
                        ret = snprintf(buffer + wrote, buflen - wrote, "<self>(%p)", lua_topointer(L, top));
                    } else {
                        if (indent < 3)
                        {
                            ret = LUAAA_DUMP_OBJECT(buffer + wrote, buflen - wrote, L, top, indent + 1);
                        }
                        else
                        {
                            ret = snprintf(buffer + wrote, buflen - wrote, "...");
                        }
                    }
                    if (ret > 0) {
                        wrote += ret;
                    }
                }
                lua_pop(L, 1);
            }
            //lua_pop(L, 0);
            return wrote;
        }
    }
    return 0;
}

inline void LUAAA_DUMP(lua_State * L, const char * name = "") {
    printf(">>>>>>>>>>>>>>>>>>>>>>>>>[%s]\n", name);
    char buffer[4096] = {0};
    int top = lua_gettop(L);
    for (int i = 1; i <= top; i++) {
        LUAAA_DUMP_OBJECT(buffer, sizeof(buffer) - 1, L, i, 1);
        printf("%d\t%s\n", i, buffer);
    }
    printf("<<<<<<<<<<<<<<<<<<<<<<<<<\n");
}
#else
# define LUAAA_DUMP(L,...)
#endif

#if LUAAA_CHECK_CONSTRUCTOR_NAME_CONFLICT
#   define LUAAA_REPORT_CTOR_NAME_CONFLICT(ctorName) { \
        ::LUAAA_NS::luaaa_detail::push_named_table(m_state, klassName(m_state)); \
        lua_pushstring(m_state, ctorName); \
        lua_rawget(m_state, -2); \
        if (!lua_isnil(m_state, -1) && lua_tocfunction(m_state, -1) != &::LUAAA_NS::luaaa_detail::overload_dispatch) { \
            printf("Error: LuaClass<%s>::ctor has duplicated name:`%s`\n", klassName(m_state), ctorName);\
        } \
        lua_pop(m_state, 2); \
    }
#else
#   define LUAAA_REPORT_CTOR_NAME_CONFLICT(ctorName)
#endif

namespace LUAAA_NS
{
#if !LUAAA_WITHOUT_CPP_STDLIB
    // to_function
    template <typename F>
    struct function_traits : public function_traits<decltype(&F::operator())> {};

    template <typename TRET, typename CLASS, typename... ARGS>
    struct function_traits<TRET(CLASS::*)(ARGS...) const>
    {
        using function_type = std::function<TRET(ARGS...)>;
    };

    template <typename F>
    using function_type_t = typename function_traits<F>::function_type;

    template <typename F>
    function_type_t<F> to_function(F& f)
    {
        return static_cast<function_type_t<F>>(f);
    }

    // to_class_getter_function
    template <typename TCLASS, typename F>
    struct class_getter_function_traits : public class_getter_function_traits<TCLASS, decltype(&F::operator())> {};

    template <typename TCLASS, typename TRET, typename CLASS, typename ARG, typename... ARGS>
    struct class_getter_function_traits<TCLASS, TRET(CLASS::*)(ARG, ARGS...) const>
    {
        static_assert((sizeof...(ARGS) == 0) && (std::is_same<typename std::decay<TCLASS>::type, typename std::decay<ARG>::type>::value), "Error: property getter does not accept param except current Class reference.");
        using function_type = std::function<TRET(ARG, ARGS...)>;
    };

    template <typename TCLASS, typename TRET, typename CLASS>
    struct class_getter_function_traits<TCLASS, TRET(CLASS::*)() const>
    {
        using function_type = std::function<TRET()>;
    };

    template <typename TCLASS, typename F>
    using class_getter_function_type_t = typename class_getter_function_traits<TCLASS, F>::function_type;

    template <typename TCLASS, typename F>
    class_getter_function_type_t<TCLASS, F> to_class_getter_function(F& f)
    {
        return static_cast<class_getter_function_type_t<TCLASS, F>>(f);
    }

    // to_class_setter_function
    template <typename TCLASS, typename F>
    struct class_setter_function_traits : public class_setter_function_traits<TCLASS, decltype(&F::operator())> {};


    template <typename TCLASS, typename TRET, typename CLASS, typename ARG1, typename... ARGS>
    struct class_setter_function_traits<TCLASS, TRET(CLASS::*)(ARG1, ARGS...) const>
    {
        static_assert((sizeof...(ARGS) == 0) || ((sizeof...(ARGS) == 1) && std::is_same<typename std::decay<TCLASS>::type, typename std::decay<ARG1>::type>::value), "Error: property setter accepts only [1 value] or [self instance and 1 value].");
        using function_type = std::function<TRET(ARG1, ARGS...)>;
    };

    template <typename TCLASS, typename TRET, typename CLASS, typename ARG1>
    struct class_setter_function_traits<TCLASS, TRET(CLASS::*)(ARG1) const>
    {
        using function_type = std::function<TRET(ARG1)>;
    };

    template <typename TCLASS, typename F>
    using class_setter_function_type_t = typename class_setter_function_traits<TCLASS, F>::function_type;

    template <typename TCLASS, typename F>
    class_setter_function_type_t<TCLASS, F> to_class_setter_function(F& f)
    {
        return static_cast<class_setter_function_type_t<TCLASS, F>>(f);
    }

    // to_module_getter_function
    template <typename F>
    struct module_getter_function_traits : public module_getter_function_traits<decltype(&F::operator())> {};

    template <typename TRET, typename... ARGS>
    struct module_getter_function_traits<TRET(*)(ARGS...)>
    {
        static_assert((sizeof...(ARGS) == 0), "Error: property getter does not accept param.");
        using function_type = std::function<TRET(ARGS...)>;
    };

    template <typename TRET, typename CLASS, typename... ARGS>
    struct module_getter_function_traits<TRET(CLASS::*)(ARGS...) const>
    {
        static_assert((sizeof...(ARGS) == 0), "Error: property getter does not accept param.");
        using function_type = std::function<TRET(ARGS...)>;
    };

    template <typename F>
    using module_getter_function_type_t = typename module_getter_function_traits<F>::function_type;

    template <typename F>
    module_getter_function_type_t<F> to_module_getter_function(F& f)
    {
        return static_cast<module_getter_function_type_t<F>>(f);
    }

    // to_module_setter_function
    template <typename F>
    struct module_setter_function_traits : public module_setter_function_traits<decltype(&F::operator())> {};

    template <typename TRET, typename ARG1, typename... ARGS>
    struct module_setter_function_traits<TRET(*)(ARG1, ARGS...)>
    {
        static_assert((sizeof...(ARGS) == 0), "Error: property setter accepts only 1 value.");
        using function_type = std::function<TRET(ARG1, ARGS...)>;
    };

    template <typename TRET, typename CLASS, typename ARG1, typename... ARGS>
    struct module_setter_function_traits<TRET(CLASS::*)(ARG1, ARGS...) const>
    {
        static_assert((sizeof...(ARGS) == 0), "Error: property setter accepts only 1 value.");
        using function_type = std::function<TRET(ARG1, ARGS...)>;
    };

    template <typename F>
    using module_setter_function_type_t = typename module_setter_function_traits<F>::function_type;

    template <typename F>
    module_setter_function_type_t<F> to_module_setter_function(F& f)
    {
        return static_cast<module_setter_function_type_t<F>>(f);
    }
#endif

    //========================================================
    // Lua Class
    //========================================================

    template <typename> struct LuaClass;

    //========================================================
    // error plumbing
    //========================================================
    // Lua reports errors with longjmp, which does not run C++ destructors. Two
    // rules keep that from leaking or corrupting C++ state (both compile away
    // when the host is built without exceptions):
    //
    // 1. A C++ exception leaving bound code is turned into a lua error only
    //    AFTER its handler has been left: the message is pushed inside the
    //    `catch`, lua_error() runs once the catch block is closed. A longjmp out
    //    of a live handler would never end the catch -- the exception object
    //    leaks and stays "currently handled" (std::current_exception() keeps
    //    returning it) for the rest of the thread. See LUAAA_TRY / LUAAA_CATCH.
    //
    // 2. A failing ARGUMENT conversion inside a bound call (LuaStack<>::get: wrong
    //    type, wrong class, number out of range, ...) is THROWN as a
    //    ConversionError instead of raised, so the arguments that were already
    //    converted (strings, containers, by-value objects) are destroyed by normal
    //    unwinding. The call boundary catches it, leaves the handler and only
    //    then raises -- by REPLAYING the failed check with the real luaL_*
    //    function, so the message is exactly the one Lua would have produced
    //    ("bad argument #2 to 'f' (number expected, got string)"). C++ unwinding
    //    leaves the Lua stack alone, so the offending value is still where the
    //    check found it. See LUAAA_BOUNDARY_TRY / LUAAA_BOUNDARY_CATCH.
    //
    //    Throwing is only correct while such a boundary will catch it. A failed
    //    conversion anywhere else -- LuaStack<>::get used directly in a raw
    //    lua_CFunction, in a property accessor, in a callback's result -- must
    //    stay a plain lua error. So a boundary arms itself by recording the
    //    address held in ITS upvalue 1 (every bound closure has one), and a
    //    failure throws only if the C function Lua is running right now has that
    //    same upvalue. This is checked on the failure path only, and a record
    //    left stale by a longjmp that skipped a boundary's cleanup can never
    //    match a different running function.
    namespace luaaa_detail {
        inline int AbsIndex(lua_State* L, int idx)
        {
            return (idx < 0 && idx > LUA_REGISTRYINDEX) ? (lua_gettop(L) + idx + 1) : idx;
        }

#if LUAAA_HAS_EXCEPTIONS
        struct ConversionError
        {
            int kind;           // CONV_*
            int a;              // stack index / argument number
            int b;              // expected lua type / stack index of the message
            const char* s;      // metatable name (CONV_UDATA)
        };
        enum { CONV_NONE = 0, CONV_PUSHED, CONV_TYPE, CONV_ARG, CONV_UDATA };

        inline const void*& ActiveBoundary()
        {
            LUAAA_THREAD_LOCAL const void* key = nullptr;
            return key;
        }

        struct BoundaryScope
        {
            // one thread-local lookup per call: keep the slot, not the accessor
            explicit BoundaryScope(const void* key) : m_slot(ActiveBoundary()), m_saved(m_slot) { m_slot = key; }
            ~BoundaryScope() { m_slot = m_saved; }
        private:
            BoundaryScope(const BoundaryScope&);
            BoundaryScope& operator=(const BoundaryScope&);
            const void*& m_slot;
            const void* m_saved;
        };

        inline bool InBoundary(lua_State* L)
        {
            const void* key = ActiveBoundary();
            return key != nullptr && lua_touserdata(L, lua_upvalueindex(1)) == key;
        }

        // Push "<where>: luaaa: <context> failed: <what>" for the exception being
        // handled; returns its stack index. Call from inside a catch block only.
        inline int PushCppException(lua_State* L, const char* context)
        {
            if (LUAAA_FOREIGN_EXCEPTION() && lua_gettop(L) > 0)
            {
                // Not a C++ exception but a lua error in transit (LuaJIT): its
                // error value is on top of the stack -- pass it on unchanged
                // instead of rewording it as an "unknown C++ exception".
                return lua_gettop(L);
            }
            luaL_where(L, 1);
            try {
                throw;   // rethrow the current exception to inspect it
            }
#if !LUAAA_WITHOUT_CPP_STDLIB
            catch (const std::exception& e) {
                lua_pushfstring(L, "luaaa: %s failed: %s", context ? context : "?", e.what());
            }
            catch (...) {
                lua_pushfstring(L, "luaaa: %s failed (unknown C++ exception)", context ? context : "?");
            }
#else
            catch (...) {
                lua_pushfstring(L, "luaaa: %s failed (C++ exception)", context ? context : "?");
            }
#endif
            lua_concat(L, 2);
            return lua_gettop(L);
        }

        // Raise the error value on top of the stack. Never returns.
        [[noreturn]] inline void RaisePushedError(lua_State* L)
        {
            lua_error(L);
            std::abort();   // unreachable: lua_error does not return
        }

        // Raise the failure a boundary caught. Never returns.
        [[noreturn]] inline void RaiseConversionError(lua_State* L, const ConversionError& e)
        {
            switch (e.kind)
            {
            case CONV_PUSHED: lua_pushvalue(L, e.a); lua_error(L); break;
            case CONV_TYPE:   luaL_checktype(L, e.a, e.b); break;
            case CONV_ARG:    luaL_argerror(L, e.a, lua_tostring(L, e.b)); break;
            case CONV_UDATA:  luaL_checkudata(L, e.a, e.s); break;
            default: break;
            }
            luaL_error(L, "luaaa: argument conversion failed");   // a replayed check that unexpectedly passed
            std::abort();   // unreachable
        }
#endif

        // The four ways a LuaStack<>::get reports a bad value. Each behaves like
        // the luaL_* function it wraps, except that inside a call boundary it
        // throws (rule 2 above) instead of raising.
        inline void conv_check_type(lua_State* L, int idx, int t)
        {
            if (lua_type(L, idx) == t)
            {
                return;
            }
#if LUAAA_HAS_EXCEPTIONS
            if (InBoundary(L))
            {
                const ConversionError e = { CONV_TYPE, AbsIndex(L, idx), t, nullptr };
                throw e;
            }
#endif
            luaL_checktype(L, idx, t);
        }

        [[noreturn]] inline void conv_arg_error(lua_State* L, int arg, const char* msg)
        {
#if LUAAA_HAS_EXCEPTIONS
            if (InBoundary(L) && lua_checkstack(L, 1))
            {
                lua_pushstring(L, msg);     // keep the text alive past the unwinding
                const ConversionError e = { CONV_ARG, arg, lua_gettop(L), nullptr };
                throw e;
            }
#endif
            luaL_argerror(L, arg, msg);
            std::abort();   // unreachable: luaL_argerror does not return
        }

        [[noreturn]] inline void conv_error(lua_State* L, const char* msg)
        {
#if LUAAA_HAS_EXCEPTIONS
            if (InBoundary(L) && lua_checkstack(L, 2))
            {
                luaL_where(L, 1);           // same position prefix luaL_error would add
                lua_pushstring(L, msg);
                lua_concat(L, 2);
                const ConversionError e = { CONV_PUSHED, lua_gettop(L), 0, nullptr };
                throw e;
            }
#endif
            luaL_error(L, "%s", msg);
            std::abort();   // unreachable: luaL_error does not return
        }

        // The value at idx is not a `tname` object: raise what luaL_checkudata
        // would ("bad argument #2 to 'f' (Cat expected, got nil)").
        [[noreturn]] inline void conv_udata_error(lua_State* L, int idx, const char* tname)
        {
#if LUAAA_HAS_EXCEPTIONS
            if (InBoundary(L))
            {
                const ConversionError e = { CONV_UDATA, AbsIndex(L, idx), 0, tname };
                throw e;
            }
#endif
            luaL_checkudata(L, AbsIndex(L, idx), tname);
            luaL_error(L, "%s expected", tname);    // the check unexpectedly passed
            std::abort();   // unreachable
        }

        inline void* conv_check_udata(lua_State* L, int idx, const char* tname)
        {
#if LUAAA_HAS_EXCEPTIONS
            if (InBoundary(L))
            {
                void* p = (lua_type(L, idx) == LUA_TUSERDATA) ? lua_touserdata(L, idx) : nullptr;
                bool matches = false;
                if (p != nullptr && lua_checkstack(L, 2) && lua_getmetatable(L, idx))
                {
                    luaL_getmetatable(L, tname);
                    matches = (lua_rawequal(L, -1, -2) != 0);
                    lua_pop(L, 2);
                }
                if (matches)
                {
                    return p;
                }
                const ConversionError e = { CONV_UDATA, AbsIndex(L, idx), 0, tname };
                throw e;
            }
#endif
            return luaL_checkudata(L, idx, tname);
        }
    }

// Rule 1: run a block; a C++ exception becomes a lua error raised after the
// handler is closed. (At most one use per scope: it declares a flag.)
//     LUAAA_TRY { ... } LUAAA_CATCH(L, "what was being done")
#if LUAAA_HAS_EXCEPTIONS
#   define LUAAA_TRY \
        bool luaaa_cpp_failed = false; \
        try
#   define LUAAA_CATCH(L, context) \
        catch (...) { ::LUAAA_NS::luaaa_detail::PushCppException(L, context); luaaa_cpp_failed = true; } \
        if (luaaa_cpp_failed) { ::LUAAA_NS::luaaa_detail::RaisePushedError(L); }
#else
#   define LUAAA_TRY
#   define LUAAA_CATCH(L, context)
#endif

// Rules 1 + 2: the call boundary of a bound function / method / constructor,
// i.e. the block that converts several arguments and then calls into C++.
// `key` is the address held in the running closure's upvalue 1 (what its caller
// already fetched as the callee holder). The block may `return`; nothing may
// follow LUAAA_BOUNDARY_CATCH.
//     LUAAA_BOUNDARY_TRY(key) { return f(LuaStack<ARGS>::get(L, i)...); } LUAAA_BOUNDARY_CATCH(L, "function call")
#if LUAAA_HAS_EXCEPTIONS
#   define LUAAA_BOUNDARY_TRY(key) \
        ::LUAAA_NS::luaaa_detail::ConversionError luaaa_failure = { ::LUAAA_NS::luaaa_detail::CONV_NONE, 0, 0, nullptr }; \
        { \
            ::LUAAA_NS::luaaa_detail::BoundaryScope luaaa_boundary(key); \
            try
#   define LUAAA_BOUNDARY_CATCH(L, context) \
            catch (const ::LUAAA_NS::luaaa_detail::ConversionError& luaaa_e) { luaaa_failure = luaaa_e; } \
            catch (...) { \
                luaaa_failure.kind = ::LUAAA_NS::luaaa_detail::CONV_PUSHED; \
                luaaa_failure.a = ::LUAAA_NS::luaaa_detail::PushCppException(L, context); \
            } \
        } \
        ::LUAAA_NS::luaaa_detail::RaiseConversionError(L, luaaa_failure);
#else
#   define LUAAA_BOUNDARY_TRY(key)
#   define LUAAA_BOUNDARY_CATCH(L, context)
#endif

    // Defined further below (holder-userdata __gc installer); used by the
    // callback LuaStack::put wrappers, so it must be declared before LuaStack.
    template <typename F>
    inline void AttachHolderFinalizer(lua_State* L);

#if !LUAAA_WITHOUT_CPP_STDLIB
    // Defined further below (with the other caller factories); referenced by
    // LuaStack<std::function<...>>::put, so it must be declared before LuaStack.
    template<typename TRET, typename ...ARGS>
    lua_CFunction NonMemberFunctionCaller(const std::function<TRET(ARGS...)>& func);
#endif

    namespace luaaa_detail {
        // Push a userdata holding a copy of `f` (collected with ~F, see AttachHolderFinalizer).
        template <typename F>
        inline void push_holder(lua_State* L, const F& f)
        {
            F* p = (F*)lua_newuserdata(L, sizeof(F));
            new (p) F(f);
            AttachHolderFinalizer<F>(L);
        }

        inline int overload_dispatch(lua_State* L);
        inline void push_named_table(lua_State* L, const char* name);
    }

    //========================================================
    // object model
    //========================================================
    // Every bound C++ object lives in Lua as a full userdata that starts with
    // this block. It is the same for every class, so one object can be viewed
    // as any of its bound base classes, and __gc needs no per-class code.
    namespace luaaa_detail {
        struct UserData
        {
            void* obj;                  // the object, as the class named by the userdata's metatable; nullptr once finalized
            void (*dtor)(UserData*);    // destroys / releases obj; nullptr for a non-owning alias
            void (*free_func)();        // custom deleter, type-erased (see LuaClass::ctor(name, spawner, deleter))
            const void* holderKind;     // nullptr, or the tag of the smart pointer kept in `holder`
            void* holder;               // the smart pointer that owns obj (stored inside this userdata)
        };

        // Integer slots of a class metatable (invisible to Lua's metamethod lookup).
        enum { MT_METHODS = 1, MT_GETTERS = 2, MT_SETTERS = 3, MT_BASES = 4 };

        // Marks a metatable as one of ours (any class). A userdata is read as a
        // UserData only if its metatable carries this.
        inline const void* ClassMetaTag() { static char k = 0; return &k; }
        inline const void* SharedHolderTag() { static char k = 0; return &k; }
        template<typename PTR> struct UniqueHolderTag { static const void* key() { static char k = 0; return &k; } };

        typedef void* (*UpcastFn)(void*);

        inline UserData* new_userdata(lua_State* L, size_t extra)
        {
            UserData* u = (UserData*)lua_newuserdata(L, sizeof(UserData) + extra);
            u->obj = nullptr;
            u->dtor = nullptr;
            u->free_func = nullptr;
            u->holderKind = nullptr;
            u->holder = nullptr;
            return u;
        }

        // First address at or after the end of the UserData block that is aligned for `align`.
        inline void* payload(UserData* u, size_t align)
        {
            return (void*)((reinterpret_cast<uintptr_t>(u + 1) + (align - 1)) & ~(uintptr_t)(align - 1));
        }

        template<typename T>
        inline void destroy_in_place(UserData* u) { static_cast<T*>(u->obj)->~T(); }

        // Is the class whose metatable is at `mt` (absolute index) the class
        // `typeKey` names, or derived from it? On success `p` -- a pointer to
        // the object as mt's class -- is adjusted to point at that base (pass
        // nullptr to only ask the question).
        inline bool upcast(lua_State* L, int mt, const void* typeKey, void** p, int depth)
        {
            LUAAA_rawgetp(L, mt, typeKey);
            const bool same = (lua_touserdata(L, -1) == typeKey);
            lua_pop(L, 1);
            if (same)
            {
                return true;
            }
            if (depth > 16 || !lua_checkstack(L, 4))
            {
                return false;
            }
            lua_rawgeti(L, mt, MT_BASES);
            if (!lua_istable(L, -1))
            {
                lua_pop(L, 1);
                return false;
            }
            const int bases = lua_gettop(L);        // { baseMetatable, caster, baseMetatable, caster, ... }
            bool found = false;
            for (int i = 1; !found; i += 2)
            {
                lua_rawgeti(L, bases, i);
                if (!lua_istable(L, -1))
                {
                    lua_pop(L, 1);
                    break;
                }
                lua_rawgeti(L, bases, i + 1);
                const UpcastFn* fn = (const UpcastFn*)lua_touserdata(L, -1);
                lua_pop(L, 1);
                void* q = p ? (fn ? (*fn)(*p) : *p) : nullptr;
                if (upcast(L, lua_gettop(L), typeKey, p ? &q : nullptr, depth + 1))
                {
                    if (p) { *p = q; }
                    found = true;
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            return found;
        }

        enum { OBJ_MISMATCH = 0, OBJ_FINALIZED = 1 };

        // The value at idx as an object of the class `typeKey` names, or nullptr.
        // Accepts a bound object of that class or of a class derived from it,
        // and a lua-side object made by luaaa:extend (a table keeping the bound
        // object under "@"). Never raises and leaves the stack as it found it --
        // a residue would poison an enclosing lua_next loop.
        // The class is recognized by the type tag in the metatable, which costs
        // one raw lookup; no class name is fetched or compared.
        inline void* to_object(lua_State* L, int idx, const void* typeKey, int* why = nullptr, UserData** owner = nullptr)
        {
            if (why) { *why = OBJ_MISMATCH; }
            idx = AbsIndex(L, idx);
            const int t = lua_type(L, idx);
            if (t == LUA_TTABLE)
            {
                if (!lua_checkstack(L, 2))
                {
                    return nullptr;
                }
                lua_pushlstring(L, "@", 1);
                lua_rawget(L, idx);
                void* r = (lua_type(L, -1) == LUA_TUSERDATA) ? to_object(L, lua_gettop(L), typeKey, why, owner) : nullptr;
                lua_pop(L, 1);
                return r;
            }
            if (t != LUA_TUSERDATA || !lua_checkstack(L, 3) || !lua_getmetatable(L, idx))
            {
                return nullptr;
            }
            const int mt = lua_gettop(L);
            UserData* u = (UserData*)lua_touserdata(L, idx);
            void* p = nullptr;
            LUAAA_rawgetp(L, mt, typeKey);
            bool ok = (lua_touserdata(L, -1) == typeKey);       // exactly this class: the usual case
            lua_pop(L, 1);
            if (ok)
            {
                p = u->obj;
            }
            else
            {
                LUAAA_rawgetp(L, mt, ClassMetaTag());
                const bool ours = (lua_toboolean(L, -1) != 0);
                lua_pop(L, 1);
                if (ours)
                {
                    p = u->obj;                     // a derived class? then adjust the pointer
                    ok = upcast(L, mt, typeKey, p ? &p : nullptr, 0);
                }
            }
            lua_pop(L, 1);
            if (!ok)
            {
                return nullptr;
            }
            if (p == nullptr)                       // the right class, but already finalized
            {
                if (why) { *why = OBJ_FINALIZED; }
                return nullptr;
            }
            if (owner) { *owner = u; }
            return p;
        }

        // Report the value at idx as not being a `kname` object. Never returns.
        [[noreturn]] inline void bad_object(lua_State* L, int idx, const char* kname, int why, const char* cppName)
        {
            if (why == OBJ_FINALIZED)
            {
                conv_arg_error(L, 1, "invalid user data");
            }
            if (kname == nullptr)
            {
                if (lua_checkstack(L, 1))
                {
                    conv_arg_error(L, 1, lua_pushfstring(L, "cpp class `%s` not export", cppName));
                }
                conv_arg_error(L, 1, "cpp class not export");
            }
            conv_udata_error(L, idx, kname);
        }
    }

    //========================================================
    // Lua stack operator
    //========================================================

    template <typename T, typename = void> struct LuaStack
    {
        // Marks the primary template (the bound-class path); see luaaa_detail::put_ref.
        enum { LUAAA_BOUND_CLASS_STACK = 1 };

        inline static T& get(lua_State * state, int idx)
        {
            int why = 0;
            void* p = luaaa_detail::to_object(state, idx, &LuaClass<T>::s_typeKey, &why);
            if (p == nullptr)
            {
                luaaa_detail::bad_object(state, idx, LuaClass<T>::klassName(state), why, LUAAA_RTTI_CLASS_NAME(T));
            }
            return *static_cast<T*>(p);
        }

        // A returned C++ object is wrapped as a FULL userdata carrying the class
        // metatable, so methods stay callable on it and the value is type-checked
        // on the way back in (a bare lightuserdata has neither). Three flavors:
        //   put(L, T*)        -- non-owning alias: Lua never deletes it
        //   put(L, const T&)  -- owning copy: constructed in place, destroyed by __gc
        //   put(L, T&&)       -- owning, MOVED in: the way a move-only type is returned
        // A pointer whose class is not bound in this state degrades to a plain
        // lightuserdata (the legacy opaque-handle pattern); a by-value object
        // cannot be represented that way without dangling, so it raises instead.
        inline static void put(lua_State * L, T * t)
        {
            if (!t) { lua_pushnil(L); return; }
            if (!LuaClass<T>::pushMetatable(L))
            {
                lua_pushlightuserdata(L, t);
                return;
            }
            luaaa_detail::UserData* uData = luaaa_detail::new_userdata(L, 0);
            uData->obj = t;                 // non-owning alias: no dtor
            lua_insert(L, -2);
            lua_setmetatable(L, -2);
        }

        inline static void put(lua_State * L, const T & t)
        {
            emplace(L, t);
        }

        inline static void put(lua_State * L, T && t)
        {
            emplace(L, std::move(t));
        }

    private:
        template <typename U>
        inline static void emplace(lua_State * L, U && u)
        {
            if (!LuaClass<T>::pushMetatable(L))
            {
                luaL_argerror(L, 1, lua_pushfstring(L, "cpp class `%s` not export", LUAAA_RTTI_CLASS_NAME(T)));
            }
            // Extra alignof(T)-1 bytes so the in-place object can be aligned even
            // when alignof(T) exceeds the base block's alignment.
            luaaa_detail::UserData* uData = luaaa_detail::new_userdata(L, (alignof(T) - 1) + sizeof(T));
            T* obj = nullptr;
            LUAAA_TRY
            {
                obj = new (luaaa_detail::payload(uData, alignof(T))) T(std::forward<U>(u));
            }
            LUAAA_CATCH(L, "copy object into lua")
            uData->obj = obj;
            uData->dtor = &luaaa_detail::destroy_in_place<T>;
            lua_insert(L, -2);
            lua_setmetatable(L, -2);
        }
    };

    template <typename T> struct LuaStack<const T> : public LuaStack<T> {};

    namespace luaaa_detail {
        // Reference-return dispatch. A reference to a BOUND CLASS aliases the
        // referent (non-owning pointer wrap), mirroring reference semantics --
        // an owning copy would silently detach mutations from the original.
        // Everything else (std::string, bool, numbers, containers, user
        // specializations, ...) forwards to the type's own by-value put:
        // `const std::string&` must stringify, not become a pointer.
        // The int/long priority picks the pointer-wrap overload only when
        // LuaStack<T> is the PRIMARY (bound class) template, detected by its
        // LUAAA_BOUND_CLASS_STACK marker -- probing for "a put that accepts T*"
        // would be too loose: a pointer converts to bool, so LuaStack<bool>
        // would happily take the address of a `const bool&` and push `true`.
        // (A const pointee is const_cast for the alias: non-owning, never
        // deleted -- just do not mutate a truly-const object through it.)
        template <typename T>
        inline auto put_ref(lua_State* L, T* p, int) -> decltype((void)LuaStack<typename std::remove_cv<T>::type>::LUAAA_BOUND_CLASS_STACK, void())
        {
            LuaStack<typename std::remove_cv<T>::type>::put(L, const_cast<typename std::remove_cv<T>::type*>(p));
        }
        template <typename T>
        inline void put_ref(lua_State* L, T* p, long)
        {
            LuaStack<typename std::remove_cv<T>::type>::put(L, *p);
        }
    }

    template <typename T> struct LuaStack<T&> : public LuaStack<T>
    {
        inline static void put(lua_State * L, T & t)
        {
            luaaa_detail::put_ref(L, &t, 0);
        }
    };

    template <typename T> struct LuaStack<const T&> : public LuaStack<T>
    {
        inline static void put(lua_State * L, const T & t)
        {
            luaaa_detail::put_ref(L, &t, 0);
        }
    };

    template <typename T> struct LuaStack<volatile T&> : public LuaStack<T> 
    {
        inline static void put(lua_State * L, volatile T & t)
        {
            LuaStack<T>::put(L, const_cast<const T &>(t));
        }
    };

    template <typename T> struct LuaStack<const volatile T&> : public LuaStack<T>
    {
        inline static void put(lua_State * L, const volatile T & t)
        {
            LuaStack<T>::put(L, const_cast<const T &>(t));
        }
    };

    template <typename T> struct LuaStack<T&&> : public LuaStack<T>
    {
        inline static void put(lua_State * L, T && t)
        {
            LuaStack<T>::put(L, std::forward<T>(t));
        }
    };

    template <typename T> struct LuaStack<T*>
    {
        // Accepts, like a `T&` parameter does: a bound object of class T or of
        // a class derived from it, and a lua-side (luaaa:extend) object. Also
        // a light userdata (an opaque handle, taken on trust), and -- only with
        // LUAAA_NIL_AS_NULLPTR -- nil or a missing argument as nullptr.
        inline static T * get(lua_State * state, int idx)
        {
            // The pointee's cv-qualifiers mean nothing to the Lua side: an object
            // bound as Foo must be passable to a const Foo* parameter.
            typedef typename std::remove_cv<T>::type MT;
            const int t = lua_type(state, idx);
            if (t == LUA_TLIGHTUSERDATA)
            {
                return (T*)lua_touserdata(state, idx);
            }
#if LUAAA_NIL_AS_NULLPTR
            if (t == LUA_TNIL || t == LUA_TNONE)
            {
                return nullptr;
            }
#endif
            int why = 0;
            void* p = luaaa_detail::to_object(state, idx, &LuaClass<MT>::s_typeKey, &why);
            if (p != nullptr)
            {
                return static_cast<MT*>(p);
            }
            // Anything else (nil, a number, a string, an unrelated object):
            // raise instead of handing the C++ side a null pointer to crash on.
            // For a bound class the message is worded like luaL_checkudata's --
            // the lua name of the class and the argument's position; the
            // (mangled) C++ type name is only a fallback for unbound pointees.
            const char * kname = LuaClass<MT>::klassName(state);
            if (kname != nullptr || why == luaaa_detail::OBJ_FINALIZED)
            {
                luaaa_detail::bad_object(state, idx, kname, why, LUAAA_RTTI_CLASS_NAME(T));
            }
            const int arg = (idx > 0) ? idx : 1;
            if (lua_checkstack(state, 1))
            {
                luaaa_detail::conv_arg_error(state, arg, lua_pushfstring(state, "cpp pointer `%s` expected", LUAAA_RTTI_CLASS_NAME(T*)));
            }
            luaaa_detail::conv_arg_error(state, arg, "cpp pointer expected");
        }

        inline static void put(lua_State * L, T * t)
        {
            // Wrap pointers of bound classes as typed, non-owning full userdata
            // (methods stay callable, type is checked on the way back in); fall
            // back to a plain lightuserdata for unbound pointee types.
            typedef typename std::remove_cv<T>::type MT;
            LuaStack<MT>::put(L, const_cast<MT*>(t));
        }
    };

    // Generic floating-point support (float, double, long double). Like the
    // integer partial above it is keyed on a trait, excludes cv/reference forms,
    // and folds every FP type onto one definition -- so `long double` is usable.
    //
    // Overflow is checked against lua_Number (Lua's number type) at run time:
    //   * get: a Lua number that does not fit the narrower C++ target
    //          (e.g. a double-valued lua_Number into a `float`) raises an error;
    //   * put: a C++ value outside lua_Number's range
    //          (e.g. a huge `long double` when lua_Number is double) raises an error.
    // Where the C++ type is at least as wide as lua_Number the guard bound
    // becomes +inf, so it never triggers a false positive. Precision loss within
    // range (mantissa truncation, or any FP on Lua 5.1/5.2/LuaJIT) is inherent to
    // Lua and is not reported.
    template<typename T>
    struct LuaStack<T, typename std::enable_if<
        std::is_floating_point<T>::value
        && std::is_same<T, typename std::remove_cv<T>::type>::value>::type>
    {
        inline static T get(lua_State * L, int idx)
        {
            // Symmetric with the integer specialization: accept a number, or a
            // string that parses as one; reject everything else. A stray string
            // like "hello" used to slip through (lua_isstring is true for any
            // string) and yield 0 via lua_tonumber. Now we require the string to
            // actually parse (lua_isnumber) and validate via lua_tonumberx's isnum.
            const int t = lua_type(L, idx);
            if (t == LUA_TNUMBER || (t == LUA_TSTRING && lua_isnumber(L, idx)))
            {
                int isnum = 0;
                const lua_Number v = LUAAA_tonumberx(L, idx, &isnum);
                if (!isnum)
                {
                    luaaa_detail::conv_error(L, "bad number (not a valid floating-point value)");
                }
                // Compare in long double: it holds both lua_Number and T's max,
                // so we never narrow an out-of-range value (that narrowing --
                // e.g. static_cast<float>(DBL_MAX) -- would be undefined behavior).
                // Infinity is a value every IEEE type holds, not an overflow:
                // only a FINITE number beyond T's range is rejected.
                const long double lv = static_cast<long double>(v);
                const long double hi = static_cast<long double>(std::numeric_limits<T>::max());
                const long double inf = std::numeric_limits<long double>::infinity();
                if ((lv > hi && lv != inf) || (lv < -hi && lv != -inf))
                {
                    luaaa_detail::conv_error(L, "number overflow: value does not fit target floating type");
                }
                return static_cast<T>(v);
            }
            else
            {
                luaaa_detail::conv_check_type(L, idx, LUA_TNUMBER);
            }
            return 0;
        }

        inline static void put(lua_State * L, const T & t)
        {
            // Same rationale as get(): widen to long double before comparing so
            // static_cast<T>(numeric_limits<lua_Number>::max()) never narrows
            // out of range.
            const long double lv = static_cast<long double>(t);
            const long double hi = static_cast<long double>(std::numeric_limits<lua_Number>::max());
            const long double inf = std::numeric_limits<long double>::infinity();
            if ((lv > hi && lv != inf) || (lv < -hi && lv != -inf))   // +-inf passes through (see get)
            {
                luaL_error(L, "number overflow: value out of lua_Number range");
            }
            lua_pushnumber(L, static_cast<lua_Number>(t));
        }
    };

    // Enumerations (plain `enum` and `enum class`) travel as their underlying
    // integer type, with that type's checks: a non-number is a type error, a
    // number outside the underlying type's range is an overflow error. A value
    // inside the range that names no enumerator is NOT rejected (C++ does not
    // reject it either). A full specialization `LuaStack<MyEnum>` written by the
    // user still takes precedence over this one.
    template<typename T>
    struct LuaStack<T, typename std::enable_if<
        std::is_enum<T>::value
        && std::is_same<T, typename std::remove_cv<T>::type>::value>::type>
    {
        typedef typename std::underlying_type<T>::type underlying;

        inline static T get(lua_State * L, int idx)
        {
            return static_cast<T>(LuaStack<underlying>::get(L, idx));
        }

        inline static void put(lua_State * L, const T & t)
        {
            LuaStack<underlying>::put(L, static_cast<underlying>(t));
        }
    };

    template<>
    struct LuaStack<bool>
    {
        // Strict boolean: only the Lua `boolean` type is accepted (true/false).
        // Numbers/strings/nil raise an argument error rather than being coerced,
        // so type mistakes on the script side surface immediately. This mirrors
        // Lua's own type-strict operations (e.g. `true + 1` errors) instead of
        // the looser truthiness rule of `if x then`, which would silently turn
        // every value into a bool and mask caller bugs.
        inline static bool get(lua_State * L, int idx)
        {
            luaaa_detail::conv_check_type(L, idx, LUA_TBOOLEAN);
            return lua_toboolean(L, idx) != 0;
        }

        inline static void put(lua_State * L, const bool & t)
        {
            lua_pushboolean(L, t);
        }
    };

    // Generic integer support for arithmetic types that don't have an explicit
    // specialization above (long, unsigned, long long, short, char, size_t, ...).
    // It is keyed on a type trait, so platform-dependent typedef aliases such as
    // size_t / int64_t / intptr_t all collapse onto this ONE partial
    // specialization -- there is no per-alias definition to collide across
    // LP64 / LLP64 / ILP32 targets. `bool` and cv-qualified/reference forms are
    // excluded so the explicit `int` specialization and the `const T` / `T&`
    // forwarders keep priority.
    //
    // Precision note: a value outside lua_Integer's range -- e.g. a large
    // unsigned 64-bit value, or ANY 64-bit value on Lua 5.1/5.2/LuaJIT where
    // numbers are doubles -- is truncated/rounded. That is an inherent Lua
    // limitation, not something this converter can avoid.
    //
    // get: a non-numeric string (e.g. "hello") or a float-valued string that is
    //   not an integer (e.g. "1e30" on 5.3+) is REJECTED via luaL_argerror rather
    //   than silently yielding 0. We use lua_tointegerx() which reports whether
    //   the conversion succeeded, so we no longer rely on the loose lua_isstring()
    //   check that admitted any string.
    // put: symmetric with the floating-point specialization -- a value that does
    //   not fit lua_Integer's range raises luaL_error instead of being silently
    //   narrowed (wrapping). Values Lua's number type cannot represent exactly
    //   (e.g. 64-bit ints when lua_Number is double) remain an inherent Lua limit.
    namespace luaaa_detail {
        // Does an integer of a type WIDER than lua_Integer fit into it?
        // (Narrower and equally wide types never get here -- see put below.)
        template<typename T>
        inline bool fits_wide(T v, std::true_type /*signed*/)
        {
            return v >= static_cast<T>((std::numeric_limits<lua_Integer>::min)())
                && v <= static_cast<T>((std::numeric_limits<lua_Integer>::max)());
        }
        template<typename T>
        inline bool fits_wide(T v, std::false_type /*unsigned*/)
        {
            return v <= static_cast<T>((std::numeric_limits<lua_Integer>::max)());
        }
        template<typename T>
        inline bool fits_lua_integer(T v, std::true_type /*wider*/)
        {
            return fits_wide(v, std::integral_constant<bool, std::is_signed<T>::value>());
        }
        template<typename T>
        inline bool fits_lua_integer(T, std::false_type)
        {
            return true;
        }
        template<typename T>
        inline bool fits_lua_integer(T v)
        {
            return fits_lua_integer(v, std::integral_constant<bool, (sizeof(T) > sizeof(lua_Integer))>());
        }
    }

    template<typename T>
    struct LuaStack<T, typename std::enable_if<
        std::is_integral<T>::value
        && !std::is_same<T, bool>::value
        && std::is_same<T, typename std::remove_cv<T>::type>::value>::type>
    {
        inline static T get(lua_State * L, int idx)
        {
            // Accept a number, or a string that parses as one; reject everything
            // else (a stray string like "hello" used to slip through and yield 0).
            // Mirrors Lua arithmetic: `1 + '2'` works, but `1 + true` / `1 + nil`
            // raise "attempt to perform arithmetic on a boolean/nil value".
            const int t = lua_type(L, idx);
            if (t == LUA_TNUMBER || (t == LUA_TSTRING && lua_isnumber(L, idx)))
            {
                int isnum = 0;
                lua_Integer v = LUAAA_tointegerx(L, idx, &isnum);
                if (!isnum)
                {
                    luaaa_detail::conv_error(L, "bad number (not an integer)");
                }
                // Symmetric with put and with the floating-point get: refuse to
                // narrow. A value outside T's range used to wrap silently
                // (300 -> uint8_t 44, 2^40 -> int 0, -1 -> unsigned 4294967295).
                // Types as wide as lua_Integer need no check; an UNSIGNED type
                // that wide keeps accepting negative values as their bit
                // pattern -- the only way a script can spell a 64-bit mask
                // such as 0xFFFFFFFFFFFFFFFF, which is -1 as a lua_Integer.
                // (LUAAA_CHECK_INTEGER_RANGE=0 restores the silent wrap.)
#if LUAAA_CHECK_INTEGER_RANGE
                if (sizeof(T) < sizeof(lua_Integer)
                    && (v < static_cast<lua_Integer>((std::numeric_limits<T>::min)())
                        || v > static_cast<lua_Integer>((std::numeric_limits<T>::max)())))
                {
                    luaaa_detail::conv_error(L, "number overflow: value does not fit target integer type");
                }
#endif
                return static_cast<T>(v);
            }
            luaaa_detail::conv_check_type(L, idx, LUA_TNUMBER);
            return 0;
        }

        inline static void put(lua_State * L, const T & t)
        {
            // The mirror image of get, decided with INTEGER comparisons only (a
            // detour through floating point rounds INT64_MAX up to 2^63 where
            // long double is just double -- arm64, MSVC -- which made
            // uint64_t(1) << 63 pass and wrap while UINT64_MAX was refused):
            //   * a type narrower than lua_Integer, or a signed one as wide,
            //     always fits;
            //   * an UNSIGNED type as wide as lua_Integer travels as its bit
            //     pattern, exactly as get accepts it: UINT64_MAX arrives as
            //     -1 and -1 comes back as UINT64_MAX, so every value round-trips;
            //   * only a type WIDER than lua_Integer can overflow, and then
            //     raises (LUAAA_CHECK_INTEGER_RANGE=0: wraps silently).
#if LUAAA_CHECK_INTEGER_RANGE
            if (!luaaa_detail::fits_lua_integer(t))
            {
                luaL_error(L, "number overflow: integer value out of lua_Integer range");
            }
#endif
            lua_pushinteger(L, static_cast<lua_Integer>(t));
        }
    };

    template<>
    struct LuaStack<const char *>
    {
        // LIFETIME: the returned pointer points into the Lua VM's internal
        // string storage. It is valid only for as long as the Lua value stays
        // alive -- in practice, for the duration of this bound call (the Lua
        // arguments remain on the stack, unpopped, until the call returns).
        // Storing it across calls (in a member/global) is a DANGLING-POINTER
        // hazard: once the value is popped or GC'd the pointer is stale.
        // To keep a string beyond the call, take a std::string parameter
        // (LuaStack<std::string> deep-copies it) or copy into your own buffer
        // (the only option in embedded/no-stdlib mode -- see example/embedded.cpp).
        // char* below inherits the exact same rule via const_cast.
        inline static const char * get(lua_State * L, int idx)
        {
            switch (lua_type(L, idx))
            {
            case LUA_TBOOLEAN:
                return (lua_toboolean(L, idx) ? "true" : "false");
            case LUA_TNUMBER:
                return lua_tostring(L, idx);
            case LUA_TSTRING:
                return lua_tostring(L, idx);
            default:
                luaaa_detail::conv_check_type(L, idx, LUA_TSTRING);
                break;
            }
            return "";
        }

        inline static void put(lua_State * L, const char * s)
        {
            lua_pushstring(L, s);
        }
    };

    template<>
    struct LuaStack<char *>
    {
        inline static char * get(lua_State * L, int idx)
        {
            return const_cast<char*>(LuaStack<const char *>::get(L, idx));
        }

        inline static void put(lua_State * L, const char * s)
        {
            LuaStack<const char *>::put(L, s);
        }
    };

#if !LUAAA_WITHOUT_CPP_STDLIB
    // std::string carries its length in both directions, so a string with
    // embedded NUL bytes ("a\0b") survives the trip. (`const char*` cannot:
    // a C string ends at its first NUL.)
    template<>
    struct LuaStack<std::string>
    {
        inline static std::string get(lua_State * L, int idx)
        {
            switch (lua_type(L, idx))
            {
            case LUA_TBOOLEAN:
                return (lua_toboolean(L, idx) ? "true" : "false");
            case LUA_TNUMBER:
            case LUA_TSTRING:
            {
                size_t len = 0;
                const char * s = lua_tolstring(L, idx, &len);
                return std::string(s, len);
            }
            default:
                luaaa_detail::conv_check_type(L, idx, LUA_TSTRING);
                break;
            }
            return std::string();
        }

        inline static void put(lua_State * L, const std::string& s)
        {
            lua_pushlstring(L, s.data(), s.size());
        }
    };
#endif

    template<>
    struct LuaStack<lua_State *>
    {
        inline static lua_State * get(lua_State * L, int)
        {
            return L;
        }

        inline static void put(lua_State * L, lua_State *)
        {
            // A lua_State* is a host handle, not a serializable Lua value. Push
            // it as an opaque lightuserdata so argument counts stay balanced --
            // a no-op here would silently drop one argument, and lua_pcall would
            // then consume the function itself (or a neighbor) as an argument.
            lua_pushlightuserdata(L, L);
        }
    };

    // push ret data to stack. The value is forwarded, so a returned temporary
    // is MOVED into Lua -- a move-only class can be returned by value.
    template <typename T>
    inline void LuaStackReturn(lua_State * L, T && t)
    {
        lua_settop(L, 0);
        LuaStack<T>::put(L, std::forward<T>(t));
    }

    //========================================================
    // lua callback error handler
    //========================================================
    // A lua function that C++ calls back (a std::function / function pointer
    // parameter) may fail: the script raises, or its result cannot be converted.
    // The callback then returns RET() to its C++ caller -- raising a lua error
    // there would longjmp across the caller's C++ frames. To learn about such
    // failures, register a handler for the lua_State (one per VM; nullptr
    // removes it):
    //
    //     luaaa::setCallbackErrorHandler(L, [](lua_State*, const char* msg, void*) {
    //         fprintf(stderr, "lua callback failed: %s\n", msg);
    //     });
    //
    // The handler runs on the thread that invoked the callback, after the
    // failed protected call has been left, so it may also THROW a C++ exception
    // to make the failure visible to the code that invoked the callback.
    typedef void (*CallbackErrorHandler)(lua_State* L, const char* message, void* userdata);

    namespace luaaa_detail {
        struct CallbackErrorHook { CallbackErrorHandler fn; void* userdata; };
        inline const void* CallbackErrorHookKey() { static char k = 0; return &k; }
    }

    inline void setCallbackErrorHandler(lua_State * L, CallbackErrorHandler handler, void * userdata = nullptr)
    {
        if (handler)
        {
            luaaa_detail::CallbackErrorHook* hook = (luaaa_detail::CallbackErrorHook*)lua_newuserdata(L, sizeof(luaaa_detail::CallbackErrorHook));
            hook->fn = handler;
            hook->userdata = userdata;
        }
        else
        {
            lua_pushnil(L);
        }
        LUAAA_rawsetp(L, LUA_REGISTRYINDEX, luaaa_detail::CallbackErrorHookKey());
    }

    // A protected call made for a C++ callback failed; the error value is on
    // top of the stack. We must NOT lua_error() here: that longjmps across the
    // intervening C++ stack frames (skipping their destructors -> UB) and, if
    // the callback was invoked outside any pcall (e.g. from another thread or
    // from pure C++), there is no setjmp target so Lua would panic/abort.
    // Instead hand the message to the registered handler (if any) and pop it,
    // so the stack stays balanced; the callback returns a default value.
    inline void HandleCallbackError(lua_State * L)
    {
        struct PopGuard
        {
            lua_State* state;
            ~PopGuard() { lua_pop(state, 1); }     // also when the handler throws
        } guard = { L };
        const char * msg = lua_tostring(L, -1);
        if (!msg) { msg = "(error object is not a string)"; }
#if LUAAA_DEBUG
        fprintf(stderr, "luaaa: error in lua callback: %s\n", msg);
#endif
        if (lua_checkstack(L, 1))
        {
            LUAAA_rawgetp(L, LUA_REGISTRYINDEX, luaaa_detail::CallbackErrorHookKey());
            const luaaa_detail::CallbackErrorHook* hook = (const luaaa_detail::CallbackErrorHook*)lua_touserdata(L, -1);
            lua_pop(L, 1);
            if (hook && hook->fn)
            {
                hook->fn(L, msg, hook->userdata);
            }
        }
    }

    //========================================================
    // index generation helper
    //========================================================
    template<std::size_t... Ns>
    struct indices
    {
        using next = indices<Ns..., sizeof...(Ns)>;
    };

    template<std::size_t N>
    struct make_indices
    {
        using type = typename make_indices<N - 1>::type::next;
    };

    template<>
    struct make_indices<0>
    {
        using type = indices<>;
    };

    //========================================================
    // lua -> C++ callback plumbing
    //========================================================
    namespace luaaa_detail {
        // The state a callback must run on. A callback is usually handed over
        // from whatever thread happens to be running (often a coroutine), but it
        // may be invoked long after that coroutine is dead and collected --
        // keeping its lua_State* would be a dangling pointer. The main thread
        // lives exactly as long as the VM, and the function ref itself sits in
        // the registry, which all threads of a VM share.
        //   5.2+ : the registry holds the main thread (LUA_RIDX_MAINTHREAD).
        //   5.1 / LuaJIT: there is no such slot, so remember the main thread the
        //          first time we are called on it (LuaClass / LuaModule do that at
        //          bind time). If it was never seen, fall back to the given state.
        inline lua_State* MainThread(lua_State* L)
        {
#if defined(LUA_RIDX_MAINTHREAD)
            lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
            lua_State* mainL = lua_tothread(L, -1);
            lua_pop(L, 1);
            return mainL ? mainL : L;
#else
            static char s_mainThreadKey = 0;
            if (lua_pushthread(L))                  // L is the main thread: remember it
            {
                LUAAA_rawsetp(L, LUA_REGISTRYINDEX, &s_mainThreadKey);
                return L;
            }
            lua_pop(L, 1);
            LUAAA_rawgetp(L, LUA_REGISTRYINDEX, &s_mainThreadKey);
            lua_State* mainL = lua_tothread(L, -1);
            lua_pop(L, 1);
            return mainL ? mainL : L;
#endif
        }

        // Everything a protected callback run needs, passed as one lightuserdata.
        struct CallbackContext
        {
            int ref;                // registry ref of the lua function
            void* const* argv;      // addresses of the C++ arguments, in order
            void* result;           // where the converted result goes (unused for void)
        };

        template<typename T>
        inline void* erase_addr(T& t)
        {
            return const_cast<void*>(static_cast<const volatile void*>(&t));
        }

        // Where a callback keeps its result while the protected call runs
        // (nothing for a void callback).
        template<typename RET>
        struct CallbackResult
        {
            RET value;
            CallbackResult() : value() {}
            void* addr() { return erase_addr(value); }
            RET take() { return std::move(value); }
        };

        template<>
        struct CallbackResult<void>
        {
            void* addr() { return nullptr; }
            void take() {}
        };

        // Run `fn(ctx)` under a protected call on L. Returns false if it raised
        // (the error has been reported and popped; the stack is left balanced).
        inline bool RunProtected(lua_State* L, lua_CFunction fn, void* ctx)
        {
#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM <= 501
            const int status = lua_cpcall(L, fn, ctx);
#else
            if (!lua_checkstack(L, 2))
            {
                return false;
            }
            lua_pushcfunction(L, fn);
            lua_pushlightuserdata(L, ctx);
            const int status = lua_pcall(L, 1, 0, 0);
#endif
            if (status != 0)
            {
                HandleCallbackError(L);
                return false;
            }
            return true;
        }

        // Fetch the lua function, convert + push the C++ arguments, call it.
        // Runs INSIDE the protected call: a failing conversion (e.g. an unbound
        // class passed by value, a number out of range) or an error in the lua
        // function itself is a plain lua error here.
        template<typename... ARGS, std::size_t... Ns>
        inline void CallbackPushAndCall(lua_State* L, const CallbackContext* ctx, int nresults, indices<Ns...>)
        {
            luaL_checkstack(L, (int)sizeof...(ARGS) + 1, "too many callback arguments");
            lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->ref);
            LUAAA_TRY
            {
                // a by-value parameter is the callback's own copy: move it into lua
                int initParams[] = { (LuaStack<ARGS>::put(L, static_cast<ARGS&&>(*static_cast<typename std::remove_reference<ARGS>::type*>(ctx->argv[Ns]))), 0)..., 0 }; (void)initParams;
            }
            LUAAA_CATCH(L, "lua callback argument")
            lua_call(L, (int)sizeof...(ARGS), nresults);
        }

        // The lua_CFunction a callback runs under RunProtected. The WHOLE round
        // trip -- arguments in, call, result out -- happens inside one protected
        // call, so nothing here can lua_error (longjmp) across the C++ frames of
        // whoever invoked the callback, or panic when that caller is plain C++
        // with no enclosing pcall.
        template<typename RET, typename... ARGS>
        struct CallbackRunner
        {
            static int Run(lua_State* L)
            {
                const CallbackContext* ctx = (const CallbackContext*)lua_touserdata(L, 1);
                CallbackPushAndCall<ARGS...>(L, ctx, 1, typename make_indices<sizeof...(ARGS)>::type());
                LUAAA_TRY
                {
                    *static_cast<RET*>(ctx->result) = LuaStack<RET>::get(L, lua_gettop(L));
                }
                LUAAA_CATCH(L, "lua callback result")
                return 0;
            }
        };

        template<typename... ARGS>
        struct CallbackRunner<void, ARGS...>
        {
            static int Run(lua_State* L)
            {
                const CallbackContext* ctx = (const CallbackContext*)lua_touserdata(L, 1);
                CallbackPushAndCall<ARGS...>(L, ctx, 0, typename make_indices<sizeof...(ARGS)>::type());
                return 0;
            }
        };
    }

// Raw function-pointer callback: a non-capturing function pointer cannot carry
// context, so (L, ref) live in per-signature slots. Those slots are LUAAA_THREAD_LOCAL,
// so each thread has its own copy -- the cross-thread data race on cacheLuaState/
// cacheLuaFuncId is eliminated and "one lua_State per thread" usage is safe.
//
// Inherent limitations that remain (they stem from the context-free function pointer,
// NOT from thread safety): within a single thread only ONE callback of a given
// signature can be live at a time (re-registering overwrites the slot); it is not
// re-entrant; and the callback must be invoked on the same thread that obtained it
// (a bare function pointer carries no state to reach another thread's slot). For
// multiple / re-entrant / cross-thread callbacks use std::function (it captures L+ref).
// (H1 is fixed: the ref is no longer unref'd after the first call, so a single
// callback may be invoked repeatedly; the ref then leaks until the state is closed.)
//
// The slot holds the MAIN thread of the VM (see luaaa_detail::MainThread), and the
// call runs entirely under one protected call (see luaaa_detail::CallbackRunner):
// a lua error, or a result/argument that cannot be converted, makes the callback
// return RET() -- it never longjmps out of the callback.
#define LUAAA_IMPLEMENT_CALLBACK_INVOKER(CALLCONV) \
    template<typename RET, typename ...ARGS> \
    struct LuaStack<RET(CALLCONV*)(ARGS...)> \
    { \
        typedef RET(CALLCONV*FTYPE)(ARGS...); \
        inline static FTYPE get(lua_State * L, int idx) \
        { \
            LUAAA_THREAD_LOCAL lua_State * cacheLuaState = nullptr; \
            LUAAA_THREAD_LOCAL int cacheLuaFuncId = 0; \
            struct HelperClass \
            { \
                static RET CALLCONV f_callback(ARGS... args) \
                { \
                    luaaa_detail::CallbackResult<RET> result; \
                    if (cacheLuaState) \
                    { \
                        void* argv[] = { luaaa_detail::erase_addr(args)..., nullptr }; \
                        luaaa_detail::CallbackContext ctx = { cacheLuaFuncId, argv, result.addr() }; \
                        luaaa_detail::RunProtected(cacheLuaState, &luaaa_detail::CallbackRunner<RET, ARGS...>::Run, &ctx); \
                    } \
                    return result.take(); \
                } \
            }; \
            if (lua_isfunction(L, idx)) \
            { \
                cacheLuaState = luaaa_detail::MainThread(L); \
                lua_pushvalue(L, idx); \
                cacheLuaFuncId = luaL_ref(L, LUA_REGISTRYINDEX); \
                return HelperClass::f_callback; \
            } \
            return nullptr; \
        } \
        inline static void put(lua_State * L, FTYPE f) \
        { \
            /* A bare lua_pushcfunction cannot carry f; the Invoke wrapper reads \
               the callable from upvalue(1), so store it in a holder userdata. */ \
            luaaa_detail::push_holder<FTYPE>(L, f); \
            lua_pushcclosure(L, NonMemberFunctionCaller(f), 1); \
        } \
    };

#if !LUAAA_WITHOUT_CPP_STDLIB
    // std::function callback: a captured lambda carries (L, ref) per callback, so it
    // supports being called multiple times, multiple callbacks of the same signature
    // living at once, and re-entrancy. The lua ref is released only when the callback
    // object (and all its copies) is destroyed, via a shared_ptr guard.
    // The captured state is the VM's MAIN thread (not the possibly short-lived
    // coroutine that passed the function in), and each call runs entirely under one
    // protected call: on a lua error or an unconvertible argument/result the
    // callback returns RET() instead of raising across the caller's C++ frames.
    // NOTE: the callback must not outlive its lua_State (the deleter calls luaL_unref).
    template<typename RET, typename ...ARGS>
    struct LuaStack<std::function<RET(ARGS...)>>
    {
        typedef std::function<RET(ARGS...)> FTYPE;
        inline static FTYPE get(lua_State * L, int idx)
        {
            if (!lua_isfunction(L, idx))
            {
                return nullptr;
            }
            lua_State * mainL = luaaa_detail::MainThread(L);
            lua_pushvalue(L, idx);
            int ref = luaL_ref(L, LUA_REGISTRYINDEX);
            std::shared_ptr<void> guard(nullptr, [mainL, ref](void*) {
                luaL_unref(mainL, LUA_REGISTRYINDEX, ref);
            });
            return [mainL, ref, guard](ARGS... args) -> RET {
                luaaa_detail::CallbackResult<RET> result;
                void* argv[] = { luaaa_detail::erase_addr(args)..., nullptr };
                luaaa_detail::CallbackContext ctx = { ref, argv, result.addr() };
                luaaa_detail::RunProtected(mainL, &luaaa_detail::CallbackRunner<RET, ARGS...>::Run, &ctx);
                return result.take();
            };
        }
        inline static void put(lua_State * L, const FTYPE& f)
        {
            // See the function-pointer variant: the callable must travel as an
            // upvalue (a holder userdata), not inside a bare lua_pushcfunction.
            luaaa_detail::push_holder<FTYPE>(L, f);
            lua_pushcclosure(L, NonMemberFunctionCaller(f), 1);
        }
    };
#endif

    // A lua_State* parameter is "transparent": LuaStack<lua_State*>::get ignores
    // its stack slot and returns the live state, so it consumes no Lua argument.
    // The argument-index generator below uses this trait to skip such slots, so
    // that f(lua_State*, int) receives the int from argument #1 (not #2), matching
    // the documented "consumes no argument" contract (B8 fix).
    // NOTE: every lua_State* parameter resolves to the *current* state; there is no
    // way to pass a distinct state through a lightuserdata without an unverifiable
    // void*->lua_State* cast (Lua lightuserdata carries no type tag), so cross-VM
    // signatures like f(lua_State*, lua_State*, ...) fold all state params onto one L.
    template<typename T> struct is_lua_state : std::false_type {};
    template<> struct is_lua_state<lua_State*> : std::true_type {};
    // const / reference / cv variants all decay to lua_State*.
    template<typename T> struct is_lua_state_decayed : is_lua_state<typename std::decay<T>::type> {};

    // Like make_indices, but each lua_State* parameter does NOT advance the stack
    // index: it emits the current running count (ignored by get) without increment.
    // Result indices<i0,i1,...>: ik = number of real (non-state) args before slot k,
    // so a real parameter receives a contiguous 0,1,2,... and the call site's
    // `Ns + base` lands each real arg on the correct Lua stack position.
    template<std::size_t Cur, typename Seq, typename... Args>
    struct make_arg_indices_impl;

    template<std::size_t Cur, std::size_t... Is, typename Head, typename... Tail>
    struct make_arg_indices_impl<Cur, indices<Is...>, Head, Tail...>
    {
    private:
        static const std::size_t emit = Cur;
        static const std::size_t next = is_lua_state_decayed<Head>::value ? Cur : (Cur + 1);
    public:
        using type = typename make_arg_indices_impl<next, indices<Is..., emit>, Tail...>::type;
    };

    template<std::size_t Cur, std::size_t... Is>
    struct make_arg_indices_impl<Cur, indices<Is...>>
    {
        using type = indices<Is...>;
    };

    template<typename... Args>
    struct make_arg_indices : make_arg_indices_impl<0, indices<>, Args...> {};

    // Number of Lua arguments a parameter list consumes (lua_State* takes none).
    template<typename... Args> struct count_lua_args;
    template<> struct count_lua_args<> { enum { value = 0 }; };
    template<typename Head, typename... Tail>
    struct count_lua_args<Head, Tail...>
    {
        enum { value = (is_lua_state_decayed<Head>::value ? 0 : 1) + count_lua_args<Tail...>::value };
    };

    // A constructor pushes the userdata for the new object BEFORE it converts
    // its arguments (the object is built inside that block). The userdata then
    // sits right after the last argument that was passed -- exactly where a
    // MISSING argument would be read from, giving "number expected, got
    // userdata". Fill the missing positions with nil first, so an omitted
    // argument reads as nil, as it does everywhere else in Lua.
    template<typename... Args>
    inline void PadMissingArgs(lua_State * L)
    {
        const int expected = count_lua_args<Args...>::value;
        if (lua_gettop(L) < expected)
        {
            lua_settop(L, expected);
        }
    }

    //========================================================
    // callers: the lua_CFunction behind every bound function
    //========================================================
    // One template serves a value-returning and a void function alike: the
    // result (if any) is pushed by ReturnPusher.
    template<typename TRET>
    struct ReturnPusher
    {
        template<typename CALL>
        static int run(lua_State* state, const CALL& call)
        {
            LuaStackReturn<TRET>(state, call());
            return 1;
        }
    };

    template<>
    struct ReturnPusher<void>
    {
        template<typename CALL>
        static int run(lua_State*, const CALL& call)
        {
            call();
            return 0;
        }
    };

    template<typename TRET, typename FTYPE, typename ...ARGS, std::size_t... Ns>
    TRET LuaInvokeImpl(lua_State* state, void* calleePtr, size_t skip, indices<Ns...>)
    {
        // A failing argument conversion or a C++ exception thrown by the call
        // becomes a lua error at this boundary (see "error plumbing").
        LUAAA_BOUNDARY_TRY(calleePtr)
        {
            return (*(FTYPE*)(calleePtr))(LuaStack<ARGS>::get(state, (int)(Ns + 1 + skip))...);
        }
        LUAAA_BOUNDARY_CATCH(state, "function call")
    }

    template<typename TRET, typename FTYPE, typename ...ARGS>
    inline TRET LuaInvoke(lua_State* state, void* calleePtr, size_t skip)
    {
        // make_arg_indices skips lua_State* slots so they consume no Lua argument (B8 fix).
        return LuaInvokeImpl<TRET, FTYPE, ARGS...>(state, calleePtr, skip, typename make_arg_indices<ARGS...>::type());
    }

    // A callable stored in the closure's upvalue 1 and called with the lua
    // arguments that follow the first SKIP ones (SKIP = 1: a class function
    // called as obj:f(...), whose `self` is not passed on).
    template<int SKIP, typename FTYPE, typename TRET, typename ...ARGS>
    struct FunctionInvoker
    {
        struct Call
        {
            lua_State* state;
            void* callee;
            TRET operator()() const { return LuaInvoke<TRET, FTYPE, ARGS...>(state, callee, SKIP); }
        };

        static int Invoke(lua_State* state)
        {
            void * calleePtr = lua_touserdata(state, lua_upvalueindex(1));
            luaL_argcheck(state, calleePtr, 1, "cpp closure function not found.");
            const Call call = { state, calleePtr };
            return ReturnPusher<TRET>::run(state, call);
        }
    };

#define LUAAA_IMPLEMENT_FUNCTION_CALLER(CALLCONV) \
    template<typename TRET, typename ...ARGS> \
    inline lua_CFunction NonMemberFunctionCaller(TRET(CALLCONV*)(ARGS...)) \
    { \
        return &FunctionInvoker<0, TRET(CALLCONV*)(ARGS...), TRET, ARGS...>::Invoke; \
    } \
    template<typename TRET, typename ...ARGS> \
    inline lua_CFunction MemberFunctionCaller(TRET(CALLCONV*)(ARGS...)) \
    { \
        return &FunctionInvoker<1, TRET(CALLCONV*)(ARGS...), TRET, ARGS...>::Invoke; \
    }

#if defined(_MSC_VER)
    LUAAA_IMPLEMENT_FUNCTION_CALLER(__cdecl)
    LUAAA_IMPLEMENT_CALLBACK_INVOKER(__cdecl)

#	ifdef _M_CEE
    LUAAA_IMPLEMENT_FUNCTION_CALLER(__clrcall)
    LUAAA_IMPLEMENT_CALLBACK_INVOKER(__clrcall)
#	endif

#	if defined(_M_IX86) && !defined(_M_CEE)
    LUAAA_IMPLEMENT_FUNCTION_CALLER(__fastcall)
    LUAAA_IMPLEMENT_CALLBACK_INVOKER(__fastcall)
#	endif

#	ifdef _M_IX86
    LUAAA_IMPLEMENT_FUNCTION_CALLER(__stdcall)
    LUAAA_IMPLEMENT_CALLBACK_INVOKER(__stdcall)
#	endif

#	if ((defined(_M_IX86) && _M_IX86_FP >= 2) || defined(_M_X64)) && !defined(_M_CEE)
    LUAAA_IMPLEMENT_FUNCTION_CALLER(__vectorcall)
    LUAAA_IMPLEMENT_CALLBACK_INVOKER(__vectorcall)
#	endif
#else
#	define LUAAA_NOTHING
    LUAAA_IMPLEMENT_FUNCTION_CALLER(LUAAA_NOTHING)
    LUAAA_IMPLEMENT_CALLBACK_INVOKER(LUAAA_NOTHING)
#	undef LUAAA_NOTHING
#endif

    //========================================================
    // member function invoker
    //========================================================
    template<typename TCLASS, typename TRET, typename FTYPE, typename ...ARGS, std::size_t... Ns>
    TRET LuaInvokeInstanceMemberImpl(lua_State* state, void* calleePtr, indices<Ns...>)
    {
        LUAAA_BOUNDARY_TRY(calleePtr)
        {
            return (LuaStack<TCLASS>::get(state, 1).**(FTYPE*)(calleePtr))(LuaStack<ARGS>::get(state, (int)(Ns + 2))...);
        }
        LUAAA_BOUNDARY_CATCH(state, "member function call")
    }

    template<typename TCLASS, typename TRET, typename FTYPE, typename ...ARGS>
    inline TRET LuaInvokeInstanceMember(lua_State* state, void* calleePtr)
    {
        // make_arg_indices skips lua_State* slots so they consume no Lua argument (B8 fix).
        return LuaInvokeInstanceMemberImpl<TCLASS, TRET, FTYPE, ARGS...>(state, calleePtr, typename make_arg_indices<ARGS...>::type());
    }

    // A member function pointer stored in upvalue 1 and applied to `self`
    // (argument 1) read as a TCLASS. TCLASS is the class being bound, which
    // may be DERIVED from the class that declares the member: binding
    // &Dog::name where name() is inherited from Animal needs no bound Animal.
    template<typename TCLASS, typename FTYPE, typename TRET, typename ...ARGS>
    struct MethodInvoker
    {
        struct Call
        {
            lua_State* state;
            void* callee;
            TRET operator()() const { return LuaInvokeInstanceMember<TCLASS, TRET, FTYPE, ARGS...>(state, callee); }
        };

        static int Invoke(lua_State* state)
        {
            void * calleePtr = lua_touserdata(state, lua_upvalueindex(1));
            luaL_argcheck(state, calleePtr, 1, "cpp closure function not found.");
            const Call call = { state, calleePtr };
            return ReturnPusher<TRET>::run(state, call);
        }
    };

    template<typename TCLASS, typename FCLASS, typename TRET, typename ...ARGS>
    inline lua_CFunction MethodCaller(TRET(FCLASS::*)(ARGS...))
    {
        return &MethodInvoker<TCLASS, TRET(FCLASS::*)(ARGS...), TRET, ARGS...>::Invoke;
    }

    template<typename TCLASS, typename FCLASS, typename TRET, typename ...ARGS>
    inline lua_CFunction MethodCaller(TRET(FCLASS::*)(ARGS...) const)
    {
        return &MethodInvoker<TCLASS, TRET(FCLASS::*)(ARGS...) const, TRET, ARGS...>::Invoke;
    }

    template<typename TCLASS, typename TRET, typename ...ARGS>
    inline lua_CFunction MemberFunctionCaller(TRET(TCLASS::*func)(ARGS...))
    {
        return MethodCaller<TCLASS>(func);
    }

    template<typename TCLASS, typename TRET, typename ...ARGS>
    inline lua_CFunction MemberFunctionCaller(TRET(TCLASS::*func)(ARGS...) const)
    {
        return MethodCaller<TCLASS>(func);
    }

#if !LUAAA_WITHOUT_CPP_STDLIB
    template<typename TRET, typename ...ARGS>
    inline lua_CFunction MemberFunctionCaller(const std::function<TRET(ARGS...)>&)
    {
        return &FunctionInvoker<1, std::function<TRET(ARGS...)>, TRET, ARGS...>::Invoke;
    }

    template<typename TRET, typename ...ARGS>
    inline lua_CFunction NonMemberFunctionCaller(const std::function<TRET(ARGS...)>&)
    {
        return &FunctionInvoker<0, std::function<TRET(ARGS...)>, TRET, ARGS...>::Invoke;
    }
#endif

    //========================================================
    // overloads
    //========================================================
    // LuaClass::overload / LuaModule::overload register several functions
    // under ONE lua name. A call runs the first of them (in registration
    // order) that the arguments fit: as many arguments as the function has
    // parameters -- fewer only where the missing ones may be nil -- and each
    // argument of a lua type the parameter can take. The check is by KIND, not
    // by value: number vs string vs boolean vs table vs function vs object of
    // a bound class. An integer parameter takes only a number with an integral
    // value, so f(int) and f(double) can be told apart for 1.5 (for 2, the one
    // registered first wins). A type luaaa knows nothing about (a user
    // LuaStack specialization) fits anything.
    namespace luaaa_detail {
        template<int N> struct rank : rank<N - 1> {};
        template<> struct rank<0> {};

        template<typename T>
        inline bool match_arg(lua_State*, int, const T*, rank<0>)
        {
            return true;
        }

        template<typename T>
        inline auto match_arg(lua_State* L, int idx, const T*, rank<1>) -> decltype((void)LuaStack<T>::LUAAA_BOUND_CLASS_STACK, bool())
        {
            return to_object(L, idx, LuaClass<T>::typeKey()) != nullptr;
        }

        template<typename T>
        inline auto match_arg(lua_State* L, int idx, const T*, rank<2>) -> decltype((void)LuaStack<T>::LUAAA_TABLE_STACK, bool())
        {
            return lua_istable(L, idx) != 0;
        }

        template<typename T>
        inline typename std::enable_if<std::is_floating_point<T>::value, bool>::type match_arg(lua_State* L, int idx, const T*, rank<3>)
        {
            return lua_type(L, idx) == LUA_TNUMBER;
        }

        template<typename T>
        inline typename std::enable_if<(std::is_integral<T>::value && !std::is_same<T, bool>::value) || std::is_enum<T>::value, bool>::type match_arg(lua_State* L, int idx, const T*, rank<3>)
        {
            if (lua_type(L, idx) != LUA_TNUMBER)
            {
                return false;
            }
            int isnum = 0;
            const lua_Integer v = LUAAA_tointegerx(L, idx, &isnum);
            return isnum && static_cast<lua_Number>(v) == lua_tonumber(L, idx);
        }

        template<typename T>
        inline bool match_arg(lua_State* L, int idx, T* const*, rank<3>)
        {
            const int t = lua_type(L, idx);
            if (t == LUA_TLIGHTUSERDATA)
            {
                return true;
            }
#if LUAAA_NIL_AS_NULLPTR
            if (t == LUA_TNIL || t == LUA_TNONE)
            {
                return true;
            }
#endif
            return to_object(L, idx, LuaClass<typename std::remove_cv<T>::type>::typeKey()) != nullptr;
        }

        inline bool match_arg(lua_State* L, int idx, const bool*, rank<4>)
        {
            return lua_type(L, idx) == LUA_TBOOLEAN;
        }

        inline bool match_arg(lua_State* L, int idx, const char* const*, rank<4>)
        {
            return lua_type(L, idx) == LUA_TSTRING;
        }

        inline bool match_arg(lua_State* L, int idx, char* const*, rank<4>)
        {
            return lua_type(L, idx) == LUA_TSTRING;
        }

        template<typename TRET, typename ...ARGS>
        inline bool match_arg(lua_State* L, int idx, TRET(* const*)(ARGS...), rank<4>)
        {
            return lua_isfunction(L, idx) != 0;
        }

#if !LUAAA_WITHOUT_CPP_STDLIB
        inline bool match_arg(lua_State* L, int idx, const std::string*, rank<4>)
        {
            return lua_type(L, idx) == LUA_TSTRING;
        }

        template<typename SIG>
        inline bool match_arg(lua_State* L, int idx, const std::function<SIG>*, rank<4>)
        {
            return lua_isfunction(L, idx) != 0;
        }

        template<typename T>
        inline bool match_arg(lua_State* L, int idx, const std::shared_ptr<T>*, rank<4>)
        {
            return lua_isnoneornil(L, idx) || to_object(L, idx, LuaClass<typename std::remove_cv<T>::type>::typeKey()) != nullptr;
        }

        template<typename T, typename D>
        inline bool match_arg(lua_State* L, int idx, const std::unique_ptr<T, D>*, rank<4>)
        {
            return lua_isnoneornil(L, idx) || to_object(L, idx, LuaClass<typename std::remove_cv<T>::type>::typeKey()) != nullptr;
        }
#endif

        // Do the `nargs` lua arguments starting at stack index `first` fit ARGS?
        template<typename ...ARGS>
        struct ArgsMatcher
        {
            static bool match(lua_State* L, int first, int nargs)
            {
                if (nargs > (int)count_lua_args<ARGS...>::value)
                {
                    return false;
                }
                return impl(L, first, typename make_arg_indices<ARGS...>::type());
            }

        private:
            template<std::size_t ...Ns>
            static bool impl(lua_State* L, int first, indices<Ns...>)
            {
                (void)L; (void)first;
                const bool fits[] = { true, (is_lua_state_decayed<ARGS>::value
                    || match_arg(L, first + (int)Ns, static_cast<const typename std::decay<ARGS>::type*>(nullptr), rank<4>()))... };
                for (std::size_t i = 0; i < sizeof(fits) / sizeof(fits[0]); ++i)
                {
                    if (!fits[i])
                    {
                        return false;
                    }
                }
                return true;
            }
        };

        typedef bool (*OverloadMatcher)(lua_State* L, int first, int nargs);

        template<typename TRET, typename ...ARGS>
        inline OverloadMatcher matcher_of(TRET(*)(ARGS...)) { return &ArgsMatcher<ARGS...>::match; }
        template<typename TCLASS, typename TRET, typename ...ARGS>
        inline OverloadMatcher matcher_of(TRET(TCLASS::*)(ARGS...)) { return &ArgsMatcher<ARGS...>::match; }
        template<typename TCLASS, typename TRET, typename ...ARGS>
        inline OverloadMatcher matcher_of(TRET(TCLASS::*)(ARGS...) const) { return &ArgsMatcher<ARGS...>::match; }
#if !LUAAA_WITHOUT_CPP_STDLIB
        template<typename TRET, typename ...ARGS>
        inline OverloadMatcher matcher_of(const std::function<TRET(ARGS...)>&) { return &ArgsMatcher<ARGS...>::match; }
#endif

        // The function behind an overloaded name.
        // upvalue 1: { fn, matcher, fn, matcher, ... }
        // upvalue 2: leading arguments the matchers skip (1 for a class function: self)
        // upvalue 3: the name, for the error message
        inline int overload_dispatch(lua_State* L)
        {
            const int top = lua_gettop(L);
            const int skip = (int)lua_tointeger(L, lua_upvalueindex(2));
            const int nargs = (top > skip) ? (top - skip) : 0;
            luaL_checkstack(L, 8, "too many arguments");
            for (int i = 1; ; i += 2)
            {
                lua_rawgeti(L, lua_upvalueindex(1), i + 1);
                const OverloadMatcher* matcher = (const OverloadMatcher*)lua_touserdata(L, -1);
                lua_pop(L, 1);
                if (!matcher)
                {
                    break;
                }
                if ((*matcher)(L, skip + 1, nargs))
                {
                    lua_rawgeti(L, lua_upvalueindex(1), i);
                    lua_insert(L, 1);
                    lua_call(L, top, LUA_MULTRET);
                    return lua_gettop(L);
                }
            }
            return luaL_error(L, "no overload of '%s' matches the given arguments", lua_tostring(L, lua_upvalueindex(3)));
        }

        // stack: [.., table, fn] -> fn becomes one more overload of table[name]; pops fn.
        inline void add_overload(lua_State* L, const char* name, OverloadMatcher matcher, int skip)
        {
            const int fn = lua_gettop(L);
            const int tbl = fn - 1;
            lua_pushstring(L, name);
            lua_rawget(L, tbl);                             // [tbl, fn, existing]
            if (!(lua_tocfunction(L, -1) == &overload_dispatch && lua_getupvalue(L, -1, 1) != nullptr))
            {
                lua_settop(L, fn);
                lua_newtable(L);                            // [tbl, fn, list]
                lua_pushstring(L, name);
                lua_pushvalue(L, -2);
                lua_pushinteger(L, skip);
                lua_pushstring(L, name);
                lua_pushcclosure(L, &overload_dispatch, 3); // [tbl, fn, list, name, dispatcher]
                lua_rawset(L, tbl);                         // [tbl, fn, list]
            }
            const int list = lua_gettop(L);
            const int n = (int)LUAAA_rawlen(L, list);
            lua_pushvalue(L, fn);
            lua_rawseti(L, list, n + 1);
            OverloadMatcher* slot = (OverloadMatcher*)lua_newuserdata(L, sizeof(OverloadMatcher));
            *slot = matcher;
            lua_rawseti(L, list, n + 2);
            lua_settop(L, tbl);
        }

        // Push the table `name` designates, creating what is missing: "_G" is
        // the globals table, "a.b.c" is _G.a.b.c (the same on every Lua
        // version). Lookups are raw, so a metatable on _G is neither consulted
        // nor triggered.
        inline void push_named_table(lua_State* L, const char* name)
        {
            luaL_checkstack(L, 6, "luaaa: stack overflow");
            LUAAA_pushglobaltable(L);
            if (strcmp(name, "_G") == 0)
            {
                return;
            }
            for (const char* p = name; ; )
            {
                const char* dot = strchr(p, '.');
                const size_t len = dot ? (size_t)(dot - p) : strlen(p);
                lua_pushlstring(L, p, len);
                lua_rawget(L, -2);                          // [parent, value]
                if (lua_isnil(L, -1))
                {
                    lua_pop(L, 1);
                    lua_newtable(L);                        // [parent, table]
                    lua_pushlstring(L, p, len);
                    lua_pushvalue(L, -2);
                    lua_rawset(L, -4);
                }
                else if (!lua_istable(L, -1))
                {
                    luaL_error(L, "luaaa: name conflict for '%s': an existing value is not a table", name);
                }
                lua_remove(L, -2);                          // [table]
                if (!dot)
                {
                    break;
                }
                p = dot + 1;
            }
#if LUAAA_REGISTER_IN_PACKAGE_LOADED
            lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");  // [table, package.loaded]
            if (lua_istable(L, -1))
            {
                lua_pushstring(L, name);
                lua_rawget(L, -2);
                const bool missing = lua_isnil(L, -1);
                lua_pop(L, 1);
                if (missing)
                {
                    lua_pushstring(L, name);
                    lua_pushvalue(L, -3);
                    lua_rawset(L, -3);
                }
            }
            lua_pop(L, 1);
#endif
        }
    }

    //========================================================
    // constructor invoker
    //========================================================
    template<typename TCLASS, typename ...ARGS>
    struct PlacementConstructorCaller
    {
        static TCLASS * Invoke(lua_State * state, void * mem, size_t skip)
        {
            return InvokeImpl(state, mem, typename make_arg_indices<ARGS...>::type(), skip);
        }

    private:
        template<std::size_t ...Ns>
        static TCLASS * InvokeImpl(lua_State * state, void * mem, indices<Ns...>, size_t skip)
        {
            // A throwing constructor leaves the placement-new'd storage unconstructed
            // (no destructor will run) -- translate the exception so Lua sees a normal
            // error and the host does not terminate.
            LUAAA_BOUNDARY_TRY(lua_touserdata(state, lua_upvalueindex(1)))
            {
                return new(mem) TCLASS(LuaStack<ARGS>::get(state, Ns + 1 + skip)...);
            }
            LUAAA_BOUNDARY_CATCH(state, "constructor")
        }
    };

    //========================================================
    // Destructor invoker
    //========================================================
    template<typename TCLASS, bool = std::is_destructible<TCLASS>::value>
    struct DestructorCaller
    {
        static void Invoke(TCLASS * obj)
        {
            delete obj;
        }
    };

    template<typename TCLASS>
    struct DestructorCaller<TCLASS, false>
    {
        static void Invoke(TCLASS * obj)
        {
            (void)obj;   // non-destructible: nothing to do.
        }
    };

    //========================================================
    // conditional default-construction
    //========================================================
    // LuaModule::def(name, class, obj) instantiates BOTH the obj!=null path and the
    // obj==null ("new TCLASS") path. Referencing `new TCLASS` directly would force
    // every def'd type to be default-constructible even when the caller supplies an
    // explicit object. Route it through this trait so `new TCLASS` is only compiled
    // for types that actually have a default constructor.
    template<typename TCLASS, bool = std::is_default_constructible<TCLASS>::value>
    struct DefaultConstructor
    {
        static TCLASS * New() { return new TCLASS(); }
    };

    template<typename TCLASS>
    struct DefaultConstructor<TCLASS, false>
    {
        static TCLASS * New() { return nullptr; }
    };

    //========================================================
    // holder finalizer
    //========================================================
    // Attach a __gc to the holder userdata on top of the stack so F's destructor
    // runs when the holder is collected. Only non-trivially-destructible F (e.g.
    // std::function) needs this; trivial holders (function pointers) skip it, so
    // the no-stdlib/embedded build has zero overhead.
    template <typename F>
    inline void AttachHolderFinalizer(lua_State* L)
    {
#if !LUAAA_WITHOUT_CPP_STDLIB
        if (!std::is_trivially_destructible<F>::value)
        {
            struct Holder {
                static int f_gc(lua_State* s) {
                    F* p = (F*)lua_touserdata(s, 1);
                    if (p) { p->~F(); }
                    return 0;
                }
            };
            lua_newtable(L);                       // [.., ud, mt]
            lua_pushcfunction(L, Holder::f_gc);    // [.., ud, mt, fn]
            lua_setfield(L, -2, "__gc");           // [.., ud, mt]
            lua_setmetatable(L, -2);               // [.., ud] (mt already holds __gc before set: satisfies 5.4+ finalizer rule)
        }
#else
        (void)L;
#endif
    }

    //========================================================
    // built-in lua-side inheritance helpers (luaaa:extend / luaaa:base)
    //========================================================
#if LUAAA_FEATURE_EXTEND
    //   Derived = luaaa:extend(Base, fields)   -- Base: a bound class, or another extended class
    //   obj     = Derived:new(...)             -- a NEW instance on every call
    //
    // The model:
    //   * `Derived` is an ordinary class table: `fields`, plus whatever the script
    //     adds to it later (methods, defaults), plus `new`. When Base is itself an
    //     extended class, Derived gets `{ __index = Base }` so lookups chain up.
    //   * An instance is a fresh table `{ ["@"] = <the bound C++ object> }` whose
    //     metatable is private to its class: a copy of the C++ class's metatable
    //     (methods, properties, metamethods, the type tag LuaStack<T>::get looks
    //     for) whose __index consults `Derived` first and then falls back to the
    //     C++ class's own __index. It carries no __gc: the C++ object is owned by
    //     the "@" userdata, which is collected on its own.
    //   * That metatable is built on the first `new` (an object is needed to reach
    //     the C++ class's metatable) and cached in the constructor closure.
    // (Until v1.5 `new` turned the class table ITSELF into the instance, so every
    // "instance" of a class was the same table and a later `new` overwrote the
    // earlier object; extending an extended class did not work at all.)

    // __index of an extended instance.
    // upvalue(1) = the class table, upvalue(2) = the C++ class's metatable.
    inline int f_extend_instance_index(lua_State* L)
    {
        lua_settop(L, 2);                               // [self, key]
        lua_pushvalue(L, 2);
        lua_gettable(L, lua_upvalueindex(1));           // class table (honours its own chain)
        if (!lua_isnil(L, -1))
        {
            return 1;
        }
        lua_pop(L, 1);
        // Fetched on every access, not cached: the C++ class switches its
        // __index from a table to a function when it gains its first property.
        lua_pushstring(L, "__index");
        lua_rawget(L, lua_upvalueindex(2));             // [self, key, index]
        if (lua_isfunction(L, -1))
        {
            lua_insert(L, 1);                           // [index, self, key]
            lua_call(L, 2, 1);
            return 1;
        }
        if (lua_istable(L, -1))                         // the bare methods table
        {
            lua_pushvalue(L, 2);
            lua_rawget(L, -2);
            return 1;
        }
        lua_pushnil(L);
        return 1;
    }

    // `new` of a class created via luaaa:extend. Colon-call: self (the class
    // table, ignored) + constructor args.
    // upvalue(1) = base, upvalue(2) = the class table,
    // upvalue(3) = the instance metatable (false until the first instance).
    inline int f_extend_derived_new(lua_State* L)
    {
        const int nargs = lua_gettop(L);
        luaL_checkstack(L, nargs + 8, "too many constructor arguments");

        // o = the bound C++ object, made by the base's constructor
        lua_pushstring(L, "new");
        lua_rawget(L, lua_upvalueindex(1));             // base.new
        const bool baseIsExtended = (lua_tocfunction(L, -1) == f_extend_derived_new);
        if (baseIsExtended)
        {
            lua_pushvalue(L, lua_upvalueindex(1));      // an extended base wants its self
        }
        for (int i = 2; i <= nargs; ++i)                // forward ctor args (skip self)
        {
            lua_pushvalue(L, i);
        }
        lua_call(L, (nargs > 0 ? nargs - 1 : 0) + (baseIsExtended ? 1 : 0), 1);
        if (baseIsExtended && lua_istable(L, -1))
        {
            lua_pushstring(L, "@");                     // unwrap the base's instance
            lua_rawget(L, -2);
            lua_remove(L, -2);
        }
        if (lua_isnil(L, -1))
        {
            return 1;                                   // the constructor yielded nil: so do we
        }
        if (lua_type(L, -1) != LUA_TUSERDATA)
        {
            return luaL_error(L, "luaaa:extend: the base constructor did not return a bound object");
        }
        const int obj = lua_gettop(L);

        if (!lua_istable(L, lua_upvalueindex(3)))
        {
            if (!lua_getmetatable(L, obj))              // [.., o, cmt]
            {
                return luaL_error(L, "luaaa:extend: the base object has no metatable");
            }
            const int cmt = lua_gettop(L);
            lua_newtable(L);                            // [.., o, cmt, imt]
            lua_pushnil(L);
            while (lua_next(L, cmt))                    // [.., o, cmt, imt, k, v]
            {
                lua_pushvalue(L, -2);
                lua_insert(L, -2);                      // [.., imt, k, k, v]
                lua_rawset(L, cmt + 1);                 // imt[k] = v
            }
            lua_pushstring(L, "__gc");
            lua_pushnil(L);
            lua_rawset(L, cmt + 1);
            lua_pushstring(L, "__metatable");           // a lua-side object stays open to its script
            lua_pushnil(L);
            lua_rawset(L, cmt + 1);
            lua_pushstring(L, "__index");
            lua_pushvalue(L, lua_upvalueindex(2));      // the class table
            lua_pushvalue(L, cmt);                      // the C++ class's metatable
            lua_pushcclosure(L, f_extend_instance_index, 2);
            lua_rawset(L, cmt + 1);
            lua_pushvalue(L, cmt + 1);
            lua_replace(L, lua_upvalueindex(3));        // cache it
            lua_settop(L, obj);                         // [.., o]
        }

        lua_newtable(L);                                // [.., o, inst]
        lua_pushstring(L, "@");
        lua_pushvalue(L, obj);
        lua_rawset(L, -3);                              // inst["@"] = o
        lua_pushvalue(L, lua_upvalueindex(3));
        lua_setmetatable(L, -2);
        return 1;
    }

    // luaaa:extend(base, obj) -> derived. colon-call: self=1, base=2, obj=3.
    inline int f_extend(lua_State* L)
    {
        luaL_checktype(L, 2, LUA_TTABLE);           // base
        if (lua_isnoneornil(L, 3))
        {
            lua_newtable(L);                        // derived = {}
        }
        else
        {
            luaL_checktype(L, 3, LUA_TTABLE);
            lua_pushvalue(L, 3);                    // derived = obj
        }
        const int derived = lua_gettop(L);

        lua_pushstring(L, "new");
        lua_pushvalue(L, 2);                        // upvalue 1: base
        lua_pushvalue(L, derived);                  // upvalue 2: the class table
        lua_pushboolean(L, 0);                      // upvalue 3: instance metatable (not built yet)
        lua_pushcclosure(L, f_extend_derived_new, 3);
        lua_rawset(L, derived);                     // derived.new = <closure>

        // Extending an extended class: chain the class tables, so the new class
        // inherits the base's lua-side methods and defaults (unless the script
        // already gave `obj` a metatable of its own -- then that one is kept).
        lua_pushstring(L, "new");
        lua_rawget(L, 2);
        const bool baseIsExtended = (lua_tocfunction(L, -1) == f_extend_derived_new);
        lua_pop(L, 1);
        if (baseIsExtended)
        {
            if (lua_getmetatable(L, derived))
            {
                lua_pop(L, 1);
            }
            else
            {
                lua_newtable(L);
                lua_pushvalue(L, 2);
                lua_setfield(L, -2, "__index");
                lua_setmetatable(L, derived);
            }
        }
        lua_settop(L, derived);
        return 1;                                   // return derived
    }

    // luaaa:base(obj) -> obj["@"] (the bound C++ object) or nil. colon-call: self=1, obj=2.
    inline int f_extend_base(lua_State* L)
    {
        if (lua_istable(L, 2))
        {
            lua_pushstring(L, "@");
            lua_rawget(L, 2);
        }
        else
        {
            lua_pushnil(L);
        }
        return 1;
    }

    // Inject the `luaaa` table with extend/base into L, once per state.
    // Respects any user-defined luaaa.extend / luaaa.base (only fills gaps).
    inline void installExtendHelper(lua_State* L)
    {
        static char s_extendKey = 0;
        LUAAA_rawgetp(L, LUA_REGISTRYINDEX, &s_extendKey);   // (returns void on 5.2: test the value)
        if (!lua_isnil(L, -1))
        {
            lua_pop(L, 1);                          // already installed
            return;
        }
        lua_pop(L, 1);

        lua_getglobal(L, "luaaa");                  // reuse existing global if present
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            lua_newtable(L);
        }

        lua_getfield(L, -1, "extend");
        if (lua_isnil(L, -1))
        {
            lua_pushcfunction(L, f_extend);
            lua_setfield(L, -3, "extend");
        }
        lua_pop(L, 1);

        lua_getfield(L, -1, "base");
        if (lua_isnil(L, -1))
        {
            lua_pushcfunction(L, f_extend_base);
            lua_setfield(L, -3, "base");
        }
        lua_pop(L, 1);

        lua_setglobal(L, "luaaa");

        lua_pushboolean(L, 1);
        LUAAA_rawsetp(L, LUA_REGISTRYINDEX, &s_extendKey);
    }
#endif

    //========================================================
    // class metatable dispatch
    //========================================================
    // What a class metatable holds:
    //   [MT_METHODS]  table: functions and constants, by name  (what obj.name finds)
    //   [MT_GETTERS]  table: property getters, by property name
    //   [MT_SETTERS]  table: property setters, by property name
    //   [MT_BASES]    table: { baseMetatable, caster, ... }     (only after inherit<>())
    //   __index       the METHODS table itself while the class has no property,
    //                 no user __index and no base (a method lookup then costs no
    //                 C call); class_index below otherwise
    //   __newindex    class_newindex
    //   __gc          class_gc
    //   "!__index" / "!__newindex" / "!__gc"   the user's own handlers
    //   metamethods (__add, __tostring, ...) registered with fun()
    //   __metatable = false  (LUAAA_PROTECT_METATABLE): scripts cannot reach any of this
    // The functions are not templates: every class shares them.
    namespace luaaa_detail {
        // Look up the key at stack index 2 in the class whose metatable is at
        // absolute index `mt`, then in its bases, depth first.
        // Returns 0 = not found, 1 = a method / constant was pushed,
        // 2 = a property getter was pushed.
        inline int class_get(lua_State* L, int mt, int depth)
        {
            const int top = lua_gettop(L);
            for (int slot = MT_METHODS; slot <= MT_GETTERS; ++slot)
            {
                lua_rawgeti(L, mt, slot);
                if (lua_istable(L, -1))
                {
                    lua_pushvalue(L, 2);
                    lua_rawget(L, -2);
                    if (!lua_isnil(L, -1))
                    {
                        lua_remove(L, -2);
                        return slot;
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
            }
            if (depth > 16 || !lua_checkstack(L, 6))
            {
                return 0;
            }
            lua_rawgeti(L, mt, MT_BASES);
            if (lua_istable(L, -1))
            {
                const int bases = lua_gettop(L);
                for (int i = 1; ; i += 2)
                {
                    lua_rawgeti(L, bases, i);
                    if (!lua_istable(L, -1))
                    {
                        break;
                    }
                    const int found = class_get(L, bases + 1, depth + 1);
                    if (found)
                    {
                        lua_replace(L, bases);
                        lua_settop(L, bases);
                        return found;
                    }
                    lua_pop(L, 1);
                }
            }
            lua_settop(L, top);
            return 0;
        }

        // Find, in the class at `mt` and then its bases: the entry of table
        // `slot` under the key at stack index 2 (slot != 0), or the metatable
        // field `field` (slot == 0). Pushes it and returns true when found.
        inline bool class_find(lua_State* L, int mt, int slot, const char* field, int depth)
        {
            const int top = lua_gettop(L);
            if (slot)
            {
                lua_rawgeti(L, mt, slot);
                if (lua_istable(L, -1))
                {
                    lua_pushvalue(L, 2);
                    lua_rawget(L, -2);
                }
                else
                {
                    lua_pushnil(L);
                }
                lua_remove(L, -2);
            }
            else
            {
                lua_pushstring(L, field);
                lua_rawget(L, mt);
            }
            if (!lua_isnil(L, -1))
            {
                return true;
            }
            lua_pop(L, 1);
            if (depth > 16 || !lua_checkstack(L, 6))
            {
                return false;
            }
            lua_rawgeti(L, mt, MT_BASES);
            if (lua_istable(L, -1))
            {
                const int bases = lua_gettop(L);
                for (int i = 1; ; i += 2)
                {
                    lua_rawgeti(L, bases, i);
                    if (!lua_istable(L, -1))
                    {
                        break;
                    }
                    if (class_find(L, bases + 1, slot, field, depth + 1))
                    {
                        lua_replace(L, bases);
                        lua_settop(L, bases);
                        return true;
                    }
                    lua_pop(L, 1);
                }
            }
            lua_settop(L, top);
            return false;
        }

        // `obj[key]`. upvalue 1 = the class metatable.
        //   string / number key : method or constant -> property getter (this
        //                         class first, then its bases) -> user __index
        //                         -> (a setter only => write-only error) -> nil
        //   other keys          : nil
        // The key is never converted: lua_tostring on a number would rewrite the
        // caller's value in place.
        inline int class_index(lua_State* L)
        {
            lua_settop(L, 2);                                   // [self, key]
            const int keyType = lua_type(L, 2);
            if (keyType != LUA_TSTRING && keyType != LUA_TNUMBER)
            {
                lua_pushnil(L);
                return 1;
            }
            lua_pushvalue(L, lua_upvalueindex(1));              // [self, key, mt]
            const int found = class_get(L, 3, 0);
            if (found == MT_METHODS)
            {
                return 1;
            }
            if (found == MT_GETTERS)
            {
                lua_pushvalue(L, 1);
                lua_pushvalue(L, 2);
                lua_call(L, 2, 1);
                return 1;
            }
            if (class_find(L, 3, 0, "!__index", 0))             // user __index: function or table
            {
                if (lua_isfunction(L, -1))
                {
                    lua_pushvalue(L, 1);
                    lua_pushvalue(L, 2);
                    lua_call(L, 2, 1);
                    return 1;
                }
                if (lua_istable(L, -1))
                {
                    lua_pushvalue(L, 2);
                    lua_rawget(L, -2);
                    return 1;
                }
                lua_pop(L, 1);
            }
            if (keyType == LUA_TSTRING && class_find(L, 3, MT_SETTERS, nullptr, 0))
            {
                // Yes, there are something write-only, e.g., stdout, printer, digital io pin in output mode.
                lua_getfield(L, 3, "__name");
                return luaL_error(L, "attempt to read Write-Only property '%s' of '%s'", lua_tostring(L, 2), luaL_optstring(L, -1, "?"));
            }
            lua_pushnil(L);                                     // non-existing: nil
            return 1;
        }

        // `obj[key] = value`. upvalue 1 = the class metatable.
        //   string key : registered setter (this class, then its bases) -> (below)
        //   lua-side (luaaa:extend) object : plain field on the instance table
        //   userdata   : user __newindex (string/number key, ORIGINAL key)
        //                -> read-only / non-existing property error
        inline int class_newindex(lua_State* L)
        {
            lua_settop(L, 3);                                   // [self, key, value]
            const int keyType = lua_type(L, 2);
            lua_pushvalue(L, lua_upvalueindex(1));              // [self, key, value, mt]
            if (keyType == LUA_TSTRING && class_find(L, 4, MT_SETTERS, nullptr, 0))
            {
                lua_pushvalue(L, 1);
                lua_pushvalue(L, 2);
                lua_pushvalue(L, 3);
                lua_call(L, 3, 0);
                return 0;
            }
            if (lua_istable(L, 1))
            {
                lua_settop(L, 3);
                lua_rawset(L, 1);
                return 0;
            }
            if (keyType != LUA_TSTRING && keyType != LUA_TNUMBER)
            {
                return 0;   // no setter can match such a key: leave the assignment untouched
            }
            if (class_find(L, 4, 0, "!__newindex", 0))
            {
                if (lua_isfunction(L, -1))
                {
                    lua_pushvalue(L, 1);
                    lua_pushvalue(L, 2);
                    lua_pushvalue(L, 3);
                    lua_call(L, 3, 0);
                    return 0;
                }
                if (lua_istable(L, -1))
                {
                    lua_pushvalue(L, 2);
                    lua_pushvalue(L, 3);
                    lua_rawset(L, -3);
                    return 0;
                }
                lua_pop(L, 1);
            }
            lua_getfield(L, 4, "__name");                       // [self, key, value, mt, name]
            if (keyType == LUA_TSTRING && class_find(L, 4, MT_GETTERS, nullptr, 0))
            {
                return luaL_error(L, "attempt to write Read-Only property '%s' of '%s'", lua_tostring(L, 2), luaL_optstring(L, 5, "?"));
            }
            lua_pushvalue(L, 2);                                // a copy: only the copy gets stringified
            const char* shown = lua_tostring(L, -1);
            return luaL_error(L, "attempt to access Non-existing property '%s' of '%s'", (shown ? shown : "?"), luaL_optstring(L, 5, "?"));
        }

        // __gc of every bound object.
        // Never raises: during lua_close there is no pcall to catch an error.
        // It can be reached by a script as well as by the collector (through
        // the debug library, or getmetatable when LUAAA_PROTECT_METATABLE is
        // off), so (1) only a FULL userdata whose metatable is one of ours is
        // read as a UserData -- not a table made by luaaa:extend, not a foreign
        // userdata such as io.stdout; and (2) an object is finalized at most
        // once: obj/dtor are cleared, so a manual call followed by the
        // collector's cannot destroy it twice, and LuaStack::get rejects the
        // finalized object ("invalid user data") instead of handing out a dead
        // pointer.
        inline int class_gc(lua_State* L)
        {
            lua_settop(L, 1);                                   // [ud]
            if (lua_type(L, 1) != LUA_TUSERDATA || !lua_getmetatable(L, 1))
            {
                return 0;
            }
            LUAAA_rawgetp(L, 2, ClassMetaTag());                // [ud, mt, tag]
            const bool ours = (lua_toboolean(L, -1) != 0);
            lua_pop(L, 1);
            UserData* uData = (UserData*)lua_touserdata(L, 1);
            if (!ours || !uData || !uData->obj)
            {
                return 0;   // not our object, or already finalized
            }

            if (class_find(L, 2, 0, "!__gc", 0))                // the user's __gc, before the object dies
            {
                if (lua_isfunction(L, -1))
                {
                    lua_pushvalue(L, 1);
                    if (lua_pcall(L, 1, 0, 0) != 0)
                    {
                        lua_pop(L, 1);                          // do not propagate errors out of __gc
                    }
                }
                else
                {
                    lua_pop(L, 1);
                }
            }

            void (*dtor)(UserData*) = uData->dtor;
            uData->dtor = nullptr;
            if (dtor)
            {
                // A C++ exception out of __gc would cross the extern "C"
                // boundary and terminate the host, and lua_error() inside __gc
                // is itself undefined: swallow it.
#if LUAAA_HAS_EXCEPTIONS
                try
#endif
                {
                    dtor(uData);
                }
#if LUAAA_HAS_EXCEPTIONS
                catch (...) {
                }
#endif
            }
            uData->obj = nullptr;   // finalized: never destroy or hand out again
            return 0;
        }

        //----------------------------------------------------
        // module property dispatch (see LuaModule)
        //----------------------------------------------------
        inline const void* ModuleMetaTag() { static char k = 0; return &k; }

        // `mod[key]` for a key that is absent from the module table.
        // upvalue 1 = the module's metatable: [1] getters, [2] setters, __name.
        //   string key : registered getter -> user __index
        //                -> (setter only => write-only error) -> nil
        //   number key : user __index -> nil
        inline int module_index(lua_State* L)
        {
            lua_settop(L, 2);                                   // [mod, key]
            const int keyType = lua_type(L, 2);
            if (keyType != LUA_TSTRING && keyType != LUA_TNUMBER)
            {
                lua_pushnil(L);
                return 1;
            }
            if (keyType == LUA_TSTRING)
            {
                lua_rawgeti(L, lua_upvalueindex(1), 1);
                lua_pushvalue(L, 2);
                lua_rawget(L, -2);                              // [mod, key, getters, getter]
                if (lua_isfunction(L, -1))
                {
                    lua_pushvalue(L, 1);
                    lua_pushvalue(L, 2);
                    lua_call(L, 2, 1);
                    return 1;
                }
                lua_pop(L, 2);
            }
            lua_pushstring(L, "__index");                       // a user handler lives in the module table
            lua_rawget(L, 1);
            if (lua_isfunction(L, -1))
            {
                lua_pushvalue(L, 1);
                lua_pushvalue(L, 2);
                lua_call(L, 2, 1);
                return 1;
            }
            if (lua_istable(L, -1))
            {
                lua_pushvalue(L, 2);
                lua_rawget(L, -2);
                return 1;
            }
            lua_pop(L, 1);
            if (keyType == LUA_TSTRING)
            {
                lua_rawgeti(L, lua_upvalueindex(1), 2);
                lua_pushvalue(L, 2);
                lua_rawget(L, -2);
                if (lua_isfunction(L, -1))
                {
                    lua_getfield(L, lua_upvalueindex(1), "__name");
                    return luaL_error(L, "attempt to read Write-Only property '%s' of '%s'", lua_tostring(L, 2), luaL_optstring(L, -1, "?"));
                }
            }
            lua_pushnil(L);
            return 1;
        }

        // `mod[key] = value` for a key that is absent from the module table.
        //   string key : registered setter -> (getter only => read-only error)
        //   then       : user __newindex (function or table)
        //                -> plain assignment, under the ORIGINAL key
        inline int module_newindex(lua_State* L)
        {
            lua_settop(L, 3);                                   // [mod, key, value]
            if (lua_type(L, 2) == LUA_TSTRING)
            {
                lua_rawgeti(L, lua_upvalueindex(1), 2);
                lua_pushvalue(L, 2);
                lua_rawget(L, -2);                              // [mod, key, value, setters, setter]
                if (lua_isfunction(L, -1))
                {
                    lua_pushvalue(L, 1);
                    lua_pushvalue(L, 2);
                    lua_pushvalue(L, 3);
                    lua_call(L, 3, 0);
                    return 0;
                }
                lua_pop(L, 2);
                lua_rawgeti(L, lua_upvalueindex(1), 1);
                lua_pushvalue(L, 2);
                lua_rawget(L, -2);
                if (lua_isfunction(L, -1))
                {
                    lua_getfield(L, lua_upvalueindex(1), "__name");
                    return luaL_error(L, "attempt to write Read-Only property '%s' of '%s'", lua_tostring(L, 2), luaL_optstring(L, -1, "?"));
                }
                lua_pop(L, 2);                                  // [mod, key, value]
            }
            lua_pushstring(L, "__newindex");
            lua_rawget(L, 1);                                   // [mod, key, value, handler]
            if (lua_isfunction(L, -1))
            {
                lua_insert(L, 1);                               // [handler, mod, key, value]
                lua_call(L, 3, 0);
                return 0;
            }
            if (lua_istable(L, -1))
            {
                lua_replace(L, 1);                              // [handler, key, value]
                lua_rawset(L, 1);
                return 0;
            }
            lua_pop(L, 1);
            lua_rawset(L, 1);
            return 0;
        }

        //----------------------------------------------------
        // push a C++ callable as a lua function
        //----------------------------------------------------
        inline void push_function(lua_State* L, lua_CFunction f)
        {
            lua_pushcclosure(L, f, 0);
        }

        template<typename TRET, typename ...ARGS>
        inline void push_function(lua_State* L, TRET(*f)(ARGS...))
        {
            push_holder(L, f);
            lua_pushcclosure(L, NonMemberFunctionCaller(f), 1);
        }

        inline bool match_anything(lua_State*, int, int) { return true; }
        inline OverloadMatcher matcher_of(lua_CFunction) { return &match_anything; }

#if !LUAAA_WITHOUT_CPP_STDLIB
        template<typename TRET, typename ...ARGS>
        inline void push_function(lua_State* L, const std::function<TRET(ARGS...)>& f)
        {
            push_holder(L, f);
            lua_pushcclosure(L, NonMemberFunctionCaller(f), 1);
        }

        // lambdas and other function objects
        template<typename F>
        inline void push_function(lua_State* L, F f)
        {
            push_function(L, to_function(f));
        }

        template<typename F>
        inline OverloadMatcher matcher_of(F f)
        {
            return matcher_of(to_function(f));
        }
#endif
    }

    //========================================================
    // export class
    //========================================================
    template <typename TCLASS>
    struct LuaClass
    {
        template<typename, typename> friend struct LuaStack;
        template<typename> friend struct LuaClass;
        friend struct LuaModule;

        typedef luaaa_detail::UserData UserDataDetail;

    public:
        LuaClass(lua_State * state, const char * name, const luaL_Reg * functions = nullptr)
            : m_state(state)
        {
            assert(state != nullptr);

#if !defined(LUA_RIDX_MAINTHREAD)
            // 5.1 / LuaJIT: note the main thread while binding (bindings normally
            // run on it) so callbacks registered later from a coroutine can find it.
            (void)luaaa_detail::MainThread(state);
#endif

            // Per-state check: the same C++ type may be bound under different names in
            // different lua_States, but binding it under two different names in the SAME
            // state is unsupported (self / arguments are resolved by type, so the second
            // name's objects could not be unpacked). Use a distinct wrapper type instead.
            const char * existing = klassName(state);
            if (existing != nullptr && strcmp(existing, name) != 0)
            {
                luaL_argerror(state, 1, lua_pushfstring(state, "C++ class `%s` bind to conflict lua name `%s`, origin name: `%s`. bind it in a separate lua_State or wrap it in a distinct C++ type.", LUAAA_RTTI_CLASS_NAME(TCLASS), name, existing));
                return;
            }

            // Object metatable, also kept under the lua name in this state's
            // registry (luaL_checkudata words its errors with that name).
            // Two DIFFERENT C++ types must never share one lua name in a state.
            // Tag the metatable with this type's key and reject foreign reuse
            // (BEFORE recording our own name, so a rejected bind leaves no
            // registration residue); re-binding the SAME type under its existing
            // name (e.g. adding methods in a second block) is fine.
            if (luaL_newmetatable(state, name))
            {
                const int mt = lua_gettop(state);
                lua_pushlightuserdata(state, (void*)&s_typeKey);
                LUAAA_rawsetp(state, mt, &s_typeKey);                   // the type tag
                lua_pushboolean(state, 1);
                LUAAA_rawsetp(state, mt, luaaa_detail::ClassMetaTag()); // "a luaaa class"
                for (int slot = luaaa_detail::MT_METHODS; slot <= luaaa_detail::MT_SETTERS; ++slot)
                {
                    lua_newtable(state);
                    lua_rawseti(state, mt, slot);
                }
                lua_pushstring(state, name);
                lua_setfield(state, mt, "__name");
                lua_pushcfunction(state, &luaaa_detail::class_gc);
                lua_setfield(state, mt, "__gc");
                lua_rawgeti(state, mt, luaaa_detail::MT_METHODS);
                lua_setfield(state, mt, "__index");                     // until a property / base / user __index needs class_index
                lua_pushvalue(state, mt);
                lua_pushcclosure(state, &luaaa_detail::class_newindex, 1);
                lua_setfield(state, mt, "__newindex");
#if LUAAA_PROTECT_METATABLE
                lua_pushboolean(state, 0);
                lua_setfield(state, mt, "__metatable");
#endif
                lua_pushvalue(state, mt);
                LUAAA_rawsetp(state, LUA_REGISTRYINDEX, &s_metaKey);
            }
            else
            {
                LUAAA_rawgetp(state, lua_gettop(state), &s_typeKey);
                const void * tag = lua_touserdata(state, -1);
                lua_pop(state, 1);
                if (tag != (const void*)&s_typeKey)
                {
                    luaL_argerror(state, 1, lua_pushfstring(state, "lua name `%s` already bound to a different cpp class", name));
                }
            }
            lua_pop(state, 1);

            // Record this type's lua name for the current state (registry[&s_typeKey]).
            setKlassName(state, name);

            if (functions)
            {
                for (const luaL_Reg* reg = functions; reg->name != nullptr; ++reg)
                {
                    lua_pushcfunction(state, reg->func);
                    _setMember(reg->name, nullptr);
                }
            }

#if LUAAA_FEATURE_EXTEND
            // any state that binds a class gets luaaa:extend / luaaa:base (once).
            installExtendHelper(state);
#endif
        }

#if !LUAAA_WITHOUT_CPP_STDLIB
        LuaClass(lua_State * state, const std::string& name, const luaL_Reg * functions = nullptr)
            : LuaClass(state, name.c_str(), functions)
        {}
#endif

        //----------------------------------------------------
        // inheritance
        //----------------------------------------------------
        // Declare BASE (a C++ base class of TCLASS that is already bound in
        // this state) as the base of this class:
        //   * an object of this class is accepted wherever a BASE is expected
        //     (BASE&, const BASE&, BASE*, the `self` of BASE's functions), and
        //   * BASE's functions, constants and properties are found on objects
        //     of this class (what this class registers itself takes precedence).
        // Call it once per base; bases of BASE are inherited with it.
        template<typename BASE>
        inline LuaClass<TCLASS>& inherit()
        {
            static_assert(std::is_base_of<BASE, TCLASS>::value && !std::is_same<BASE, TCLASS>::value,
                "Error: inherit<BASE>() requires BASE to be a base class of the bound class");
            struct Caster
            {
                static void* cast(void* p) { return static_cast<BASE*>(static_cast<TCLASS*>(p)); }
            };
            lua_State* L = m_state;
            if (!LuaClass<BASE>::pushMetatable(L))
            {
                luaL_error(L, "luaaa: the base class of '%s' is not bound in this state; bind it before inherit<>()", klassName(L));
                return (*this);
            }
            const int bmt = lua_gettop(L);                      // [bmt]
            pushMetatable(L);                                   // [bmt, mt]
            lua_rawgeti(L, bmt + 1, luaaa_detail::MT_BASES);
            if (!lua_istable(L, -1))
            {
                lua_pop(L, 1);
                lua_newtable(L);
                lua_pushvalue(L, -1);
                lua_rawseti(L, bmt + 1, luaaa_detail::MT_BASES);
            }                                                   // [bmt, mt, bases]
            const int n = (int)LUAAA_rawlen(L, bmt + 2);
            lua_pushvalue(L, bmt);
            lua_rawseti(L, bmt + 2, n + 1);
            luaaa_detail::UpcastFn* caster = (luaaa_detail::UpcastFn*)lua_newuserdata(L, sizeof(luaaa_detail::UpcastFn));
            *caster = &Caster::cast;
            lua_rawseti(L, bmt + 2, n + 2);
            _useIndexFunction(bmt + 1);
            lua_settop(L, bmt - 1);
            return (*this);
        }

        //----------------------------------------------------
        // constructors (functions of the class table: Class.new(...))
        //----------------------------------------------------
        template<typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const char * name = "new")
        {
            LUAAA_REPORT_CTOR_NAME_CONFLICT(name);
            _pushPlacementCtor<ARGS...>();
            return _setStatic(name, nullptr);
        }

        // One more constructor under a name that already has one: the call
        // picks the first whose parameters fit the arguments (see "overloads").
        template<typename ...ARGS>
        inline LuaClass<TCLASS>& overloadCtor(const char * name = "new")
        {
            _pushPlacementCtor<ARGS...>();
            return _setStatic(name, &luaaa_detail::ArgsMatcher<ARGS...>::match);
        }

        template<typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const char * name, TCLASS*(*spawner)(ARGS...)) {
            struct HelperClass {
                static int f_new(lua_State* state) {
                    return _spawn<ARGS...>(state, &LuaClass<TCLASS>::_dtorDelete, nullptr);
                }
            };
            LUAAA_REPORT_CTOR_NAME_CONFLICT(name);
            luaaa_detail::push_holder(m_state, spawner);
            lua_pushcclosure(m_state, &HelperClass::f_new, 1);
            return _setStatic(name, nullptr);
        }

        template<typename TRET, typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const char * name, TCLASS*(*spawner)(ARGS...), TRET(*deleter)(TCLASS*)){
            typedef decltype(deleter) DELETERFTYPE;
            struct HelperClass {
                static void f_dtor(luaaa_detail::UserData* uData) {
                    if (uData->obj && uData->free_func)
                    {
                        (reinterpret_cast<DELETERFTYPE>(uData->free_func))(static_cast<TCLASS*>(uData->obj));
                    }
                }

                static int f_new(lua_State* state) {
                    void * deleterPtr = lua_touserdata(state, lua_upvalueindex(2));
                    luaL_argcheck(state, deleterPtr, 2, "cpp closure deleter not found.");
                    // The deleter is copied INTO the object: it must not depend on
                    // this closure staying alive (`Class = nil` can collect it first).
                    return _spawn<ARGS...>(state, &HelperClass::f_dtor, reinterpret_cast<void(*)()>(*(DELETERFTYPE*)deleterPtr));
                }
            };
            LUAAA_REPORT_CTOR_NAME_CONFLICT(name);
            luaaa_detail::push_holder(m_state, spawner);
            luaaa_detail::push_holder(m_state, deleter);
            lua_pushcclosure(m_state, &HelperClass::f_new, 2);
            return _setStatic(name, nullptr);
        }

        template<typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const char * name, TCLASS*(*spawner)(ARGS...), std::nullptr_t) {
            struct HelperClass {
                static int f_new(lua_State* state) {
                    return _spawn<ARGS...>(state, nullptr, nullptr);    // never deleted by lua
                }
            };
            LUAAA_REPORT_CTOR_NAME_CONFLICT(name);
            luaaa_detail::push_holder(m_state, spawner);
            lua_pushcclosure(m_state, &HelperClass::f_new, 1);
            return _setStatic(name, nullptr);
        }

#if !LUAAA_WITHOUT_CPP_STDLIB
        // A factory returning std::shared_ptr<TCLASS>: the object is shared
        // between C++ and lua, and can be passed to std::shared_ptr parameters.
        template<typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const char * name, std::shared_ptr<TCLASS>(*spawner)(ARGS...)) {
            typedef decltype(spawner) SPAWNERFTYPE;
            struct HelperClass {
                static int f_new(lua_State* state) {
                    void * spawnerPtr = lua_touserdata(state, lua_upvalueindex(1));
                    luaL_argcheck(state, spawnerPtr, 1, "cpp closure spawner not found.");
                    LuaStack<std::shared_ptr<TCLASS>>::put(state, LuaInvoke<std::shared_ptr<TCLASS>, SPAWNERFTYPE, ARGS...>(state, spawnerPtr, 0));
                    return 1;
                }
            };
            LUAAA_REPORT_CTOR_NAME_CONFLICT(name);
            luaaa_detail::push_holder(m_state, spawner);
            lua_pushcclosure(m_state, &HelperClass::f_new, 1);
            return _setStatic(name, nullptr);
        }

        template<typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const std::string& name)
        {
            return ctor<ARGS...>(name.c_str());
        }

        template<typename S, typename ...D>
        inline LuaClass<TCLASS>& ctor(const std::string& name, S spawner, D... deleter) {
            return ctor(name.c_str(), spawner, deleter...);
        }
#endif

        //----------------------------------------------------
        // functions: obj:name(...)
        //----------------------------------------------------
        // `f` may be a member function of this class or of a base class of it,
        // a free function, a std::function, a lambda, or a raw lua_CFunction.
        // A free function / lambda does not receive `self`.
        template<typename F>
        inline LuaClass<TCLASS>& fun(const char * name, F f)
        {
            return _reg(name, f, false);
        }

        // Like fun, but ADDS `f` to the functions already registered under
        // `name` with overload(); the call picks the first one the arguments fit.
        template<typename F>
        inline LuaClass<TCLASS>& overload(const char * name, F f)
        {
            return _reg(name, f, true);
        }

        // A function of the class table: Class.name(...) -- no object involved.
        template<typename F>
        inline LuaClass<TCLASS>& staticFun(const char * name, F f)
        {
            luaaa_detail::push_function(m_state, f);
            return _setStatic(name, nullptr);
        }

        // A constant found on every object: obj.name
        template <typename V>
        inline LuaClass<TCLASS>& def(const char * name, const V& val)
        {
            LuaStack<V>::put(m_state, val);
            _setMember(name, nullptr);
            return (*this);
        }

        // disable cast from "const char [#]" to "char (*)[#]"
        inline LuaClass<TCLASS>& def(const char* name, const char* str)
        {
            lua_pushstring(m_state, str);
            _setMember(name, nullptr);
            return (*this);
        }

        // A constant of the class table: Class.name
        template <typename V>
        inline LuaClass<TCLASS>& constant(const char * name, const V& val)
        {
            LuaStack<V>::put(m_state, val);
            return _setStatic(name, nullptr);
        }

        inline LuaClass<TCLASS>& constant(const char* name, const char* str)
        {
            lua_pushstring(m_state, str);
            return _setStatic(name, nullptr);
        }

        //----------------------------------------------------
        // properties: obj.name / obj.name = value
        //----------------------------------------------------
    private:
        // How a getter / setter is called: MODE 0 = f(), 1 = f(self), 2 = (self.*f)()
        // -- for a setter with the value (lua argument 3) as the last argument.
        template<typename F, typename P, int MODE>
        struct PropGet
        {
            static P call(lua_State*, F& f, std::integral_constant<int, 0>) { return f(); }
            static P call(lua_State* state, F& f, std::integral_constant<int, 1>) { return f(LuaStack<TCLASS>::get(state, 1)); }
            static P call(lua_State* state, F& f, std::integral_constant<int, 2>) { return (LuaStack<TCLASS>::get(state, 1).*f)(); }

            static int Invoke(lua_State* state)
            {
                void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                if (!calleePtr)
                {
                    lua_pushnil(state);
                    return 1;
                }
                LUAAA_TRY
                {
                    LuaStackReturn<P>(state, call(state, *(F*)calleePtr, std::integral_constant<int, MODE>()));
                }
                LUAAA_CATCH(state, "property getter")
                return 1;
            }
        };

        template<typename F, typename P, int MODE>
        struct PropSet
        {
            static void call(lua_State* state, F& f, std::integral_constant<int, 0>) { f(LuaStack<P>::get(state, 3)); }
            static void call(lua_State* state, F& f, std::integral_constant<int, 1>) { f(LuaStack<TCLASS>::get(state, 1), LuaStack<P>::get(state, 3)); }
            static void call(lua_State* state, F& f, std::integral_constant<int, 2>) { (LuaStack<TCLASS>::get(state, 1).*f)(LuaStack<P>::get(state, 3)); }

            static int Invoke(lua_State* state)
            {
                void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                if (calleePtr)
                {
                    LUAAA_TRY
                    {
                        call(state, *(F*)calleePtr, std::integral_constant<int, MODE>());
                    }
                    LUAAA_CATCH(state, "property setter")
                }
                return 0;
            }
        };

        template <typename F>
        inline LuaClass<TCLASS>& _property(int slot, const char* name, lua_CFunction invoker, const F& f)
        {
#if LUAAA_FEATURE_PROPERTY
            lua_State* L = m_state;
            pushMetatable(L);                                   // [mt]
            const int mt = lua_gettop(L);
            lua_rawgeti(L, mt, slot);                           // [mt, props]
            lua_pushstring(L, name);
            luaaa_detail::push_holder(L, f);
            lua_pushcclosure(L, invoker, 1);
            lua_rawset(L, mt + 1);
            _useIndexFunction(mt);
            lua_settop(L, mt - 1);
#else
            (void)slot; (void)name; (void)invoker; (void)f;
#endif
            return (*this);
        }

    public:
        template<typename P>
        inline LuaClass<TCLASS>& get(const char* name, P(*f)())
        {
            return _property(luaaa_detail::MT_GETTERS, name, &PropGet<P(*)(), P, 0>::Invoke, f);
        }

        template<typename P>
        inline LuaClass<TCLASS>& get(const char* name, P(*f)(const TCLASS&))
        {
            return _property(luaaa_detail::MT_GETTERS, name, &PropGet<P(*)(const TCLASS&), P, 1>::Invoke, f);
        }

        template<typename P, typename FCLASS>
        inline LuaClass<TCLASS>& get(const char* name, P(FCLASS::*f)()const)
        {
            static_assert(std::is_base_of<FCLASS, TCLASS>::value, "Error: prop function can be member function of only associated class");
            return _property(luaaa_detail::MT_GETTERS, name, &PropGet<P(FCLASS::*)()const, P, 2>::Invoke, f);
        }

        template<typename P, typename TRET>
        inline LuaClass<TCLASS>& set(const char* name, TRET(*f)(P))
        {
            return _property(luaaa_detail::MT_SETTERS, name, &PropSet<TRET(*)(P), P, 0>::Invoke, f);
        }

        template<typename P, typename TRET, typename FCLASS>
        inline LuaClass<TCLASS>& set(const char* name, TRET(FCLASS::* f)(P))
        {
            static_assert(std::is_base_of<FCLASS, TCLASS>::value, "prop function can be member function of only associated class");
            return _property(luaaa_detail::MT_SETTERS, name, &PropSet<TRET(FCLASS::*)(P), P, 2>::Invoke, f);
        }

        template<typename P, typename TRET>
        inline LuaClass<TCLASS>& set(const char* name, TRET(*f)(TCLASS&, P))
        {
            return _property(luaaa_detail::MT_SETTERS, name, &PropSet<TRET(*)(TCLASS&, P), P, 1>::Invoke, f);
        }

#if !LUAAA_WITHOUT_CPP_STDLIB
        // access prop from lambdas
        template<typename P>
        inline LuaClass<TCLASS>& get(const char* name, std::function<P()> f)
        {
            return _property(luaaa_detail::MT_GETTERS, name, &PropGet<std::function<P()>, P, 0>::Invoke, f);
        }

        template<typename P>
        inline LuaClass<TCLASS>& get(const char* name, std::function<P(const TCLASS&)> f)
        {
            return _property(luaaa_detail::MT_GETTERS, name, &PropGet<std::function<P(const TCLASS&)>, P, 1>::Invoke, f);
        }

        template<typename P>
        inline LuaClass<TCLASS>& get(const char* name, std::function<P(TCLASS&)> f)
        {
            return _property(luaaa_detail::MT_GETTERS, name, &PropGet<std::function<P(TCLASS&)>, P, 1>::Invoke, f);
        }

        template<typename F>
        inline LuaClass<TCLASS>& get(const char* name, F f)
        {
            return get(name, to_class_getter_function<TCLASS>(f));
        }

        template<typename P, typename TRET>
        inline LuaClass<TCLASS>& set(const char* name, std::function<TRET(P)> f)
        {
            return _property(luaaa_detail::MT_SETTERS, name, &PropSet<std::function<TRET(P)>, P, 0>::Invoke, f);
        }

        template<typename P, typename TRET>
        inline LuaClass<TCLASS>& set(const char* name, std::function<TRET(TCLASS&, P)> f)
        {
            return _property(luaaa_detail::MT_SETTERS, name, &PropSet<std::function<TRET(TCLASS&, P)>, P, 1>::Invoke, f);
        }

        template<typename P, typename TRET>
        inline LuaClass<TCLASS>& set(const char* name, std::function<TRET(const TCLASS&, P)> f)
        {
            return _property(luaaa_detail::MT_SETTERS, name, &PropSet<std::function<TRET(const TCLASS&, P)>, P, 1>::Invoke, f);
        }

        template<typename F>
        inline LuaClass<TCLASS>& set(const char* name, F f)
        {
            return set(name, to_class_setter_function<TCLASS>(f));
        }

        template <typename F>
        inline LuaClass<TCLASS>& fun(const std::string& name, F f)
        {
            return fun(name.c_str(), f);
        }

        template <typename F>
        inline LuaClass<TCLASS>& overload(const std::string& name, F f)
        {
            return overload(name.c_str(), f);
        }

        template <typename F>
        inline LuaClass<TCLASS>& staticFun(const std::string& name, F f)
        {
            return staticFun(name.c_str(), f);
        }

        template <typename V>
        inline LuaClass<TCLASS>& def(const std::string& name, const V& val)
        {
            return def(name.c_str(), val);
        }

        template <typename V>
        inline LuaClass<TCLASS>& constant(const std::string& name, const V& val)
        {
            return constant(name.c_str(), val);
        }

        template <typename F>
        inline LuaClass<TCLASS>& get(const std::string& name, F f)
        {
            return get(name.c_str(), f);
        }

        template <typename F>
        inline LuaClass<TCLASS>& set(const std::string& name, F f)
        {
            return set(name.c_str(), f);
        }
#endif

    public:
        // The address that identifies this C++ type (see s_typeKey).
        static const void * typeKey() { return &s_typeKey; }

        // Is this type bound in the given state?
        static bool isBound(lua_State * L) {
            LUAAA_rawgetp(L, LUA_REGISTRYINDEX, &s_metaKey);
            const bool bound = lua_istable(L, -1);
            lua_pop(L, 1);
            return bound;
        }

        // Push this type's metatable in the given state; pushes nothing and
        // returns false when the type is not bound there.
        static bool pushMetatable(lua_State * L) {
            LUAAA_rawgetp(L, LUA_REGISTRYINDEX, &s_metaKey);
            if (lua_istable(L, -1))
            {
                return true;
            }
            lua_pop(L, 1);
            return false;
        }

        // name of this type in the given state, or nullptr if not registered there.
        static const char * klassName(lua_State * L) {
            LUAAA_rawgetp(L, LUA_REGISTRYINDEX, &s_typeKey);
            const char * name = lua_tostring(L, -1);   // nil -> nullptr
            lua_pop(L, 1);
            return name;
        }

    private:
        //----------------------------------------------------
        // registration plumbing
        //----------------------------------------------------
        // metatable.__index = class_index (instead of the bare methods table).
        void _useIndexFunction(int mt)
        {
            lua_pushvalue(m_state, mt);
            lua_pushcclosure(m_state, &luaaa_detail::class_index, 1);
            lua_setfield(m_state, mt, "__index");
        }

        // table[name] = value (stack index v), or one more overload of it.
        void _store(int table, const char* name, int v, luaaa_detail::OverloadMatcher matcher, int skip)
        {
            lua_State* L = m_state;
            if (matcher)
            {
                lua_pushvalue(L, table);
                lua_pushvalue(L, v);
                luaaa_detail::add_overload(L, name, matcher, skip);
                lua_pop(L, 1);
            }
            else
            {
                lua_pushstring(L, name);
                lua_pushvalue(L, v);
                lua_rawset(L, table);
            }
        }

        // Register the value on top of the stack (popped) as a member:
        //   __gc / __index / __newindex : the user's handler, called by ours
        //   other "__name"              : a metamethod -- into the metatable,
        //                                 where Lua looks for it (and also
        //                                 reachable as obj.__name)
        //   anything else               : into the methods table
        void _setMember(const char* name, luaaa_detail::OverloadMatcher matcher)
        {
            lua_State* L = m_state;
            const int v = lua_gettop(L);
            if (!pushMetatable(L))
            {
                lua_settop(L, v - 1);
                return;
            }
            const int mt = v + 1;
            if (strcmp(name, "__gc") == 0 || strcmp(name, "__index") == 0 || strcmp(name, "__newindex") == 0)
            {
                lua_pushfstring(L, "!%s", name);
                lua_pushvalue(L, v);
                lua_rawset(L, mt);
                if (strcmp(name, "__index") == 0)
                {
                    _useIndexFunction(mt);
                }
            }
            else
            {
                if (name[0] == '_' && name[1] == '_')
                {
                    _store(mt, name, v, matcher, 1);
                }
                lua_rawgeti(L, mt, luaaa_detail::MT_METHODS);
                _store(mt + 1, name, v, matcher, 1);
            }
            lua_settop(L, v - 1);
        }

        // Register the value on top of the stack (popped) in the class table.
        LuaClass<TCLASS>& _setStatic(const char* name, luaaa_detail::OverloadMatcher matcher)
        {
            lua_State* L = m_state;
            const int v = lua_gettop(L);
            luaaa_detail::push_named_table(L, klassName(L));    // [v, classTable]
            _store(v + 1, name, v, matcher, 0);
            lua_settop(L, v - 1);
            return (*this);
        }

        template<typename F>
        inline LuaClass<TCLASS>& _bind(const char* name, lua_CFunction caller, const F& f, luaaa_detail::OverloadMatcher matcher)
        {
            luaaa_detail::push_holder(m_state, f);
            lua_pushcclosure(m_state, caller, 1);
            _setMember(name, matcher);
            return (*this);
        }

        // `self` is read as the class being bound when the member belongs to
        // one of its base classes, so that base need not be bound itself.
        template<typename FCLASS>
        struct SelfOf : std::conditional<std::is_base_of<FCLASS, TCLASS>::value, TCLASS, FCLASS> {};

        template<typename FCLASS, typename FRET, typename ...FARGS>
        inline LuaClass<TCLASS>& _reg(const char * name, FRET(FCLASS::*f)(FARGS...), bool overloaded)
        {
            return _bind(name, MethodCaller<typename SelfOf<FCLASS>::type>(f), f, overloaded ? luaaa_detail::matcher_of(f) : nullptr);
        }

        template<typename FCLASS, typename FRET, typename ...FARGS>
        inline LuaClass<TCLASS>& _reg(const char * name, FRET(FCLASS::*f)(FARGS...) const, bool overloaded)
        {
            return _bind(name, MethodCaller<typename SelfOf<FCLASS>::type>(f), f, overloaded ? luaaa_detail::matcher_of(f) : nullptr);
        }

        template<typename FRET, typename ...FARGS>
        inline LuaClass<TCLASS>& _reg(const char * name, FRET(*f)(FARGS...), bool overloaded)
        {
            return _bind(name, MemberFunctionCaller(f), f, overloaded ? luaaa_detail::matcher_of(f) : nullptr);
        }

        inline LuaClass<TCLASS>& _reg(const char * name, lua_CFunction f, bool overloaded)
        {
            lua_pushcclosure(m_state, f, 0);
            _setMember(name, overloaded ? &luaaa_detail::match_anything : nullptr);
            return (*this);
        }

#if !LUAAA_WITHOUT_CPP_STDLIB
        template<typename TRET, typename ...ARGS>
        inline LuaClass<TCLASS>& _reg(const char * name, const std::function<TRET(ARGS...)>& f, bool overloaded)
        {
            return _bind(name, MemberFunctionCaller(f), f, overloaded ? luaaa_detail::matcher_of(f) : nullptr);
        }

        // lambdas and other function objects
        template<typename F>
        inline LuaClass<TCLASS>& _reg(const char * name, F f, bool overloaded)
        {
            return _reg(name, to_function(f), overloaded);
        }
#endif

        template<typename ...ARGS>
        inline void _pushPlacementCtor()
        {
            struct HelperClass {
                static int f_new(lua_State* state) {
                    // The object is constructed inside the userdata, right after
                    // the UserData block, with room for alignof(TCLASS)-1 bytes of
                    // padding: an over-aligned TCLASS (SIMD, alignas) must not be
                    // placement-new'd at a misaligned address.
                    PadMissingArgs<ARGS...>(state);
                    luaaa_detail::UserData* uData = luaaa_detail::new_userdata(state, (alignof(TCLASS) - 1) + sizeof(TCLASS));
                    TCLASS * obj = PlacementConstructorCaller<TCLASS, ARGS...>::Invoke(state, luaaa_detail::payload(uData, alignof(TCLASS)), 0);
                    uData->obj = obj;
                    uData->dtor = &luaaa_detail::destroy_in_place<TCLASS>;
                    pushMetatable(state);
                    lua_setmetatable(state, -2);
                    return 1;
                }
            };
            // The closure carries one upvalue (this type's key): a call boundary
            // identifies itself by its upvalue 1 (see "error plumbing"), and a
            // placement constructor has no callable holder to use for that.
            lua_pushlightuserdata(m_state, (void*)&s_typeKey);
            lua_pushcclosure(m_state, &HelperClass::f_new, 1);
        }

        // Body of the spawner-based constructors: call the spawner held in
        // upvalue 1 and wrap the object it returns (nil when it returns nullptr).
        template<typename ...ARGS>
        static int _spawn(lua_State* state, void (*dtor)(luaaa_detail::UserData*), void (*free_func)())
        {
            typedef TCLASS*(*SPAWNERFTYPE)(ARGS...);
            void * spawner = lua_touserdata(state, lua_upvalueindex(1));
            luaL_argcheck(state, spawner, 1, "cpp closure spawner not found.");
            PadMissingArgs<ARGS...>(state);
            luaaa_detail::UserData* uData = luaaa_detail::new_userdata(state, 0);
            TCLASS * obj = LuaInvoke<TCLASS*, SPAWNERFTYPE, ARGS...>(state, spawner, 0);
            if (!obj)
            {
                lua_pushnil(state);
                return 1;
            }
            uData->obj = obj;
            uData->dtor = dtor;
            uData->free_func = free_func;
            pushMetatable(state);
            lua_setmetatable(state, -2);
            return 1;
        }

        static void _dtorDelete(luaaa_detail::UserData* uData)
        {
            DestructorCaller<TCLASS>::Invoke(static_cast<TCLASS*>(uData->obj));
        }

        static void setKlassName(lua_State * L, const char * name) {
            lua_pushstring(L, name);
            LUAAA_rawsetp(L, LUA_REGISTRYINDEX, &s_typeKey);
        }

    private:
        lua_State * m_state;

        // Two per-TCLASS unique ids; their ADDRESSES are used as keys.
        //   registry[&s_typeKey]  = the lua name of the type in that state, so the
        //                           same C++ type can be bound under different names
        //                           in different (even coexisting) lua_States;
        //   metatable[&s_typeKey] = the type tag LuaStack<>::get checks;
        //   registry[&s_metaKey]  = the metatable.
        static char s_typeKey;
        static char s_metaKey;
    };

    template <typename TCLASS> char LuaClass<TCLASS>::s_typeKey = 0;
    template <typename TCLASS> char LuaClass<TCLASS>::s_metaKey = 0;


    // -----------------------------------
    // export module
    // -----------------------------------
    // A module is a plain table: "_G" (the default) is the globals table,
    // "a.b" is _G.a.b. Functions and values are stored in it directly. Nothing
    // else is touched -- in particular the table gets a metatable only when the
    // first PROPERTY (get / set) or a user __index / __newindex handler is
    // registered on it, because nothing else needs one. A table that already carries someone else's metatable is left
    // alone: properties are then unavailable on it.
    struct LuaModule
    {
    public:
        LuaModule(lua_State * state, const char * name = "_G")
            : m_state(state)
        {
#if !defined(LUA_RIDX_MAINTHREAD)
            // 5.1 / LuaJIT: note the main thread while binding (see LuaClass).
            (void)luaaa_detail::MainThread(state);
#endif
            size_t strBufLen = strlen(name) + 1;
            m_moduleName = new char[strBufLen];
            memcpy(m_moduleName, name, strBufLen);
            luaaa_detail::push_named_table(m_state, m_moduleName);      // create it
            lua_pop(m_state, 1);
        }

#if !LUAAA_WITHOUT_CPP_STDLIB
        LuaModule(lua_State * state, const std::string& name)
            : LuaModule(state, name.empty() ? "_G" : name.c_str())
        {}
#endif

        ~LuaModule()
        {
            if (m_moduleName) {
                delete[] m_moduleName;
                m_moduleName = nullptr;
            }
        }

        // m_moduleName is an owning new[]/delete[] buffer; a default copy would
        // alias it and double-free. LuaModule is a transient binding helper, so
        // just forbid copying rather than deep-copying.
        LuaModule(const LuaModule&) = delete;
        LuaModule& operator=(const LuaModule&) = delete;

    private:
        // Push the metatable of the module table at absolute index `module`,
        // giving it our dispatching one if it has none. Returns false (and
        // still pushes the metatable) when the table carries a foreign one.
        bool _pushMeta(int module)
        {
            lua_State* L = m_state;
            if (!lua_getmetatable(L, module))
            {
                lua_newtable(L);
                const int mt = lua_gettop(L);
                lua_pushboolean(L, 1);
                LUAAA_rawsetp(L, mt, luaaa_detail::ModuleMetaTag());
                lua_newtable(L);
                lua_rawseti(L, mt, 1);                          // getters
                lua_newtable(L);
                lua_rawseti(L, mt, 2);                          // setters
                lua_pushstring(L, m_moduleName);
                lua_setfield(L, mt, "__name");
                lua_pushvalue(L, mt);
                lua_pushcclosure(L, &luaaa_detail::module_index, 1);
                lua_setfield(L, mt, "__index");
                lua_pushvalue(L, mt);
                lua_pushcclosure(L, &luaaa_detail::module_newindex, 1);
                lua_setfield(L, mt, "__newindex");
                lua_pushvalue(L, mt);
                lua_setmetatable(L, module);
                return true;
            }
            LUAAA_rawgetp(L, lua_gettop(L), luaaa_detail::ModuleMetaTag());
            const bool ours = (lua_toboolean(L, -1) != 0);
            lua_pop(L, 1);
            return ours;
        }

        // module[name] = the value on top of the stack (popped).
        LuaModule& _set(const char* name)
        {
            lua_State* L = m_state;
            const int v = lua_gettop(L);
            luaaa_detail::push_named_table(L, m_moduleName);    // [v, module]
            lua_pushstring(L, name);
            lua_pushvalue(L, v);
            lua_rawset(L, v + 1);
            if (strcmp(name, "__index") == 0 || strcmp(name, "__newindex") == 0)
            {
                _pushMeta(v + 1);       // a user handler is called by our dispatcher
            }
            lua_settop(L, v - 1);
            return (*this);
        }

        // Register the closure on top of the stack (popped) as the getter
        // (slot 1) or setter (slot 2) of property `name`.
        LuaModule& _property(int slot, const char* name)
        {
            lua_State* L = m_state;
            const int v = lua_gettop(L);
            luaaa_detail::push_named_table(L, m_moduleName);    // [v, module]
            if (_pushMeta(v + 1))                               // [v, module, mt]
            {
                lua_rawgeti(L, v + 2, slot);                    // [v, module, mt, props]
                lua_pushstring(L, name);
                lua_pushvalue(L, v);
                lua_rawset(L, -3);
            }
            lua_settop(L, v - 1);
            return (*this);
        }

    public:
        // `f`: a free function, a std::function, a lambda, or a raw lua_CFunction.
        template<typename F>
        inline LuaModule& fun(const char * name, F f)
        {
            luaaa_detail::push_function(m_state, f);
            return _set(name);
        }

        // Like fun, but ADDS `f` to the functions already registered under
        // `name` with overload(); the call picks the first one the arguments fit.
        template<typename F>
        inline LuaModule& overload(const char * name, F f)
        {
            luaaa_detail::push_named_table(m_state, m_moduleName);
            luaaa_detail::push_function(m_state, f);
            luaaa_detail::add_overload(m_state, name, luaaa_detail::matcher_of(f), 0);
            lua_pop(m_state, 1);
            return (*this);
        }

        template <typename V>
        inline LuaModule& def(const char * name, const V& val)
        {
            LuaStack<V>::put(m_state, val);
            return _set(name);
        }

        // An object of a bound class as a module field. With `obj` == nullptr a
        // default-constructed one that lua owns; otherwise `obj` itself, which
        // lua releases with `deleter` (never, when that is nullptr too).
        template <typename TCLASS>
        inline LuaModule& def(const char* name, const luaaa::LuaClass<TCLASS>&, const TCLASS* obj = nullptr, void(*deleter)(TCLASS*) = nullptr)
        {
            struct HelperClass
            {
                static void f_free(luaaa_detail::UserData* uData) {
                    if (uData->obj && uData->free_func)
                    {
                        (reinterpret_cast<void(*)(TCLASS*)>(uData->free_func))(static_cast<TCLASS*>(uData->obj));
                    }
                }
                static void f_delete(luaaa_detail::UserData* uData) {
                    delete static_cast<TCLASS*>(uData->obj);
                }
            };
            if (!LuaClass<TCLASS>::isBound(m_state))
            {
                luaL_argerror(m_state, 1, lua_pushfstring(m_state, "cpp class `%s` not export in this state", LUAAA_RTTI_CLASS_NAME(TCLASS)));
            }
            TCLASS* target = const_cast<TCLASS*>(obj);
            void (*dtor)(luaaa_detail::UserData*) = nullptr;
            void (*free_func)() = nullptr;
            if (target)
            {
                if (deleter)
                {
                    dtor = &HelperClass::f_free;
                    free_func = reinterpret_cast<void(*)()>(deleter);   // stored BY VALUE in the object
                }
            }
            else
            {
                target = DefaultConstructor<TCLASS>::New();
                if (!target)
                {
                    // New() only returns nullptr when TCLASS has no default
                    // constructor: fail loudly at registration time.
                    luaL_error(m_state, "luaaa: cpp class `%s` is not default constructible; pass an explicit object to def()", LUAAA_RTTI_CLASS_NAME(TCLASS));
                }
                dtor = &HelperClass::f_delete;
            }
            luaaa_detail::UserData* uData = luaaa_detail::new_userdata(m_state, 0);
            uData->obj = target;
            uData->dtor = dtor;
            uData->free_func = free_func;
            LuaClass<TCLASS>::pushMetatable(m_state);
            lua_setmetatable(m_state, -2);
            return _set(name);
        }

        template <typename V>
        inline LuaModule& def(const char * name, const V val[], size_t length)
        {
            lua_newtable(m_state);
            for (size_t idx = 0; idx < length; ++idx)
            {
                LuaStack<V>::put(m_state, val[idx]);
                lua_rawseti(m_state, -2, (int)(idx + 1));
            }
            return _set(name);
        }

        // disable the cast from "const char [#]" to "char (*)[#]"
        inline LuaModule& def(const char * name, const char * str)
        {
            lua_pushstring(m_state, str);
            return _set(name);
        }

#if !LUAAA_WITHOUT_CPP_STDLIB
        template<typename F>
        inline LuaModule& fun(const std::string& name, F f)
        {
            return fun(name.c_str(), f);
        }

        template<typename F>
        inline LuaModule& overload(const std::string& name, F f)
        {
            return overload(name.c_str(), f);
        }

        template <typename V>
        inline LuaModule& def(const std::string& name, const V& val)
        {
            return def(name.c_str(), val);
        }
#endif

    private:
        template<typename F, typename P>
        struct PropGet
        {
            static int Invoke(lua_State* state)
            {
                void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                if (!calleePtr)
                {
                    lua_pushnil(state);
                    return 1;
                }
                LUAAA_TRY
                {
                    LuaStackReturn<P>(state, (*(F*)(calleePtr))());
                }
                LUAAA_CATCH(state, "property getter")
                return 1;
            }
        };

        template<typename F, typename P>
        struct PropSet
        {
            static int Invoke(lua_State* state)
            {
                void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                if (calleePtr)
                {
                    LUAAA_TRY
                    {
                        (*(F*)(calleePtr))(LuaStack<P>::get(state, 3));
                    }
                    LUAAA_CATCH(state, "property setter")
                }
                return 0;
            }
        };

        template <typename F>
        inline LuaModule& _accessor(int slot, const char* name, lua_CFunction invoker, const F& f)
        {
#if LUAAA_FEATURE_PROPERTY
            luaaa_detail::push_holder(m_state, f);
            lua_pushcclosure(m_state, invoker, 1);
            return _property(slot, name);
#else
            (void)slot; (void)name; (void)invoker; (void)f;
            return (*this);
#endif
        }

    public:
        template<typename P>
        inline LuaModule& get(const char* name, P(*f)())
        {
            static_assert(!std::is_void<P>::value, "Error: getter function must return a value.");
            return _accessor(1, name, &PropGet<P(*)(), P>::Invoke, f);
        }

        template<typename P, typename TRET>
        inline LuaModule& set(const char* name, TRET(*f)(P))
        {
            return _accessor(2, name, &PropSet<TRET(*)(P), P>::Invoke, f);
        }

#if !LUAAA_WITHOUT_CPP_STDLIB
        // access prop from lambdas
        template<typename P>
        inline LuaModule& get(const char* name, std::function<P()> f)
        {
            static_assert(!std::is_void<P>::value, "Error: getter function must return a value.");
            return _accessor(1, name, &PropGet<std::function<P()>, P>::Invoke, f);
        }

        template<typename F>
        inline LuaModule& get(const char* name, F f)
        {
            return get(name, to_module_getter_function(f));
        }

        template<typename P, typename TRET>
        inline LuaModule& set(const char* name, std::function<TRET(P)> f)
        {
            return _accessor(2, name, &PropSet<std::function<TRET(P)>, P>::Invoke, f);
        }

        template<typename F>
        inline LuaModule& set(const char* name, F f)
        {
            return set(name, to_module_setter_function(f));
        }

        template <typename F>
        inline LuaModule& get(const std::string& name, F f)
        {
            return get(name.c_str(), f);
        }

        template <typename F>
        inline LuaModule& set(const std::string& name, F f)
        {
            return set(name.c_str(), f);
        }
#endif

    private:
        lua_State * m_state;
        char * m_moduleName;
    };

}



#if !LUAAA_WITHOUT_CPP_STDLIB

#include <array>
#include <vector>
#include <deque>
#include <list>
#include <forward_list>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <tuple>

namespace LUAAA_NS
{
    // array
    template<typename K, size_t N>
    struct LuaStack<std::array<K, N>>
    {
        enum { LUAAA_TABLE_STACK = 1 };
        typedef std::array<K, N> Container;
        inline static Container get(lua_State * L, int idx)
        {
            Container result{};   // value-init so a short table leaves defined (zeroed) tail
            if (!(lua_istable(L, idx))) { luaaa_detail::conv_arg_error(L, idx, "required table not found on stack."); }
            if (lua_istable(L, idx))
            {
                // read by index to keep order deterministic, avoid stack residue on
                // over-long tables, and tolerate short tables (stop at first nil).
                for (size_t index = 0; index < N; ++index)
                {
                    lua_rawgeti(L, idx, (int)(index + 1));
                    if (lua_isnil(L, lua_gettop(L)))
                    {
                        lua_pop(L, 1);
                        break;
                    }
                    result[index] = LuaStack<typename Container::value_type>::get(L, lua_gettop(L));
                    lua_pop(L, 1);
                }
            }
            return result;
        }
        inline static void put(lua_State * L, const Container& s)
        {
            lua_newtable(L);
            int index = 1;
            for (auto it = s.begin(); it != s.end(); ++it)
            {
                LuaStack<typename Container::value_type>::put(L, *it);
                lua_rawseti(L, -2, index++);
            }
        }
    };

    // Array-like containers (vector, deque, list, forward_list and the set
    // family) are read the way `put` writes them and the way ipairs walks them:
    // t[1], t[2], ... up to the first nil. lua_next is NOT used for these: its
    // order is unspecified (a table built as {[1]=..,[2]=..} or filled back to
    // front may live in the hash part and come out shuffled), and it would also
    // sweep the values of non-array keys ({'a','b', x='c'}) into the container.
    //
    // Every container converter is one of two templates -- SequenceStack or
    // MapStack -- parameterized by how an element is added.
    namespace luaaa_detail {
        template<typename C>
        struct AddBack          // deque, list
        {
            C& c;
            AddBack(C& container, lua_State*, int) : c(container) {}
            template<typename V> void operator()(V&& v) { c.push_back(std::forward<V>(v)); }
        };

        template<typename C>
        struct AddBackReserved  // vector: the table length is a capacity hint only
        {
            C& c;
            AddBackReserved(C& container, lua_State* L, int idx) : c(container) { c.reserve(LUAAA_rawlen(L, idx)); }
            template<typename V> void operator()(V&& v) { c.push_back(std::forward<V>(v)); }
        };

        template<typename C>
        struct AddInsert        // the set family
        {
            C& c;
            AddInsert(C& container, lua_State*, int) : c(container) {}
            template<typename V> void operator()(V&& v) { c.insert(std::forward<V>(v)); }
        };

        template<typename C>
        struct AddAfter         // forward_list has no push_back; push_front would reverse the order
        {
            C& c;
            typename C::iterator it;
            AddAfter(C& container, lua_State*, int) : c(container), it(container.before_begin()) {}
            template<typename V> void operator()(V&& v) { it = c.insert_after(it, std::forward<V>(v)); }
        };

        template<typename C, typename ADD>
        struct SequenceStack
        {
            enum { LUAAA_TABLE_STACK = 1 };     // "travels as a lua table" (see match_arg)

            inline static C get(lua_State * L, int idx)
            {
                C result;
                if (!lua_istable(L, idx)) { conv_arg_error(L, idx, "required table not found on stack."); }
                idx = AbsIndex(L, idx);
                ADD add(result, L, idx);
                for (int i = 1; ; ++i)              // t[1], t[2], ... up to the first nil
                {
                    lua_rawgeti(L, idx, i);
                    if (lua_isnil(L, -1))
                    {
                        lua_pop(L, 1);
                        break;
                    }
                    add(LuaStack<typename C::value_type>::get(L, lua_gettop(L)));
                    lua_pop(L, 1);
                }
                return result;
            }

            inline static void put(lua_State * L, const C& s)
            {
                lua_newtable(L);
                int index = 1;
                for (auto it = s.begin(); it != s.end(); ++it)
                {
                    LuaStack<typename C::value_type>::put(L, *it);
                    lua_rawseti(L, -2, index++);
                }
            }
        };

        struct MapAssign        // map, unordered_map
        {
            template<typename C, typename K, typename V>
            static void add(C& c, K&& k, V&& v) { c[std::forward<K>(k)] = std::forward<V>(v); }
        };

        struct MapEmplace       // the multimaps have no operator[]
        {
            template<typename C, typename K, typename V>
            static void add(C& c, K&& k, V&& v) { c.emplace(std::forward<K>(k), std::forward<V>(v)); }
        };

        template<typename C, typename ADD>
        struct MapStack
        {
            enum { LUAAA_TABLE_STACK = 1 };

            inline static C get(lua_State * L, int idx)
            {
                C result;
                if (!lua_istable(L, idx)) { conv_arg_error(L, idx, "required table not found on stack."); }
                idx = AbsIndex(L, idx);
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    const int top = lua_gettop(L);
                    // Copy the key before converting it: converting a NUMBER key
                    // to a string rewrites it in place and would "confuse the
                    // next call to lua_next" (Lua manual).
                    lua_pushvalue(L, top - 1);
                    typename C::key_type key = LuaStack<typename C::key_type>::get(L, top + 1);
                    lua_pop(L, 1);
                    ADD::add(result, std::move(key), LuaStack<typename C::mapped_type>::get(L, top));
                    lua_pop(L, 1);
                }
                return result;
            }

            inline static void put(lua_State * L, const C& s)
            {
                lua_newtable(L);
                for (auto it = s.begin(); it != s.end(); ++it)
                {
                    LuaStack<typename C::key_type>::put(L, it->first);
                    LuaStack<typename C::mapped_type>::put(L, it->second);
                    lua_rawset(L, -3);
                }
            }
        };
    }

    template<typename K, typename ...ARGS>
    struct LuaStack<std::vector<K, ARGS...>>
        : luaaa_detail::SequenceStack<std::vector<K, ARGS...>, luaaa_detail::AddBackReserved<std::vector<K, ARGS...>>> {};

    template<typename K, typename ...ARGS>
    struct LuaStack<std::deque<K, ARGS...>>
        : luaaa_detail::SequenceStack<std::deque<K, ARGS...>, luaaa_detail::AddBack<std::deque<K, ARGS...>>> {};

    template<typename K, typename ...ARGS>
    struct LuaStack<std::list<K, ARGS...>>
        : luaaa_detail::SequenceStack<std::list<K, ARGS...>, luaaa_detail::AddBack<std::list<K, ARGS...>>> {};

    template<typename K, typename ...ARGS>
    struct LuaStack<std::forward_list<K, ARGS...>>
        : luaaa_detail::SequenceStack<std::forward_list<K, ARGS...>, luaaa_detail::AddAfter<std::forward_list<K, ARGS...>>> {};

    template<typename K, typename ...ARGS>
    struct LuaStack<std::set<K, ARGS...>>
        : luaaa_detail::SequenceStack<std::set<K, ARGS...>, luaaa_detail::AddInsert<std::set<K, ARGS...>>> {};

    template<typename K, typename ...ARGS>
    struct LuaStack<std::multiset<K, ARGS...>>
        : luaaa_detail::SequenceStack<std::multiset<K, ARGS...>, luaaa_detail::AddInsert<std::multiset<K, ARGS...>>> {};

    template<typename K, typename ...ARGS>
    struct LuaStack<std::unordered_set<K, ARGS...>>
        : luaaa_detail::SequenceStack<std::unordered_set<K, ARGS...>, luaaa_detail::AddInsert<std::unordered_set<K, ARGS...>>> {};

    template<typename K, typename ...ARGS>
    struct LuaStack<std::unordered_multiset<K, ARGS...>>
        : luaaa_detail::SequenceStack<std::unordered_multiset<K, ARGS...>, luaaa_detail::AddInsert<std::unordered_multiset<K, ARGS...>>> {};

    template<typename K, typename V, typename ...ARGS>
    struct LuaStack<std::map<K, V, ARGS...>>
        : luaaa_detail::MapStack<std::map<K, V, ARGS...>, luaaa_detail::MapAssign> {};

    template<typename K, typename V, typename ...ARGS>
    struct LuaStack<std::multimap<K, V, ARGS...>>
        : luaaa_detail::MapStack<std::multimap<K, V, ARGS...>, luaaa_detail::MapEmplace> {};

    template<typename K, typename V, typename ...ARGS>
    struct LuaStack<std::unordered_map<K, V, ARGS...>>
        : luaaa_detail::MapStack<std::unordered_map<K, V, ARGS...>, luaaa_detail::MapAssign> {};

    template<typename K, typename V, typename ...ARGS>
    struct LuaStack<std::unordered_multimap<K, V, ARGS...>>
        : luaaa_detail::MapStack<std::unordered_multimap<K, V, ARGS...>, luaaa_detail::MapEmplace> {};

    //========================================================
    // smart pointers
    //========================================================
    // std::shared_ptr<T> (T a bound class). Returned to lua, the object is
    // SHARED: the userdata keeps a shared_ptr of its own and releases it when
    // collected. Such an object can be passed back to a std::shared_ptr<T> (or
    // shared_ptr<Base>) parameter, which then shares ownership with lua -- and
    // to T& / T* parameters and as `self`, like any other object. An object
    // that lua holds in another way (made by ctor<>(), a raw pointer, ...) is
    // not accepted by a shared_ptr parameter: there is nothing to share.
    // nil <-> an empty pointer, in both directions.
    template<typename T>
    struct LuaStack<std::shared_ptr<T>>
    {
        typedef typename std::remove_cv<T>::type MT;

        inline static std::shared_ptr<T> get(lua_State * L, int idx)
        {
            if (lua_isnoneornil(L, idx))
            {
                return std::shared_ptr<T>();
            }
            int why = 0;
            luaaa_detail::UserData* owner = nullptr;
            void* p = luaaa_detail::to_object(L, idx, LuaClass<MT>::typeKey(), &why, &owner);
            if (p == nullptr)
            {
                luaaa_detail::bad_object(L, idx, LuaClass<MT>::klassName(L), why, LUAAA_RTTI_CLASS_NAME(T));
            }
            if (owner->holderKind != luaaa_detail::SharedHolderTag())
            {
                luaaa_detail::conv_arg_error(L, (idx > 0) ? idx : 1, "std::shared_ptr expected: this object is not held by a shared_ptr");
            }
            // aliasing constructor: share the holder's ownership, point at the (possibly base) object
            return std::shared_ptr<T>(*static_cast<std::shared_ptr<void>*>(owner->holder), static_cast<MT*>(p));
        }

        inline static void put(lua_State * L, const std::shared_ptr<T>& sp)
        {
            if (!sp) { lua_pushnil(L); return; }
            if (!LuaClass<MT>::pushMetatable(L))
            {
                luaL_argerror(L, 1, lua_pushfstring(L, "cpp class `%s` not export", LUAAA_RTTI_CLASS_NAME(T)));
            }
            typedef std::shared_ptr<void> Holder;
            luaaa_detail::UserData* uData = luaaa_detail::new_userdata(L, (alignof(Holder) - 1) + sizeof(Holder));
            uData->holder = new (luaaa_detail::payload(uData, alignof(Holder))) Holder(std::const_pointer_cast<MT>(sp));
            uData->holderKind = luaaa_detail::SharedHolderTag();
            uData->obj = const_cast<MT*>(sp.get());
            uData->dtor = &release;
            lua_insert(L, -2);
            lua_setmetatable(L, -2);
        }

    private:
        static void release(luaaa_detail::UserData* uData)
        {
            typedef std::shared_ptr<void> Holder;
            static_cast<Holder*>(uData->holder)->~Holder();
        }
    };

    // std::unique_ptr<T, D> (T a bound class). Returned to lua, the object is
    // OWNED by lua: the unique_ptr moves into the userdata and its deleter runs
    // when the userdata is collected. Passed to a std::unique_ptr<T, D>
    // parameter, ownership moves back to C++ and the lua object becomes
    // invalid (using it raises "invalid user data") -- only an object that
    // came from exactly that unique_ptr type can be taken this way.
    // nil <-> an empty pointer, in both directions.
    template<typename T, typename D>
    struct LuaStack<std::unique_ptr<T, D>>
    {
        typedef std::unique_ptr<T, D> Ptr;
        typedef typename std::remove_cv<T>::type MT;

        inline static Ptr get(lua_State * L, int idx)
        {
            if (lua_isnoneornil(L, idx))
            {
                return Ptr();
            }
            int why = 0;
            luaaa_detail::UserData* owner = nullptr;
            void* p = luaaa_detail::to_object(L, idx, LuaClass<MT>::typeKey(), &why, &owner);
            if (p == nullptr)
            {
                luaaa_detail::bad_object(L, idx, LuaClass<MT>::klassName(L), why, LUAAA_RTTI_CLASS_NAME(T));
            }
            if (owner->holderKind != luaaa_detail::UniqueHolderTag<Ptr>::key())
            {
                luaaa_detail::conv_arg_error(L, (idx > 0) ? idx : 1, "std::unique_ptr expected: this object is not owned by a matching unique_ptr");
            }
            Ptr* holder = static_cast<Ptr*>(owner->holder);
            Ptr taken(std::move(*holder));
            holder->~Ptr();
            owner->holder = nullptr;
            owner->holderKind = nullptr;
            owner->dtor = nullptr;
            owner->obj = nullptr;       // the lua object is dead from here on
            return taken;
        }

        inline static void put(lua_State * L, Ptr&& up)
        {
            if (!up) { lua_pushnil(L); return; }
            if (!LuaClass<MT>::pushMetatable(L))
            {
                luaL_argerror(L, 1, lua_pushfstring(L, "cpp class `%s` not export", LUAAA_RTTI_CLASS_NAME(T)));
            }
            luaaa_detail::UserData* uData = luaaa_detail::new_userdata(L, (alignof(Ptr) - 1) + sizeof(Ptr));
            uData->obj = const_cast<MT*>(up.get());
            uData->holder = new (luaaa_detail::payload(uData, alignof(Ptr))) Ptr(std::move(up));
            uData->holderKind = luaaa_detail::UniqueHolderTag<Ptr>::key();
            uData->dtor = &release;
            lua_insert(L, -2);
            lua_setmetatable(L, -2);
        }

    private:
        static void release(luaaa_detail::UserData* uData)
        {
            static_cast<Ptr*>(uData->holder)->~Ptr();
        }
    };

    // std::pair
    template<typename U, typename V>
    struct LuaStack<std::pair<U, V>>
    {
        enum { LUAAA_TABLE_STACK = 1 };
        typedef std::pair<U, V> Container;
        inline static Container get(lua_State * L, int idx)
        {
            Container result;
            if (!(lua_istable(L, idx))) { luaaa_detail::conv_arg_error(L, idx, "required table not found on stack."); }
            if (lua_istable(L, idx))
            {
                // read elements from inside the table (symmetric with put), not
                // from absolute stack slots idx+1/idx+2.
                lua_rawgeti(L, idx, 1);
                result.first = LuaStack<typename Container::first_type>::get(L, lua_gettop(L));
                lua_pop(L, 1);
                lua_rawgeti(L, idx, 2);
                result.second = LuaStack<typename Container::second_type>::get(L, lua_gettop(L));
                lua_pop(L, 1);
            }
            return result;
        }

        inline static void put(lua_State * L, const Container& s)
        {
            lua_newtable(L);
            LuaStack<typename Container::first_type>::put(L, s.first);
            lua_rawseti(L, -2, 1);
            LuaStack<typename Container::second_type>::put(L, s.second);
            lua_rawseti(L, -2, 2);
        }
    };
 
    // std::tuple 
#if LUAAA_CPLUSPLUS >= 201402L
    template <typename> struct is_tuple : std::false_type {};
    template <typename ...T> struct is_tuple<std::tuple<T...>> : std::true_type {};

    template<typename Tuple, std::size_t ...Index, typename = typename std::enable_if<is_tuple<Tuple>::value>::type>
    inline void load_tuple_from_lua_table(lua_State* L, int idx, Tuple& tuple, std::index_sequence<Index...>)
    {
        if (!(lua_istable(L, idx))) { luaaa_detail::conv_arg_error(L, idx, "required table not found on stack."); }
        if (lua_istable(L, idx))
        {
            // Read by index (symmetric with put's lua_rawseti writes) instead of
            // lua_next: no iteration-order dependency, and the stack stays
            // balanced per element. The old lua_next loop never popped its last
            // key, and that residue poisoned any ENCLOSING lua_next loop (e.g. a
            // vector<tuple<...>> argument): the outer lua_next popped the
            // leftover key as its cursor and died with "invalid key to 'next'".
            // Missing tail elements keep their value-initialized defaults (same
            // tolerance as std::array's stop-at-first-nil).
            (void)std::initializer_list<int>{(
                lua_rawgeti(L, idx, (int)(Index + 1)),
                (lua_isnil(L, -1) ? 0 : (std::get<Index>(tuple) = LuaStack<typename std::tuple_element<Index, Tuple>::type>::get(L, lua_gettop(L)), 0)),
                lua_pop(L, 1),
                0)...};
        }
    }

    template<typename Tuple, std::size_t ...Index, typename = typename std::enable_if<is_tuple<Tuple>::value>::type>
    inline void save_tuple_to_lua_table(lua_State* L, const Tuple& tuple, std::index_sequence<Index...>)
    {
        lua_newtable(L);
        (void)std::initializer_list<int>{(
            // by element TYPE, not decltype(std::get<>) (a const T&): a reference
            // put would alias an element of this soon-to-be-destroyed tuple.
            LuaStack<typename std::tuple_element<Index, Tuple>::type>::put(L, std::get<Index>(tuple)),
            lua_rawseti(L, -2, Index + 1),
            0)...};
    }

    template<typename ...TS> 
    struct LuaStack<std::tuple<TS...>>
    {
        enum { LUAAA_TABLE_STACK = 1 };
        typedef std::tuple<TS...> Container;
        inline static Container get(lua_State* L, int idx)
        {
            Container result;
            load_tuple_from_lua_table(L, idx, result, std::make_index_sequence<std::tuple_size<Container>::value>{});
            return result;
        }

        inline static void put(lua_State* L, const Container& s)
        {
            save_tuple_to_lua_table(L, s, std::make_index_sequence<std::tuple_size<Container>::value>{});
        }
    };
#else
    template <typename T, size_t N>
    struct TupleAccessor
    {
        static void get(lua_State* L, int idx, T& tuple)
        {
            TupleAccessor<T, N - 1>::get(L, idx, tuple);
            // rawgeti (not lua_next): keeps the stack balanced per element and
            // the order deterministic -- see load_tuple_from_lua_table above for
            // why a leftover lua_next key breaks enclosing container loops.
            lua_rawgeti(L, idx, (int)N);
            if (!lua_isnil(L, -1))
            {
                std::get<N - 1>(tuple) = LuaStack<typename std::tuple_element<N - 1, T>::type>::get(L, lua_gettop(L));
            }
            lua_pop(L, 1);
        }

        static void put(lua_State* L, const T& tuple) {
            TupleAccessor<T, N - 1>::put(L, tuple);
            LuaStack<typename std::tuple_element<N - 1, T>::type>::put(L, std::get<N - 1>(tuple));
            lua_rawseti(L, -2, N);
        }
    };

    template <typename T>
    struct TupleAccessor<T, 0>
    {
        static void get(lua_State*, int, T&) {}
        static void put(lua_State*, const T&) {}
    };

    template<typename ...TS>
    struct LuaStack<std::tuple<TS...>>
    {
        enum { LUAAA_TABLE_STACK = 1 };
        typedef std::tuple<TS...> Container;
        inline static Container get(lua_State* L, int idx)
        {
            Container result;
            if (!(lua_istable(L, idx))) { luaaa_detail::conv_arg_error(L, idx, "required table not found on stack."); }
            if (lua_istable(L, idx))
            {
                TupleAccessor<Container, std::tuple_size<Container>::value>::get(L, idx, result);
            }
            return result;
        }

        inline static void put(lua_State* L, const Container& s)
        {
            lua_newtable(L);
            TupleAccessor<Container, std::tuple_size<Container>::value>::put(L, s);
        }
    };
#endif

}

#endif //#if !LUAAA_WITHOUT_CPP_STDLIB

#if !LUAAA_WITHOUT_CPP_STDLIB && LUAAA_CPLUSPLUS >= 201703L

#include <optional>
#include <string_view>

namespace LUAAA_NS
{
    // std::optional<T>: nil (or a missing argument) <-> std::nullopt; anything
    // else is converted as a T.
    template<typename T>
    struct LuaStack<std::optional<T>>
    {
        inline static std::optional<T> get(lua_State * L, int idx)
        {
            if (lua_isnoneornil(L, idx))
            {
                return std::nullopt;
            }
            return std::optional<T>(LuaStack<T>::get(L, idx));
        }

        inline static void put(lua_State * L, const std::optional<T>& v)
        {
            if (v)
            {
                LuaStack<T>::put(L, *v);
            }
            else
            {
                lua_pushnil(L);
            }
        }
    };

    // std::string_view: like std::string it carries its length (embedded NULs
    // survive); like `const char*` a view READ from lua points into the lua
    // string and is only valid while that value is alive -- in practice, for
    // the duration of the bound call. Unlike them it takes a string only (a
    // number is not converted: that would rewrite the lua value in place).
    template<>
    struct LuaStack<std::string_view>
    {
        inline static std::string_view get(lua_State * L, int idx)
        {
            luaaa_detail::conv_check_type(L, idx, LUA_TSTRING);
            size_t len = 0;
            const char * s = lua_tolstring(L, idx, &len);
            return std::string_view(s, len);
        }

        inline static void put(lua_State * L, const std::string_view& s)
        {
            lua_pushlstring(L, s.data(), s.size());
        }
    };

    namespace luaaa_detail {
        template<typename T>
        inline bool match_arg(lua_State* L, int idx, const std::optional<T>*, rank<4>)
        {
            return lua_isnoneornil(L, idx) || match_arg(L, idx, static_cast<const T*>(nullptr), rank<4>());
        }

        inline bool match_arg(lua_State* L, int idx, const std::string_view*, rank<4>)
        {
            return lua_type(L, idx) == LUA_TSTRING;
        }
    }
}

#endif // C++17

#undef LUAAA_TRY
#undef LUAAA_CATCH
#undef LUAAA_BOUNDARY_TRY
#undef LUAAA_BOUNDARY_CATCH
#undef LUAAA_IMPLEMENT_CALLBACK_INVOKER
#undef LUAAA_IMPLEMENT_FUNCTION_CALLER
#undef LUAAA_REPORT_CTOR_NAME_CONFLICT
#undef LUAAA_RTTI_CLASS_NAME
#undef LUAAA_CPLUSPLUS
#undef LUAAA_FOREIGN_EXCEPTION

#endif


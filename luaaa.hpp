
/*
 Copyright (c) 2019 gengyong
 https://github.com/gengyong/luaaa
 licensed under MIT License.
*/

#ifndef HEADER_LUAAA_HPP
#define HEADER_LUAAA_HPP

#define LUAAA_NS luaaa
#define LUAAA_VER_MAJOR (1)
#define LUAAA_VER_MINOR (5)

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

extern "C"
{
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM <= 501
inline void luaL_setfuncs(lua_State * L, const luaL_Reg * l, int nup) {
    luaL_checkstack(L, nup + 1, "too many upvalues");
    for (; l->name != nullptr; l++) {
        int i;
        lua_pushstring(L, l->name);
        for (i = 0; i < nup; i++)
            lua_pushvalue(L, -(nup + 1));
        lua_pushcclosure(L, l->func, nup);
        lua_settable(L, -(nup + 3));
    }
    lua_pop(L, nup);
}

inline void luaL_setmetatable(lua_State * L, const char * tname) {
    luaL_getmetatable(L, tname);
    lua_setmetatable(L, -2);
}

// lua_rawgetp / lua_rawsetp were introduced in 5.2; provide them for 5.1 / luajit.
// Only used here with LUA_REGISTRYINDEX (a pseudo-index unaffected by stack changes).
inline int lua_rawgetp(lua_State * L, int idx, const void * p) {
    lua_pushlightuserdata(L, const_cast<void*>(p));
    lua_rawget(L, idx);
    return lua_type(L, -1);
}

inline void lua_rawsetp(lua_State * L, int idx, const void * p) {
    lua_pushlightuserdata(L, const_cast<void*>(p)); // stack: ..., value, p
    lua_insert(L, -2);                              // stack: ..., p, value
    lua_rawset(L, idx);
}
#endif

#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM > 501 && !defined(LUA_COMPAT_MODULE)
#	define LUAAA_USE_NEW_MODULE_REGISTRY 1
#else
#	define LUAAA_USE_NEW_MODULE_REGISTRY 0
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
// LuaJIT's declarations -- route through these wrappers instead. On 5.1 the
// integer wrapper clamps to lua_Integer's range (like LuaJIT does) instead of
// relying on the implementation-defined double->integer cast.
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
#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM <= 501
    const lua_Number n = lua_tonumber(L, idx);
    if (isnum) *isnum = lua_isnumber(L, idx);
    const lua_Number hi = (lua_Number)(std::numeric_limits<lua_Integer>::max)();
    const lua_Number lo = (lua_Number)(std::numeric_limits<lua_Integer>::min)();
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
        lua_getglobal(m_state, (LuaClass<TCLASS>::klassName(m_state))); \
        if (!lua_isnil(m_state, -1)) { \
            lua_pushstring(m_state, ctorName); \
            lua_gettable(m_state, -2); \
            if (!lua_isnil(m_state, -1)) { \
                printf("Error: LuaClass<%s>::ctor has duplicated name:`%s`\n", LuaClass<TCLASS>::klassName(m_state), ctorName);\
            } \
            lua_pop(m_state, 1); \
        } \
        lua_pop(m_state, 1); \
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

        inline int AbsIndex(lua_State* L, int idx)
        {
            return (idx < 0 && idx > LUA_REGISTRYINDEX) ? (lua_gettop(L) + idx + 1) : idx;
        }

        // Push "<where>: luaaa: <context> failed: <what>" for the exception being
        // handled; returns its stack index. Call from inside a catch block only.
        inline int PushCppException(lua_State* L, const char* context)
        {
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
    template<typename ...ARGS>
    lua_CFunction NonMemberFunctionCaller(const std::function<void(ARGS...)>& func);
#endif

    //========================================================
    // Lua stack operator
    //========================================================

    template <typename T, typename = void> struct LuaStack
    {
        // Marks the primary template (the bound-class path); see luaaa_detail::put_ref.
        enum { LUAAA_BOUND_CLASS_STACK = 1 };

        inline static T& get(lua_State * state, int idx)
        {
#if LUAAA_WITHOUT_CPP_STDLIB
            if (!(LuaClass<T>::klassName(state) != nullptr)) { luaaa_detail::conv_arg_error(state, 1, "cpp class not export"); }
#else
            if (!(LuaClass<T>::klassName(state) != nullptr)) { luaaa_detail::conv_arg_error(state, 1, (std::string("cpp class `") + LUAAA_RTTI_CLASS_NAME(T) + "` not export").c_str()); }
#endif
            if (lua_istable(state, idx)) {
                // A lua-side object made by luaaa:extend: a table that keeps the
                // bound C++ object under "@", and whose metatable carries this
                // class's type tag (the class metatable itself, or the per-subclass
                // copy luaaa:extend derives from it). The tag is checked instead
                // of the metatable's identity or __name so the test is the same on
                // every Lua version and holds for those derived copies.
                // Every branch below leaves the stack exactly as it found it: a
                // residue here would poison an enclosing lua_next loop (e.g. a
                // map of extend-derived tables) -- lua_next pops whatever is
                // on top as its cursor key.
                if (lua_getmetatable(state, idx)) {                             // [mt]
                    lua_rawgetp(state, lua_gettop(state), &LuaClass<T>::s_typeKey);   // [mt, tag]
                    const bool tagged = (lua_touserdata(state, -1) == (void*)&LuaClass<T>::s_typeKey);
                    lua_pop(state, 2);                                          // []
                    if (tagged) {
                        lua_pushstring(state, "@");
                        lua_rawget(state, idx);                                 // ["@" value]
                        if (lua_type(state, -1) == LUA_TUSERDATA) {
                            T ** t = (T**)luaaa_detail::conv_check_udata(state, -1, LuaClass<T>::klassName(state));
                            if (!(t != nullptr && *t != nullptr)) { luaaa_detail::conv_arg_error(state, 1, "invalid user data"); }
                            T& ref = **t;
                            lua_pop(state, 1);
                            return ref;
                        }
                        lua_pop(state, 1);                                      // "@" value
                    }
                }
            }
            T ** t = (T**)luaaa_detail::conv_check_udata(state, idx, LuaClass<T>::klassName(state));
            if (!(t != nullptr && *t != nullptr)) { luaaa_detail::conv_arg_error(state, 1, "invalid user data"); }
            return (**t);
        }

        // A returned C++ object is wrapped as a FULL userdata carrying the class
        // metatable, so methods stay callable on it and the value is type-checked
        // on the way back in (a bare lightuserdata has neither). Two flavors:
        //   put(L, T*)        -- non-owning alias: Lua never deletes it (dtor=nullptr)
        //   put(L, const T&)  -- owning copy: constructed in place, destroyed by __gc
        // A pointer whose class is not bound in this state degrades to a plain
        // lightuserdata (the legacy opaque-handle pattern); a by-value object
        // cannot be represented that way without dangling, so it raises instead.
        inline static void put(lua_State * L, T * t)
        {
            if (!t) { lua_pushnil(L); return; }
            const char * kname = LuaClass<T>::klassName(L);
            if (kname)
            {
                auto uData = (typename LuaClass<T>::UserDataDetail*)lua_newuserdata(L, sizeof(typename LuaClass<T>::UserDataDetail));
                uData->obj = t;
                uData->dtor = nullptr;          // non-owning alias
                uData->free_func = nullptr;
                luaL_setmetatable(L, kname);
            }
            else
            {
                lua_pushlightuserdata(L, t);
            }
        }

        inline static void put(lua_State * L, const T & t)
        {
            const char * kname = LuaClass<T>::klassName(L);
#if LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(L, kname != nullptr, 1, "cpp class not export");
#else
            luaL_argcheck(L, kname != nullptr, 1, (std::string("cpp class `") + LUAAA_RTTI_CLASS_NAME(T) + "` not export").c_str());
#endif
            struct Holder {
                static int f_dtor(typename LuaClass<T>::UserDataDetail* uData) {
                    if (uData && uData->obj) { (uData->obj)->~T(); }
                    return 0;
                }
            };
            // Extra alignof(T)-1 bytes so the in-place object can be aligned even
            // when alignof(T) exceeds the base block's alignment.
            auto uData = (typename LuaClass<T>::UserDataDetail*)lua_newuserdata(L, sizeof(typename LuaClass<T>::UserDataDetail) + (alignof(T) - 1) + sizeof(T));
            const uintptr_t addr = (reinterpret_cast<uintptr_t>(uData + 1) + (alignof(T) - 1)) & ~(uintptr_t)(alignof(T) - 1);
            T* obj = nullptr;
            LUAAA_TRY
            {
                obj = new ((void*)addr) T(t);   // owning copy
            }
            LUAAA_CATCH(L, "copy object into lua")
            uData->obj = obj;
            uData->dtor = Holder::f_dtor;
            uData->free_func = nullptr;
            luaL_setmetatable(L, kname);
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
        inline static T * get(lua_State * state, int idx)
        {
            // The pointee's cv-qualifiers mean nothing to the Lua side: an object
            // bound as Foo must be passable to a const Foo* parameter. Normalize
            // before the klassName lookup (only LuaClass<Foo> is ever registered).
            typedef typename std::remove_cv<T>::type MT;
            if (lua_islightuserdata(state, idx))
            {
                T * t = (T*)lua_touserdata(state, idx);
                return t;
            }
            else if (lua_isuserdata(state, idx))
            {
                if (LuaClass<MT*>::klassName(state) != nullptr)
                {
                    T ** t = (T**)luaaa_detail::conv_check_udata(state, idx, LuaClass<MT*>::klassName(state));
                    if (!(t != nullptr && *t != nullptr)) { luaaa_detail::conv_arg_error(state, 1, "invalid user data"); }
                    return *t;
                }
                if (LuaClass<MT>::klassName(state) != nullptr)
                {
                    T ** t = (T**)luaaa_detail::conv_check_udata(state, idx, LuaClass<MT>::klassName(state));
                    if (!(t != nullptr && *t != nullptr)) { luaaa_detail::conv_arg_error(state, 1, "invalid user data"); }
                    return *t;
                }
            }
            // The argument is neither a bound userdata nor a light userdata (e.g. nil,
            // a number, a string, a table). Returning nullptr silently would hand the
            // C++ side a null pointer that a subsequent dereference would crash on.
            // Raise a clear Lua argument error instead, mirroring LuaStack<T>::get.
            // For a bound class, word it like luaL_checkudata does -- the lua
            // name of the class and the offending argument's position; the
            // (mangled) C++ type name is only a fallback for unbound pointees.
            const int arg = (idx > 0) ? idx : 1;
            const char * kname = LuaClass<MT>::klassName(state);
            if (kname != nullptr)
            {
                luaaa_detail::conv_arg_error(state, arg, lua_pushfstring(state, "%s expected, got %s", kname, luaL_typename(state, idx)));
            }
#if LUAAA_WITHOUT_CPP_STDLIB
            luaaa_detail::conv_arg_error(state, arg, "cpp pointer expected");
#else
            luaaa_detail::conv_arg_error(state, arg, (std::string("cpp pointer `") + LUAAA_RTTI_CLASS_NAME(T*) + "` expected").c_str());
#endif
            return nullptr;
        }

        inline static void put(lua_State * L, T * t)
        {
            // Wrap pointers of bound classes as typed, non-owning full userdata
            // (methods stay callable, type is checked on the way back in); fall
            // back to a plain lightuserdata for unbound pointee types.
            if (!t) { lua_pushnil(L); return; }
            typedef typename std::remove_cv<T>::type MT;
            if (LuaClass<MT>::klassName(L))
            {
                LuaStack<MT>::put(L, const_cast<MT*>(t));
            }
            else
            {
                lua_pushlightuserdata(L, const_cast<MT*>(t));
            }
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
            // Mirror the floating-point specialization: refuse to narrow a value
            // that is out of lua_Integer's representable range. Widening to long
            // double before comparing keeps the comparison itself UB-free.
            const long double lv = static_cast<long double>(t);
            const long double lo = static_cast<long double>(std::numeric_limits<lua_Integer>::min());
            const long double hi = static_cast<long double>(std::numeric_limits<lua_Integer>::max());
            if (lv < lo || lv > hi)
            {
                luaL_error(L, "number overflow: integer value out of lua_Integer range");
            }
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
    template<>
    struct LuaStack<std::string>
    {
        inline static std::string get(lua_State * L, int idx)
        {
            return LuaStack<const char *>::get(L, idx);
        }

        inline static void put(lua_State * L, const std::string& s)
        {
            LuaStack<const char *>::put(L, s.c_str());
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

    // push ret data to stack
    template <typename T>
    inline void LuaStackReturn(lua_State * L, T t)
    {
        lua_settop(L, 0);
        LuaStack<T>::put(L, t);
    }

    // A lua_pcall inside a C++ callback failed. We must NOT lua_error() here:
    // that longjmps across the intervening C++ stack frames (skipping their
    // destructors -> UB) and, if the callback was invoked outside any pcall
    // (e.g. from another thread or from pure C++), there is no setjmp target so
    // Lua would panic/abort. Instead report the message (debug builds only) and
    // pop it so the stack stays balanced; the caller returns a default value.
    inline void HandleCallbackError(lua_State * L)
    {
#if LUAAA_DEBUG
        const char * msg = lua_tostring(L, -1);
        fprintf(stderr, "luaaa: error in lua callback: %s\n", msg ? msg : "?");
#endif
        lua_pop(L, 1);   // discard the error message left by lua_pcall
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
                lua_rawsetp(L, LUA_REGISTRYINDEX, &s_mainThreadKey);
                return L;
            }
            lua_pop(L, 1);
            lua_rawgetp(L, LUA_REGISTRYINDEX, &s_mainThreadKey);
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
                int initParams[] = { (LuaStack<ARGS>::put(L, *static_cast<typename std::remove_reference<ARGS>::type*>(ctx->argv[Ns])), 0)..., 0 }; (void)initParams;
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
                    RET result = RET(); \
                    if (cacheLuaState) \
                    { \
                        void* argv[] = { luaaa_detail::erase_addr(args)..., nullptr }; \
                        luaaa_detail::CallbackContext ctx = { cacheLuaFuncId, argv, luaaa_detail::erase_addr(result) }; \
                        luaaa_detail::RunProtected(cacheLuaState, &luaaa_detail::CallbackRunner<RET, ARGS...>::Run, &ctx); \
                    } \
                    return result; \
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
            FTYPE* funPtr = (FTYPE*)lua_newuserdata(L, sizeof(FTYPE)); \
            new (funPtr) FTYPE(f); \
            AttachHolderFinalizer<FTYPE>(L); \
            lua_pushcclosure(L, NonMemberFunctionCaller(f), 1); \
        } \
    }; \
    template<typename ...ARGS> \
    struct LuaStack<void(CALLCONV*)(ARGS...)> \
    { \
        typedef void(CALLCONV*FTYPE)(ARGS...); \
        inline static FTYPE get(lua_State * L, int idx) \
        { \
            LUAAA_THREAD_LOCAL lua_State * cacheLuaState = nullptr; \
            LUAAA_THREAD_LOCAL int cacheLuaFuncId = 0; \
            struct HelperClass \
            { \
                static void CALLCONV f_callback(ARGS... args) \
                { \
                    if (cacheLuaState) \
                    { \
                        void* argv[] = { luaaa_detail::erase_addr(args)..., nullptr }; \
                        luaaa_detail::CallbackContext ctx = { cacheLuaFuncId, argv, nullptr }; \
                        luaaa_detail::RunProtected(cacheLuaState, &luaaa_detail::CallbackRunner<void, ARGS...>::Run, &ctx); \
                    } \
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
            FTYPE* funPtr = (FTYPE*)lua_newuserdata(L, sizeof(FTYPE)); \
            new (funPtr) FTYPE(f); \
            AttachHolderFinalizer<FTYPE>(L); \
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
                RET result = RET();
                void* argv[] = { luaaa_detail::erase_addr(args)..., nullptr };
                luaaa_detail::CallbackContext ctx = { ref, argv, luaaa_detail::erase_addr(result) };
                luaaa_detail::RunProtected(mainL, &luaaa_detail::CallbackRunner<RET, ARGS...>::Run, &ctx);
                return result;
            };
        }
        inline static void put(lua_State * L, FTYPE f)
        {
            // See the function-pointer variant: the callable must travel as an
            // upvalue (a holder userdata), not inside a bare lua_pushcfunction.
            FTYPE* funPtr = (FTYPE*)lua_newuserdata(L, sizeof(FTYPE));
            new (funPtr) FTYPE(f);
            AttachHolderFinalizer<FTYPE>(L);
            lua_pushcclosure(L, NonMemberFunctionCaller(f), 1);
        }
    };

    template<typename ...ARGS>
    struct LuaStack<std::function<void(ARGS...)>>
    {
        typedef std::function<void(ARGS...)> FTYPE;
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
            return [mainL, ref, guard](ARGS... args) -> void {
                void* argv[] = { luaaa_detail::erase_addr(args)..., nullptr };
                luaaa_detail::CallbackContext ctx = { ref, argv, nullptr };
                luaaa_detail::RunProtected(mainL, &luaaa_detail::CallbackRunner<void, ARGS...>::Run, &ctx);
            };
        }
        inline static void put(lua_State * L, FTYPE f)
        {
            // See the function-pointer variant: the callable must travel as an
            // upvalue (a holder userdata), not inside a bare lua_pushcfunction.
            FTYPE* funPtr = (FTYPE*)lua_newuserdata(L, sizeof(FTYPE));
            new (funPtr) FTYPE(f);
            AttachHolderFinalizer<FTYPE>(L);
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
    // non-member function caller & static member function caller
    //========================================================
    template<typename TRET, typename FTYPE, typename ...ARGS, std::size_t... Ns>
    TRET LuaInvokeImpl(lua_State* state, void* calleePtr, size_t skip, indices<Ns...>)
    {
        // Argument extraction (LuaStack<ARGS>::get) may itself luaL_error (longjmp),
        // but the actual C++ call below can throw. Wrap it so an exception becomes a
        // lua_error instead of escaping the extern "C" boundary and terminating.
        LUAAA_BOUNDARY_TRY(calleePtr)
        {
            return (*(FTYPE*)(calleePtr))(LuaStack<ARGS>::get(state, Ns + 1 + skip)...);
        }
        LUAAA_BOUNDARY_CATCH(state, "function call")
    }

    template<typename TRET, typename FTYPE, typename ...ARGS>
    inline TRET LuaInvoke(lua_State* state, void* calleePtr, size_t skip)
    {
        // make_arg_indices skips lua_State* slots so they consume no Lua argument (B8 fix).
        return LuaInvokeImpl<TRET, FTYPE, ARGS...>(state, calleePtr, skip, typename make_arg_indices<ARGS...>::type());
    }


#define LUAAA_IMPLEMENT_FUNCTION_CALLER(CALLERNAME, CALLCONV, SKIPPARAM) \
    template<typename TRET, typename ...ARGS> \
    lua_CFunction CALLERNAME(TRET(CALLCONV*func)(ARGS...)) \
    { \
        typedef decltype(func) FTYPE; (void)(func); \
        struct HelperClass \
        { \
            static int Invoke(lua_State* state) \
            { \
                void * calleePtr = lua_touserdata(state, lua_upvalueindex(1)); \
                luaL_argcheck(state, calleePtr, 1, "cpp closure function not found."); \
                if (calleePtr) \
                { \
                    LuaStackReturn<TRET>(state, LuaInvoke<TRET, FTYPE, ARGS...>(state, calleePtr, SKIPPARAM)); \
                    return 1; \
                } \
                return 0; \
            } \
        }; \
        return HelperClass::Invoke; \
    } \
    template<typename ...ARGS> \
    lua_CFunction CALLERNAME(void(CALLCONV*func)(ARGS...)) \
    { \
        typedef decltype(func) FTYPE; (void)(func); \
        struct HelperClass \
        { \
            static int Invoke(lua_State* state) \
            { \
                void * calleePtr = lua_touserdata(state, lua_upvalueindex(1)); \
                luaL_argcheck(state, calleePtr, 1, "cpp closure function not found."); \
                if (calleePtr) \
                { \
                    LuaInvoke<void, FTYPE, ARGS...>(state, calleePtr, SKIPPARAM); \
                } \
                return 0; \
            } \
        }; \
        return HelperClass::Invoke; \
    }

#if defined(_MSC_VER)
    LUAAA_IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, __cdecl, 0);
    LUAAA_IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, __cdecl, 1);
    LUAAA_IMPLEMENT_CALLBACK_INVOKER(__cdecl);

#	ifdef _M_CEE
    LUAAA_IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, __clrcall, 0);
    LUAAA_IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, __clrcall, 1);
    LUAAA_IMPLEMENT_CALLBACK_INVOKER(__clrcall);
#	endif

#	if defined(_M_IX86) && !defined(_M_CEE)
    LUAAA_IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, __fastcall, 0);
    LUAAA_IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, __fastcall, 1);
    LUAAA_IMPLEMENT_CALLBACK_INVOKER(__fastcall);
#	endif

#	ifdef _M_IX86
    LUAAA_IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, __stdcall, 0);
    LUAAA_IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, __stdcall, 1);
    LUAAA_IMPLEMENT_CALLBACK_INVOKER(__stdcall);
#	endif

#	if ((defined(_M_IX86) && _M_IX86_FP >= 2) || defined(_M_X64)) && !defined(_M_CEE)
    LUAAA_IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, __vectorcall, 0);
    LUAAA_IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, __vectorcall, 1);
    LUAAA_IMPLEMENT_CALLBACK_INVOKER(__vectorcall);
#	endif
#elif defined(__clang__)
#	define LUAAA_NOTHING
    LUAAA_IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, LUAAA_NOTHING, 0);
    LUAAA_IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, LUAAA_NOTHING, 1);
    LUAAA_IMPLEMENT_CALLBACK_INVOKER(LUAAA_NOTHING);
#	undef LUAAA_NOTHING
#elif defined(__GNUC__)
#	define LUAAA_NOTHING
    LUAAA_IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, LUAAA_NOTHING, 0);
    LUAAA_IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, LUAAA_NOTHING, 1);
    LUAAA_IMPLEMENT_CALLBACK_INVOKER(LUAAA_NOTHING);
#	undef LUAAA_NOTHING
#else
#	define LUAAA_NOTHING
    LUAAA_IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, LUAAA_NOTHING, 0);
    LUAAA_IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, LUAAA_NOTHING, 1);
    LUAAA_IMPLEMENT_CALLBACK_INVOKER(LUAAA_NOTHING);
#	undef LUAAA_NOTHING
#endif	

    //========================================================
    // member function invoker
    //========================================================
    template<typename TCLASS, typename TRET, typename FTYPE, typename ...ARGS, std::size_t... Ns>
    TRET LuaInvokeInstanceMemberImpl(lua_State* state, void* calleePtr, indices<Ns...>)
    {
        // Mirrors LuaInvokeImpl: a member function call can throw a C++ exception,
        // which must become a lua_error instead of escaping the extern "C" boundary
        // and terminating the host process (H4 -- the member path was the one gap
        // left by the original H2 exception-translation fix).
        // Argument extraction (LuaStack<ARGS>::get) may itself luaL_error (longjmp),
        // which is not a C++ exception and so is not caught here -- same as non-member.
        LUAAA_BOUNDARY_TRY(calleePtr)
        {
            return (LuaStack<TCLASS>::get(state, 1).**(FTYPE*)(calleePtr))(LuaStack<ARGS>::get(state, Ns + 2)...);
        }
        LUAAA_BOUNDARY_CATCH(state, "member function call")
    }

    template<typename TCLASS, typename TRET, typename FTYPE, typename ...ARGS>
    inline TRET LuaInvokeInstanceMember(lua_State* state, void* calleePtr)
    {
        // make_arg_indices skips lua_State* slots so they consume no Lua argument (B8 fix).
        return LuaInvokeInstanceMemberImpl<TCLASS, TRET, FTYPE, ARGS...>(state, calleePtr, typename make_arg_indices<ARGS...>::type());
    }


    template<typename TCLASS, typename TRET, typename ...ARGS>
    lua_CFunction MemberFunctionCaller(TRET(TCLASS::*func)(ARGS...))
    {
        typedef decltype(func) FTYPE; (void)(func);
        struct HelperClass
        {
            static int Invoke(lua_State* state)
            {
                void * calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                luaL_argcheck(state, calleePtr, 1, "cpp closure function not found.");
                if (calleePtr)
                {
                    LuaStackReturn<TRET>(state, LuaInvokeInstanceMember<TCLASS, TRET, FTYPE, ARGS...>(state, calleePtr));
                    return 1;
                }
                return 0;
            }
        };
        return HelperClass::Invoke;
    }

    template<typename TCLASS, typename TRET, typename ...ARGS>
    lua_CFunction MemberFunctionCaller(TRET(TCLASS::*func)(ARGS...)const)
    {
        typedef decltype(func) FTYPE; (void)(func);
        struct HelperClass
        {
            static int Invoke(lua_State* state)
            {
                void * calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                luaL_argcheck(state, calleePtr, 1, "cpp closure function not found.");
                if (calleePtr)
                {
                    LuaStackReturn<TRET>(state, LuaInvokeInstanceMember<TCLASS, TRET, FTYPE, ARGS...>(state, calleePtr));
                    return 1;
                }
                return 0;
            }
        };
        return HelperClass::Invoke;
    }

    template<typename TCLASS, typename ...ARGS>
    lua_CFunction MemberFunctionCaller(void(TCLASS::*func)(ARGS...))
    {
        typedef decltype(func) FTYPE; (void)(func);
        struct HelperClass
        {
            static int Invoke(lua_State* state)
            {
                void * calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                luaL_argcheck(state, calleePtr, 1, "cpp closure function not found.");
                if (calleePtr)
                {
                    LuaInvokeInstanceMember<TCLASS, void, FTYPE, ARGS...>(state, calleePtr);
                }
                return 0;
            }
        };
        return HelperClass::Invoke;
    }

    template<typename TCLASS, typename ...ARGS>
    lua_CFunction MemberFunctionCaller(void(TCLASS::*func)(ARGS...)const)
    {
        typedef decltype(func) FTYPE; (void)(func);
        struct HelperClass
        {
            static int Invoke(lua_State* state)
            {
                void * calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                luaL_argcheck(state, calleePtr, 1, "cpp closure function not found.");
                if (calleePtr)
                {
                    LuaInvokeInstanceMember<TCLASS, void, FTYPE, ARGS...>(state, calleePtr);
                }
                return 0;
            }
        };
        return HelperClass::Invoke;
    }

#if !LUAAA_WITHOUT_CPP_STDLIB
    template<typename TRET, typename ...ARGS>
    lua_CFunction MemberFunctionCaller(const std::function<TRET(ARGS...)>& func)
    {
        typedef std::function<TRET(ARGS...)> FTYPE; (void)(func);
        struct HelperClass
        {
            static int Invoke(lua_State* state)
            {
                void * calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                luaL_argcheck(state, calleePtr, 1, "cpp closure function not found.");
                if (calleePtr)
                {
                    LuaStackReturn<TRET>(state, LuaInvoke<TRET, FTYPE, ARGS...>(state, calleePtr, 1));
                    return 1;
                }
                return 0;
            }
        };
        return HelperClass::Invoke;
    }

    template<typename ...ARGS>
    lua_CFunction MemberFunctionCaller(const std::function<void(ARGS...)>& func)
    {
        typedef std::function<void(ARGS...)> FTYPE; (void)(func);
        struct HelperClass
        {
            static int Invoke(lua_State* state)
            {
                void * calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                luaL_argcheck(state, calleePtr, 1, "cpp closure function not found.");
                if (calleePtr)
                {
                    LuaInvoke<void, FTYPE, ARGS...>(state, calleePtr, 1);
                }
                return 0;
            }
        };
        return HelperClass::Invoke;
    }

    template<typename TRET, typename ...ARGS>
    lua_CFunction NonMemberFunctionCaller(const std::function<TRET(ARGS...)>& func)
    {
        typedef std::function<TRET(ARGS...)> FTYPE; (void)(func);
        struct HelperClass
        {
            static int Invoke(lua_State* state)
            {
                void * calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                luaL_argcheck(state, calleePtr, 1, "cpp closure function not found.");
                if (calleePtr)
                {
                    LuaStackReturn<TRET>(state, LuaInvoke<TRET, FTYPE, ARGS...>(state, calleePtr, 0));
                    return 1;
                }
                return 0;
            }
        };
        return HelperClass::Invoke;
    }

    template<typename ...ARGS>
    lua_CFunction NonMemberFunctionCaller(const std::function<void(ARGS...)>& func)
    {
        typedef std::function<void(ARGS...)> FTYPE; (void)(func);
        struct HelperClass
        {
            static int Invoke(lua_State* state)
            {
                void * calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                luaL_argcheck(state, calleePtr, 1, "cpp closure function not found.");
                if (calleePtr)
                {
                    LuaInvoke<void, FTYPE, ARGS...>(state, calleePtr, 0);
                }
                return 0;
            }
        };
        return HelperClass::Invoke;
    }
#endif

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
    // internal property-name builder
    //========================================================
    // Property getters/setters are stored under a prefixed key ("<name" for a
    // getter, ">name" for a setter). Build that key into `buf`, raising a lua
    // error instead of silently truncating -- a truncated key could otherwise
    // collide with a different property and read/write the wrong one.
    inline void MakeInternalPropName(lua_State* L, char* buf, size_t buflen, char prefix, const char* name)
    {
        int n = snprintf(buf, buflen, "%c%s", prefix, (name ? name : ""));
        if (n < 0 || (size_t)n >= buflen)
        {
            luaL_error(L, "luaaa: property name too long (max %d bytes): '%s'", (int)(buflen - 2), (name ? name : "?"));
        }
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
    // upvalue(1) = the class table, upvalue(2) = the C++ class metatable's __index.
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
        if (lua_isfunction(L, lua_upvalueindex(2)))
        {
            lua_pushvalue(L, lua_upvalueindex(2));
            lua_insert(L, 1);                           // [index, self, key]
            lua_call(L, 2, 1);
            return 1;
        }
        if (lua_istable(L, lua_upvalueindex(2)))        // LUAAA_FEATURE_PROPERTY=0: __index is a table
        {
            lua_pushvalue(L, 2);
            lua_gettable(L, lua_upvalueindex(2));
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
            lua_pushstring(L, "__index");
            lua_pushvalue(L, lua_upvalueindex(2));      // the class table
            lua_pushstring(L, "__index");
            lua_rawget(L, cmt);                         // the C++ class's __index
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
        lua_rawgetp(L, LUA_REGISTRYINDEX, &s_extendKey);   // (returns void on 5.2: test the value)
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
        lua_rawsetp(L, LUA_REGISTRYINDEX, &s_extendKey);
    }
#endif

    //========================================================
    // export class
    //========================================================
    template <typename TCLASS>
    struct LuaClass
    {
        friend struct DestructorCaller<TCLASS>;
        template<typename, typename> friend struct LuaStack;
        friend struct LuaModule;

        typedef struct _UserDataDetail {
            TCLASS * obj;
            int (*dtor)(_UserDataDetail*);
            // Custom deleter, type-erased as a generic function pointer (a
            // function-pointer <-> function-pointer round trip is always
            // well-defined). It is stored BY VALUE: the object must not depend
            // on any other Lua value staying alive to be destroyed -- the
            // constructor closure that also knows the deleter can be collected
            // first (e.g. `Class = nil`), long before the object is.
            void (*free_func)();
        } UserDataDetail;

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
#if LUAAA_WITHOUT_CPP_STDLIB
                luaL_argcheck(state, false, 1, "LuaClass name conflict: this C++ type is already bound to another name in this state");
#else
                luaL_argcheck(state, false, 1, (std::string("C++ class `") + LUAAA_RTTI_CLASS_NAME(TCLASS) + "` bind to conflict lua name `" + name + "`, origin name: `" + existing + "`. bind it in a separate lua_State or wrap it in a distinct C++ type.").c_str());
#endif
                return;
            }

            struct HelperClass {
                static int f__objgc(lua_State* state) {
                    // Never raise out of __gc: during lua_close there is no pcall
                    // to catch a luaL_error, so it would panic/abort the host.
                    //
                    // __gc is reachable from scripts too (obj:__gc(), or
                    // getmetatable(obj).__gc(anything)), not only from the collector.
                    // So (1) only trust a FULL userdata whose metatable carries this
                    // type's tag -- anything else (a table made by luaaa:extend, a
                    // foreign userdata such as io.stdout, a stripped metatable) is
                    // not ours to reinterpret as UserDataDetail; and (2) finalize at
                    // most once: obj/dtor are cleared afterwards, so a manual call
                    // followed by the collector's call cannot destroy the object
                    // twice, and LuaStack::get rejects the finalized object
                    // ("invalid user data") instead of handing out a dead pointer.
                    lua_settop(state, 1);                           // [ud]
                    if (lua_type(state, 1) != LUA_TUSERDATA || !lua_getmetatable(state, 1))
                    {
                        return 0;
                    }
                    lua_rawgetp(state, 2, &s_typeKey);              // [ud, mt, tag]
                    const bool ours = (lua_touserdata(state, -1) == (void*)&s_typeKey);
                    lua_pop(state, 1);                              // [ud, mt]
                    auto uData = (UserDataDetail*)lua_touserdata(state, 1);
                    if (!ours || !uData || !uData->obj)
                    {
                        return 0;   // not our object, or already finalized
                    }

                    lua_getfield(state, 2, "!__gc");                // [ud, mt, fn]
                    if (lua_isfunction(state, -1))
                    {
                        lua_insert(state, 1);                       // [fn, ud, mt]
                        // do not propagate errors out of a __gc handler.
                        lua_pcall(state, 2, 0, 0);
                    }

                    int (*dtor)(UserDataDetail*) = uData->dtor;
                    uData->dtor = nullptr;
                    if (dtor)
                    {
                        // __gc must not propagate exceptions: a C++ exception
                        // out of a __gc handler crosses the extern "C" boundary
                        // and terminates the host, and lua_error() inside __gc
                        // is itself undefined per the Lua manual. So swallow
                        // any exception here rather than translating it (H6).
#if LUAAA_HAS_EXCEPTIONS
                        try
#endif
                        {
                            dtor(uData);
                        }
#if LUAAA_HAS_EXCEPTIONS
                        catch (...) {
                            // suppressed: see comment above
                        }
#endif
                    }
                    uData->obj = nullptr;   // finalized: never destroy or hand out again
                    return 0;
                }

#if LUAAA_FEATURE_PROPERTY
                // Lookup order for `obj[key]`:
                //   string key : bound method/constant -> registered getter
                //                -> user __index -> (setter only => write-only error) -> nil
                //   number key : user __index -> nil        (e.g. an array-like class)
                //   other keys : nil
                // Methods and properties are named by STRINGS only. The key is never
                // converted: lua_tostring on a number would rewrite the caller's
                // value in place, so a user __index bound for obj[1] would be handed
                // "1", and a lua-side (luaaa:extend) object would store t[1] under "1".
                static int f_internal_index(lua_State* state)
                {
                    lua_settop(state, 2);                               // [self, key]
                    const int keyType = lua_type(state, 2);
                    if (keyType != LUA_TSTRING && keyType != LUA_TNUMBER)
                    {
                        lua_pushnil(state);
                        return 1;
                    }
                    lua_getmetatable(state, 1);                         // [self, key, mt]
                    const char* key = nullptr;
                    char internal_name[256];
                    if (keyType == LUA_TSTRING)
                    {
                        key = lua_tostring(state, 2);
                        lua_pushvalue(state, 2);
                        lua_rawget(state, 3);                           // [self, key, mt, value]
                        if (!lua_isnil(state, -1))
                        {
                            return 1;
                        }
                        lua_pop(state, 1);

                        MakeInternalPropName(state, internal_name, sizeof(internal_name), '<', key);
                        lua_getfield(state, 3, internal_name);          // [self, key, mt, getter]
                        if (lua_isfunction(state, -1))
                        {
                            lua_insert(state, 1);                       // [getter, self, key, mt]
                            lua_call(state, 3, 1);
                            return 1;
                        }
                        lua_pop(state, 1);
                    }

                    // user defined __index (function or table), given the ORIGINAL key
                    lua_getfield(state, 3, "!__index");                 // [self, key, mt, handler]
                    if (lua_isfunction(state, -1))
                    {
                        lua_insert(state, 1);                           // [handler, self, key, mt]
                        lua_call(state, 3, 1);
                        return 1;
                    }
                    if (lua_istable(state, -1))
                    {
                        lua_pushvalue(state, 2);
                        lua_rawget(state, -2);
                        return 1;
                    }
                    lua_pop(state, 1);                                  // [self, key, mt]

                    if (key)
                    {
                        MakeInternalPropName(state, internal_name, sizeof(internal_name), '>', key);
                        lua_getfield(state, 3, internal_name);
                        if (lua_isfunction(state, -1))
                        {
                            // Yes, there are something write-only, e.g., stdout, printer, digital io pin in output mode.
                            luaL_error(state, "attempt to read Write-Only property '%s' of '%s'", key, LuaClass<TCLASS>::klassName(state));
                        }
                    }
                    lua_pushnil(state);                                 // non-existing: nil
                    return 1;
                }

                // `obj[key] = value`:
                //   string key : registered setter -> (below)
                //   lua-side (luaaa:extend) object : plain field on the instance table
                //   userdata   : user __newindex (string/number key, ORIGINAL key)
                //                -> read-only / non-existing property error
                static int f_internal_newindex(lua_State* state)
                {
                    lua_settop(state, 3);                               // [self, key, value]
                    const int keyType = lua_type(state, 2);
                    lua_getmetatable(state, 1);                         // [self, key, value, mt]
                    const char* key = nullptr;
                    char internal_name[256];
                    if (keyType == LUA_TSTRING)
                    {
                        key = lua_tostring(state, 2);
                        MakeInternalPropName(state, internal_name, sizeof(internal_name), '>', key);
                        lua_getfield(state, 4, internal_name);          // [self, key, value, mt, setter]
                        if (lua_isfunction(state, -1))
                        {
                            lua_insert(state, 1);                       // [setter, self, key, value, mt]
                            lua_call(state, 4, 0);
                            return 0;
                        }
                        lua_pop(state, 1);
                    }

                    if (lua_istable(state, 1))
                    {
                        lua_settop(state, 3);
                        lua_rawset(state, 1);
                        return 0;
                    }

                    if (keyType != LUA_TSTRING && keyType != LUA_TNUMBER)
                    {
                        return 0;   // no setter can match such a key: leave the assignment untouched
                    }

                    lua_getfield(state, 4, "!__newindex");              // [self, key, value, mt, handler]
                    if (lua_isfunction(state, -1))
                    {
                        lua_insert(state, 1);                           // [handler, self, key, value, mt]
                        lua_call(state, 4, 0);
                        return 0;
                    }
                    if (lua_istable(state, -1))
                    {
                        lua_pushvalue(state, 2);
                        lua_pushvalue(state, 3);
                        lua_rawset(state, -3);
                        return 0;
                    }
                    lua_pop(state, 1);                                  // [self, key, value, mt]

                    if (key)
                    {
                        MakeInternalPropName(state, internal_name, sizeof(internal_name), '<', key);
                        lua_getfield(state, 4, internal_name);
                        if (lua_isfunction(state, -1))
                        {
                            luaL_error(state, "attempt to write Read-Only property '%s' of '%s'", key, LuaClass<TCLASS>::klassName(state));
                        }
                    }
                    lua_pushvalue(state, 2);                            // a copy: only the copy gets stringified
                    const char* shown = lua_tostring(state, -1);
                    luaL_error(state, "attempt to access Non-existing property '%s' of '%s'", (shown ? shown : "?"), LuaClass<TCLASS>::klassName(state));
                    return 0;
                }
#endif
            };

            // Object metatable, keyed by the lua name in this state's registry.
            // Two DIFFERENT C++ types must never share one lua name in a state:
            // luaL_checkudata matches by name, so both types' objects would pass
            // each other's checks -> reinterpret UB. Tag the metatable with this
            // type's key and reject foreign reuse (BEFORE recording our own name,
            // so a rejected bind leaves no registration residue); re-binding the
            // SAME type under its existing name (e.g. adding methods in a second
            // block) is fine.
            const int mtIndex = lua_gettop(state);
            if (luaL_newmetatable(state, name))
            {
                // NOTE: index the metatable by its ABSOLUTE position -- the 5.1
                // lua_rawgetp/lua_rawsetp polyfills resolve idx lazily, so a
                // relative index would slide as the key is pushed.
                lua_pushlightuserdata(state, (void*)&s_typeKey);
                lua_rawsetp(state, mtIndex + 1, &s_typeKey);   // metatable[&s_typeKey] = &s_typeKey
            }
            else
            {
                lua_rawgetp(state, mtIndex + 1, &s_typeKey);
                const void * tag = lua_touserdata(state, -1);
                lua_pop(state, 1);
#if LUAAA_WITHOUT_CPP_STDLIB
                luaL_argcheck(state, tag == (const void*)&s_typeKey, 1, "lua name already bound to a different cpp class");
#else
                luaL_argcheck(state, tag == (const void*)&s_typeKey, 1, (std::string("lua name `") + name + "` already bound to a different cpp class").c_str());
#endif
            }

            // Record this type's lua name for the current state (registry[&s_typeKey]).
            setKlassName(state, name);
            luaL_Reg objgc[] = {
                { "__gc", HelperClass::f__objgc },
#if LUAAA_FEATURE_PROPERTY
                { "__index", HelperClass::f_internal_index },
                { "__newindex", HelperClass::f_internal_newindex },
#endif
                { nullptr, nullptr }
            };
            luaL_setfuncs(state, objgc, 0);

#if !LUAAA_FEATURE_PROPERTY
            // if not register __index function, uncomment below codes
            lua_pushvalue(state, -1);
            lua_setfield(state, -2, "__index");
#endif

            if (functions)
            {
                luaL_setfuncs(state, functions, 0);
            }

            lua_pop(state, 1);

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

        template<typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const char * name = "new")
        {
            struct HelperClass {
                static int f_dtor(typename LuaClass<TCLASS>::UserDataDetail * uData) {
                    if (uData && uData->obj)
                    {
                        (uData->obj)->~TCLASS();
                    }
                    return 0;
                }

                static int f_new(lua_State* state) {
                    // Room for alignof(TCLASS)-1 bytes of padding: the object is
                    // constructed right after UserDataDetail, which only guarantees
                    // pointer alignment -- an over-aligned TCLASS (SIMD, alignas)
                    // would otherwise be placement-new'd at a misaligned address (UB).
                    PadMissingArgs<ARGS...>(state);
                    auto uData = (typename LuaClass<TCLASS>::UserDataDetail*)lua_newuserdata(state, sizeof(LuaClass<TCLASS>::UserDataDetail) + (alignof(TCLASS) - 1) + sizeof(TCLASS));
                    if (uData)
                    {
                        const uintptr_t aligned = (reinterpret_cast<uintptr_t>(uData + 1) + (alignof(TCLASS) - 1)) & ~(uintptr_t)(alignof(TCLASS) - 1);
                        TCLASS * obj = PlacementConstructorCaller<TCLASS, ARGS...>::Invoke(state, (void*)aligned, 0);
                        if (obj)
                        {
                            uData->obj = obj;
                            uData->dtor = HelperClass::f_dtor;
                            uData->free_func = nullptr;   // hygiene: leave no garbage field
                            luaL_setmetatable(state, (LuaClass<TCLASS>::klassName(state)));
                            return 1;
                        }
                        lua_pop(state, 1);
                    }
                    lua_pushnil(state);
                    return 1;
                }
            };

            LUAAA_REPORT_CTOR_NAME_CONFLICT(name);

            // The closure carries one upvalue (this type's key): a call boundary
            // identifies itself by its upvalue 1 (see "error plumbing"), and a
            // placement constructor has no callable holder to use for that.
            luaL_Reg constructor[] = { { name, HelperClass::f_new }, { nullptr, nullptr } };
#if LUAAA_USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, klassName(m_state));
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
            lua_pushlightuserdata(m_state, (void*)&s_typeKey);
            luaL_setfuncs(m_state, constructor, 1);
            lua_setglobal(m_state, klassName(m_state));
#else
            lua_pushlightuserdata(m_state, (void*)&s_typeKey);
            luaL_openlib(m_state, klassName(m_state), constructor, 1);
            lua_pop(m_state, 1);   // luaL_openlib leaves the class table on the stack
#endif
            return (*this);
        }

        template<typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const char * name, TCLASS*(*spawner)(ARGS...)) {
            typedef decltype(spawner) SPAWNERFTYPE;
            struct HelperClass {
                static int f_dtor(typename LuaClass<TCLASS>::UserDataDetail* uData) {
                    if (uData && uData->obj)
                    {
                        DestructorCaller<TCLASS>::Invoke(uData->obj);
                    }
                    return 0;
                }

                static int f_new(lua_State* state) {
                    void * spawner = lua_touserdata(state, lua_upvalueindex(1));
                    luaL_argcheck(state, spawner, 1, "cpp closure spawner not found.");
                    if (spawner) {
                        PadMissingArgs<ARGS...>(state);
                        auto uData = (typename LuaClass<TCLASS>::UserDataDetail*)lua_newuserdata(state, sizeof(LuaClass<TCLASS>::UserDataDetail));
                        if (uData)
                        {
                            auto obj = LuaInvoke<TCLASS*, SPAWNERFTYPE, ARGS...>(state, spawner, 0);
                            if (obj)
                            {
                                uData->obj = obj;
                                uData->dtor = HelperClass::f_dtor;
                                luaL_setmetatable(state, (LuaClass<TCLASS>::klassName(state)));
                                return 1;
                            }
                            lua_pop(state, 1);
                        }
                    }
                    lua_pushnil(state);
                    return 1;
                }
            };

            LUAAA_REPORT_CTOR_NAME_CONFLICT(name);

            luaL_Reg constructor[] = { { name, HelperClass::f_new }, { nullptr, nullptr } };
#if LUAAA_USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, klassName(m_state));
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
#endif

            SPAWNERFTYPE * spawnerPtr = (SPAWNERFTYPE*)lua_newuserdata(m_state, sizeof(SPAWNERFTYPE));
#if LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(m_state, spawnerPtr != nullptr, 1, "failed to allocate memory to store spawner for ctor");
#else
            luaL_argcheck(m_state, spawnerPtr != nullptr, 1, (std::string("failed to allocate memory to store spawner for ctor `") + name + "`").c_str());
#endif
            if (!spawnerPtr)
            {
                lua_pop(m_state, 1);
                return  (*this);
            }

            memset(spawnerPtr, 0, sizeof(SPAWNERFTYPE));
            *spawnerPtr = spawner;

#if LUAAA_USE_NEW_MODULE_REGISTRY
            luaL_setfuncs(m_state, constructor, 1);
            lua_setglobal(m_state, klassName(m_state));
#else
            luaL_openlib(m_state, klassName(m_state), constructor, 1);
            lua_pop(m_state, 1);   // luaL_openlib leaves the class table on the stack
#endif
            return (*this);
        }

        template<typename TRET, typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const char * name, TCLASS*(*spawner)(ARGS...), TRET(*deleter)(TCLASS*)){
            typedef decltype(spawner) SPAWNERFTYPE;
            typedef decltype(deleter) DELETERFTYPE;

            struct HelperClass {
                static int f_dtor(typename LuaClass<TCLASS>::UserDataDetail* uData) {
                    if (uData && uData->obj && uData->free_func)
                    {
                        (reinterpret_cast<DELETERFTYPE>(uData->free_func))(uData->obj);
                    }
                    return 0;
                }

                static int f_new(lua_State* state) {
                    void * spawner = lua_touserdata(state, lua_upvalueindex(1));
                    luaL_argcheck(state, spawner, 1, "cpp closure spawner not found.");

                    void * deleter = lua_touserdata(state, lua_upvalueindex(2));
                    luaL_argcheck(state, deleter, 2, "cpp closure deleter not found.");

                    if (spawner) {
                        PadMissingArgs<ARGS...>(state);
                        auto uData = (typename LuaClass<TCLASS>::UserDataDetail*)lua_newuserdata(state, sizeof(LuaClass<TCLASS>::UserDataDetail));
                        if (uData)
                        {
                            auto obj = LuaInvoke<TCLASS*, SPAWNERFTYPE, ARGS...>(state, spawner, 0);
                            if (obj)
                            {
                                uData->obj = obj;
                                uData->dtor = HelperClass::f_dtor;
                                uData->free_func = reinterpret_cast<void(*)()>(*(DELETERFTYPE*)deleter);
                                luaL_setmetatable(state, (LuaClass<TCLASS>::klassName(state)));
                                return 1;
                            }
                            lua_pop(state, 1);
                        }
                    }
                    lua_pushnil(state);
                    return 1;
                }
            };

            LUAAA_REPORT_CTOR_NAME_CONFLICT(name);

#if LUAAA_USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, klassName(m_state));
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
#endif

            SPAWNERFTYPE * spawnerPtr = (SPAWNERFTYPE*)lua_newuserdata(m_state, sizeof(SPAWNERFTYPE));
#if LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(m_state, spawnerPtr != nullptr, 0, ("failed to allocate memory to store spawner for ctor"));
#else
            luaL_argcheck(m_state, spawnerPtr != nullptr, 0, (std::string("failed to allocate memory to store spawner for ctor `") + name + "`").c_str());
#endif
            if (!spawnerPtr)
            {
                lua_pop(m_state, 2);
                return (*this);
            }

            memset(spawnerPtr, 0, sizeof(SPAWNERFTYPE));
            *spawnerPtr = spawner;

            DELETERFTYPE * deleterPtr = (DELETERFTYPE*)lua_newuserdata(m_state, sizeof(DELETERFTYPE));
#if LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(m_state, deleterPtr != nullptr, 0, ("failed to allocate memory to store deleter for ctor"));
#else
            luaL_argcheck(m_state, deleterPtr != nullptr, 0, (std::string("failed to allocate memory to store deleter for ctor `") + name + "`").c_str());
#endif
            if (!deleterPtr)
            {
                lua_pop(m_state, 3);
                return (*this);
            }

            memset(deleterPtr, 0, sizeof(DELETERFTYPE));
            *deleterPtr = deleter;

            luaL_Reg constructor[] = { { name, HelperClass::f_new }, { nullptr, nullptr } };
#if LUAAA_USE_NEW_MODULE_REGISTRY
            luaL_setfuncs(m_state, constructor, 2);
            lua_setglobal(m_state, klassName(m_state));
#else
            luaL_openlib(m_state, klassName(m_state), constructor, 2);
            lua_pop(m_state, 1);   // luaL_openlib leaves the class table on the stack
#endif
            return (*this);
        }

        template<typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const char * name, TCLASS*(*spawner)(ARGS...), std::nullptr_t) {
            typedef decltype(spawner) SPAWNERFTYPE;

            struct HelperClass {
                static int f_new(lua_State* state) {
                    void * spawner = lua_touserdata(state, lua_upvalueindex(1));
                    luaL_argcheck(state, spawner, 1, "cpp closure spawner not found.");
                    if (spawner) {
                        PadMissingArgs<ARGS...>(state);
                        auto uData = (typename LuaClass<TCLASS>::UserDataDetail*)lua_newuserdata(state, sizeof(LuaClass<TCLASS>::UserDataDetail));
                        if (uData)
                        {
                            auto obj = LuaInvoke<TCLASS*, SPAWNERFTYPE, ARGS...>(state, spawner, 0);
                            if (obj)
                            {
                                uData->obj = obj;
                                uData->dtor = nullptr;
                                luaL_setmetatable(state, (LuaClass<TCLASS>::klassName(state)));
                                return 1;
                            }
                            lua_pop(state, 1);
                        }
                    }
                    lua_pushnil(state);
                    return 1;
                }

            };

            LUAAA_REPORT_CTOR_NAME_CONFLICT(name);

#if LUAAA_USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, klassName(m_state));
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
#endif
            SPAWNERFTYPE * spawnerPtr = (SPAWNERFTYPE*)lua_newuserdata(m_state, sizeof(SPAWNERFTYPE));
#if LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(m_state, spawnerPtr != nullptr, 1, "failed to allocate memory to store spawner for ctor of cpp class");
#else
            luaL_argcheck(m_state, spawnerPtr != nullptr, 1, (std::string("failed to allocate memory to store spawner for ctor `") + name + "`").c_str());
#endif
            if (!spawnerPtr)
            {
                lua_pop(m_state, 1);
                return (*this);
            }

            memset(spawnerPtr, 0, sizeof(SPAWNERFTYPE));
            *spawnerPtr = spawner;

            luaL_Reg constructor[] = { { name, HelperClass::f_new }, { nullptr, nullptr } };
#if LUAAA_USE_NEW_MODULE_REGISTRY
            luaL_setfuncs(m_state, constructor, 1);
            lua_setglobal(m_state, klassName(m_state));
#else
            luaL_openlib(m_state, klassName(m_state), constructor, 1);
            lua_pop(m_state, 1);   // luaL_openlib leaves the class table on the stack
#endif
            return (*this);
        }

       
#if !LUAAA_WITHOUT_CPP_STDLIB
        template<typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const std::string& name)
        {
            return ctor<ARGS...>(name.c_str());
        }

        template<typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const std::string& name, TCLASS*(*spawner)(ARGS...)) {
            return ctor(name.c_str(), spawner);
        }

        template<typename TRET, typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const std::string& name, TCLASS*(*spawner)(ARGS...), TRET(*deleter)(TCLASS*)) {
            return ctor(name.c_str(), spawner, deleter);
        }

        template<typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const std::string& name, TCLASS*(*spawner)(ARGS...), std::nullptr_t) {
            return ctor(name.c_str(), spawner, nullptr);
        }
#endif

    private:
        template<typename F>
        inline LuaClass<TCLASS>& _registerClassFunction(const char* name, lua_CFunction caller, F f)
        {
            luaL_getmetatable(m_state, klassName(m_state));
            if (strcmp(name, "__gc") == 0)
            {
                lua_pushstring(m_state, "!__gc");
            }
#if LUAAA_FEATURE_PROPERTY
            else if (strcmp(name, "__index") == 0)
            {
                lua_pushstring(m_state, "!__index");
            }
            else if (strcmp(name, "__newindex") == 0)
            {
                lua_pushstring(m_state, "!__newindex");
            }
#endif
            else
            {
                lua_pushstring(m_state, name);
            }

            F* funPtr = (F*)lua_newuserdata(m_state, sizeof(F));
#if LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(m_state, funPtr != nullptr, 1, "failed to allocate memory to store function");
#else
            luaL_argcheck(m_state, funPtr != nullptr, 1, (std::string("failed to allocate memory to store function `") + name + "`").c_str());
#endif
            if (funPtr)
            {
                // placement-new: F may be non-trivially-copyable (e.g. std::function),
                // memset + operator= on unconstructed storage would be UB.
                new (funPtr) F(f);
                AttachHolderFinalizer<F>(m_state);   // run ~F() when the holder is collected
                lua_pushcclosure(m_state, caller, 1);
                lua_settable(m_state, -3);
                lua_pop(m_state, 1);
            }
            else
            {
                lua_pop(m_state, 2);
            }
            return (*this);
        }

    private:
        template<typename F>
        inline LuaClass<TCLASS>& _funImpl(const char * name, F f)
        {
            return _registerClassFunction(name, MemberFunctionCaller(f), f);
        }

    public:
        template<typename FCLASS, typename FRET, typename ...FARGS>
        inline LuaClass<TCLASS>& fun(const char * name, FRET(FCLASS::*f)(FARGS...))
        {
            return _funImpl(name, f);
        }

        template<typename FCLASS, typename FRET, typename ...FARGS>
        inline LuaClass<TCLASS>& fun(const char * name, FRET(FCLASS::*f)(FARGS...) const)
        {
            return _funImpl(name, f);
        }

        template<typename FCLASS, typename ...FARGS>
        inline LuaClass<TCLASS>& fun(const char * name, void(FCLASS::*f)(FARGS...))
        {
            return _funImpl(name, f);
        }

        template<typename FCLASS, typename ...FARGS>
        inline LuaClass<TCLASS>& fun(const char * name, void(FCLASS::*f)(FARGS...) const)
        {
            return _funImpl(name, f);
        }

        template<typename FRET, typename ...FARGS>
        inline LuaClass<TCLASS>& fun(const char * name, FRET(*f)(FARGS...))
        {
            return _funImpl(name, f);
        }

        template<typename ...FARGS>
        inline LuaClass<TCLASS>& fun(const char * name, void(*f)(FARGS...))
        {
            return _funImpl(name, f);
        }

#if !LUAAA_WITHOUT_CPP_STDLIB
        // register lambdas as lua class function
        template<typename TRET, typename ...ARGS>
        inline LuaClass<TCLASS>& fun(const char * name, const std::function<TRET(ARGS...)>& f)
        {
            return _funImpl<std::function<TRET(ARGS...)>>(name, f);
        }

        template<typename ...ARGS>
        inline LuaClass<TCLASS>& fun(const char * name, const std::function<void(ARGS...)>& f)
        {
            return _funImpl<std::function<void(ARGS...)>>(name, f);
        }

        template<typename F>
        inline LuaClass<TCLASS>& fun(const char * name, F f)
        {
            return fun(name, to_function(f));
        }
#endif

        inline LuaClass<TCLASS>& fun(const char * name, lua_CFunction f)
        {
            luaL_getmetatable(m_state, klassName(m_state));
            if (strcmp(name, "__gc") == 0)
            {
                lua_pushstring(m_state, "!__gc");
            }
#if LUAAA_FEATURE_PROPERTY
            else if (strcmp(name, "__index") == 0)
            {
                lua_pushstring(m_state, "!__index");
            }
            else  if (strcmp(name, "__newindex") == 0)
            {
                lua_pushstring(m_state, "!__newindex");
            }
#endif
            else
            {
                lua_pushstring(m_state, name);
            }
            lua_pushcclosure(m_state, f, 0);
            lua_settable(m_state, -3);
            lua_pop(m_state, 1);
            return (*this);
        }

        template <typename V>
        inline LuaClass<TCLASS>& def(const char * name, const V& val)
        {
            luaL_getmetatable(m_state, klassName(m_state));
            lua_pushstring(m_state, name);
            LuaStack<V>::put(m_state, val);
            lua_settable(m_state, -3);
            lua_pop(m_state, 1);
            return (*this);
        }

        // disable cast from "const char [#]" to "char (*)[#]"
        inline LuaClass<TCLASS>& def(const char* name, const char* str)
        {
            luaL_getmetatable(m_state, klassName(m_state));
            lua_pushstring(m_state, name);
            LuaStack<decltype(str)>::put(m_state, str);
            lua_settable(m_state, -3);
            lua_pop(m_state, 1);
            return (*this);
        }
      
    private:
        template <typename F>
        inline LuaClass<TCLASS>& _getterImpl(const char* name, lua_CFunction invoker, F f)
        {
#if LUAAA_FEATURE_PROPERTY
            char internal_name[256];
            MakeInternalPropName(m_state, internal_name, sizeof(internal_name), '<', name);
            return _registerClassFunction(internal_name, invoker, f);
#else
            return *this;
#endif
        }

        template<typename F>
        inline LuaClass<TCLASS>& _setterImpl(const char* name, lua_CFunction invoker, F f)
        {
#if LUAAA_FEATURE_PROPERTY
            char internal_name[256];
            MakeInternalPropName(m_state, internal_name, sizeof(internal_name), '>', name);
            return _registerClassFunction(internal_name, invoker, f);
#else
            return *this;
#endif
        }

    public:
        template<typename P>
        inline LuaClass<TCLASS>& get(const char* name, P(*f)())
        {
            struct HelperClass {
                typedef decltype(f) FTYPE;
                static inline int Invoke(lua_State* state) {
                    void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                    if (calleePtr)
                    {
                        LUAAA_TRY
                        {
                            LuaStackReturn<P>(state, (*(FTYPE*)(calleePtr))());
                        }
                        LUAAA_CATCH(state, "property getter")
                    }
                    else
                    {
                        lua_pushnil(state);
                    }
                    return 1;
                }
            };
            return _getterImpl(name, HelperClass::Invoke, f);
        }

        template<typename P>
        inline LuaClass<TCLASS>& get(const char* name, P(*f)(const TCLASS&))
        {
            struct HelperClass {
                static inline int Invoke(lua_State* state) {
                    typedef decltype(f) FTYPE;
                    void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                    if (calleePtr)
                    {
                        LUAAA_TRY
                        {
                            LuaStackReturn<P>(state, (*(FTYPE*)(calleePtr))(LuaStack<TCLASS>::get(state, 1)));
                        }
                        LUAAA_CATCH(state, "property getter")
                    }
                    else
                    {
                        lua_pushnil(state);
                    }
                    return 1;
                }
            };
            return _getterImpl(name, HelperClass::Invoke, f);
        }

        template<typename P, typename FCLASS>
        inline LuaClass<TCLASS>& get(const char* name, P(FCLASS::*f)()const)
        {
            static_assert(std::is_same<typename std::decay<TCLASS>::type, typename std::decay<FCLASS>::type>::value, "Error: prop function can be member function of only associated class");
            struct HelperClass {
                static inline int Invoke(lua_State* state) {
                    typedef decltype(f) FTYPE;
                    void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                    if (calleePtr)
                    {
                        LUAAA_TRY
                        {
                            LuaStackReturn<P>(state, (LuaStack<TCLASS>::get(state, 1).**(FTYPE*)(calleePtr))());
                        }
                        LUAAA_CATCH(state, "property getter")
                    }
                    else
                    {
                        lua_pushnil(state);
                    }
                    return 1;
                }
            };
            return _getterImpl(name, HelperClass::Invoke, f);
        }


        template<typename P, typename TRET>
        inline LuaClass<TCLASS>& set(const char* name, TRET(*f)(P))
        {
            struct HelperClass {
                static inline int Invoke(lua_State* state) {
                    typedef decltype(f) FTYPE;
                    void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                    if (calleePtr)
                    {
                        LUAAA_TRY
                        {
                            (*(FTYPE*)(calleePtr))(LuaStack<P>::get(state, 3));
                        }
                        LUAAA_CATCH(state, "property setter")
                    }
                    return 0;
                }
            };
            return _setterImpl(name, HelperClass::Invoke, f);
        }

        template<typename P, typename TRET, typename FCLASS>
        inline LuaClass<TCLASS>& set(const char* name, TRET(FCLASS::* f)(P))
        {
            static_assert(std::is_same<typename std::decay<TCLASS>::type, typename std::decay<FCLASS>::type>::value, "prop function can be member function of only associated class");
            struct HelperClass {
                static inline int Invoke(lua_State* state) {
                    typedef decltype(f) FTYPE;
                    void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                    if (calleePtr)
                    {
                        LUAAA_TRY
                        {
                            (LuaStack<TCLASS>::get(state, 1).**(FTYPE*)(calleePtr))(LuaStack<P>::get(state, 3));
                        }
                        LUAAA_CATCH(state, "property setter")
                    }
                    return 0;
                }
            };
            return _setterImpl(name, HelperClass::Invoke, f);
        }

        template<typename P, typename TRET>
        inline LuaClass<TCLASS>& set(const char* name, TRET(*f)(TCLASS&, P))
        {
            struct HelperClass {
                static inline int Invoke(lua_State* state) {
                    typedef decltype(f) FTYPE;
                    void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                    if (calleePtr)
                    {
                        LUAAA_TRY
                        {
                            (*(FTYPE*)(calleePtr))(LuaStack<TCLASS>::get(state, 1), LuaStack<P>::get(state, 3));
                        }
                        LUAAA_CATCH(state, "property setter")
                    }
                    return 0;
                }
            };
            return _setterImpl(name, HelperClass::Invoke, f);
        }


    public:
#if !LUAAA_WITHOUT_CPP_STDLIB
        // access prop from lambdas
        template<typename P>
        inline LuaClass<TCLASS>& get(const char* name, std::function<P()> f)
        {
            struct HelperClass {
                static inline int Invoke(lua_State* state) {
                    typedef decltype(f) FTYPE;
                    void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                    if (calleePtr)
                    {
                        LUAAA_TRY
                        {
                            LuaStackReturn<P>(state, (*(FTYPE*)(calleePtr))());
                        }
                        LUAAA_CATCH(state, "property getter")
                    }
                    else
                    {
                        lua_pushnil(state);
                    }
                    return 1;
                }
            };
            return _getterImpl(name, HelperClass::Invoke, f);
        }

        template<typename P>
        inline LuaClass<TCLASS>& get(const char* name, std::function<P(const TCLASS&)> f)
        {
            struct HelperClass {

                static inline int Invoke(lua_State* state) {
                    typedef decltype(f) FTYPE;
                    void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                    if (calleePtr)
                    {
                        LUAAA_TRY
                        {
                            LuaStackReturn<P>(state, (*(FTYPE*)(calleePtr))(LuaStack<TCLASS>::get(state, 1)));
                        }
                        LUAAA_CATCH(state, "property getter")
                    }
                    else
                    {
                        lua_pushnil(state);
                    }
                    return 1;
                }
            };
            return _getterImpl(name, HelperClass::Invoke, f);
        }

        template<typename P>
        inline LuaClass<TCLASS>& get(const char* name, std::function<P(TCLASS&)> f)
        {
            struct HelperClass {
                static inline int Invoke(lua_State* state) {
                    typedef decltype(f) FTYPE;
                    void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                    if (calleePtr)
                    {
                        LUAAA_TRY
                        {
                            LuaStackReturn<P>(state, (*(FTYPE*)(calleePtr))(LuaStack<TCLASS>::get(state, 1)));
                        }
                        LUAAA_CATCH(state, "property getter")
                    }
                    else
                    {
                        lua_pushnil(state);
                    }
                    return 1;
                }
            };
            return _getterImpl(name, HelperClass::Invoke, f);
        }

        template<typename F>
        inline LuaClass<TCLASS>& get(const char* name, F f)
        {
            return get(name, to_class_getter_function<TCLASS>(f));
        }

        template<typename P, typename TRET>
        inline LuaClass<TCLASS>& set(const char* name, std::function<TRET(P)> f)
        {
            struct HelperClass {
                static inline int Invoke(lua_State* state) {
                    typedef decltype(f) FTYPE;
                    void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                    if (calleePtr)
                    {
                        LUAAA_TRY
                        {
                            (*(FTYPE*)(calleePtr))(LuaStack<P>::get(state, 3));
                        }
                        LUAAA_CATCH(state, "property setter")
                    }
                    return 0;
                }
            };
            return _setterImpl(name, HelperClass::Invoke, f);
        }

        template<typename P, typename TRET>
        inline LuaClass<TCLASS>& set(const char* name, std::function<TRET(TCLASS&, P)> f)
        {
            struct HelperClass {
                static inline int Invoke(lua_State* state) {
                    typedef decltype(f) FTYPE;
                    void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                    if (calleePtr)
                    {
                        LUAAA_TRY
                        {
                            (*(FTYPE*)(calleePtr))(LuaStack<TCLASS>::get(state, 1), LuaStack<P>::get(state, 3));
                        }
                        LUAAA_CATCH(state, "property setter")
                    }
                    return 0;
                }
            };
            return _setterImpl(name, HelperClass::Invoke, f);
        }

        template<typename P, typename TRET>
        inline LuaClass<TCLASS>& set(const char* name, std::function<TRET(const TCLASS&, P)> f)
        {
            struct HelperClass {
                static inline int Invoke(lua_State* state) {
                    typedef decltype(f) FTYPE;
                    void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                    if (calleePtr)
                    {
                        LUAAA_TRY
                        {
                            (*(FTYPE*)(calleePtr))(LuaStack<TCLASS>::get(state, 1), LuaStack<P>::get(state, 3));
                        }
                        LUAAA_CATCH(state, "property setter")
                    }
                    return 0;
                }
            };
            return _setterImpl(name, HelperClass::Invoke, f);
        }

        //template<typename F>
        //inline LuaClass<TCLASS>& set(const char* name, std::function<F> f)
        //{
        //    //static_assert(false, "Error: invalid signature of setter function");
        //    assert(!"Error: invalid signature of setter function");
        //    return (*this);
        //}

        template<typename F>
        inline LuaClass<TCLASS>& set(const char* name, F f)
        {
            return set(name, to_class_setter_function<TCLASS>(f));
        }
#endif

    public:
#if !LUAAA_WITHOUT_CPP_STDLIB
        template <typename F>
        inline LuaClass<TCLASS>& fun(const std::string& name, F f)
        {
            return fun(name.c_str(), f);
        }

        template <typename V>
        inline LuaClass<TCLASS>& def(const std::string& name, const V& val)
        {
            return def(name.c_str(), val);
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

    private:
        lua_State * m_state;

    private:
        // A per-TCLASS unique id: its address is used as a registry key. The lua
        // name is stored per-state at registry[&s_typeKey], so the same C++ type can be
        // bound under different names in different (even coexisting) lua_States.
        static char s_typeKey;

        // name of this type in the given state, or nullptr if not registered there.
        static const char * klassName(lua_State * L) {
            lua_rawgetp(L, LUA_REGISTRYINDEX, &s_typeKey);
            const char * name = lua_tostring(L, -1);   // nil -> nullptr
            lua_pop(L, 1);
            return name;
        }

        static void setKlassName(lua_State * L, const char * name) {
            lua_pushstring(L, name);
            lua_rawsetp(L, LUA_REGISTRYINDEX, &s_typeKey);
        }
    };

    template <typename TCLASS> char LuaClass<TCLASS>::s_typeKey = 0;


    // -----------------------------------
    // export module
    // -----------------------------------
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
            def("__name", m_moduleName);
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
        // Push this state's module metatable (created on first use).
        // ONE metatable serves every module: the dispatchers keep no per-module
        // state -- property closures and `__name` live in the module table itself.
        // It is kept in the registry under a private address key, NOT under the
        // module's name: luaL_newmetatable(L, name) shares its namespace with the
        // class metatables, so a module named like a class (the natural way to
        // hang static helpers on the class table, `LuaModule(L, "Cat")`) used to
        // fetch the CLASS metatable and overwrite its __index/__newindex.
        static void _pushModuleMetaTable(lua_State * state) {
            struct HelperClass {
#if LUAAA_FEATURE_PROPERTY
                // `mod[key]` for a key that is absent from the module table:
                //   string key : registered getter -> user __index
                //                -> (setter only => write-only error) -> nil
                //   number key : user __index -> nil
                //   other keys : nil
                // The key is never converted (see LuaClass::f_internal_index).
                static int f_internal_index(lua_State* state)
                {
                    lua_settop(state, 2);                               // [mod, key]
                    const int keyType = lua_type(state, 2);
                    if (keyType != LUA_TSTRING && keyType != LUA_TNUMBER)
                    {
                        lua_pushnil(state);
                        return 1;
                    }
                    const char* key = nullptr;
                    char internal_name[256];
                    if (keyType == LUA_TSTRING)
                    {
                        // property getter/setter and functions are stored in the
                        // module table itself, so look them up there.
                        key = lua_tostring(state, 2);
                        MakeInternalPropName(state, internal_name, sizeof(internal_name), '<', key);
                        lua_pushstring(state, internal_name);
                        lua_rawget(state, 1);                           // [mod, key, getter]
                        if (lua_isfunction(state, -1))
                        {
                            lua_insert(state, 1);                       // [getter, mod, key]
                            lua_call(state, 2, 1);
                            return 1;
                        }
                        lua_pop(state, 1);
                    }

                    // user defined __index (function or table), given the ORIGINAL key
                    lua_pushstring(state, "__index");
                    lua_rawget(state, 1);                               // [mod, key, handler]
                    if (lua_isfunction(state, -1))
                    {
                        lua_pushvalue(state, 1);
                        lua_pushvalue(state, 2);
                        lua_call(state, 2, 1);
                        return 1;
                    }
                    if (lua_istable(state, -1))
                    {
                        lua_pushvalue(state, 2);
                        lua_rawget(state, -2);
                        return 1;
                    }
                    lua_pop(state, 1);                                  // [mod, key]

                    if (key)
                    {
                        // no custom __index: distinguish write-only from non-existing
                        MakeInternalPropName(state, internal_name, sizeof(internal_name), '>', key);
                        lua_pushstring(state, internal_name);
                        lua_rawget(state, 1);
                        if (lua_isfunction(state, -1))
                        {
                            lua_pushstring(state, "__name");
                            lua_rawget(state, 1);
                            luaL_error(state, "attempt to read Write-Only property '%s' of '%s'", key, luaL_optstring(state, -1, "?"));
                        }
                    }
                    lua_pushnil(state);                                 // non-existing: nil
                    return 1;
                }

                // `mod[key] = value` for a key that is absent from the module table:
                //   string key : registered setter -> (getter only => read-only error)
                //   then       : user __newindex (function or table)
                //                -> plain assignment, under the ORIGINAL key
                static int f_internal_newindex(lua_State* state)
                {
                    lua_settop(state, 3);                               // [mod, key, value]
                    if (lua_type(state, 2) == LUA_TSTRING)
                    {
                        const char* key = lua_tostring(state, 2);
                        char internal_name[256];
                        MakeInternalPropName(state, internal_name, sizeof(internal_name), '>', key);
                        lua_pushstring(state, internal_name);
                        lua_rawget(state, 1);                           // [mod, key, value, setter]
                        if (lua_isfunction(state, -1))
                        {
                            lua_insert(state, 1);                       // [setter, mod, key, value]
                            lua_call(state, 3, 0);
                            return 0;
                        }
                        lua_pop(state, 1);

                        MakeInternalPropName(state, internal_name, sizeof(internal_name), '<', key);
                        lua_pushstring(state, internal_name);
                        lua_rawget(state, 1);
                        if (lua_isfunction(state, -1))
                        {
                            lua_pushstring(state, "__name");
                            lua_rawget(state, 1);
                            luaL_error(state, "attempt to write Read-Only property '%s' of '%s'", key, luaL_optstring(state, -1, "?"));
                        }
                        lua_pop(state, 1);                              // [mod, key, value]
                    }

                    // no associated property: user defined __newindex, else the module itself
                    lua_pushstring(state, "__newindex");
                    lua_rawget(state, 1);                               // [mod, key, value, handler]
                    if (lua_isfunction(state, -1))
                    {
                        lua_insert(state, 1);                           // [handler, mod, key, value]
                        lua_call(state, 3, 0);
                        return 0;
                    }
                    if (lua_istable(state, -1))
                    {
                        lua_replace(state, 1);                          // [handler, key, value]
                        lua_rawset(state, 1);
                        return 0;
                    }
                    lua_pop(state, 1);                                  // [mod, key, value]
                    lua_rawset(state, 1);
                    return 0;
                }
#endif
            };

            static char s_moduleMetaKey = 0;
            lua_rawgetp(state, LUA_REGISTRYINDEX, &s_moduleMetaKey);   // (returns void on 5.2: test the value)
            if (lua_istable(state, -1))
            {
                return;                                             // [.., mt]
            }
            lua_pop(state, 1);
            lua_newtable(state);                                    // [.., mt]
            luaL_Reg objfunc[] = {
#if LUAAA_FEATURE_PROPERTY
                { "__index", HelperClass::f_internal_index },
                { "__newindex", HelperClass::f_internal_newindex },
#endif
                { nullptr, nullptr }
            };
            luaL_setfuncs(state, objfunc, 0);
            lua_pushvalue(state, -1);
            lua_rawsetp(state, LUA_REGISTRYINDEX, &s_moduleMetaKey);
        }

        // Attach our property-dispatch metatable to the module table (expected on
        // top of the stack) unless it already has one. This must run on EVERY
        // registration, not only when the global table is first created: the
        // default module "_G" (and any table the script pre-created, e.g.
        // `M = {}`) already exists, and without the metatable its `<name`/`>name`
        // property closures have no __index/__newindex to dispatch through --
        // reads silently yield nil and writes become plain assignments.
        // A table carrying a FOREIGN metatable (installed by the host app or
        // another library) is left untouched: we must not clobber someone else's
        // metatable; module properties simply stay unavailable there.
        void _ensureMetaTable(lua_State * state) {
            if (lua_getmetatable(state, -1)) {            // [mod, mt]: ours already, or foreign
                lua_pop(state, 1);
                return;
            }
            _pushModuleMetaTable(state);                  // [mod, ours]
            lua_setmetatable(state, -2);                  // [mod]
        }

        template<typename F>
        inline LuaModule& _registerModuleFunction(const char* name, lua_CFunction caller, F f) {
#if LUAAA_USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, m_moduleName);
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
            _ensureMetaTable(m_state);

            F* funPtr = (F*)lua_newuserdata(m_state, sizeof(F));
#   if LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(m_state, funPtr != nullptr, 1, "failed to allocate memory to store function of module");
#   else
            luaL_argcheck(m_state, funPtr != nullptr, 1, (std::string("failed to allocate memory to store function `") + name + "`").c_str());
#   endif
            if (!funPtr)
            {
                lua_pop(m_state, 2);
                return (*this);
            }

            new (funPtr) F(f);
            AttachHolderFinalizer<F>(m_state);   // run ~F() when the holder is collected

            lua_pushvalue(m_state, -1);
            lua_pushcclosure(m_state, caller, 1);
            lua_pushstring(m_state, name);
            lua_insert(m_state, -2);
            lua_rawset(m_state, -4);
            lua_pop(m_state, 1);
            lua_setglobal(m_state, m_moduleName);
#else
            luaL_Reg regtab[] = { { name, caller },{ nullptr, nullptr } };
            F* funPtr = (F*)lua_newuserdata(m_state, sizeof(F));
#   if LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(m_state, funPtr != nullptr, 1, "failed to allocate memory to store function of module");
#   else
            luaL_argcheck(m_state, funPtr != nullptr, 1, (std::string("failed to allocate memory to store function `") + name + "`").c_str());
#   endif
            if (funPtr)
            {
                new (funPtr) F(f);
                AttachHolderFinalizer<F>(m_state);   // run ~F() when the holder is collected
                luaL_openlib(m_state, m_moduleName, regtab, 1);
                // luaL_openlib leaves the module table on the stack (5.1/LuaJIT).
                _ensureMetaTable(m_state);   // module properties need our __index/__newindex
                lua_pop(m_state, 1);         // ...and the residue must not pile up.
            }
#endif
            return (*this);
        }

        template<typename F>
        inline LuaModule& _funImpl(const char * name, F f)
        {
            return _registerModuleFunction(name, NonMemberFunctionCaller(f), f);
        }

    public:
        template<typename FRET, typename ...FARGS>
        inline LuaModule& fun(const char * name, FRET(*f)(FARGS...))
        {
            return _funImpl(name, f);
        }

        template<typename ...FARGS>
        inline LuaModule& fun(const char * name, void(*f)(FARGS...))
        {
            return _funImpl(name, f);
        }

#if !LUAAA_WITHOUT_CPP_STDLIB
        template<typename TRET, typename ...ARGS>
        inline LuaModule& fun(const char * name, const std::function<TRET(ARGS...)>& f)
        {
            return _funImpl<std::function<TRET(ARGS...)>>(name, f);
        }

        template<typename ...ARGS>
        inline LuaModule& fun(const char * name, const std::function<void(ARGS...)>& f)
        {
            return _funImpl<std::function<void(ARGS...)>>(name, f);
        }

        template<typename F>
        inline LuaModule& fun(const char * name, F f)
        {
            return fun(name, to_function(f));
        }
#endif

        inline LuaModule& fun(const char * name, lua_CFunction f)
        {
            
#if LUAAA_USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, m_moduleName);
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
            _ensureMetaTable(m_state);
            lua_pushcclosure(m_state, f, 0);
            lua_pushstring(m_state, name);
            lua_insert(m_state, -2);
            lua_rawset(m_state, -3);
            lua_setglobal(m_state, m_moduleName);
#else
            luaL_Reg regtab[] = { { name, f },{ nullptr, nullptr } };
            luaL_openlib(m_state, m_moduleName, regtab, 0);
            _ensureMetaTable(m_state);   // luaL_openlib leaves the module table on the stack
            lua_pop(m_state, 1);
#endif
            return (*this);
        }

        template <typename V>
        inline LuaModule& def(const char * name, const V& val)
        {
#if LUAAA_USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, m_moduleName);
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
            _ensureMetaTable(m_state);
            LuaStack<V>::put(m_state, val);
            lua_pushstring(m_state, name);
            lua_insert(m_state, -2);
            lua_rawset(m_state, -3);
            lua_setglobal(m_state, m_moduleName);
#else
            luaL_Reg regtab = { nullptr, nullptr };
            luaL_openlib(m_state, m_moduleName, &regtab, 0);
            LuaStack<V>::put(m_state, val);
            lua_pushstring(m_state, name);
            lua_insert(m_state, -2);
            lua_rawset(m_state, -3);
            _ensureMetaTable(m_state);   // luaL_openlib leaves the module table on the stack
            lua_pop(m_state, 1);
#endif
            return (*this);
        }

        template <typename TCLASS>
        inline LuaModule& def(const char* name, const luaaa::LuaClass<TCLASS>&, const TCLASS* obj = nullptr, void(*deleter)(TCLASS*) = nullptr)
        {
#if LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(m_state, LuaClass<TCLASS>::klassName(m_state) != nullptr, 1, "cpp class not export in this state");
#else
            luaL_argcheck(m_state, LuaClass<TCLASS>::klassName(m_state) != nullptr, 1, (std::string("cpp class `") + LUAAA_RTTI_CLASS_NAME(TCLASS) + "` not export in this state").c_str());
#endif
            typename LuaClass<TCLASS>::UserDataDetail userData{};
            if (obj)
            {
                userData.obj = const_cast<TCLASS*>(obj);
                if (deleter)
                {
                    struct HelperClass
                    {
                        static int f_dtor(typename LuaClass<TCLASS>::UserDataDetail* uData) {
                            typedef decltype(deleter) DELETERFTYPE;
                            if (uData && uData->obj && uData->free_func)
                            {
                                (reinterpret_cast<DELETERFTYPE>(uData->free_func))(uData->obj);
                                uData->obj = nullptr;
                            }
                            return 0;
                        }
                    };

                    userData.dtor = HelperClass::f_dtor;
                    userData.free_func = reinterpret_cast<void(*)()>(deleter);
                }
                else
                {
                    userData.dtor = nullptr;
                    userData.free_func = nullptr;
                }
            }
            else
            {
                struct HelperClass
                {
                    static int f_dtor(typename LuaClass<TCLASS>::UserDataDetail* uData)
                    {
                        if (uData && uData->obj)
                        {
                            delete uData->obj;
                            uData->obj = nullptr;
                        }
                        return 0;
                    }
                };
                userData.obj = DefaultConstructor<TCLASS>::New();
                if (!userData.obj)
                {
                    // New() only returns nullptr when TCLASS has no default
                    // constructor. Registering a null object would defer the
                    // failure to every single access ("invalid user data") --
                    // fail loudly at registration time instead.
#if LUAAA_WITHOUT_CPP_STDLIB
                    luaL_error(m_state, "luaaa: def singleton requires a default-constructible class or an explicit object");
#else
                    luaL_error(m_state, "luaaa: cpp class `%s` is not default constructible; pass an explicit object to def()", LUAAA_RTTI_CLASS_NAME(TCLASS));
#endif
                }
                userData.dtor = HelperClass::f_dtor;
                userData.free_func = nullptr;
            }

#if LUAAA_USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, m_moduleName);
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
            _ensureMetaTable(m_state);
            auto uData = (typename LuaClass<TCLASS>::UserDataDetail*)lua_newuserdata(m_state, sizeof(typename LuaClass<TCLASS>::UserDataDetail));
#if LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(m_state, uData != nullptr, 1, "failed to allocate memory to store object");
#else
            luaL_argcheck(m_state, uData != nullptr, 1, (std::string("failed to allocate memory to store object `") + name + "`").c_str());
#endif
            if (uData)
            {
                uData->obj = userData.obj;
                uData->dtor = userData.dtor;
                uData->free_func = userData.free_func;
                luaL_setmetatable(m_state, (LuaClass<TCLASS>::klassName(m_state)));
                lua_pushstring(m_state, name);
                lua_insert(m_state, -2);
                lua_rawset(m_state, -3);
                lua_setglobal(m_state, m_moduleName);
            }
            else
            {
                lua_pop(m_state, 2);
            }
#else
            luaL_Reg regtab = { nullptr, nullptr };
            luaL_openlib(m_state, m_moduleName, &regtab, 0);
            auto uData = (typename LuaClass<TCLASS>::UserDataDetail*)lua_newuserdata(m_state, sizeof(typename LuaClass<TCLASS>::UserDataDetail));
            if (uData)
            {
                uData->obj = userData.obj;
                uData->dtor = userData.dtor;
                uData->free_func = userData.free_func;
                luaL_setmetatable(m_state, (LuaClass<TCLASS>::klassName(m_state)));
                lua_pushstring(m_state, name);
                lua_insert(m_state, -2);
                lua_rawset(m_state, -3);
            }
            else
            {
                lua_pop(m_state, 1);
            }
            _ensureMetaTable(m_state);   // luaL_openlib leaves the module table on the stack
            lua_pop(m_state, 1);
#endif
            return (*this);
        }

        template <typename V>
        inline LuaModule& def(const char * name, const V val[], size_t length)
        {
#if LUAAA_USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, m_moduleName);
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
            _ensureMetaTable(m_state);
            lua_newtable(m_state);
            for (size_t idx = 0; idx < length; ++idx)
            {
                LuaStack<V>::put(m_state, val[idx]);
                lua_rawseti(m_state, -2, idx + 1);
            }
            lua_pushstring(m_state, name);
            lua_insert(m_state, -2);
            lua_rawset(m_state, -3);
            lua_setglobal(m_state, m_moduleName);
#else
            luaL_Reg regtab = { nullptr, nullptr };
            luaL_openlib(m_state, m_moduleName, &regtab, 0);
            lua_newtable(m_state);
            for (size_t idx = 0; idx < length; ++idx)
            {
                LuaStack<V>::put(m_state, val[idx]);
                lua_rawseti(m_state, -2, idx + 1);
            }
            lua_pushstring(m_state, name);
            lua_insert(m_state, -2);
            lua_rawset(m_state, -3);
            _ensureMetaTable(m_state);   // luaL_openlib leaves the module table on the stack
            lua_pop(m_state, 1);
#endif
            return (*this);
        }

        // disable the cast from "const char [#]" to "char (*)[#]"
        inline LuaModule& def(const char * name, const char * str)
        {
#if LUAAA_USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, m_moduleName);
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
            _ensureMetaTable(m_state);
            LuaStack<decltype(str)>::put(m_state, str);
            lua_pushstring(m_state, name);
            lua_insert(m_state, -2);
            lua_rawset(m_state, -3);
            lua_setglobal(m_state, m_moduleName);
#else
            luaL_Reg regtab = { nullptr, nullptr };
            luaL_openlib(m_state, m_moduleName, &regtab, 0);
            LuaStack<decltype(str)>::put(m_state, str);
            lua_pushstring(m_state, name);
            lua_insert(m_state, -2);
            lua_rawset(m_state, -3);
            _ensureMetaTable(m_state);   // luaL_openlib leaves the module table on the stack
            lua_pop(m_state, 1);
#endif
            return (*this);
        }

#if !LUAAA_WITHOUT_CPP_STDLIB
        template<typename F>
        inline LuaModule& fun(const std::string& name, F f)
        {
            return fun(name.c_str(), f);
        }

        template <typename V>
        inline LuaModule& def(const std::string& name, const V& val)
        {
            return def(name.c_str(), val);
        }
#endif

    private:
        template <typename F>
        inline LuaModule& _getterImpl(const char* name, lua_CFunction invoker, F f)
        {
#if LUAAA_FEATURE_PROPERTY
            char internal_name[256];
            MakeInternalPropName(m_state, internal_name, sizeof(internal_name), '<', name);
            return _registerModuleFunction(internal_name, invoker, f);
#else
            return *this;
#endif
        }

        template<typename F>
        inline LuaModule& _setterImpl(const char* name, lua_CFunction invoker, F f)
        {
#if LUAAA_FEATURE_PROPERTY
            char internal_name[256];
            MakeInternalPropName(m_state, internal_name, sizeof(internal_name), '>', name);
            return _registerModuleFunction(internal_name, invoker, f);
#else
            return *this;
#endif
        }

    public:
        template<typename P>
        inline LuaModule& get(const char* name, P(*f)())
        {
            static_assert(!std::is_void<P>::value, "Error: getter function must return a value.");
            struct HelperClass {
                static inline int Invoke(lua_State* state) {
                    typedef decltype(f) FTYPE;
                    void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                    if (calleePtr)
                    {
                        LUAAA_TRY
                        {
                            LuaStackReturn<P>(state, (*(FTYPE*)(calleePtr))());
                        }
                        LUAAA_CATCH(state, "property getter")
                    }
                    else
                    {
                        lua_pushnil(state);
                    }
                    return 1;
                }
            };
            return _getterImpl(name, HelperClass::Invoke, f);
        }

        template<typename P, typename TRET>
        inline LuaModule& set(const char* name, TRET(*f)(P))
        {
            struct HelperClass {
                static inline int Invoke(lua_State* state) {
                    typedef decltype(f) FTYPE;
                    void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                    if (calleePtr)
                    {
                        LUAAA_TRY
                        {
                            (*(FTYPE*)(calleePtr))(LuaStack<P>::get(state, 3));
                        }
                        LUAAA_CATCH(state, "property setter")
                    }
                    return 0;
                }
            };
            return _setterImpl(name, HelperClass::Invoke, f);
        }

    public:
#if !LUAAA_WITHOUT_CPP_STDLIB
        // access prop from lambdas
        template<typename P>
        inline LuaModule& get(const char* name, std::function<P()> f)
        {
            static_assert(!std::is_void<P>::value, "Error: getter function must return a value.");
            struct HelperClass {
                static inline int Invoke(lua_State* state) {
                    typedef decltype(f) FTYPE;
                    void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                    if (calleePtr)
                    {
                        LUAAA_TRY
                        {
                            LuaStackReturn<P>(state, (*(FTYPE*)(calleePtr))());
                        }
                        LUAAA_CATCH(state, "property getter")
                    }
                    else
                    {
                        lua_pushnil(state);
                    }
                    return 1;
                }
            };
            return _getterImpl(name, HelperClass::Invoke, f);
        }

        // template<typename F>
        // inline LuaModule& get(const char* name, std::function<F> f)
        // {
        //     //static_assert(false, "Error: invalid signature of getter function");
        //     assert(!"Error: invalid signature of getter function");
        //     return (*this);
        // }

        template<typename F>
        inline LuaModule& get(const char* name, F f)
        {
            return get(name, to_module_getter_function(f));
        }

        template<typename P, typename TRET>
        inline LuaModule& set(const char* name, std::function<TRET(P)> f)
        {
            struct HelperClass {
                static inline int Invoke(lua_State* state) {
                    typedef decltype(f) FTYPE;
                    void* calleePtr = lua_touserdata(state, lua_upvalueindex(1));
                    if (calleePtr)
                    {
                        LUAAA_TRY
                        {
                            (*(FTYPE*)(calleePtr))(LuaStack<P>::get(state, 3));
                        }
                        LUAAA_CATCH(state, "property setter")
                    }
                    return 0;
                }
            };
            return _setterImpl(name, HelperClass::Invoke, f);
        }

        // template<typename F>
        // inline LuaModule& set(const char* name, std::function<F> f)
        // {
        //     //static_assert(std::false_type::value, "Error: invalid signature of setter function");
        //     //assert(!"Error: invalid signature of setter function");
        //     return (*this);
        // }

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

    // vector
    template<typename K, typename ...ARGS>
    struct LuaStack<std::vector<K, ARGS...>>
    {
        typedef std::vector<K, ARGS...> Container;
        inline static Container get(lua_State * L, int idx)
        {
            Container result;
            if (!(lua_istable(L, idx))) { luaaa_detail::conv_arg_error(L, idx, "required table not found on stack."); }
            if (lua_istable(L, idx))
            {
                result.reserve(LUAAA_rawlen(L, idx));   // a hint only: the loop below decides the length
                // read t[1], t[2], ... up to the first nil
                for (int i = 1; ; ++i)
                {
                    lua_rawgeti(L, idx, i);
                    if (lua_isnil(L, -1))
                    {
                        lua_pop(L, 1);
                        break;
                    }
                    result.push_back(LuaStack<typename Container::value_type>::get(L, lua_gettop(L)));
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

    // deque
    template<typename K, typename ...ARGS>
    struct LuaStack<std::deque<K, ARGS...>>
    {
        typedef std::deque<K, ARGS...> Container;
        inline static Container get(lua_State * L, int idx)
        {
            Container result;
            if (!(lua_istable(L, idx))) { luaaa_detail::conv_arg_error(L, idx, "required table not found on stack."); }
            if (lua_istable(L, idx))
            {
                // read t[1], t[2], ... up to the first nil (see std::vector)
                for (int i = 1; ; ++i)
                {
                    lua_rawgeti(L, idx, i);
                    if (lua_isnil(L, -1))
                    {
                        lua_pop(L, 1);
                        break;
                    }
                    result.push_back(LuaStack<typename Container::value_type>::get(L, lua_gettop(L)));
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

    // list
    template<typename K, typename ...ARGS>
    struct LuaStack<std::list<K, ARGS...>>
    {
        typedef std::list<K, ARGS...> Container;
        inline static Container get(lua_State * L, int idx)
        {
            Container result;
            if (!(lua_istable(L, idx))) { luaaa_detail::conv_arg_error(L, idx, "required table not found on stack."); }
            if (lua_istable(L, idx))
            {
                // read t[1], t[2], ... up to the first nil (see std::vector)
                for (int i = 1; ; ++i)
                {
                    lua_rawgeti(L, idx, i);
                    if (lua_isnil(L, -1))
                    {
                        lua_pop(L, 1);
                        break;
                    }
                    result.push_back(LuaStack<typename Container::value_type>::get(L, lua_gettop(L)));
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

    // forward_list
    template<typename K, typename ...ARGS>
    struct LuaStack<std::forward_list<K, ARGS...>>
    {
        typedef std::forward_list<K, ARGS...> Container;
        inline static Container get(lua_State * L, int idx)
        {
            Container result;
            if (!(lua_istable(L, idx))) { luaaa_detail::conv_arg_error(L, idx, "required table not found on stack."); }
            if (lua_istable(L, idx))
            {
                // forward_list has no push_back (it is a singly linked list).
                // insert_after advancing from before_begin() preserves the Lua
                // array order (1,2,3,...) -- push_front would reverse it.
                auto it = result.before_begin();
                // read t[1], t[2], ... up to the first nil (see std::vector)
                for (int i = 1; ; ++i)
                {
                    lua_rawgeti(L, idx, i);
                    if (lua_isnil(L, -1))
                    {
                        lua_pop(L, 1);
                        break;
                    }
                    it = result.insert_after(it, LuaStack<typename Container::value_type>::get(L, lua_gettop(L)));
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

    // set
    template<typename K, typename ...ARGS>
    struct LuaStack<std::set<K, ARGS...>>
    {
        typedef std::set<K, ARGS...> Container;
        inline static Container get(lua_State * L, int idx)
        {
            Container result;
            if (!(lua_istable(L, idx))) { luaaa_detail::conv_arg_error(L, idx, "required table not found on stack."); }
            if (lua_istable(L, idx))
            {
                // read t[1], t[2], ... up to the first nil (see std::vector)
                for (int i = 1; ; ++i)
                {
                    lua_rawgeti(L, idx, i);
                    if (lua_isnil(L, -1))
                    {
                        lua_pop(L, 1);
                        break;
                    }
                    result.insert(LuaStack<typename Container::value_type>::get(L, lua_gettop(L)));
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

    // multiset
    template<typename K, typename ...ARGS>
    struct LuaStack<std::multiset<K, ARGS...>>
    {
        typedef std::multiset<K, ARGS...> Container;
        inline static Container get(lua_State * L, int idx)
        {
            Container result;
            if (!(lua_istable(L, idx))) { luaaa_detail::conv_arg_error(L, idx, "required table not found on stack."); }
            if (lua_istable(L, idx))
            {
                // read t[1], t[2], ... up to the first nil (see std::vector)
                for (int i = 1; ; ++i)
                {
                    lua_rawgeti(L, idx, i);
                    if (lua_isnil(L, -1))
                    {
                        lua_pop(L, 1);
                        break;
                    }
                    result.insert(LuaStack<typename Container::value_type>::get(L, lua_gettop(L)));
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

    // unordered_set
    template<typename K, typename ...ARGS>
    struct LuaStack<std::unordered_set<K, ARGS...>>
    {
        typedef std::unordered_set<K, ARGS...> Container;
        inline static Container get(lua_State * L, int idx)
        {
            Container result;
            if (!(lua_istable(L, idx))) { luaaa_detail::conv_arg_error(L, idx, "required table not found on stack."); }
            if (lua_istable(L, idx))
            {
                // read t[1], t[2], ... up to the first nil (see std::vector)
                for (int i = 1; ; ++i)
                {
                    lua_rawgeti(L, idx, i);
                    if (lua_isnil(L, -1))
                    {
                        lua_pop(L, 1);
                        break;
                    }
                    result.insert(LuaStack<typename Container::value_type>::get(L, lua_gettop(L)));
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

    // unordered_multiset
    template<typename K, typename ...ARGS>
    struct LuaStack<std::unordered_multiset<K, ARGS...>>
    {
        typedef std::unordered_multiset<K, ARGS...> Container;
        inline static Container get(lua_State * L, int idx)
        {
            Container result;
            if (!(lua_istable(L, idx))) { luaaa_detail::conv_arg_error(L, idx, "required table not found on stack."); }
            if (lua_istable(L, idx))
            {
                // read t[1], t[2], ... up to the first nil (see std::vector)
                for (int i = 1; ; ++i)
                {
                    lua_rawgeti(L, idx, i);
                    if (lua_isnil(L, -1))
                    {
                        lua_pop(L, 1);
                        break;
                    }
                    result.insert(LuaStack<typename Container::value_type>::get(L, lua_gettop(L)));
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

    // map
    template<typename K, typename V, typename ...ARGS>
    struct LuaStack<std::map<K, V, ARGS...>>
    {
        typedef std::map<K, V, ARGS...> Container;
        inline static Container get(lua_State * L, int idx)
        {
            Container result;
            if (!(lua_istable(L, idx))) { luaaa_detail::conv_arg_error(L, idx, "required table not found on stack."); }
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    const int top = lua_gettop(L);
                    // Copy the key before converting it: LuaStack<std::string>::get
                    // calls lua_tostring, which may replace a NUMBER key in place and
                    // "confuse the next call to lua_next" (Lua manual). Convert the copy
                    // so the original key on the stack is left untouched.
                    lua_pushvalue(L, top - 1);
                    typename Container::key_type key = LuaStack<typename Container::key_type>::get(L, top + 1);
                    lua_pop(L, 1);
                    result[key] = LuaStack<typename Container::mapped_type>::get(L, top);
                    lua_pop(L, 1);
                }
            }
            return result;
        }
        inline static void put(lua_State * L, const Container& s)
        {
            lua_newtable(L);
            for (auto it = s.begin(); it != s.end(); ++it)
            {
                LuaStack<typename Container::key_type>::put(L, it->first);
                LuaStack<typename Container::mapped_type>::put(L, it->second);
                lua_rawset(L, -3);
            }
        }
    };

    // multimap
    template<typename K, typename V, typename ...ARGS>
    struct LuaStack<std::multimap<K, V, ARGS...>>
    {
        typedef std::multimap<K, V, ARGS...> Container;
        inline static Container get(lua_State * L, int idx)
        {
            Container result;
            if (!(lua_istable(L, idx))) { luaaa_detail::conv_arg_error(L, idx, "required table not found on stack."); }
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    const int top = lua_gettop(L);
                    // std::multimap has no operator[]; use emplace. Copy the key first
                    // (see std::map above) so stringifying a NUMBER key can't confuse
                    // the next lua_next.
                    lua_pushvalue(L, top - 1);
                    typename Container::key_type key = LuaStack<typename Container::key_type>::get(L, top + 1);
                    lua_pop(L, 1);
                    result.emplace(key, LuaStack<typename Container::mapped_type>::get(L, top));
                    lua_pop(L, 1);
                }
            }
            return result;
        }
        inline static void put(lua_State * L, const Container& s)
        {
            lua_newtable(L);
            for (auto it = s.begin(); it != s.end(); ++it)
            {
                LuaStack<typename Container::key_type>::put(L, it->first);
                LuaStack<typename Container::mapped_type>::put(L, it->second);
                lua_rawset(L, -3);
            }
        }
    };

    // unordered_map
    template<typename K, typename V, typename ...ARGS>
    struct LuaStack<std::unordered_map<K, V, ARGS...>>
    {
        typedef std::unordered_map<K, V, ARGS...> Container;
        inline static Container get(lua_State * L, int idx)
        {
            Container result;
            if (!(lua_istable(L, idx))) { luaaa_detail::conv_arg_error(L, idx, "required table not found on stack."); }
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    const int top = lua_gettop(L);
                    // Copy the key before converting it (see std::map above): a NUMBER
                    // key stringified in place would confuse the next lua_next.
                    lua_pushvalue(L, top - 1);
                    typename Container::key_type key = LuaStack<typename Container::key_type>::get(L, top + 1);
                    lua_pop(L, 1);
                    result[key] = LuaStack<typename Container::mapped_type>::get(L, top);
                    lua_pop(L, 1);
                }
            }
            return result;
        }
        inline static void put(lua_State * L, const Container& s)
        {
            lua_newtable(L);
            for (auto it = s.begin(); it != s.end(); ++it)
            {
                LuaStack<typename Container::key_type>::put(L, it->first);
                LuaStack<typename Container::mapped_type>::put(L, it->second);
                lua_rawset(L, -3);
            }
        }
    };

    // unordered_multimap
    template<typename K, typename V, typename ...ARGS>
    struct LuaStack<std::unordered_multimap<K, V, ARGS...>>
    {
        typedef std::unordered_multimap<K, V, ARGS...> Container;
        inline static Container get(lua_State * L, int idx)
        {
            Container result;
            if (!(lua_istable(L, idx))) { luaaa_detail::conv_arg_error(L, idx, "required table not found on stack."); }
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    const int top = lua_gettop(L);
                    // no operator[] on unordered_multimap; use emplace. Copy the key
                    // first (see std::map above) to keep lua_next intact.
                    lua_pushvalue(L, top - 1);
                    typename Container::key_type key = LuaStack<typename Container::key_type>::get(L, top + 1);
                    lua_pop(L, 1);
                    result.emplace(key, LuaStack<typename Container::mapped_type>::get(L, top));
                    lua_pop(L, 1);
                }
            }
            return result;
        }
        inline static void put(lua_State * L, const Container& s)
        {
            lua_newtable(L);
            for (auto it = s.begin(); it != s.end(); ++it)
            {
                LuaStack<typename Container::key_type>::put(L, it->first);
                LuaStack<typename Container::mapped_type>::put(L, it->second);
                lua_rawset(L, -3);
            }
        }
    };

    // std::pair
    template<typename U, typename V>
    struct LuaStack<std::pair<U, V>>
    {
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
#if __cplusplus >= 201402 || _MSVC_LANG >= 201402
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
#elif __cplusplus
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

#undef LUAAA_TRY
#undef LUAAA_CATCH
#undef LUAAA_BOUNDARY_TRY
#undef LUAAA_BOUNDARY_CATCH
#undef LUAAA_IMPLEMENT_CALLBACK_INVOKER
#undef LUAAA_IMPLEMENT_FUNCTION_CALLER
#undef LUAAA_REPORT_CTOR_NAME_CONFLICT
#undef LUAAA_RTTI_CLASS_NAME
#undef LUAAA_USE_NEW_MODULE_REGISTRY

#endif


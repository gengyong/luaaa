
/*
 Copyright (c) 2019 gengyong
 https://github.com/gengyong/luaaa
 licensed under MIT License.
*/

#ifndef HEADER_LUAAA_HPP
#define HEADER_LUAAA_HPP

#define LUAAA_NS luaaa

/// set LUAAA_WITHOUT_CPP_STDLIB to disable C++ std libs. 
//#define LUAAA_WITHOUT_CPP_STDLIB 1

extern "C"
{
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM <= 501
inline void luaL_setfuncs(lua_State *L, const luaL_Reg *l, int nup) {
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
#endif

#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM > 501 && !defined(LUA_COMPAT_MODULE)
#	define USE_NEW_MODULE_REGISTRY 1
#else
#	define USE_NEW_MODULE_REGISTRY 0
#endif

#include <typeinfo>
#include <utility>

#if defined(_MSC_VER)
#   define RTTI_CLASS_NAME(a) typeid(a).name() //vc always has this operator even if RTTI was disabled.
#elif __GXX_RTTI
#   define RTTI_CLASS_NAME(a) typeid(a).name()
#elif _HAS_STATIC_RTTI
#   define RTTI_CLASS_NAME(a) typeid(a).name()
#else
#   define RTTI_CLASS_NAME(a) "?"
#endif

#ifndef LUAAA_WITHOUT_CPP_STDLIB
#   include <cstring>
#   include <string>
#   include <functional>
#else
#   include <type_traits>
#   include <cstring>
#endif

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
        return snprintf(buffer, buflen, "%s<%s>(%p)", prefix, luaL_typename(L, idx), lua_topointer(L, idx));
    case LUA_TNUMBER:
    case LUA_TSTRING:
        return snprintf(buffer, buflen, "%s\"%s\"", prefix, lua_tostring(L, idx));
    case LUA_TBOOLEAN:
        return snprintf(buffer, buflen, "%s%s", prefix, (lua_toboolean(L, idx) ? "true" : "false"));
    case LUA_TNIL:
        return snprintf(buffer, buflen, "%s%s", prefix, "nil");
    case LUA_TTABLE:
        int ret = 0;
        if (luaL_getmetafield(L, idx, "__name") == LUA_TSTRING) {
            ret = snprintf(buffer, buflen, "%s<%s>(%p):", prefix, lua_tostring(L, -1), lua_topointer(L, idx));
        } else {
            ret = snprintf(buffer, buflen, "%s<%s>(%p):", prefix, luaL_typename(L, idx), lua_topointer(L, idx));
        }
        if (ret > 0) {
            size_t wrote = ret;
            lua_pushnil(L);
            while (lua_next(L, idx)) {
                const int top = lua_gettop(L);
                if (buflen > wrote) {
                    ret = snprintf(buffer + wrote, buflen - wrote, "\n");
                    if (ret > 0) {
                        wrote += ret;
                    }
                }

                if (buflen > wrote) {
                    ret = LUAAA_DUMP_OBJECT(buffer + wrote, buflen - wrote, L, top - 1, indent+1);
                    if (ret > 0) {
                        wrote += ret;
                    }
                }

                if (buflen > wrote) {
                    ret = snprintf(buffer + wrote, buflen - wrote, "\t");
                    if (ret > 0) {
                        wrote += ret;
                    }
                }

                if (buflen > wrote) {
                    if (lua_topointer(L, top) == lua_topointer(L, idx)) {
                        ret = snprintf(buffer + wrote, buflen - wrote, "%s<self>(%p)", prefix, lua_topointer(L, top));
                    } else {
                        ret = LUAAA_DUMP_OBJECT(buffer + wrote, buflen - wrote, L, top, 0);
                    }
                    if (ret > 0) {
                        wrote += ret;
                    }
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 0);
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
        LUAAA_DUMP_OBJECT(buffer, sizeof(buffer) - 1, L, i);
        printf("%d\t%s\n", i, buffer);
    }
    printf("<<<<<<<<<<<<<<<<<<<<<<<<<\n");
}

namespace LUAAA_NS
{
#ifndef LUAAA_WITHOUT_CPP_STDLIB
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
#endif

    //========================================================
    // Lua Class
    //========================================================

    template <typename> struct LuaClass;

    //========================================================
    // Lua stack operator
    //========================================================

    template <typename T> struct LuaStack
    {
        inline static T& get(lua_State * state, int idx)
        {
#ifndef LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(state, LuaClass<T>::klassName != nullptr, 1, (std::string("cpp class `") + RTTI_CLASS_NAME(T) + "` not export").c_str());
#else
            luaL_argcheck(state, LuaClass<T>::klassName != nullptr, 1, "cpp class not export");
#endif
            if (lua_istable(state, idx)) {
#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM <= 502
                if (lua_getmetatable(state, idx)) {
                    lua_getfield(state, LUA_REGISTRYINDEX, LuaClass<T>::klassName);
                    if (lua_rawequal(state, -1, -2)) {
                        lua_getfield(state, idx, "@");
                        if (lua_type(state, -1) == LUA_TUSERDATA) {
                            T ** t = (T**)luaL_checkudata(state, -1, LuaClass<T>::klassName);
                            luaL_argcheck(state, t != nullptr, 1, "invalid user data");
                            luaL_argcheck(state, *t != nullptr, 1, "invalid user data");
                            return (**t);
                        }
                    }
                }
#else
                if (luaL_getmetafield(state, idx, "__name") == LUA_TSTRING) {
                    if (strcmp(lua_tostring(state, -1), LuaClass<T>::klassName) == 0) {
                        if (lua_getfield(state, idx, "@") == LUA_TUSERDATA) {
                            T ** t = (T**)luaL_checkudata(state, -1, LuaClass<T>::klassName);
                            luaL_argcheck(state, t != nullptr, 1, "invalid user data");
                            luaL_argcheck(state, *t != nullptr, 1, "invalid user data");
                            return (**t);
                        }
                    }
                }
#endif
            }
            T ** t = (T**)luaL_checkudata(state, idx, LuaClass<T>::klassName);
            luaL_argcheck(state, t != nullptr, 1, "invalid user data");
            luaL_argcheck(state, *t != nullptr, 1, "invalid user data");
            return (**t);
        }

        inline static void put(lua_State * L, T * t)
        {
            lua_pushlightuserdata(L, t);
        }
    };

    template <typename T> struct LuaStack<const T> : public LuaStack<T> {};
    template <typename T> struct LuaStack<T&> : public LuaStack<T> {};
    template <typename T> struct LuaStack<const T&> : public LuaStack<T> {};

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
            if (lua_islightuserdata(state, idx))
            {
                T * t = (T*)lua_touserdata(state, idx);
                return t;
            }
            else if (lua_isuserdata(state, idx)) 
            {
                if (LuaClass<T*>::klassName != nullptr)
                {
                    T ** t = (T**)luaL_checkudata(state, idx, LuaClass<T*>::klassName);
                    luaL_argcheck(state, t != nullptr, 1, "invalid user data");
                    luaL_argcheck(state, *t != nullptr, 1, "invalid user data");
                    return *t;
                }
                if (LuaClass<T>::klassName != nullptr)
                {
                    T ** t = (T**)luaL_checkudata(state, idx, LuaClass<T>::klassName);
                    luaL_argcheck(state, t != nullptr, 1, "invalid user data");
                    luaL_argcheck(state, *t != nullptr, 1, "invalid user data");
                    return *t;
                }
            }
            return nullptr;
        }

        inline static void put(lua_State * L, T * t)
        {
            lua_pushlightuserdata(L, t);
        }
    };

    template<>
    struct LuaStack<float>
    {
        inline static float get(lua_State * L, int idx)
        {
            if (lua_isnumber(L, idx) || lua_isstring(L, idx))
            {
                return float(lua_tonumber(L, idx));
            }
            else
            {
                luaL_checktype(L, idx, LUA_TNUMBER);
            }
            return 0;
        }

        inline static void put(lua_State * L, const float & t)
        {
            lua_pushnumber(L, t);
        }
    };

    template<>
    struct LuaStack<double>
    {
        inline static double get(lua_State * L, int idx)
        {
            if (lua_isnumber(L, idx) || lua_isstring(L, idx))
            {
                return double(lua_tonumber(L, idx));
            }
            else
            {
                luaL_checktype(L, idx, LUA_TNUMBER);
            }
            return 0;
        }

        inline static void put(lua_State * L, const double & t)
        {
            lua_pushnumber(L, t);
        }
    };

    template<>
    struct LuaStack<int>
    {
        inline static int get(lua_State * L, int idx)
        {
            if (lua_isnumber(L, idx) || lua_isstring(L, idx))
            {
                return int(lua_tointeger(L, idx));
            }
            else
            {
                luaL_checktype(L, idx, LUA_TNUMBER);
            }
            return 0;
        }

        inline static void put(lua_State * L, const int & t)
        {
            lua_pushinteger(L, t);
        }
    };

    template<>
    struct LuaStack<bool>
    {
        inline static bool get(lua_State * L, int idx)
        {
            luaL_checktype(L, idx, LUA_TBOOLEAN);
            return lua_toboolean(L, idx) != 0;
        }

        inline static void put(lua_State * L, const bool & t)
        {
            lua_pushboolean(L, t);
        }
    };

    template<>
    struct LuaStack<const char *>
    {
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
                luaL_checktype(L, idx, LUA_TSTRING);
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

#ifndef LUAAA_WITHOUT_CPP_STDLIB
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

        inline static void put(lua_State *, lua_State *)
        {
        }
    };

    // push ret data to stack
    template <typename T>
    inline void LuaStackReturn(lua_State * L, T t)
    {
        lua_settop(L, 0);
        LuaStack<T>::put(L, t);
    }


#define IMPLEMENT_CALLBACK_INVOKER(CALLCONV) \
    template<typename RET, typename ...ARGS> \
    struct LuaStack<RET(CALLCONV*)(ARGS...)> \
    { \
        typedef RET(CALLCONV*FTYPE)(ARGS...); \
        inline static FTYPE get(lua_State * L, int idx) \
        { \
            static lua_State * cacheLuaState = nullptr; \
            static int cacheLuaFuncId = 0; \
            struct HelperClass \
            { \
                static RET CALLCONV f_callback(ARGS... args) \
                { \
                    lua_rawgeti(cacheLuaState, LUA_REGISTRYINDEX, cacheLuaFuncId); \
                    if (lua_isfunction(cacheLuaState, -1)) \
                    { \
                        int initParams[] = { (LuaStack<ARGS>::put(cacheLuaState, args), 0)..., 0 }; \
                        if (lua_pcall(cacheLuaState, sizeof...(ARGS), 1, 0) != 0) \
                        { \
                            lua_error(cacheLuaState); \
                        } \
                        luaL_unref(cacheLuaState, LUA_REGISTRYINDEX, cacheLuaFuncId); \
                    } \
                    else \
                    { \
                        lua_pushnil(cacheLuaState); \
                    } \
                    return LuaStack<RET>::get(cacheLuaState, lua_gettop(cacheLuaState)); \
                } \
            }; \
            if (lua_isfunction(L, idx)) \
            { \
                cacheLuaState = L; \
                lua_pushvalue(L, idx); \
                cacheLuaFuncId = luaL_ref(L, LUA_REGISTRYINDEX); \
                return HelperClass::f_callback; \
            } \
            return nullptr; \
        } \
        inline void put(lua_State * L, FTYPE f) \
        { \
            lua_pushcfunction(L, NonMemberFunctionCaller(f)); \
        } \
    }; \
    template<typename ...ARGS> \
    struct LuaStack<void(CALLCONV*)(ARGS...)> \
    { \
        typedef void(CALLCONV*FTYPE)(ARGS...); \
        inline static FTYPE get(lua_State * L, int idx) \
        { \
            static lua_State * cacheLuaState = nullptr; \
            static int cacheLuaFuncId = 0; \
            struct HelperClass \
            { \
                static void CALLCONV f_callback(ARGS... args) \
                { \
                    lua_rawgeti(cacheLuaState, LUA_REGISTRYINDEX, cacheLuaFuncId); \
                    if (lua_isfunction(cacheLuaState, -1)) \
                    { \
                        int initParams[] = { (LuaStack<ARGS>::put(cacheLuaState, args), 0)..., 0 }; \
                        if (lua_pcall(cacheLuaState, sizeof...(ARGS), 1, 0) != 0) \
                        { \
                            lua_error(cacheLuaState); \
                        } \
                        luaL_unref(cacheLuaState, LUA_REGISTRYINDEX, cacheLuaFuncId); \
                    } \
                    return; \
                } \
            }; \
            if (lua_isfunction(L, idx)) \
            { \
                cacheLuaState = L; \
                lua_pushvalue(L, idx); \
                cacheLuaFuncId = luaL_ref(L, LUA_REGISTRYINDEX); \
                return HelperClass::f_callback; \
            } \
            return nullptr; \
        } \
        inline void put(lua_State * L, FTYPE f) \
        { \
            lua_pushcfunction(L, NonMemberFunctionCaller(f)); \
        } \
    };

#ifndef LUAAA_WITHOUT_CPP_STDLIB
    template<typename RET, typename ...ARGS>
    struct LuaStack<std::function<RET(ARGS...)>>
    {
        typedef std::function<RET(ARGS...)> FTYPE;
        inline static FTYPE get(lua_State * L, int idx)
        {
            static lua_State * cacheLuaState = nullptr;
            static int cacheLuaFuncId = 0;
            struct HelperClass
            {
                static RET f_callback(ARGS... args)
                {
                    lua_rawgeti(cacheLuaState, LUA_REGISTRYINDEX, cacheLuaFuncId);
                    if (lua_isfunction(cacheLuaState, -1))
                    {
                        int initParams[] = { (LuaStack<ARGS>::put(cacheLuaState, args), 0)..., 0 };
                        if (lua_pcall(cacheLuaState, sizeof...(ARGS), 1, 0) != 0)
                        {
                            lua_error(cacheLuaState);
                        }
                        luaL_unref(cacheLuaState, LUA_REGISTRYINDEX, cacheLuaFuncId);
                    }
                    else
                    {
                        lua_pushnil(cacheLuaState);
                    }
                    return LuaStack<RET>::get(cacheLuaState, lua_gettop(cacheLuaState));
                }
            };
            if (lua_isfunction(L, idx))
            {
                cacheLuaState = L;
                lua_pushvalue(L, idx);
                cacheLuaFuncId = luaL_ref(L, LUA_REGISTRYINDEX);
                return HelperClass::f_callback;
            }
            return nullptr;
        }
        inline void put(lua_State * L, FTYPE f)
        {
            lua_pushcfunction(L, NonMemberFunctionCaller(f));
        }
    };
    
    template<typename ...ARGS>
    struct LuaStack<std::function<void(ARGS...)>>
    {
        typedef std::function<void(ARGS...)> FTYPE;
        inline static FTYPE get(lua_State * L, int idx)
        {
            static lua_State * cacheLuaState = nullptr;
            static int cacheLuaFuncId = 0;
            struct HelperClass
            {
                static void f_callback(ARGS... args)
                {
                    lua_rawgeti(cacheLuaState, LUA_REGISTRYINDEX, cacheLuaFuncId);
                    if (lua_isfunction(cacheLuaState, -1))
                    {
                        int initParams[] = { (LuaStack<ARGS>::put(cacheLuaState, args), 0)..., 0 };
                        if (lua_pcall(cacheLuaState, sizeof...(ARGS), 1, 0) != 0)
                        {
                            lua_error(cacheLuaState);
                        }
                        luaL_unref(cacheLuaState, LUA_REGISTRYINDEX, cacheLuaFuncId);
                    }
                    return;
                }
            };
            if (lua_isfunction(L, idx))
            {
                cacheLuaState = L;
                lua_pushvalue(L, idx);
                cacheLuaFuncId = luaL_ref(L, LUA_REGISTRYINDEX);
                return HelperClass::f_callback;
            }
            return nullptr;
        }
        inline void put(lua_State * L, FTYPE f)
        {
            lua_pushcfunction(L, NonMemberFunctionCaller(f));
        }
    };
#endif

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
    // non-member function caller & static member function caller
    //========================================================
    template<typename TRET, typename FTYPE, typename ...ARGS, std::size_t... Ns>
    TRET LuaInvokeImpl(lua_State* state, void* calleePtr, size_t skip, indices<Ns...>)
    {
        return (*(FTYPE*)(calleePtr))(LuaStack<ARGS>::get(state, Ns + 1 + skip)...);
    }

    template<typename TRET, typename FTYPE, typename ...ARGS>
    inline TRET LuaInvoke(lua_State* state, void* calleePtr, size_t skip)
    {
        return LuaInvokeImpl<TRET, FTYPE, ARGS...>(state, calleePtr, skip, typename make_indices<sizeof...(ARGS)>::type());
    }


#define IMPLEMENT_FUNCTION_CALLER(CALLERNAME, CALLCONV, SKIPPARAM) \
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
    IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, __cdecl, 0);
    IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, __cdecl, 1);
    IMPLEMENT_CALLBACK_INVOKER(__cdecl);

#	ifdef _M_CEE
    IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, __clrcall, 0);
    IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, __clrcall, 1);
    IMPLEMENT_CALLBACK_INVOKER(__clrcall);
#	endif

#	if defined(_M_IX86) && !defined(_M_CEE)
    IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, __fastcall, 0);
    IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, __fastcall, 1);
    IMPLEMENT_CALLBACK_INVOKER(__fastcall);
#	endif

#	ifdef _M_IX86
    IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, __stdcall, 0);
    IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, __stdcall, 1);
    IMPLEMENT_CALLBACK_INVOKER(__stdcall);
#	endif

#	if ((defined(_M_IX86) && _M_IX86_FP >= 2) || defined(_M_X64)) && !defined(_M_CEE)
    IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, __vectorcall, 0);
    IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, __vectorcall, 1);
    IMPLEMENT_CALLBACK_INVOKER(__vectorcall);
#	endif
#elif defined(__clang__)
#	define _NOTHING
    IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, _NOTHING, 0);
    IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, _NOTHING, 1);
    IMPLEMENT_CALLBACK_INVOKER(_NOTHING);
#	undef _NOTHING
#elif defined(__GNUC__)
#	define _NOTHING
    IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, _NOTHING, 0);
    IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, _NOTHING, 1);
    IMPLEMENT_CALLBACK_INVOKER(_NOTHING);
#	undef _NOTHING
#else
#	define _NOTHING
    IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, _NOTHING, 0);
    IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, _NOTHING, 1);
    IMPLEMENT_CALLBACK_INVOKER(_NOTHING);
#	undef _NOTHING
#endif	

    //========================================================
    // member function invoker
    //========================================================
    template<typename TCLASS, typename TRET, typename FTYPE, typename ...ARGS, std::size_t... Ns>
    TRET LuaInvokeInstanceMemberImpl(lua_State* state, void* calleePtr, indices<Ns...>)
    {
        return (LuaStack<TCLASS>::get(state, 1).**(FTYPE*)(calleePtr))(LuaStack<ARGS>::get(state, Ns + 2)...);
    }

    template<typename TCLASS, typename TRET, typename FTYPE, typename ...ARGS>
    inline TRET LuaInvokeInstanceMember(lua_State* state, void* calleePtr)
    {
        return LuaInvokeInstanceMemberImpl<TCLASS, TRET, FTYPE, ARGS...>(state, calleePtr, typename make_indices<sizeof...(ARGS)>::type());
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

#ifndef LUAAA_WITHOUT_CPP_STDLIB
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
            return InvokeImpl(state, mem, typename make_indices<sizeof...(ARGS)>::type(), skip);
        }

    private:
        template<std::size_t ...Ns>
        static TCLASS * InvokeImpl(lua_State * state, void * mem, indices<Ns...>, size_t skip)
        {
            return new(mem) TCLASS(LuaStack<ARGS>::get(state, Ns + 1 + skip)...);
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
        }
    };

    //========================================================
    // export class
    //========================================================
    template <typename TCLASS>
    struct LuaClass
    {
         friend struct DestructorCaller<TCLASS>;
         template<typename> friend struct LuaStack;
    public:
        LuaClass(lua_State * state, const char * name, const luaL_Reg * functions = nullptr)
            : m_state(state)
        {
            assert(state != nullptr);
            assert(klassName == nullptr);

#ifndef LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(state, (klassName == nullptr), 1, (std::string("C++ class `") + RTTI_CLASS_NAME(TCLASS) + "` bind to conflict lua name `" + name + "`, origin name: " + klassName).c_str());
#else
            luaL_argcheck(state, (klassName == nullptr), 1, "C++ class bind to conflict lua class name");
#endif

            struct HelperClass {
                static int f__clsgc(lua_State*) {
                    LuaClass<TCLASS>::klassName = nullptr;
                    return 0;
                }
            };

            size_t strBufLen = strlen(name) + 1;
            klassName = reinterpret_cast<char *>(lua_newuserdata(state, strBufLen + 1));
            memcpy(klassName, name, strBufLen);

            klassName[strBufLen - 1] = '$';
            klassName[strBufLen] = 0;
            luaL_newmetatable(state, klassName);
            luaL_Reg destructor[] = { { "__gc", HelperClass::f__clsgc },{ nullptr, nullptr } };
            luaL_setfuncs(state, destructor, 0);
            lua_setmetatable(state, -2);
           
            klassName[strBufLen - 1] = 0;
            luaL_newmetatable(state, klassName);
            lua_pushvalue(state, -1);
            lua_setfield(state, -2, "__index");

            lua_pushvalue(state, -2);
            lua_setfield(state, -2, "$");

            if (functions)
            {
                luaL_setfuncs(state, functions, 0);
            }

            lua_pop(state, 2);
        }

#ifndef LUAAA_WITHOUT_CPP_STDLIB
        LuaClass(lua_State * state, const std::string& name, const luaL_Reg * functions = nullptr)
            : LuaClass(state, name.c_str(), functions)
        {}
#endif

        template<typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const char * name = "new")
        {
            struct HelperClass {
                static int f_gc(lua_State* state) {
                    if (lua_isuserdata(state, -1)) {
                        TCLASS ** objPtr = (TCLASS**)luaL_checkudata(state, -1, LuaClass<TCLASS>::klassName);
                        if (objPtr && *objPtr)
                        {
                            (*objPtr)->~TCLASS();
                        }
                    }
                    return 0;
                }

                static int f_new(lua_State* state) {
                    TCLASS ** objPtr = (TCLASS **)lua_newuserdata(state, sizeof(TCLASS*) + sizeof(TCLASS));
                    if (objPtr)
                    {
                        TCLASS * obj = PlacementConstructorCaller<TCLASS, ARGS...>::Invoke(state, (void*)(objPtr + 1), 0);
                        *objPtr = obj;
                        luaL_Reg destructor[] = { { "__gc", HelperClass::f_gc },{ nullptr, nullptr } };
                        luaL_getmetatable(state, LuaClass<TCLASS>::klassName);
                        luaL_setfuncs(state, destructor, 0);
                        lua_setmetatable(state, -2);
                        return 1;
                    }
                    lua_pushnil(state);
                    return 1;
                }
            };

            luaL_Reg constructor[] = { { name, HelperClass::f_new },{ nullptr, nullptr } };
#if USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, klassName);
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
            luaL_setfuncs(m_state, constructor, 0);
            lua_setglobal(m_state, klassName);
#else
            luaL_openlib(m_state, klassName, constructor, 0);
#endif

            return (*this);
        }

        template<typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const char * name, TCLASS*(*spawner)(ARGS...)) {
            typedef decltype(spawner) SPAWNERFTYPE;
            struct HelperClass {
                static int f_gc(lua_State* state) {
                    if (lua_isuserdata(state, -1)) {
                        TCLASS ** objPtr = (TCLASS**)luaL_checkudata(state, -1, LuaClass<TCLASS>::klassName);
                        if (objPtr)
                        {
                            DestructorCaller<TCLASS>::Invoke(*objPtr);
                        }
                    }
                    return 0;
                }

                static int f_new(lua_State* state) {
                    void * spawner = lua_touserdata(state, lua_upvalueindex(1));
                    luaL_argcheck(state, spawner, 1, "cpp closure spawner not found.");
                    if (spawner) {
                        auto obj = LuaInvoke<TCLASS*, SPAWNERFTYPE, ARGS...>(state, spawner, 0);
                        if (obj)
                        {
                            TCLASS ** objPtr = (TCLASS**)lua_newuserdata(state, sizeof(TCLASS*));
                            if (objPtr)
                            {
                                *objPtr = obj;
                                luaL_Reg destructor[] = { { "__gc", HelperClass::f_gc }, { nullptr, nullptr } };
                                luaL_getmetatable(state, LuaClass<TCLASS>::klassName);
                                luaL_setfuncs(state, destructor, 0);
                                lua_setmetatable(state, -2);
                                return 1;
                            }
                            else
                            {
                                DestructorCaller<TCLASS>::Invoke(obj);
                            }
                        }
                    }
                    lua_pushnil(state);
                    return 1;
                }

            };

            luaL_Reg constructor[] = { { name, HelperClass::f_new },{ nullptr, nullptr } };
#if USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, klassName);
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
#endif

            SPAWNERFTYPE * spawnerPtr = (SPAWNERFTYPE*)lua_newuserdata(m_state, sizeof(SPAWNERFTYPE));
#   ifndef LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(m_state, spawnerPtr != nullptr, 1, (std::string("faild to alloc mem to store spawner for ctor `") + name + "`").c_str());
#   else
            luaL_argcheck(m_state, spawnerPtr != nullptr, 1, "faild to alloc mem to store spawner for ctor");
#   endif
            memset(spawnerPtr, 0, sizeof(SPAWNERFTYPE));
            *spawnerPtr = spawner;

#if USE_NEW_MODULE_REGISTRY
            luaL_setfuncs(m_state, constructor, 1);
            lua_setglobal(m_state, klassName);
#else
            luaL_openlib(m_state, klassName, constructor, 1);
#endif

            return (*this);
        }

        template<typename TRET, typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const char * name, TCLASS*(*spawner)(ARGS...), TRET(*deleter)(TCLASS*)){
            typedef decltype(spawner) SPAWNERFTYPE;
            typedef decltype(deleter) DELETERFTYPE;

            struct HelperClass {
                static int f_gc(lua_State* state) {
                    void * deleter = lua_touserdata(state, lua_upvalueindex(1));
                    luaL_argcheck(state, deleter, 1, "cpp closure deleter not found.");
                    if (deleter) {
                        if (lua_isuserdata(state, -1)) {
                            TCLASS ** objPtr = (TCLASS**)luaL_checkudata(state, -1, LuaClass<TCLASS>::klassName);
                            if (objPtr)
                            {
                                (*(DELETERFTYPE*)(deleter))(*objPtr);
                            }
                        }
                    }
                    return 0;
                }

                static int f_new(lua_State* state) {
                    void * spawner = lua_touserdata(state, lua_upvalueindex(1));
                    luaL_argcheck(state, spawner, 1, "cpp closure spawner not found.");

                    void * deleter = lua_touserdata(state, lua_upvalueindex(2));
                    luaL_argcheck(state, deleter, 1, "cpp closure deleter not found.");

                    if (spawner) {
                        auto obj = LuaInvoke<TCLASS*, SPAWNERFTYPE, ARGS...>(state, spawner, 0);
                        if (obj)
                        {
                            TCLASS ** objPtr = (TCLASS**)lua_newuserdata(state, sizeof(TCLASS*));
                            if (objPtr)
                            {
                                *objPtr = obj;

                                luaL_Reg destructor[] = { { "__gc", HelperClass::f_gc }, { nullptr, nullptr } };

                                luaL_getmetatable(state, LuaClass<TCLASS>::klassName);

                                DELETERFTYPE * deleterPtr = (DELETERFTYPE*)lua_newuserdata(state, sizeof(DELETERFTYPE));
#   ifndef LUAAA_WITHOUT_CPP_STDLIB
                                luaL_argcheck(state, deleterPtr != nullptr, 1, (std::string("faild to alloc mem to store deleter for ctor meta table `") + LuaClass<TCLASS>::klassName + "`").c_str());
#   else
                                luaL_argcheck(state, deleterPtr != nullptr, 1, "faild to alloc mem to store deleter for ctor meta table");
#   endif
                                memset(deleterPtr, 0, sizeof(DELETERFTYPE));
                                *deleterPtr = *(DELETERFTYPE*)(deleter);
                                luaL_setfuncs(state, destructor, 1);
                                lua_setmetatable(state, -2);
                                return 1;
                            }
                            else
                            {
                                if (deleter) {
                                    (*(DELETERFTYPE*)(deleter))(obj);
                                }
                            }
                        }
                    }
                    lua_pushnil(state);
                    return 1;
                }

            };

            luaL_Reg constructor[] = { { name, HelperClass::f_new },{ nullptr, nullptr } };

#if USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, klassName);
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
#endif

            SPAWNERFTYPE * spawnerPtr = (SPAWNERFTYPE*)lua_newuserdata(m_state, sizeof(SPAWNERFTYPE));
#   ifndef LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(m_state, spawnerPtr != nullptr, 1, (std::string("faild to alloc mem to store spawner for ctor `") + name + "`").c_str());
#   else
            luaL_argcheck(m_state, spawnerPtr != nullptr, 1, ("faild to alloc mem to store spawner for ctor"));
#   endif
            memset(spawnerPtr, 0, sizeof(SPAWNERFTYPE));
            *spawnerPtr = spawner;

            DELETERFTYPE * deleterPtr = (DELETERFTYPE*)lua_newuserdata(m_state, sizeof(DELETERFTYPE));
#   ifndef LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(m_state, deleterPtr != nullptr, 1, (std::string("faild to alloc mem to store deleter for ctor `") + name + "`").c_str());
#   else
            luaL_argcheck(m_state, spawnerPtr != nullptr, 1, ("faild to alloc mem to store deleter for ctor"));
#   endif
            memset(deleterPtr, 0, sizeof(DELETERFTYPE));
            *deleterPtr = deleter;

#if USE_NEW_MODULE_REGISTRY
            luaL_setfuncs(m_state, constructor, 2);
            lua_setglobal(m_state, klassName);
#else
            luaL_openlib(m_state, klassName, constructor, 2);
#endif

            return (*this);
        }

        template<typename ...ARGS>
        inline LuaClass<TCLASS>& ctor(const char * name, TCLASS*(*spawner)(ARGS...), std::nullptr_t) {
            typedef decltype(spawner) SPAWNERFTYPE;

            struct HelperClass {
                static int f_nogc(lua_State* state) {
                    return 0;
                }

                static int f_new(lua_State* state) {
                    void * spawner = lua_touserdata(state, lua_upvalueindex(1));
                    luaL_argcheck(state, spawner, 1, "cpp closure spawner not found.");
                    if (spawner) {
                        auto obj = LuaInvoke<TCLASS*, SPAWNERFTYPE, ARGS...>(state, spawner, 0);
                        if (obj)
                        {
                            TCLASS ** objPtr = (TCLASS**)lua_newuserdata(state, sizeof(TCLASS*));
                            if (objPtr)
                            {
                                *objPtr = obj;

                                luaL_Reg destructor[] = {
                                    { "__gc", HelperClass::f_nogc },
                                    { nullptr, nullptr }
                                };

                                luaL_getmetatable(state, LuaClass<TCLASS>::klassName);
                                luaL_setfuncs(state, destructor, 0);
                                lua_setmetatable(state, -2);
                                return 1;
                            }
                        }
                    }
                    lua_pushnil(state);
                    return 1;
                }

            };

            luaL_Reg constructor[] = { { name, HelperClass::f_new },{ nullptr, nullptr } };

#if USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, klassName);
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
#endif
            SPAWNERFTYPE * spawnerPtr = (SPAWNERFTYPE*)lua_newuserdata(m_state, sizeof(SPAWNERFTYPE));
#ifndef LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(m_state, spawnerPtr != nullptr, 1, (std::string("faild to alloc mem to store spawner for ctor `") + name + "`").c_str());
#else
            luaL_argcheck(m_state, spawnerPtr != nullptr, 1, "faild to alloc mem to store spawner for ctor of cpp class");
#endif
            memset(spawnerPtr, 0, sizeof(SPAWNERFTYPE));
            *spawnerPtr = spawner;

#if USE_NEW_MODULE_REGISTRY
            luaL_setfuncs(m_state, constructor, 1);
            lua_setglobal(m_state, klassName);
#else
            luaL_openlib(m_state, klassName, constructor, 1);
#endif

            return (*this);
        }

       
#ifndef LUAAA_WITHOUT_CPP_STDLIB
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
        inline LuaClass<TCLASS>& _funImpl(const char * name, F f)
        {
            luaL_getmetatable(m_state, klassName);
            lua_pushstring(m_state, name);

            F * funPtr = (F*)lua_newuserdata(m_state, sizeof(F));
#ifndef LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(m_state, funPtr != nullptr, 1, (std::string("faild to alloc mem to store function `") + name + "`").c_str());
#else
            luaL_argcheck(m_state, funPtr != nullptr, 1, "faild to alloc mem to store function");
#endif
            memset(funPtr, 0, sizeof(F));
            *funPtr = f;
            lua_pushcclosure(m_state, MemberFunctionCaller(f), 1);
            lua_settable(m_state, -3);
            lua_pop(m_state, 1);
            return (*this);
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

#ifndef LUAAA_WITHOUT_CPP_STDLIB
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
            luaL_getmetatable(m_state, klassName);
            lua_pushstring(m_state, name);
            lua_pushcclosure(m_state, f, 0);
            lua_settable(m_state, -3);
            lua_pop(m_state, 1);
            return (*this);
        }

        template <typename V>
        inline LuaClass<TCLASS>& def(const char * name, const V& val)
        {
            luaL_getmetatable(m_state, klassName);
            lua_pushstring(m_state, name);
            LuaStack<V>::put(m_state, val);
            lua_settable(m_state, -3);
            lua_pop(m_state, 1);
            return (*this);
        }

        // disable cast from "const char [#]" to "char (*)[#]"
        inline LuaClass<TCLASS>& def(const char * name, const char * str)
        {
            luaL_getmetatable(m_state, klassName);
            lua_pushstring(m_state, name);
            LuaStack<decltype(str)>::put(m_state, str);
            lua_settable(m_state, -3);
            lua_pop(m_state, 1);
            return (*this);
        }

#ifndef LUAAA_WITHOUT_CPP_STDLIB
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
#endif

    private:
        lua_State * m_state;

    private:
        static char * klassName;
    };

    template <typename TCLASS> char * LuaClass<TCLASS>::klassName = nullptr;


    // -----------------------------------
    // export module
    // -----------------------------------
    struct LuaModule
    {
    public:
        LuaModule(lua_State * state, const char * name = "_G")
            : m_state(state)
        {
            size_t strBufLen = strlen(name) + 1;
            m_moduleName = reinterpret_cast<char *>(lua_newuserdata(state, strBufLen));
            memcpy(m_moduleName, name, strBufLen);
        }

#ifndef LUAAA_WITHOUT_CPP_STDLIB
        LuaModule(lua_State * state, const std::string& name)
            : LuaModule(state, name.empty() ? "_G" : name.c_str())
        {}
#endif

    private:
        template<typename F>
        inline LuaModule& _funImpl(const char * name, F f)
        {
            luaL_Reg regtab[] = { { name, NonMemberFunctionCaller(f) },{ nullptr, nullptr } };

#if USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, m_moduleName);
            if (lua_isnil(m_state, -1)) 
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }

            F * funPtr = (F*)lua_newuserdata(m_state, sizeof(F));
#   ifndef LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(m_state, funPtr != nullptr, 1, (std::string("faild to alloc mem to store function `") + name + "`").c_str());
#   else
            luaL_argcheck(m_state, funPtr != nullptr, 1, "faild to alloc mem to store function of module");
#   endif
            memset(funPtr, 0, sizeof(F));
            *funPtr = f;

            luaL_setfuncs(m_state, regtab, 1);
            lua_setglobal(m_state, m_moduleName);
#else
            F * funPtr = (F*)lua_newuserdata(m_state, sizeof(F));
#   ifndef LUAAA_WITHOUT_CPP_STDLIB
            luaL_argcheck(m_state, funPtr != nullptr, 1, (std::string("faild to alloc mem to store function `") + name + "`").c_str());
#   else
            luaL_argcheck(m_state, funPtr != nullptr, 1, "faild to alloc mem to store function of module");
#   endif
            memset(funPtr, 0, sizeof(F));
            *funPtr = f;

            luaL_openlib(m_state, m_moduleName, regtab, 1);
#endif

            return (*this);
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

#ifndef LUAAA_WITHOUT_CPP_STDLIB
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
            luaL_Reg regtab[] = { { name, f },{ nullptr, nullptr } };
#if USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, m_moduleName);
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
            luaL_setfuncs(m_state, regtab, 0);
            lua_setglobal(m_state, m_moduleName);
#else
            luaL_openlib(m_state, m_moduleName, regtab, 0);
#endif
            return (*this);
        }

        template <typename V>
        inline LuaModule& def(const char * name, const V& val)
        {
#if USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, m_moduleName);
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
            LuaStack<V>::put(m_state, val);
            lua_setfield(m_state, -2, name);
            lua_setglobal(m_state, m_moduleName);
#else
            luaL_Reg regtab = { nullptr, nullptr };
            luaL_openlib(m_state, m_moduleName, &regtab, 0);
            LuaStack<V>::put(m_state, val);
            lua_setfield(m_state, -2, name);
#endif
            return (*this);
        }

        template <typename V>
        inline LuaModule& def(const char * name, const V val[], size_t length)
        {
#if USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, m_moduleName);
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
            lua_newtable(m_state);
            for (size_t idx = 0; idx < length; ++idx)
            {
                LuaStack<V>::put(m_state, val[idx]);
                lua_rawseti(m_state, -2, idx + 1);
            }
            lua_setfield(m_state, -2, name);
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
            lua_setfield(m_state, -2, name);
#endif
            return (*this);
        }

        // disable the cast from "const char [#]" to "char (*)[#]"
        inline LuaModule& def(const char * name, const char * str)
        {
            
#if USE_NEW_MODULE_REGISTRY
            lua_getglobal(m_state, m_moduleName);
            if (lua_isnil(m_state, -1))
            {
                lua_pop(m_state, 1);
                lua_newtable(m_state);
            }
            LuaStack<decltype(str)>::put(m_state, str);
            lua_setfield(m_state, -2, name);
            lua_setglobal(m_state, m_moduleName);
#else
            luaL_Reg regtab = { nullptr, nullptr };
            luaL_openlib(m_state, m_moduleName, &regtab, 0);
            LuaStack<decltype(str)>::put(m_state, str);
            lua_setfield(m_state, -2, name);
#endif
            return (*this);
        }

#ifndef LUAAA_WITHOUT_CPP_STDLIB
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
        lua_State * m_state;
        char * m_moduleName;
    };

}



#ifndef LUAAA_WITHOUT_CPP_STDLIB

#include <array>
#include <vector>
#include <deque>
#include <list>
#include <forward_list>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>

namespace LUAAA_NS
{
    // array
    template<typename K, size_t N>
    struct LuaStack<std::array<K, N>>
    {
        typedef std::array<K, N> Container;
        inline static Container get(lua_State * L, int idx)
        {
            Container result;

            luaL_argcheck(L, lua_istable(L, idx), 1, "required table not found on stack.");
            if (lua_istable(L, idx))
            {
                int index = 0;
                lua_pushnil(L);
                while (0 != lua_next(L, idx) && index < N)
                {
                    result[index++] = LuaStack<typename Container::value_type>::get(L, lua_gettop(L));
                    lua_pop(L, 1);
                }
                lua_pop(L, 0);
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

    // vector
    template<typename K, typename ...ARGS>
    struct LuaStack<std::vector<K, ARGS...>>
    {
        typedef std::vector<K, ARGS...> Container;
        inline static Container get(lua_State * L, int idx)
        {
            Container result;
            luaL_argcheck(L, lua_istable(L, idx), 1, "required table not found on stack.");
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    result.push_back(LuaStack<typename Container::value_type>::get(L, lua_gettop(L)));
                    lua_pop(L, 1);
                }
                lua_pop(L, 0);
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
            luaL_argcheck(L, lua_istable(L, idx), 1, "required table not found on stack.");
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    result.push_back(LuaStack<typename Container::value_type>::get(L, lua_gettop(L)));
                    lua_pop(L, 1);
                }
                lua_pop(L, 0);
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
            luaL_argcheck(L, lua_istable(L, idx), 1, "required table not found on stack.");
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    result.push_back(LuaStack<typename Container::value_type>::get(L, lua_gettop(L)));
                    lua_pop(L, 1);
                }
                lua_pop(L, 0);
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
            luaL_argcheck(L, lua_istable(L, idx), 1, "required table not found on stack.");
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    result.push_back(LuaStack<typename Container::value_type>::get(L, lua_gettop(L)));
                    lua_pop(L, 1);
                }
                lua_pop(L, 0);
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
            luaL_argcheck(L, lua_istable(L, idx), 1, "required table not found on stack.");
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    result.insert(LuaStack<typename Container::value_type>::get(L, lua_gettop(L)));
                    lua_pop(L, 1);
                }
                lua_pop(L, 0);
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
            luaL_argcheck(L, lua_istable(L, idx), 1, "required table not found on stack.");
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    result.insert(LuaStack<typename Container::value_type>::get(L, lua_gettop(L)));
                    lua_pop(L, 1);
                }
                lua_pop(L, 0);
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
            luaL_argcheck(L, lua_istable(L, idx), 1, "required table not found on stack.");
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    result.insert(LuaStack<typename Container::value_type>::get(L, lua_gettop(L)));
                    lua_pop(L, 1);
                }
                lua_pop(L, 0);
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
            luaL_argcheck(L, lua_istable(L, idx), 1, "required table not found on stack.");
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    result.insert(LuaStack<typename Container::value_type>::get(L, lua_gettop(L)));
                    lua_pop(L, 1);
                }
                lua_pop(L, 0);
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
            luaL_argcheck(L, lua_istable(L, idx), 1, "required table not found on stack.");
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    const int top = lua_gettop(L);
                    result[LuaStack<typename Container::key_type>::get(L, top - 1)] = LuaStack<typename Container::mapped_type>::get(L, top);
                    lua_pop(L, 1);
                }
                lua_pop(L, 0);
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
            luaL_argcheck(L, lua_istable(L, idx), 1, "required table not found on stack.");
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    const int top = lua_gettop(L);
                    result[LuaStack<typename Container::key_type>::get(L, top - 1)] = LuaStack<typename Container::mapped_type>::get(L, top);
                    lua_pop(L, 1);
                }
                lua_pop(L, 0);
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
            luaL_argcheck(L, lua_istable(L, idx), 1, "required table not found on stack.");
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    const int top = lua_gettop(L);
                    result[LuaStack<typename Container::key_type>::get(L, top - 1)] = LuaStack<typename Container::mapped_type>::get(L, top);
                    lua_pop(L, 1);
                }
                lua_pop(L, 0);
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
            luaL_argcheck(L, lua_istable(L, idx), 1, "required table not found on stack.");
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    const int top = lua_gettop(L);
                    result[LuaStack<typename Container::key_type>::get(L, top - 1)] = LuaStack<typename Container::mapped_type>::get(L, top);
                    lua_pop(L, 1);
                }
                lua_pop(L, 0);
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
            luaL_argcheck(L, lua_istable(L, idx), 1, "required table not found on stack.");
            if (lua_istable(L, idx))
            {
                result.first = LuaStack<typename Container::first_type>::get(L, idx + 1);
                result.second = LuaStack<typename Container::second_type>::get(L, idx + 2);
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
}

#endif //#if !defined(LUAAA_WITHOUT_CPP_STDLIB)

#endif


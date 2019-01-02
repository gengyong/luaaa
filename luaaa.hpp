
/*
 Copyright (c) 2019 gengyong
 https://github.com/gengyong/luaaa
 licensed under MIT License.
*/

#ifndef HEADER_LUAAA_HPP
#define HEADER_LUAAA_HPP

#define LUAAA_NS luaaa

extern "C"
{
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM <= 501
inline void luaL_setfuncs(lua_State *L, const luaL_Reg *l, int nup) {
	luaL_checkstack(L, nup + 1, "too many upvalues");
	for (; l->name != NULL; l++) {
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


#include <string>
#include <typeinfo>

namespace LUAAA_NS
{
	template <typename>	struct LuaClass;

	//========================================================
	// Lua stack operator
	//========================================================

	template<typename T> struct LuaStack
	{
		inline static T& get(lua_State * state, int idx)
		{
			luaL_argcheck(state, !LuaClass<T>::klassName.empty(), 1, (std::string("cpp class `") + typeid(T).name() + "` not export").c_str());
			T ** t = (T**)luaL_checkudata(state, idx, LuaClass<T>::klassName.c_str());
			luaL_argcheck(state, t != NULL, 1, "invalid user data");
			luaL_argcheck(state, *t != NULL, 1, "invalid user data");
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
			if (!LuaClass<T*>::klassName.empty())
			{
				T ** t = (T**)luaL_checkudata(state, -1, LuaClass<T*>::klassName.c_str());
				luaL_argcheck(state, t != NULL, 1, "invalid user data");
				return *t;
			}
			else if (!LuaClass<T>::klassName.empty())
			{
				T ** t = (T**)luaL_checkudata(state, -1, LuaClass<T>::klassName.c_str());
				luaL_argcheck(state, t != NULL, 1, "invalid user data");
				luaL_argcheck(state, *t != NULL, 1, "invalid user data");
				return *t;
			}
			else if (lua_islightuserdata(state, idx))
			{
				T * t = (T*)lua_touserdata(state, idx);
				return t;
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
			thread_local static char sss[256];

			switch (lua_type(L, idx))
			{
			case LUA_TBOOLEAN:
				return (lua_toboolean(L, idx) ? "true" : "false");
			case LUA_TNUMBER:
				snprintf((sss), sizeof(sss), LUA_NUMBER_FMT, (lua_tonumber(L, idx)));
				return sss;
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

	// push ret data to stack
	template <typename T>
	inline void LuaStackReturn(lua_State * L, T t)
	{
		lua_settop(L, 0);
		LuaStack<T>::put(L, t);
	}


	//========================================================
	// non-member function caller & static member function caller
	//========================================================

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
					volatile int idx = sizeof...(ARGS) + SKIPPARAM; (void)(idx); \
					LuaStackReturn<TRET>(state, (*(FTYPE*)(calleePtr))((LuaStack<ARGS>::get(state, idx--))...)); \
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
					volatile int idx = sizeof...(ARGS) + SKIPPARAM; (void)(idx); \
					(*(FTYPE*)(calleePtr))((LuaStack<ARGS>::get(state, idx--))...); \
				} \
				return 0; \
			} \
		}; \
		return HelperClass::Invoke; \
	}
	
#if defined(_MSC_VER)	
	IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, __cdecl, 0);
	IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, __cdecl, 1);

#	ifdef _M_CEE
	IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, __clrcall, 0);
	IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, __clrcall, 1);
#	endif

#	if defined(_M_IX86) && !defined(_M_CEE)
	IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, __fastcall, 0);
	IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, __fastcall, 1);
#	endif

#	ifdef _M_IX86
	IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, __stdcall, 0);
	IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, __stdcall, 1);
#	endif

#	if ((defined(_M_IX86) && _M_IX86_FP >= 2) || defined(_M_X64)) && !defined(_M_CEE)
	IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, __vectorcall, 0);
	IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, __vectorcall, 1);
#	endif

#elif defined(__GNUC__)
#	define _NOTHING
	IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, _NOTHING, 0);
	IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, _NOTHING, 1);
#	undef _NOTHING	
#else
#	define _NOTHING	
	IMPLEMENT_FUNCTION_CALLER(NonMemberFunctionCaller, _NOTHING, 0);
	IMPLEMENT_FUNCTION_CALLER(MemberFunctionCaller, _NOTHING, 1);
#	undef _NOTHING		
#endif	

	//========================================================
	// member function invoker
	//========================================================
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
					volatile int idx = sizeof...(ARGS) + 1; (void)(idx);
					LuaStackReturn<TRET>(state, (LuaStack<TCLASS>::get(state, 1).**(FTYPE*)(calleePtr))(LuaStack<ARGS>::get(state, idx--)...));
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
					volatile int idx = sizeof...(ARGS) + 1; (void)(idx);
					LuaStackReturn<TRET>(state, (LuaStack<TCLASS>::get(state, 1).**(FTYPE*)(calleePtr))(LuaStack<ARGS>::get(state, idx--)...));
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
					volatile int idx = sizeof...(ARGS) + 1; (void)(idx);
					(LuaStack<TCLASS>::get(state, 1).**(FTYPE*)(calleePtr))(LuaStack<ARGS>::get(state, idx--)...);
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
					volatile int idx = sizeof...(ARGS) + 1; (void)(idx);
					(LuaStack<TCLASS>::get(state, 1).**(FTYPE*)(calleePtr))(LuaStack<ARGS>::get(state, idx--)...);
				}
				return 0;
			}
		};
		return HelperClass::Invoke;
	}
	
	//========================================================
	// constructor invoker
	//========================================================
	template<typename TCLASS, typename ...ARGS>
	struct ConstructorCaller
	{
		static TCLASS * Invoke(lua_State * state)
		{
			(void)(state);
			volatile int idx = sizeof...(ARGS); (void)(idx);
			return new TCLASS((LuaStack<ARGS>::get(state, idx--))...);
		}
	};
}

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
	// native array
	/*
	template <typename T>
	std::vector<T> & NativeArrayDataHolder(int size)
	{
		thread_local static std::vector<T> sss;
		sss.reserve(size);
		return sss;

	}

	template<typename K, size_t N>
	struct LuaStack<K[N]>
	{
		inline static K * get(lua_State * L, int idx)
		{
			DUMP(L, "LuaStack<K[N]> aaa");
			printf("idx: %d", idx);

			auto & holder = NativeArrayDataHolder<K>(N);
			luaL_argcheck(L, lua_istable(L, idx), 1, "required table not found on stack.");
			if (lua_istable(L, idx))
			{
				int index = 0;
				lua_pushnil(L);
				while (0 != lua_next(L, idx) && index < N)
				{
					DUMP(L, "LuaStack<K[N]> bbb");
					holder[index++] = LuaStack<K>::get(L, idx + 2);
					DUMP(L, "LuaStack<K[N]> ccc");
					lua_pop(L, 1);
					DUMP(L, "LuaStack<K[N]> ddd");
				}
				lua_pop(L, 0);
				DUMP(L, "LuaStack<K[N]> eee");
			}
			return holder.data();
		}

		inline static void put(lua_State * L, const K* p)
		{
			lua_newtable(L);
			for (size_t index = 0; index < N; index++)
			{
				LuaStack<K>::put(L, p[index]);
				lua_rawseti(L, -2, index + 1);
			}
		}
	};
	*/

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
					result[index++] = LuaStack<typename Container::value_type>::get(L, idx + 2);
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
					result.push_back(LuaStack<typename Container::value_type>::get(L, idx + 2));
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
					result.push_back(LuaStack<typename Container::value_type>::get(L, idx + 2));
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
					result.push_back(LuaStack<typename Container::value_type>::get(L, idx + 2));
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
					result.push_back(LuaStack<typename Container::value_type>::get(L, idx + 2));
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
					result.insert(LuaStack<typename Container::value_type>::get(L, idx + 2));
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
					result.insert(LuaStack<typename Container::value_type>::get(L, idx + 2));
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
					result.insert(LuaStack<typename Container::value_type>::get(L, idx + 2));
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
					result.insert(LuaStack<typename Container::value_type>::get(L, idx + 2));
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
					result[LuaStack<typename Container::key_type>::get(L, idx + 1)] = LuaStack<typename Container::mapped_type>::get(L, idx + 2);
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
					result[LuaStack<typename Container::key_type>::get(L, idx + 1)] = LuaStack<typename Container::mapped_type>::get(L, idx + 2);
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
					result[LuaStack<typename Container::key_type>::get(L, idx + 1)] = LuaStack<typename Container::mapped_type>::get(L, idx + 2);
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
					result[LuaStack<typename Container::key_type>::get(L, idx + 1)] = LuaStack<typename Container::mapped_type>::get(L, idx + 2);
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


	//========================================================
	// export class
	//========================================================
	template <typename TCLASS>
	struct LuaClass
	{
	public:
		LuaClass(lua_State * state, const std::string& name, const luaL_Reg * functions = nullptr)
			: m_state(state)
		{
			luaL_argcheck(state, (klassName.empty() || klassName == name), 1, (std::string("C++ class `") + typeid(TCLASS).name() + "` bind to conflict lua name `" + name + "`, origin name: " + klassName).c_str());

			klassName = name;

			if (state)
			{
				struct HelperClass {
					static int f__gc(lua_State* state) {
						TCLASS ** objPtr = (TCLASS**)luaL_checkudata(state, -1, LuaClass<TCLASS>::klassName.c_str());
						if (objPtr)
						{
							delete *objPtr;
						}
						return 0;
					}
				};

				luaL_Reg destructor[] = {
					{ "__gc", HelperClass::f__gc },
					{ nullptr, nullptr }
				};

				luaL_newmetatable(state, klassName.c_str());
				lua_pushvalue(state, -1);
				lua_setfield(state, -2, "__index");
				luaL_setfuncs(state, destructor, 0);
				if (functions)
				{
					luaL_setfuncs(state, functions, 0);
				}
				lua_pop(state, 1);

				ctor<>();
			}
		}


		template<typename ...ARGS>
		inline LuaClass<TCLASS>& ctor(const char * name = "new")
		{
			struct HelperClass {
				static int f_new(lua_State* state) {
					auto obj = ConstructorCaller<TCLASS, ARGS...>::Invoke(state);
					if (obj)
					{
						TCLASS ** objPtr = (TCLASS**)lua_newuserdata(state, sizeof(TCLASS*));
						if (objPtr)
						{
							*objPtr = obj;
							luaL_getmetatable(state, LuaClass<TCLASS>::klassName.c_str());
							lua_setmetatable(state, -2);
							return 1;
						}
						else
						{
							delete obj;
						}
					}
					lua_pushnil(state);			
					return 1;
				}

			};

			luaL_Reg constructor[] = { { name, HelperClass::f_new },{ nullptr, nullptr } };

#if USE_NEW_MODULE_REGISTRY
			lua_newtable(m_state);
			luaL_setfuncs(m_state, constructor, 0);
			lua_setglobal(m_state, klassName.c_str());
#else
			luaL_register(m_state, klassName.c_str(), constructor);	//luaL_openlib(state, klassName.c_str(), constructor, 0);
			lua_pop(m_state, 1);
#endif
			return (*this);
		}

		template<typename ...ARGS>
		inline LuaClass<TCLASS>& ctor(const std::string& name)
		{
			return ctor<ARGS...>(name.c_str());
		}

		template<typename F>
		inline LuaClass<TCLASS>& fun(const char * name, F f)
		{
			luaL_getmetatable(m_state, klassName.c_str());
			lua_pushstring(m_state, name);

			F * funPtr = (F*)lua_newuserdata(m_state, sizeof(F));
			luaL_argcheck(m_state, funPtr != nullptr, 1, (std::string("faild to alloc mem to store function `") + name + "`").c_str());
			*funPtr = f;
			lua_pushcclosure(m_state, MemberFunctionCaller(f), 1);
			lua_settable(m_state, -3);
			lua_pop(m_state, 1);
			return (*this);
		}

		inline LuaClass<TCLASS>& fun(const char * name, lua_CFunction f)
		{
			luaL_getmetatable(m_state, klassName.c_str());
			lua_pushstring(m_state, name);
			lua_pushcclosure(m_state, f, 0);
			lua_settable(m_state, -3);
			lua_pop(m_state, 1);
			return (*this);
		}

		template <typename F>
		inline LuaClass<TCLASS>& fun(const std::string& name, F f)
		{
			return fun(name.c_str(), f);
		}

		template <typename V>
		inline LuaClass<TCLASS>& def(const char * name, const V& val)
		{
			luaL_getmetatable(m_state, klassName.c_str());
			lua_pushstring(m_state, name);
			LuaStack<V>::put(m_state, val);
			lua_settable(m_state, -3);
			lua_pop(m_state, 1);
			return (*this);
		}

		// disable cast from "const char [#]" to "char (*)[#]"
		inline LuaClass<TCLASS>& def(const char * name, const char * str)
		{
			luaL_getmetatable(m_state, klassName.c_str());
			lua_pushstring(m_state, name);
			LuaStack<decltype(str)>::put(m_state, str);
			lua_settable(m_state, -3);
			lua_pop(m_state, 1);
			return (*this);
		}

		template <typename V>
		inline LuaClass<TCLASS>& def(const std::string& name, const V& val)
		{
			return def(name.c_str(), val);
		}

	private:
		lua_State *	m_state;

	private:
		template<typename> friend struct LuaStack;
		static std::string klassName;
	};

	template <typename TCLASS> std::string LuaClass<TCLASS>::klassName;


	// -----------------------------------
	// export module
	// -----------------------------------
	struct LuaModule
	{
	public:
		LuaModule(lua_State * state, const std::string& name = "_G")
			: m_state(state)
			, m_moduleName(name.empty() ? "_G" : name)
		{
		}

	public:
		template<typename F>
		inline LuaModule& fun(const char * name, F f)
		{
			luaL_Reg regtab[] = { { name, NonMemberFunctionCaller(f) },{ nullptr, nullptr } };

#if USE_NEW_MODULE_REGISTRY
			lua_getglobal(m_state, m_moduleName.c_str());
			if (lua_isnil(m_state, -1)) 
			{
				lua_pop(m_state, 1);
				lua_newtable(m_state);
			}

			F * funPtr = (F*)lua_newuserdata(m_state, sizeof(F));
			luaL_argcheck(m_state, funPtr != nullptr, 1, (std::string("faild to alloc mem to store function `") + name + "`").c_str());
			*funPtr = f;

			luaL_setfuncs(m_state, regtab, 1);
			lua_setglobal(m_state, m_moduleName.c_str());
#else
			F * funPtr = (F*)lua_newuserdata(m_state, sizeof(F));
			luaL_argcheck(m_state, funPtr != nullptr, 1, (std::string("faild to alloc mem to store function `") + name + "`").c_str());
			*funPtr = f;

			luaL_openlib(m_state, m_moduleName.c_str(), regtab, 1);
#endif

			return (*this);
		}

		inline LuaModule& fun(const char * name, lua_CFunction f)
		{
			luaL_Reg regtab[] = { { name, f },{ nullptr, nullptr } };
#if USE_NEW_MODULE_REGISTRY
			lua_getglobal(m_state, m_moduleName.c_str());
			if (lua_isnil(m_state, -1))
			{
				lua_pop(m_state, 1);
				lua_newtable(m_state);
			}
			luaL_setfuncs(m_state, regtab, 0);
			lua_setglobal(m_state, m_moduleName.c_str());
#else
			luaL_openlib(m_state, m_moduleName.c_str(), regtab, 0);
#endif
			return (*this);
		}

		template <typename V>
		inline LuaModule& def(const char * name, const V& val)
		{
#if USE_NEW_MODULE_REGISTRY
			lua_getglobal(m_state, m_moduleName.c_str());
			if (lua_isnil(m_state, -1))
			{
				lua_pop(m_state, 1);
				lua_newtable(m_state);
			}
			LuaStack<V>::put(m_state, val);
			lua_setfield(m_state, -2, name);
			lua_setglobal(m_state, m_moduleName.c_str());
#else
			luaL_Reg regtab = { nullptr, nullptr };
			luaL_openlib(m_state, m_moduleName.c_str(), &regtab, 0);
			LuaStack<V>::put(m_state, val);
			lua_setfield(m_state, -2, name);
#endif
			return (*this);
		}

		// disable the cast from "const char [#]" to "char (*)[#]"
		inline LuaModule& def(const char * name, const char * str)
		{
			
#if USE_NEW_MODULE_REGISTRY
			lua_getglobal(m_state, m_moduleName.c_str());
			if (lua_isnil(m_state, -1))
			{
				lua_pop(m_state, 1);
				lua_newtable(m_state);
			}
			LuaStack<decltype(str)>::put(m_state, str);
			lua_setfield(m_state, -2, name);
			lua_setglobal(m_state, m_moduleName.c_str());
#else
			luaL_Reg regtab = { nullptr, nullptr };
			luaL_openlib(m_state, m_moduleName.c_str(), &regtab, 0);
			LuaStack<decltype(str)>::put(m_state, str);
			lua_setfield(m_state, -2, name);
#endif
			return (*this);
		}

		template <typename V>
		inline LuaModule& def(const std::string& name, const V& val)
		{
			return def(name.c_str(), val);
		}

	private:
		lua_State *	m_state;
		std::string m_moduleName;
	};

}

#endif
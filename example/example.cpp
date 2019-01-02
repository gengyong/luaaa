

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cassert>

#include "../luaaa.hpp"

#define LOG printf

void bindToLUA(lua_State *);

void runLuaExample(lua_State * ls)
{
	bindToLUA(ls);
	
	do {
		LOG("------------------------------------------\n");
		std::stringstream buffer;
		std::ifstream file("example.lua");
		if (file)
		{
			buffer << file.rdbuf();
			file.close();
		}


		int err = luaL_loadbuffer(ls, buffer.str().c_str(), buffer.str().length(), "console");
		if (err == 0)
		{
			err = lua_pcall(ls, 0, 0, 0);
		}

		if (err)
		{
			LOG("lua err: %s", lua_tostring(ls, -1));
			lua_pop(ls, 1);
		}
		
	} while (std::cin.get() != 27);
	
}


int main()
{
	const luaL_Reg lualibs[] = {
		{ LUA_COLIBNAME, luaopen_base },
		{ LUA_LOADLIBNAME, luaopen_package },
		{ LUA_TABLIBNAME, luaopen_table },
		{ LUA_IOLIBNAME, luaopen_io },
		{ LUA_OSLIBNAME, luaopen_os },
		{ LUA_STRLIBNAME, luaopen_string },
		{ LUA_MATHLIBNAME, luaopen_math },
		{ LUA_DBLIBNAME, luaopen_debug },
		{ NULL, NULL }
	};

	auto ls = luaL_newstate();

	if (ls != NULL)
	{
		const luaL_Reg *lib = lualibs;
		for (; lib->func; lib++) {
			lua_pushcfunction(ls, lib->func);
			lua_pushstring(ls, lib->name);
			lua_call(ls, 1, 0);
		}
	
		runLuaExample(ls);

		lua_close(ls);
	}
	return 0;
}



//===============================================================================
// example c++ class 
//===============================================================================


class Cat
{
public:
	Cat()
		: m_age(1), m_weight(1.0f)
	{
		LOG("Cat: a cat spawn at %p.\n", this);
	}

	Cat(const std::string& name)
		: m_name(name), m_age(1), m_weight(1.0f)
	{
		LOG("Cat: %s spawn at %p\n", m_name.c_str(), this);
	}

	~Cat()
	{
		LOG("Cat: cat[%p] %s is free.\n", this, m_name.c_str());
	}

	const std::string& getName() const {
		return m_name; 
	}


	const std::string& setName(const std::string& name)
	{
		m_name = name; 
		return m_name;
	}

	int setAge(const int age)
	{ 
		m_age = age; 
		return m_age;
	}

	int getAge() const
	{
		return m_age; 
	}

	void eat(const std::list<std::string>& foods)
	{
		for (auto it : foods)
		{
			LOG("%s eat %s.\n", m_name.c_str(), it.c_str());
			m_weight += 0.1;
		}
		LOG("%s is getting fatter.", m_name.c_str());
	}

	std::string toString() const
	{ 
		std::stringstream result;
		result << "\"cat %s is "<< m_name << ", " << m_age <<" years old, has a weight of " << m_weight << " kg.\"";
		return result.str();
	}

	static void speak(const std::string& w)
	{
		LOG("%s, miaow~~", w.c_str());
	}

private:
	std::string m_name;
	int m_age;
	float m_weight;
};



//===============================================================================
// example c functions
//===============================================================================

void testSet(const std::set<int>& s)
{
	LOG("testSet: set<int> size: %lu\n", s.size());
	LOG("--------------------------\n");
	for (auto it = s.begin(); it != s.end(); ++it)
	{
		LOG("%d ", *it);
	}
	LOG("\n--------------------------\n");
}

void testSetSet(const std::multiset<std::set<std::string>>& s)
{
	LOG("testSetSet: multiset<set<str>> size: %lu\n", s.size());
	LOG("--------------------------\n");
	for (auto it = s.begin(); it != s.end(); ++it)
	{
		LOG("=>set<str> size: %lu\n", it->size());
		for (auto nit = it->begin(); nit != it->end(); ++nit)
		{
			LOG("'%s', ", nit->c_str());
		}
		LOG("\n");
	}
	LOG("\n--------------------------\n");
}


void testMapMap(const std::map<std::string, std::map<std::string, std::string>>& s)
{
	LOG("testMapMap: map<str, map<str, str>> size: %lu\n", s.size());
	LOG("--------------------------\n");
	for (auto it = s.begin(); it != s.end(); ++it)
	{
		LOG("'%s' => map<str, str> size: %lu\n", it->first.c_str(), it->second.size());
		for (auto nit = it->second.begin(); nit != it->second.end(); ++nit)
		{
			LOG("%s = %s, ", nit->first.c_str(),  nit->second.c_str());
		}
		LOG("\n");
	}
	LOG("\n--------------------------\n");
}



//===============================================
// below shows ho to bind c++ with lua
//===============================================
using namespace luaaa;

void bindToLUA(lua_State * L)
{
	// bind class to lua
	LuaClass<Cat> luaCat(L, "AwesomeCat");
	luaCat.ctor<std::string>();
	luaCat.fun("setName", &Cat::setName);
	luaCat.fun("getName", &Cat::getName);
	luaCat.fun("setAge", &Cat::setAge);
	luaCat.fun("getAge", &Cat::getAge);
	luaCat.fun("eat", &Cat::eat);
	luaCat.fun("speak", &Cat::speak);
	luaCat.fun("__tostring", &Cat::toString);
	luaCat.def("tag", "Animal");


	// define a module with name "AwesomeMod"
	LuaModule awesomeMod(L, "AwesomeMod");
	awesomeMod.def("cint", 20190101);
	awesomeMod.def("cstr", "this is c string");

	std::list<std::string> dict {
		"AMICUS", "AMOS", "AMTRAK", "ANGELICA", "ANNIE OAKLEY", "BEETHOVEN", "BERTHA", "BESSEYA", "BILLIE JEAN", "BIMBO", "BISS", "DECATHLON", "DELIRIUM", "DELIUS", "DEMPSEY" 
	};

	awesomeMod.def("dict", dict);

	// c++11 standard conatiners(array, vector, deque, list, forward_list, set/multiset, map/multimap, unordered_set/unordered_multiset, unordered_map/unordered_multimap)
	awesomeMod.fun("testSet", testSet);
	awesomeMod.fun("testSetSet", testSetSet);
	awesomeMod.fun("testMapMap", testMapMap);


	// put something to global, just emit the module name
	LuaModule(L).def("pi", 3.1415926535897932);


	// operations can be chained.
	LuaClass<int*>(L, "int")
	.ctor<int*>("new")
	.def("type", std::string("[c int *]"))
	.def("max", INT_MAX)
	.def("min", INT_MIN);

}


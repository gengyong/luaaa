

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
            m_weight += 0.1f;
        }
        LOG("%s is getting fatter.\n", m_name.c_str());
    }

    void test(int a, const std::string& b, float c, const std::string& d, const std::string& e)
    {
        LOG("cat test: got params from lua: [0: %d, 1:%s, 2:%f, 3:%s, 4:%s]\n", a, b.c_str(), c, d.c_str(), e.c_str());
    }

    std::string toString() const
    { 
        std::stringstream result;
        result << m_name << " is a cat, he is " << m_age <<" years old, has a weight of " << m_weight << " kg.";
        return result.str();
    }

    static void speak(const std::string& w)
    {
        LOG("%s, miaow~~\n", w.c_str());
    }

    void testfunctor(std::function<int(int param)> callback)
    {
	int result = callback(42);
	LOG("Callback with argument 42 leads to %d.\n", result);
    }

private:
    std::string m_name;
    int m_age;
    float m_weight;
};


class SingletonWorld 
{
public:
    static SingletonWorld * getInstance() {
        static SingletonWorld instance("singleton");
        return &instance;
    }

    static SingletonWorld * newInstance(const std::string tagName) {
        return new SingletonWorld(tagName);
    }

    static void delInstance(SingletonWorld * instance) {
        delete instance;
    }
public:
    const std::string getTag() const {
        return mTag;
    }

    SingletonWorld() {
        mTag = "default";
        LOG("SingletonWorld[%s] constructed.\n", mTag.c_str());
    }

    SingletonWorld(const std::string tagName) : mTag(tagName) {
        LOG("SingletonWorld[%s] constructed.\n", mTag.c_str());
    }

    ~SingletonWorld() {
        LOG("SingletonWorld[%s] destructed.\n", mTag.c_str());
    }
private:
    std::string mTag;
};

class Position {
public:
    float x;
    float y;
    float z;

    Position():x(0), y(0), z(0) {}
    Position(float fx, float fy, float fz):x(fx), y(fy), z(fz) {}
};



//===============================================================================
// example c functions
//===============================================================================
void testSet(const std::set<int>& s1, const std::set<int>& s2)
{
    LOG("testSet: set<int> size: s1:%lu s2:%lu\n", s1.size(), s2.size());
    LOG("--------------------------\n");
    LOG("s1:");
    for (auto it = s1.begin(); it != s1.end(); ++it)
    {
        LOG("%d ", *it);
    }
    LOG("\ns2:");
    for (auto it = s2.begin(); it != s2.end(); ++it)
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



void testMultipleParams(int a, int b, const std::string& c, float d, double e)
{
    LOG("c++ testCallback: got params from lua: [0: %d, 1:%d, 2:%s, 3:%f, 4:%g]\n", a, b, c.c_str(), d, e);
}

void testCallback(int (*f)(const std::string&, int, float), int val, const std::string& str)
{
    auto result = f("a string from c++:" + str, val, 1.2345678f);
    LOG("c++ testCallback: got result from lua callback: %d\n", result);
}

void testCallbackFunctor(std::function<int(const std::string&, int, float)> f, int val, const std::string& str)
{
    auto result = f("a string from c++:" + str, val, 8.7654321f);
    LOG("c++ testCallbackFunctor: got result from lua callback: %d\n", result);
}


//===============================================
// declare custom LuaStack operators
//===============================================
// for GCC, it must be delcared in namespace luaaa.
namespace luaaa {
    template<> struct LuaStack<Position>
    {
        inline static Position get(lua_State * L, int idx)
        {
            auto dict = LuaStack<std::map<std::string, float>>::get(L, idx);
            return Position(dict.find("x")->second, dict.find("y")->second, dict.find("z")->second);
        }

        inline static void put(lua_State * L, const Position & v)
        {
            std::map<std::string, float> dict;
            dict["x"] = v.x;
            dict["y"] = v.y;
            dict["z"] = v.z;
            LuaStack<decltype(dict)>::put(L, dict);
        }
    };
}
//*/

Position testPosition(const Position& a, const Position& b)
{
    return Position(a.x + b.x, a.y + b.y, a.z + b.z);
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
    luaCat.fun("test", &Cat::test);
    luaCat.fun("testSet", testSet);
    luaCat.fun("testfunctor", &Cat::testfunctor);
    luaCat.fun(std::string("testFunctor1"), [](int n1, int n2) -> int {
        LOG("testFunctor1:%d, %d\n", n1, n2);
        return n1 * n2;
    });
    luaCat.fun("testFunctor2", std::function<void(int, int)>([](int n1, int n2) {
        LOG("testFunctor2:%d, %d\n", n1, n2);
    }));
    luaCat.fun("__tostring", &Cat::toString);
    luaCat.def("tag", "Animal");

    // bind singleton class to lua
    LuaClass<SingletonWorld> luaWorld(L, "SingletonWorld");
    /// use class constructor as instance spawner, default destructor will be called from gc.
    luaWorld.ctor();
    /// use static function as instance spawner, default destructor will be called from gc.
    luaWorld.ctor("newInstance", &SingletonWorld::newInstance);
    /// use static function as instance spawner and static function as delete function which be called from gc.
    luaWorld.ctor("managedInstance", &SingletonWorld::newInstance , &SingletonWorld::delInstance);
    /// for singleton pattern, set deleter(gc) to nullptr to avoid singleton instance be destroyed.
    luaWorld.ctor("getInstance", &SingletonWorld::getInstance, nullptr);
    luaWorld.fun("getTag", &SingletonWorld::getTag);


    // define a module with name "AwesomeMod"
    LuaModule awesomeMod(L, "AwesomeMod");
    awesomeMod.def("cint", 20190101);
    awesomeMod.def("cstr", "this is c string");

    std::list<std::string> dict {
        "AMICUS", "AMOS", "AMTRAK", "ANGELICA", "ANNIE OAKLEY", 
        "BEETHOVEN", "BERTHA", "BESSEYA", "BILLIE JEAN", "BIMBO", 
        "BISS", "DECATHLON", "DELIRIUM", "DELIUS", "DEMPSEY" 
    };

    awesomeMod.def("dict", dict);

    // c++11 standard conatiners(array, vector, deque, list, forward_list, set/multiset, map/multimap, unordered_set/unordered_multiset, unordered_map/unordered_multimap)
    awesomeMod.fun("testSet", testSet);
    awesomeMod.fun("testSetSet", testSetSet);
    awesomeMod.fun("testMapMap", testMapMap);
    awesomeMod.fun("testMultipleParams", testMultipleParams);
    awesomeMod.fun("testCallback", testCallback);
    awesomeMod.fun("testCallbackFunctor", testCallbackFunctor);
    awesomeMod.fun("testPosition", testPosition);
    awesomeMod.fun("testFunctor1", [](int a, float b) {
        LOG("awesomeMod call testFunctor1: %d, %f", a, b);
    });
    awesomeMod.fun("testFunctor2", [](int a, float b) -> float {
        LOG("awesomeMod call testFunctor2: %d * %f = %f", a, b, a*b);
        return a * b;
    });

    // put something to global, just emit the module name
    LuaModule(L).def("pi", 3.1415926535897932);

    LuaModule(L).def("WITHOUT_CPP_STDLIB", false);

    // operations can be chained.
    LuaClass<int*>(L, "int")
    .ctor<int*>("new")
    .def("type", std::string("[c int *]"))
    .def("max", INT_MAX)
    .def("min", INT_MIN);

}


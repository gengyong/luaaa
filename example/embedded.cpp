

#include <cassert>
#include <cstring>
#include <cstdlib>

#define LUAAA_WITHOUT_CPP_STDLIB 1

#include "../luaaa.hpp"

#define LOG printf

#if defined(_MSC_VER)
#   define strncpy strncpy_s
#endif


//===============================================================================
using namespace luaaa;
class MyType {
public:
    MyType() :m_dummy(0) {
        LOG("MyType::MyType\n");
    }
    virtual ~MyType() {
        LOG("MyType::~MyType\n");
    }
private:
    int m_dummy;
};

class MyTypeB {
public:
    MyTypeB() :m_dummy(0) {
        LOG("MyTypeB::MyTypeB\n");
    }
    virtual ~MyTypeB() {
        LOG("MyTypeB::~MyTypeB\n");
    }
private:
    int m_dummy;
};

void test1()
{
    lua_State* L = luaL_newstate();
    LuaClass<MyType> myLuaType(L, "MyType");
    myLuaType.ctor();
    LOG("export MyType 1.\n");

    LuaClass<MyTypeB> myLuaTypeB(L, "MyTypeB");
    myLuaTypeB.ctor();
    LOG("export MyTypeB 1.\n");

    luaL_dostring(L, "local a = MyType.new();local b = MyTypeB.new();");

    LOG("ready to close....\n");

    lua_close(L);
}

void test2()
{
    lua_State* L = luaL_newstate();
    LuaClass<MyType> myLuaType(L, "MyType");
    myLuaType.ctor();
    printf("export MyType 2.\n");

    lua_gc(L, LUA_GCCOLLECT, 0);

    LuaClass<MyTypeB> myLuaTypeB(L, "MyTypeB");
    myLuaTypeB.ctor();
    LOG("export MyTypeB 2.\n");

    lua_gc(L, LUA_GCCOLLECT, 0);

    luaL_dostring(L, "local a = MyType.new();local b = MyTypeB.new();");

    lua_gc(L, LUA_GCCOLLECT, 0);

    LOG("ready to close2....\n");

    lua_close(L);
}

void testExportMoreThanOnce() {
    test1();
    test2();
}

//===============================================================================
void bindToLUA(lua_State *);

void runLuaExample(lua_State * ls)
{
	bindToLUA(ls);
	
    LOG("------------------------------------------\n");
    if (luaL_dofile(ls, "example.lua"))
    {
        LOG("lua err: %s", lua_tostring(ls, -1));
        lua_pop(ls, 1);
    }
}


int main()
{
    testExportMoreThanOnce();

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
// example class 
//===============================================================================
#ifndef __PLACEMENT_NEW_INLINE
void * operator new(size_t sz, void * p) {
    return p;
}

void operator delete(void *, void *) noexcept {
}
#endif

void * operator new(size_t sz) {
    return malloc(sz);
}
void operator delete(void * p) noexcept {
    free(p);
}

class Cat
{
public:
	Cat()
		: m_age(1), m_weight(1.0f)
	{
		LOG("Cat: a cat spawn at %p.\n", this);
	}

	Cat(const char * name)
		: m_age(1), m_weight(1.0f)
	{
        strncpy(m_name, name, sizeof(m_name));
		LOG("Cat: %s spawn at %p\n", m_name, this);
	}

	~Cat()
	{
		LOG("Cat: cat[%p] %s is free.\n", this, m_name);
	}

	const char * getName() const {
		return m_name; 
	}


	const char * setName(const char * name)
	{
        strncpy(m_name, name, sizeof(m_name));
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

    void eat(lua_State * L)
	{
        if (lua_istable(L, -1))
        {
            lua_pushnil(L);
            while (0 != lua_next(L, -2))
            {
                const int top = lua_gettop(L);
                const char * food = luaaa::LuaStack<const char *>::get(L, top);                

                LOG("%s eat %s.\n", m_name, food);
                m_weight += 0.1f;

                lua_pop(L, 1);
            }
            lua_pop(L, 0);
        }
        
		LOG("%s is getting fatter.\n", m_name);
        return;
	}

    void test(int a, const char * b, float c, const char * d, const char * e)
    {
        LOG("cat test: got params from lua: [0: %d, 1:%s, 2:%f, 3:%s, 4:%s]\n", a, b, c, d, e);
    }

	const char * toString() const
	{ 
		static char buf[128];
        snprintf(buf, sizeof(buf), "%s is a cat, he is %d years old, has a weight of %f kg.", m_name, m_age, m_weight);
		return buf;
	}

	static void speak(const char * w)
	{
		LOG("%s, miaow~~\n", w);
	}

private:
	char m_name[32];
	int m_age;
	float m_weight;
};


class SingletonWorld 
{   
public:
    static SingletonWorld * getInstance() {
        if (s_instance == nullptr) {
            s_instance = new SingletonWorld("singleton");
        }
        return s_instance;
    }

    static SingletonWorld * newInstance(const char * tagName) {
        return new SingletonWorld(tagName);
    }

    static void delInstance(SingletonWorld * instance) {
        delete instance;
    }
public:
    const char * getTag() const {
        return mTag;
    }

    SingletonWorld() {
        snprintf(mTag, sizeof(mTag), "%s", "default");
        LOG("SingletonWorld[%s] constructed.\n", mTag);
    }

    SingletonWorld(const char * tagName) {
        snprintf(mTag, sizeof(mTag), "%s", tagName);
        LOG("SingletonWorld[%s] constructed.\n", mTag);
    }

    ~SingletonWorld() {
        LOG("SingletonWorld[%s] destructed.\n", mTag);
    }
private:
    char mTag[16];
    static SingletonWorld * s_instance;
};

SingletonWorld * SingletonWorld::s_instance = nullptr;

class Position {
public:
    float x;
    float y;
    float z;

    Position():x(0), y(0), z(0) {}
    Position(float fx, float fy, float fz):x(fx), y(fy), z(fz) {}
};



//===============================================================================
// example c++ functions
//===============================================================================
void testSet()
{
    LOG("testSet: [unsupported]\n");
}

void testSetSet()
{
    LOG("testSetSet: [unsupported]\n");
}


void testMapMap()
{
    LOG("testMapMap: [unsupported]\n");
}

void testMultipleParams(int a, int b, const char * c, float d, double e)
{
    LOG("c++ testCallback: got params from lua: [0: %d, 1:%d, 2:%s, 3:%f, 4:%g]\n", a, b, c, d, e);
}

void testCallback(int (*f)(const char *, int, float), int val, const char * str)
{
    char strbuf[128];
    snprintf(strbuf, sizeof(strbuf), "a string from c:%s", str);
	auto result = f(strbuf, val, 1.2345678f);
	LOG("c++ testCallback: got result from lua callback: %d\n", result);
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
            Position result;
            if (lua_istable(L, idx))
            {
                lua_pushnil(L);
                while (0 != lua_next(L, idx))
                {
                    const int top = lua_gettop(L);
                    const char * name = LuaStack<const char *>::get(L, top - 1);
                    if (strncmp(name, "x", 1) == 0) {
                        result.x = LuaStack<float>::get(L, top);
                    }
                    else if (strncmp(name, "y", 1) == 0) {
                        result.y = LuaStack<float>::get(L, top);
                    }
                    else if (strncmp(name, "z", 1) == 0) {
                        result.z = LuaStack<float>::get(L, top);
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 0);
            }
            return result;
        }

        inline static void put(lua_State * L, const Position & v)
        {
            lua_newtable(L);            
            LuaStack<const char *>::put(L, "x");
            LuaStack<float>::put(L, v.x);
            lua_rawset(L, -3);
            LuaStack<const char *>::put(L, "y");
            LuaStack<float>::put(L, v.y);
            lua_rawset(L, -3);
            LuaStack<const char *>::put(L, "z");
            LuaStack<float>::put(L, v.z);
            lua_rawset(L, -3);
        }
    };
}

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
	luaCat.ctor<const char *>();
	luaCat.fun("setName", &Cat::setName);
	luaCat.fun("getName", &Cat::getName);
	luaCat.fun("setAge", &Cat::setAge);
	luaCat.fun("getAge", &Cat::getAge);
	luaCat.fun("eat", &Cat::eat);
    luaCat.fun("test", &Cat::test);
	luaCat.fun("speak", &Cat::speak);
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
    
	const char * dict[] = {
		"AMICUS", "AMOS", "AMTRAK", "ANGELICA", "ANNIE OAKLEY", 
        "BEETHOVEN", "BERTHA", "BESSEYA", "BILLIE JEAN", "BIMBO", 
        "BISS", "DECATHLON", "DELIRIUM", "DELIUS", "DEMPSEY" 
    };

	awesomeMod.def("dict", dict, sizeof(dict)/sizeof(dict[0]));

    int dict2[] = {
        2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47
    };
    awesomeMod.def("dict2", dict2, sizeof(dict2) / sizeof(dict2[0]));
    
    awesomeMod.fun("testSet", testSet);
    awesomeMod.fun("testSetSet", testSetSet);
    awesomeMod.fun("testMapMap", testMapMap);
    awesomeMod.fun("testMultipleParams", testMultipleParams);
	awesomeMod.fun("testCallback", testCallback);
    awesomeMod.fun("testPosition", testPosition);


	// put something to global, just emit the module name
	LuaModule(L).def("pi", 3.1415926535897932);

	// operations can be chained.
	LuaClass<int*>(L, "int")
	.ctor<int*>("new")
	.def("type", "[c int *]")
	.def("max", INT_MAX)
	.def("min", INT_MIN);
    
}



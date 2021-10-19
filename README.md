
## Introduction

Luaaa is a simple tool to bind c++ class to lua. 

It was implement intent to use only one header file, with very simple interface, easy to integrate to exists project.

With luaaa, you don't need to write wrapper codes for exists class/function, and you don't need to run any other tool to generate wrapper codes. Just define the class to export and enjoy using it in lua.

Luaaa has no dependencies to other libs but lua and c++11 standard lib, no cpp files. 

To use it, just copy and include 'luaaa.hpp' in source file.

feel free to report bugs.
## Features

* simple.
* powerful.
* no wrapper codes.
* works with lua from 5.1 to 5.4.

## Quick Start

export a class to lua:
```cpp

// include luaaa file
#include "luaaa.hpp"
using namespace luaaa;	


// Your exists class
class Cat
{
public:
	Cat();
	virtual ~Cat();
public:
	void setName(const std::string&);
	const std::string& getName() const;
	void eat(const std::list<std::string>& foods);
	static void speak(const std::string& w);
	//...
private:
	//...
};


lua_State * state; // create and init lua

// To export it:
LuaClass<Cat> luaCat(state, "AwesomeCat");
luaCat.ctor<std::string>();
luaCat.fun("setName", &Cat::setName);
luaCat.fun("getName", &Cat::getName);
luaCat.fun("eat", &Cat::eat);
// static mmember fuction was exported as Lua class member fuction.
// from Lua, call it as same as other member fuctions.
luaCat.fun("speak", &Cat::speak);
luaCat.def("tag", "Cat");

// Done.

```

ok, then you can access lua class "AwesomeCat" from lua.
```lua

local cat = AwesomeCat.new("Bingo");
cat:eat({"fish", "milk", "cookie", "odd thing" });
cat:speak("Thanks!");

```

to export constructors, for example, instance getter of singleton pattern:
```
LuaClass<SingletonWorld> luaWorld(L, "SingletonWorld");
/// use class constructor as instance spawner, default destructor will be called from gc.
luaWorld.ctor<std::string>();

/// use static function as instance spawner, default destructor will be called from gc.
luaWorld.ctor("newInstance", &SingletonWorld::newInstance);

/// use static function as instance spawner and static function as delete function which be called from gc.
luaWorld.ctor("managedInstance", &SingletonWorld::newInstance , &SingletonWorld::delInstance);

/// for singleton pattern, set deleter(gc) to nullptr to avoid singleton instance be destroyed.
luaWorld.ctor("getInstance", &SingletonWorld::getInstance, nullptr);
```
instance spawner and delete function can be static member function or global function,
and delete function must accept one instance pointer which to be collect back or delete. 

----------------------------
### ***Breaking Changes***

Always define **at least one** 'ctor' for a LuaClass.

> in previous version, a default 'ctor' was generated in LuaClass constructor.  
the default 'ctor' will call default C++ class constructor, and register C++ class destructor as gc function.  
In some case, this default 'ctor' is not fit requirements it's invalid.  
for example, singleton class declare constructor/destructor as protected/private methods. in this case, default 'ctor' cannot access it, so default 'ctor' doesn't work, a custom 'ctor' is required here.  
in current version, the default 'ctor' was removed from LuaClass constructor. User must provide a 'ctor' in binding codes otherwise the LuaClass cannot be instantiate in lua.  
In most case, just define a default 'ctor' as below:
```cpp
LuaClass<XXX> luaCls(luaState, 'XXXname');
luaCls.ctor(); 
```
> above codes will define a lua object constructor named as 'new', in lua `XXXname.new()` equivalent to C++:
```cpp
new XXX();
```
> or change constructor name to 'create':
```cpp
luaCls.ctor("create");
```
> if C++ constructor is not the default constructor, add sigature to match C++ class constructor:
```cpp
luaCls.ctor<std::string>('create');
```
> which defined a lua object constructor named as 'create', in lua `XXXname.create("string param")` equivalent to C++:
```cpp
new XXX("string param");
```
----------------------------


static member function, global fuctions or constant can be export in module.
module has no constructor or destructor.
```cpp

#include "luaaa.hpp"
using namespace luaaa;

void func1(int);
void func2(int, int, int);
int  func3(int, const char *, float, int, int , float);
bool globalFunc(const std::string&, const std::map<std::string, std::string>&);

lua_State * state; // create and init lua

LuaModule(state, "moduleName") MyMod;
MyMod.fun("func1", func1);
MyMod
.fun("func2", func2)
.fun("func3", func3)
.def("cstr", "this is cstring");

// or export function or some value to global(just emit module name)
LuaModule(state)
.fun("globalFunc", globalFunc)
.def("cint", 12345)
.def("dict", std::set<std::string>({"cat", "dog", "cow"}));

// etc...

// Done.

```

ok, then access it from lua:
```lua
-- access module members
MyMod.func1(123)
MyMod.func2(123, "456", 523.3)
MyMod.func3(123, "string or any can be cast to string", 1.23, "1000", "2000", "9.876")
print(MyMod.cstr)

-- call global function
globalFunc("string or any thing can be cast to string", { key = "table will be cast to map"})

-- print global value 'dict' comes from c++
for k,v in pairs(dict) do
	print(tostring(k) .. " = " .. tostring(v))
end

```

to export c++ functions with same name, for example:
```cpp
 bool samename(const std::string&);
 void samename(int);

 class MyClass
 {
 public:
 	void sameNameFunc(int, int);
 	void sameNameFunc(int);
 	bool sameNameFunc();
 };
```
in this case, function signature is required here to know which function should be exported:
```cpp
MyMod.fun("func1", (bool(*)(const std::string&)) samename);
MyMod.fun("func2", (void(*)(int)) samename);

LuaClass<MyCLass>(state, "MyClass")
	.fun("sameNameFunc1", (void(MyClass::*)(int, int)) &MyClass::sameNameFunc)
	.fun("sameNameFunc2", (void(MyClass::*)(int) &MyClass::sameNameFunc))
	.fun("sameNameFunc3", (bool(MyClass::*)() &MyClass::sameNameFunc));
```

## Advanced Topic



## Run Example

### 1. Linux / Unix / Macos

1. install lua dev libs
```bash
# debian/ubuntu
$ sudo apt install lua5.3-dev
# redhat/centos/fedora
$ sudo yum install lua5.3-dev
```


2. build & run.
```bash
$ cd example
$ g++ -std=c++11  example.cpp -I/usr/include/lua5.3 -o example -g -lstdc++ -llua5.3
$ ./example
```

or use LLVM:
```bash
$ cd example
$ clang -std=c++11  example.cpp -I/usr/include/lua5.3 -o example -g -lstdc++ -llua5.3
$ ./example
```

for embedded device, declare 'LUAAA_WITHOUT_CPP_STDLIB' to disable c++ stdlib.
```
$ cd example
$ gcc -fno-exceptions -fno-rtti -std=c++11 embedded.cpp -I/usr/include/lua5.3 -o embedded -g  -llua5.3 -DLUAAA_WITHOUT_CPP_STDLIB
$ ./embedded
```

### 2. Visual C++

Of course you know how to do it.


## License

See the LICENSE file.

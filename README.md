
## Introduction

Luaaa is a simple tool to bind c++ class to lua. 

It was implement intent to use only one header file, with very simple interface, easy to integrate to exists project.

With luaaa, you don't need to write wrapper codes for your exists class/function, and you don't need to run any other tool to generate wrapper codes. Just define the class to export and enjoy using it in lua.

Luaaa has no dependencies to other libs but lua and c++11 standard lib, no cpp files. 

To use it, just copy and include 'luaaa.hpp' in your source file.


## Features

* simple.
* powerful.
* works lua 5.1, 5.2 and 5.3 version.

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
luaCat.def("tag", "Cat");

// Done.

```

ok, then you can aeess lua class "AwesomeCat" from lua.
```lua

local cat = AwesomeCat.new("Bingo");
cat.eat({"fish", "milk", "cookie", "odd thing" });

```

you can export something as a module to lua, you can access the module without create instance for it.
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

ok, then you can access it from lua:
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
 	void overrideFunc(int, int);
 	void overrideFunc(int);
 	bool overrideFunc();
 };
 
```
for this case, function signature is required for c++ to know which function should be select:
```cpp
MyMod.fun("func1", (bool(*)(const std::string&)) samename);
MyMod.fun("func2", (void(*)(int)) samename);

LuaClass<MyCLass>(state, "MyClass")
	.fun("overrideFunc1", (void(MyClass::*)(int, int)) &MyClass::overrideFunc)
	.fun("overrideFunc2", (void(MyClass::*)(int) &MyClass::overrideFunc))
	.fun("overrideFunc3", (bool(MyClass::*)() &MyClass::overrideFunc));
```



## Run Example

### Linux

1. install lua dev libs
```bash
$ sudo apt install lua5.1-0-dev
```

2. build & run.
```bash
$ cd example
$ g++ -std=c++11  example.cpp -I/usr/include/lua5.1 -o example -g -lstdc++ -llua5.1
$ ./example
```

or use LLVM:
```bash
$ cd example
$ clang -std=c++11  example.cpp -I/usr/include/lua5.1 -o example -g -lstdc++ -llua5.1
$ ./example
```


### Visual C++

Of course you know how to do it.

## Documents

TBD.

feel free to report bugs.

## License

See the LICENSE file.

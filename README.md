
## Introduction

Luaaa is a simple tool to bind c++ class to lua. 

It was implemented intent to use only one header file, with simple interface, easy to integrate to existing project.

With luaaa, you don't need to write wrapper codes for existing class/function, and you don't need to run any other tool to generate wrapper codes. Just define the class to export and enjoy using it in lua.

Luaaa has no dependencies to other libs but lua and c++11 standard lib, no cpp files.

To use it, just copy and include 'luaaa.hpp' in source file.

feel free to report bugs.
## Features

* simple.
* no wrapper codes.
* works with lua from 5.1 to 5.4, and luajit.

## Quick Start

export a class to lua:
```cpp

// include luaaa file
#include "luaaa.hpp"
using namespace luaaa;


// Your existing class
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
// static member fuction was exported as Lua class member fuction.
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

you can add property to AwesomeCat:
```cpp
luaCat.set("name", &Cat::setName);
luaCat.get("name", &Cat::getName);
luaCat.set("age", &Cat::setAge);
luaCat.get("age", &Cat::getAge);
```

then you can access property from lua as below:
```lua
local oldName = cat.name;
print("cat's old name:", oldName);
cat.name = "NewName";
print("cat's new name:", cat.name);
```
for the property getter, property type depends on the return value of getter function.

property getter accepts a function likes below:
```cpp
// 1) member function of origin c++ class which has no parameter
luaCat.get("name", &Cat::getName);

// 2) global function which has no parameter
//std::string getProp1() {
//    return "whatever";
//}
luaCat.get("prop1", getProp1);

// 3) global function which has origin c++ class as the only ONE parameter, parameter can be const or non-const.
//std::string getProp2(const Cat& cat) {
//    return cat.name;
//}
luaCat.get("prop2", getProp2);

// 4) a lambda function which has no parameter
luaCat.get("prop3", []() -> float { return 0.123f; });

// 5) a lambda function which has origin c++ class as the only ONE parameter, parameter can be const or non-const.
luaCat.get("prop4", [](Cat& cat) -> float { return cat.getWeight(); });

```

for the property setter, property type depends on the parameter of setter function.

property setter accepts a function likes below:
```cpp
// 1) member function of origin c++ class which has only ONE parameter
// in lua,
//   cat.name = "some thing...";
// will call c++ function:
//   catObject.setName("some thing...");
luaCat.set("name", &Cat::setName);
// in lua,
//   cat.age = 2;
// will call c++ function:
//   catObject.setAge(2);
luaCat.set("age", &Cat::setAge);

// 2) global function which has only ONE parameter
//void setProp1(cons std::string p) {
//    // do some thing...
//}
// in lua,
//   cat.prop1 = "prop value";
// will call c++ function:
//   setProp1("prop value");
luaCat.set("prop1", setProp1);

// 3) global function which accepts an origin c++ class and an extra parameter, origin c++ class can be const or non-const.
//void setProp2(Cat& cat, const std::string p) {
//    cat.setName(p);
//}
// in lua,
//   cat.prop2 = "prop value";
// will call c++ function:
//   setProp2(catObject, "prop value");
luaCat.set("prop2", setProp2);

// 4) lambda function which has only ONE parameter
luaCat.set("prop3", [](float val) -> void { printf("set prop3=%f\n", val); });

// 5) lambda function which accepts an origin c++ class and an extra parameter, origin c++ class can be const or non-const.
luaCat.set("prop4", [](Cat& cat, float val) -> void { cat.setWeight(val); });
```

if a property has only getter, it's read-only, if it has only setter, it's write-only, or if has both setter and getter, it can be read&write.

if write a read-only property, or read a write-only property from lua, a lua exception will be rised:

for example, with below defination:
```cpp
LuaClass<Cat> luaCat(state, "AnotherCat");
luaCat.ctor<std::string>();
luaCat.set("name", &Cat::setName);
luaCat.get("age", &Cat::getAge);
```

in lua:
```lua
local cat = AnotherCat.new("Orange");
print("Cat name:", cat.name);
```
will rise below exception:
```bash
lua err: [string "console"]:79: attempt to read Write-Only property 'name' of 'AwesomeCat'
```

```lua
local cat = AnotherCat.new("Orange");
cat.age = 10;
```
will rise below exception:
```bash
lua err: [string "console"]:1: attempt to write Read-Only property 'age' of 'AwesomeCat'
```


to export constructors, for example, instance getter of singleton pattern:
```cpp
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


A 'ctor'(constructor) is always required for LuaClass, you can define more than one 'ctor'.
In most case, a 'ctor' likes below is enought:
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

> which defines a lua object constructor named as 'create', in lua `XXXname.create("string param")` equivalent to C++:

```cpp
new XXX("string param");
```


static member function, global fuctions or constant can be export in module.
module has no constructor or destructor.
```cpp

#include "luaaa.hpp"
using namespace luaaa;

void func1(int);
void func2(int, int, int);
int  func3(int, const char *, float, int, int , float);
bool globalFunc(const std::string&, const std::map<std::string, std::string>&);

lua_State * state; 

/*
 init lua state here...
*/

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

to export lambda function:
```cpp
MyMod.fun("lambdaFunc", [](int a, int b) -> int {
    return a * b;
});
```




to extend exported lua class, add below codes to your project:
```lua
-- put utility functions to name space 'luaaa'
luaaa = {}

-- create subclass for base, obj can be exist table or nil
function luaaa:extend(base, obj)
	derived = obj or {}
	derived.new = function(self, ...)
		o = base.new(...)
		setmetatable(self, getmetatable(o))
		self["@"] = o
		return self
	end
	return derived
end

-- get base class of obj
function luaaa:base(obj)
	if (type(obj) == "table") then
		return obj["@"]
	end
	return nil
end

```

extends exported lua class as below:
```lua
SpecialCat = luaaa:extend(AwesomeCat, {value = 1})
-- or:
--   SpecialCat = luaaa:extend(AwesomeCat)
-- in this case there no attribute was extended

function SpecialCat:onlyInSpecial()
    print(self:getName() .. " has a special cat function")
    print("Special cat " .. self:getName() .." has value:" .. self.value)
end

function SpecialCat:speak(text)
    print("Special cat[" .. self:getName() .. "] says: " .. text)
    -- call override base method:
	luaaa:base(self):speak(text)
end
```

then use SpecialCat:
```lua
xxx = SpecialCat:new("xxx")
xxx:speak("I am a special cat.")
xxx:onlyInSpecial()
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

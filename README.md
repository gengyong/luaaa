
## Introduction

luaaa is a simple tool to bind c++ class to lua. 

It was implement intent to use only one header file, with very simple interface, easy to integrate to exists project.

with luaaa, you don't need to write wrapper codes for your exists class/function, and you don't need to run any other tool to generate wrapper codes. just define the class to export and enjoy using it in lua.

luaaa has no depencies to other libs but lua and c++11 standard lib, no cpp files. 

To use it, just copy and include 'luaaa.hpp' in your source file.


## Features

* simple.
* powerful.

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
	bool func4(const std::string&, const std::map<std::string, std::string>&);

	lua_State * state; // create and init lua

	LuaModule("moduleName") MyMod;
	MyMod.fun("func1", func1);
	MyMod.fun("func2", func2);
	MyMod.fun("func3", func3);
	MyMod.fun("func4", func4);
	// etc...

	// Done.

```

ok, then you can access it from lua:
```lua
	MyMod.func1(123)
	MyMod.func2(123, "456", 523.3)
	MyMod.func3(123, "string or any can be cast to string", 1.23, "1000", "2000", "9.876")
	MyMod.func4("string or any thing can be cast to string", { key = "table will be cast to map"})

```



## Run Example

### Linux

1. install lua dev libs
```bash
$ sudo apt install lua5.1-0-dev
```

2. make a link to lua header files
```bash
$ ln -s /usr/include/lua5.1 lua
```

3. build & run.
```bash
$ g++ -std=c++11  example.cpp -o example -lstdc++ -llua5.1
$ ./example
```

### Windows


## Documents

TBD.

feel free to report bugs.

## License

See the LICENSE file.

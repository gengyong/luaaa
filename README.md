# luaaa

**English** | [中文](README.zh-CN.md) · Advanced guide: [GUIDE.md](GUIDE.md) | [GUIDE.zh-CN.md](GUIDE.zh-CN.md)

**Bind C++ classes and functions to Lua with a single header — no code generation, no wrapper boilerplate.**

```cpp
LuaClass<Cat>(L, "Cat")
    .ctor<std::string>()
    .fun("eat", &Cat::eat)
    .get("name", &Cat::name).set("name", &Cat::setName);
```
```lua
local c = Cat.new("Bingo")
c:eat{ "fish", "milk" }
c.name = "Bingo the Brave"
```

That's the whole idea. Keep reading for a gentle, top-to-bottom tour.

---

## Table of contents

1. [Why luaaa](#why-luaaa)
2. [Requirements](#requirements)
3. [Install](#install)
4. [Quick start](#quick-start)
5. [Binding member & static functions](#binding-member--static-functions)
6. [Constructors](#constructors)
7. [Properties](#properties)
8. [Modules & globals](#modules--globals)
9. [Inheritance in Lua](#inheritance-in-lua)
10. [Lua functions as C++ callbacks](#lua-functions-as-c-callbacks)
11. [Run the example](#run-the-example)
12. [Where to next](#where-to-next)
13. [License](#license)

---

## Why luaaa

* **One header.** Copy `luaaa.hpp`, `#include` it, done. No build step, no `.cpp`, no external tool.
* **No wrappers.** Bind your *existing* classes and functions directly — you don't rewrite them.
* **Small surface.** Three names do almost everything: `LuaClass`, `LuaModule`, and (for custom types) `LuaStack`.
* **Automatic conversions.** Numbers, strings, `std::string`, and every standard container flow between C++ and Lua for you. (`const char*` args are borrowed from Lua and valid only for the call — use `std::string` to keep a string; see Guide → String lifetime.)
* **Portable.** Works with Lua 5.1 – 5.5 and LuaJIT, and has an embedded mode with no C++ standard library.

## Requirements

* A C++11 compiler (C++14 or newer unlocks a faster `std::tuple` path; still optional).
* Lua 5.1, 5.2, 5.3, 5.4, 5.5, or LuaJIT — headers and library available to your build.

## Install

luaaa is header-only. Drop `luaaa.hpp` into your project and include it:

```cpp
#include "luaaa.hpp"
using namespace luaaa;   // optional, but the examples assume it
```

It needs nothing but Lua and the C++ standard library.

## Quick start

Say you already have this plain C++ class — it knows nothing about Lua:

```cpp
class Cat {
public:
    explicit Cat(const std::string& name) : m_name(name), m_age(1) {}
    void eat(const std::list<std::string>& foods);   // takes a list
    const std::string& name() const;
    void setName(const std::string& n);
private:
    std::string m_name;
    int m_age;
};
```

Bind it to an already-created `lua_State* L`:

```cpp
#include "luaaa.hpp"
using namespace luaaa;

void bind(lua_State* L) {
    LuaClass<Cat>(L, "Cat")          // expose C++ `Cat` to Lua as "Cat"
        .ctor<std::string>()          // a constructor taking a string
        .fun("eat", &Cat::eat)        // a method
        .get("name", &Cat::name)      // a readable property...
        .set("name", &Cat::setName);  // ...that is also writable
}
```

Now Lua can use it — the array passed to `eat` becomes a `std::list<std::string>` automatically:

```lua
local c = Cat.new("Bingo")     -- calls the C++ constructor
c:eat{ "fish", "milk" }        -- Lua table -> std::list<std::string>
print(c.name)                  --> Bingo
c.name = "Bingo the Brave"     -- calls setName()
```

The object's lifetime is managed by Lua's garbage collector: when `c` is collected, `Cat`'s destructor runs.

## Binding member & static functions

`fun` binds anything callable: member functions, `static` members, free functions, lambdas.

```cpp
class Cat {
public:
    void eat(const std::list<std::string>&);   // member
    static void meow(const std::string& who);  // static
    std::string toString() const;              // for __tostring
};

LuaClass<Cat>(L, "Cat")
    .ctor<std::string>()
    .fun("eat",  &Cat::eat)
    .fun("meow", &Cat::meow)              // a static member, bound like any method
    .fun("__tostring", &Cat::toString)    // Lua metamethods work too
    .fun("play", [](int minutes) {        // a lambda is fine
        printf("played for %d min\n", minutes);
    });
```

```lua
local c = Cat.new("Bingo")
c:eat{ "fish" }
c:meow("Bingo")        -- static members are called on an instance, too
print(c)               -- __tostring: prints "Bingo (1y)"
c:play(10)
```

> Note: a `static`/free function bound with `fun` is called like a method — `c:meow("Bingo")`. The instance in front of the colon is passed as a hidden `self` and skipped; the remaining arguments map to the function's parameters. To call it on the class itself — `Cat.meow("Bingo")` — bind it with `staticFun` instead; `constant` puts a value there (`Cat.MAX_LIVES`).

## Constructors

Every class needs at least one constructor. The template arguments are the C++ constructor's parameter types; the string names it on the Lua side (default `"new"`).

```cpp
LuaClass<Cat>(L, "Cat")
    .ctor()                       // Cat.new()          -> new Cat()
    .ctor<std::string>("create"); // Cat.create("Tom")  -> new Cat("Tom")
```

```lua
local a = Cat.new()
local b = Cat.create("Tom")
```

You can register several constructors under different names. Factory functions, singletons, and custom deleters are covered in the [advanced guide](GUIDE.md#constructors-in-depth).

## Properties

`get` and `set` turn C++ accessors into Lua fields, so Lua reads and writes them with plain `.field` syntax.

```cpp
LuaClass<Cat>(L, "Cat")
    .ctor<std::string>()
    .get("name", &Cat::name).set("name", &Cat::setName)   // read + write
    .get("age",  &Cat::age ).set("age",  &Cat::setAge);
```

```lua
local c = Cat.new("Bingo")
print(c.name)      -- calls name()
c.age = 3          -- calls setAge(3)
```

* Only a getter → **read-only** (writing raises a Lua error).
* Only a setter → **write-only** (reading raises a Lua error).
* Both → read/write.

Getters and setters can also be free functions or lambdas (with or without a `Cat&` first parameter). See [Properties in depth](GUIDE.md#properties-in-depth).

## Modules & globals

A `LuaModule` groups free functions and constants under one Lua table — handy for things that aren't tied to an object.

```cpp
void adopt(const std::string& name, std::function<void(const std::string&)> onDone);

LuaModule(L, "shelter")
    .def("city",     "Catville")   // a constant
    .def("capacity", 50)
    .fun("adopt", adopt);          // a free function
```

```lua
print(shelter.city)            --> Catville
shelter.adopt("Bingo", function(who) print(who .. " adopted!") end)
```

Give the module no name (or `"_G"`) to put things straight into Lua's globals:

```cpp
LuaModule(L).def("pi", 3.14159);   // global `pi`
```

## Inheritance in Lua

As soon as you bind any class, luaaa injects two helpers into that `lua_State` — no glue to paste:

* `luaaa:extend(Base, fields)` — make a subclass of an exported class (`fields` optional).
* `luaaa:base(obj)` — get the underlying C++ object of an instance (to call an overridden method).

```lua
local SpecialCat = luaaa:extend(Cat, { tricks = 0 })

function SpecialCat:learn() self.tricks = self.tricks + 1 end

function SpecialCat:meow(who)          -- override...
    print(self.name .. " purrs first")
    luaaa:base(self):meow(who)         -- ...then call the C++ method
end

local felix = SpecialCat:new("Felix")
felix:learn()
felix:meow("Felix")
```

Every `SpecialCat:new(...)` creates a separate instance with its own C++ `Cat`, and an extended class can be extended again. Details: [Lua-side inheritance in depth](GUIDE.md#lua-side-inheritance-in-depth).

## Lua functions as C++ callbacks

A C++ function can accept a Lua function. Just declare the parameter as a `std::function` (preferred) or a plain function pointer:

```cpp
void onEach(std::function<int(int)> cb);   // preferred
void onEvent(int (*cb)(const char*));      // raw pointer (limited)
```

```lua
onEach(function(x) return x * 2 end)
```

Prefer the `std::function` form: it owns its own reference to the Lua function, so it can be stored, copied, called many times, and re-entered. The differences and the caveats of the raw-pointer form are explained in [Callbacks in depth](GUIDE.md#callbacks-in-depth).

## Run the example

The [`example/`](example/) folder contains a complete, runnable story — *The Little Cat Shelter* — plus an embedded (no-stdlib) variant, *The Feeder Node*.

```bash
bash example/build.sh            # build & run both
bash example/build.sh desktop    # only the full std-lib example
bash example/build.sh embedded   # only the no-stdlib example
```

The script auto-detects Lua via `pkg-config`. If it can't find your Lua, point it at the right paths:

```bash
LUA_CFLAGS=-I/usr/include/lua5.4 LUA_LIBS=-llua5.4 bash example/build.sh
```

Or compile by hand:

```bash
cd example
c++ -std=c++11 example.cpp -I/usr/include/lua5.4 -llua5.4 -lm -o example && ./example
```

## Where to next

You've seen everything you need for day-to-day use. The **[Advanced Guide](GUIDE.md)** covers the rest, as a reference and FAQ:

* Automatic type conversions and the full container/`pair`/`tuple` support table
* Teaching luaaa your own types with `LuaStack`
* Constructor variants: factories, singletons, custom deleters, name conflicts
* Overloaded functions: disambiguation, and several functions under one Lua name with `overload` / `overloadCtor`
* C++ inheritance with `inherit<Base>()`
* `std::shared_ptr` / `std::unique_ptr`, `std::optional`, `std::string_view`, move-only return types
* Callbacks in depth (`std::function` vs raw pointers)
* Metamethods (`__index`, `__newindex`, `__gc`, `__tostring`)
* Multiple `lua_State`s
* **Embedded / no-stdlib** builds for microcontrollers
* Feature macros, GC ownership rules, and troubleshooting

Upgrading from 1.x? [CHANGELOG.md](CHANGELOG.md) lists what changed in 2.0, including the incompatible changes.

## License

MIT. See [LICENSE](LICENSE).

# luaaa — Advanced Guide

[English](GUIDE.md) | [中文](GUIDE.zh-CN.md) · Back to [README.md](README.md)

This guide is a reference and FAQ for people who already know the basics from the [README](README.md). Each section stands on its own — jump to what you need.

1. [Type conversion & `LuaStack`](#type-conversion--luastack)
2. [Teaching luaaa your own types](#teaching-luaaa-your-own-types)
3. [Constructors in depth](#constructors-in-depth)
4. [Overloaded functions & disambiguation](#overloaded-functions--disambiguation)
5. [Callbacks in depth](#callbacks-in-depth)
6. [Properties in depth](#properties-in-depth)
7. [`def`: constants, arrays & embedded instances](#def-constants-arrays--embedded-instances)
8. [Metamethods](#metamethods)
9. [Errors & exceptions](#errors--exceptions)
10. [Lua-side inheritance in depth](#lua-side-inheritance-in-depth)
11. [C++ inheritance](#c-inheritance)
12. [Smart pointers, `std::optional` & move-only types](#smart-pointers-stdoptional--move-only-types)
13. [Multiple `lua_State`s](#multiple-lua_states)
14. [Embedded / no-stdlib builds](#embedded--no-stdlib-builds)
15. [Feature macros](#feature-macros)
16. [String lifetime: `const char*` vs `std::string`](#string-lifetime-const-char-vs-stdstring)
17. [Object lifetime & GC ownership](#object-lifetime--gc-ownership)
18. [Compatibility notes](#compatibility-notes)
19. [Troubleshooting / FAQ](#troubleshooting--faq)

---

## Type conversion & `LuaStack`

Every value that crosses the C++ ⇄ Lua boundary goes through `LuaStack<T>`, which has two jobs: `get` (Lua → C++) and `put` (C++ → Lua). luaaa ships specializations for:

| Category | Types |
|---|---|
| Floating | `float`, `double`, `long double` |
| Enumerations | any `enum` / `enum class` — converted through its underlying integer type |
| Boolean | `bool` |
| Integers | `int`, `long`, `long long`, `short`, `char` and their `unsigned` forms — and therefore every alias (`size_t`, `int64_t`, `uint32_t`, `ptrdiff_t`, …) |
| Strings | `const char*`, `char*`, `std::string`, `std::string_view` (C++17) |
| Sequence containers | `std::array`, `vector`, `deque`, `list`, `forward_list` |
| Sets | `set`, `multiset`, `unordered_set`, `unordered_multiset` |
| Maps | `map`, `multimap`, `unordered_map`, `unordered_multimap` |
| Tuple-like | `std::pair`, `std::tuple` |
| Lua handle | `lua_State*` (receives the current state; consumes no argument) |
| Bound classes | `T`, `T&`, `const T&`, `T*` for any class you exported with `LuaClass` — and for classes derived from it (see [C++ inheritance](#c-inheritance)) |
| Smart pointers | `std::shared_ptr<T>`, `std::unique_ptr<T, D>` of a bound class (see [Smart pointers](#smart-pointers-stdoptional--move-only-types)) |
| Optional | `std::optional<T>` (C++17): `nil` ⇄ `std::nullopt` |

Containers map to Lua tables: sequences become array-style tables (`{1,2,3}`), maps become key/value tables (`{a=1}`), `pair`/`tuple` become positional tables (`{first, second}`).

Going the other way, sequences **and sets** are read like `ipairs` walks a table: `t[1]`, `t[2]`, … up to the first `nil`. The order is therefore always the array order, however the table was built, and values stored under non-array keys (`{ 'a', 'b', x = 'c' }`) are not swept in. Maps read every key/value pair.

> ⚠️ **Integer precision across Lua versions.** All integer types are converted through `lua_Integer`. On Lua 5.3+ that's 64-bit, so `int`/`long`/`size_t` round-trip exactly. On **Lua 5.1/5.2 and LuaJIT** numbers are doubles, so any integer beyond 2⁵³ loses precision — keep large 64-bit integers as strings if you need them exact there.
>
> ⚠️ **`uint64_t` travels as its bit pattern.** An unsigned type as wide as `lua_Integer` has values a `lua_Integer` cannot hold as such, so in **both** directions the bits are passed through unchanged: C++ `UINT64_MAX` arrives in Lua as `-1`, `uint64_t(1) << 63` as `math.mininteger`, and passing `-1` to a `uint64_t` parameter gives `UINT64_MAX`. Every value therefore round-trips; use `math.ult` / `string.format('%u', v)` on the Lua side when you need the unsigned reading. Only an integer type *wider* than `lua_Integer` can overflow on the way out, and then raises.
>
> ⚠️ **Integer arguments are range-checked.** A Lua number that does not fit the C++ integer parameter it is passed to raises a Lua error (`number overflow`) instead of wrapping: `300` into a `uint8_t`, `2^40` into an `int`, `-1` into an `unsigned int`. One deliberate exception: an **unsigned type as wide as `lua_Integer`** (`uint64_t` on 5.3+) still accepts a negative value as its bit pattern — that is the only way a script can spell a 64-bit mask such as `0xFFFFFFFFFFFFFFFF` (which *is* `-1` as a `lua_Integer`). Define `LUAAA_CHECK_INTEGER_RANGE` as `0` to get the old silent `static_cast` wrap-around back.
>
> ⚠️ **Floating-point overflow is checked.** Every FP value is converted through `lua_Number`. If a value can't fit — a huge `long double` pushed into a `double`-typed `lua_Number`, or a `double`-valued Lua number read back into a `float` — luaaa raises a Lua error rather than silently producing `inf`. Only a *finite* value that does not fit is an overflow: an actual infinity (`math.huge`, or `inf` returned from C++) is a legal value of every floating type and passes through in both directions, as does NaN. Precision loss *within* range (mantissa truncation, or any FP on Lua 5.1/5.2/LuaJIT) is inherent to Lua and is not reported.
>
> **Enumerations** are plain numbers on the Lua side. A parameter of enum type takes a number and gets the checks of the underlying type (a non-number is a type error; `300` into an `enum class Mode : uint8_t` is a `number overflow`), but a value inside that range that names no enumerator is not rejected — C++ does not reject it either. Export the enumerators as constants with `def`: `LuaModule(L, "Mode").def("Auto", Mode::Auto)`. A `LuaStack<MyEnum>` specialization of your own (e.g. to accept names as strings) still takes precedence.
>
> Other non-arithmetic scalars (raw structs) have no specialization; add a `LuaStack` for those (next section).

## Teaching luaaa your own types

To pass a type luaaa doesn't know, specialize `LuaStack<YourType>`. **The specialization must live in `namespace luaaa`** (GCC requires it, and it's harmless elsewhere). You typically build on the specializations that already exist.

```cpp
struct Color { int r, g, b; };   // your type; Lua sees it as { r=.., g=.., b=.. }

namespace luaaa {
    template<> struct LuaStack<Color> {
        static Color get(lua_State* L, int idx) {
            auto t = LuaStack<std::map<std::string, int>>::get(L, idx);
            return Color{ t["r"], t["g"], t["b"] };
        }
        static void put(lua_State* L, const Color& c) {
            std::map<std::string, int> t{ {"r", c.r}, {"g", c.g}, {"b", c.b} };
            LuaStack<decltype(t)>::put(L, t);
        }
    };
}
```

Now any bound function taking or returning `Color` just works. This is exactly what `example/example.cpp` does for collar colors.

## Constructors in depth

A class needs at least one constructor. There are five flavors; all can coexist under different names, and all return a fresh Lua object.

```cpp
LuaClass<Cat> cat(L, "Cat");

// 1) placement constructor — object is built inside the Lua userdata,
//    ~Cat() runs when Lua collects it. Template args = C++ ctor params.
cat.ctor<std::string>("new");            // Cat.new("Tom") -> new Cat("Tom")

// 2) factory / spawner — a static or free function returns a heap object.
//    luaaa `delete`s it on GC by default.
cat.ctor("fromShelter", &Cat::adopt);    // Cat* adopt() { return new Cat(...); }

// 3) spawner + custom deleter — your deleter is called on GC.
cat.ctor("managed", &Cat::adopt, &Cat::release);   // void release(Cat*)

// 4) spawner + nullptr deleter — Lua NEVER destroys it. Use for singletons
//    or objects owned elsewhere.
cat.ctor("shared", &Cat::instance, nullptr);

// 5) factory returning std::shared_ptr — the object is shared between C++ and
//    Lua, and can be passed to std::shared_ptr parameters (see Smart pointers).
cat.ctor("create", &Cat::create);        // std::shared_ptr<Cat> create(...)
```

A spawner is `TCLASS* (*)(ARGS...)`; a deleter is `R (*)(TCLASS*)`. Both may be static members or free functions.

**Missing arguments.** An argument left out of a constructor call reads as `nil`, as everywhere else in Lua: `Cat.new()` against `ctor<std::string>` fails with `bad argument #1 ... (string expected, got nil)`. Extra arguments are ignored.

**Name conflicts.** Two constructors registered with `ctor` under the same Lua name: the later one overwrites the earlier. When `LUAAA_CHECK_CONSTRUCTOR_NAME_CONFLICT` is `1` (the default), luaaa prints a warning to help you catch it. To have several constructors answer to **one** name, register them with `overloadCtor` instead — the call then picks one by its arguments (see [Overloaded functions](#overloaded-functions--disambiguation)):

```cpp
cat.overloadCtor<>("new").overloadCtor<std::string>("new").overloadCtor<std::string, int>("new");
```

## Overloaded functions & disambiguation

If a C++ name is overloaded, the compiler can't tell which one you mean from a bare `&Name`. Cast to the exact signature:

```cpp
bool  parse(const std::string&);
void  parse(int);

LuaModule(L, "m")
    .fun("parseStr", (bool(*)(const std::string&)) parse)
    .fun("parseInt", (void(*)(int))               parse);

struct Calc { int add(int,int); int add(int); };

LuaClass<Calc>(L, "Calc")
    .ctor()
    .fun("add2", (int(Calc::*)(int,int)) &Calc::add)
    .fun("add1", (int(Calc::*)(int))     &Calc::add);
```

Then either give each overload a distinct Lua name with `fun` (as above), or register them all under **one** name with `overload` and let luaaa pick at call time:

```cpp
LuaModule(L, "m")
    .overload("parse", (bool(*)(const std::string&)) parse)
    .overload("parse", (void(*)(int))               parse);

LuaClass<Calc>(L, "Calc")
    .overloadCtor<>().overloadCtor<int>()                 // Calc.new() / Calc.new(3)
    .overload("add", (int(Calc::*)(int,int)) &Calc::add)
    .overload("add", (int(Calc::*)(int))     &Calc::add); // c:add(1) / c:add(1, 2)
```

A call runs the **first** registered overload that the arguments fit:

* **Count** — no more arguments than the function has parameters; fewer only where the missing ones may be `nil` (`std::optional`, smart pointers, and `T*` with `LUAAA_NIL_AS_NULLPTR`).
* **Kind** — each argument must be of a Lua type the parameter can take: number / string / boolean / table (any container, `pair`, `tuple`) / function / an object of the right bound class (or a class derived from it). An integer parameter takes only a number with an integral value, so `f(int)` and `f(double)` are told apart for `1.5`; for `2` both fit and the one registered first wins. A string parameter takes only a string here (no number-to-string coercion).
* A parameter type luaaa knows nothing about (your own `LuaStack` specialization) fits anything, so register such overloads last.

If nothing fits, the call raises `no overload of 'name' matches the given arguments`. `fun` and `overload` do not mix under one name: a later `fun` replaces the whole set.

## Callbacks in depth

A bound C++ function can receive a Lua function. Declare the parameter as either a `std::function` or a raw function pointer — the two behave very differently.

```cpp
void onEach(std::function<int(int)> cb);   // (a) preferred
void onEvent(int (*cb)(const char*));      // (b) raw pointer
```

**(a) `std::function` — recommended.** Each callback captures its own reference to the Lua function (guarded by a `shared_ptr`). Therefore it can be:
* stored and called many times,
* held alongside other callbacks of the same signature,
* re-entrant (a callback may trigger another).

The Lua reference is released when the callback object and all its copies are destroyed. **The callback must not outlive its `lua_State`** (the destructor calls `luaL_unref`).

**(b) raw function pointer — limited.** A non-capturing function pointer can't carry context, so luaaa parks the `(state, ref)` in `static` slots keyed by the *signature*. Consequences:
* only **one** callback of a given signature can be live at a time — registering another overwrites it;
* it is **not** re-entrant or thread-safe;
* the reference is not released until the state is closed (it leaks meanwhile).

**Both forms** share these rules:
* The Lua function runs on the VM's **main thread**, not on whichever coroutine happened to pass it in — so a callback registered from inside a coroutine stays valid after that coroutine has finished and been collected. (On Lua 5.1/LuaJIT, which have no registry slot for the main thread, luaaa notes it when you bind a `LuaClass`/`LuaModule`; do your binding on the main state.)
* A parameter that is a reference to a bound class (`std::function<void(Cat&)>`, `const Cat&`) reaches Lua as a **non-owning alias** of the C++ object — changes made by the Lua function are visible to the caller, and the alias must not be kept after the callback returns. Pass by value (`Cat`) to hand Lua its own copy.
* The whole call — arguments in, the Lua function, the result out — runs under one **protected call**. If the Lua function raises, or an argument/return value can't be converted (e.g. it returns a string where an `int` is expected), the callback returns a default-constructed `RET()` (nothing for `void`); a Lua error never propagates through the C++ frames of whoever invoked the callback.
* A by-value parameter (`std::function<void(Cat)>`) is the callback's own copy and is **moved** into Lua.

**Learning about failed callbacks.** By default such a failure is silent (apart from `LUAAA_DEBUG`, which prints it to `stderr`). Register a handler per `lua_State` to be told:

```cpp
luaaa::setCallbackErrorHandler(L, [](lua_State*, const char* message, void* userdata) {
    log_error("lua callback failed: %s", message);
}, /*userdata=*/nullptr);
```

The handler runs on the thread that invoked the callback, *after* the failed protected call has been left — so it may also **throw a C++ exception**, which then propagates to the C++ code that invoked the callback. The callback still returns `RET()` if the handler returns normally. Pass `nullptr` to remove the handler.

Use the raw form only for a single, simple, long-lived callback (it's the only option in [embedded builds](#embedded--no-stdlib-builds)); otherwise prefer `std::function`.

## Properties in depth

`get`/`set` accept several shapes. `TCLASS` is the bound class.

**Getters** (return the property; must return non-void):
```cpp
cat.get("name", &Cat::name);                       // member:  P (Cat::*)() const
cat.get("total", &globalCount);                    // free:    P (*)()
cat.get("label", &describe);                       // free:    P (*)(const Cat&)
cat.get("w", [](Cat& c){ return c.weight(); });    // lambda:  []( [const] Cat& )->P
cat.get("k", []{ return 3.14; });                  // lambda:  []()->P
```

**Setters** (take the new value; return type ignored):
```cpp
cat.set("name", &Cat::setName);                    // member:  R (Cat::*)(P)
cat.set("flag", &setGlobalFlag);                   // free:    R (*)(P)
cat.set("w", &applyWeight);                        // free:    R (*)(Cat&, P)
cat.set("w", [](Cat& c, float v){ c.setW(v); });   // lambda:  [](Cat&, P)
cat.set("k", [](float v){ /*...*/ });              // lambda:  [](P)
```

**Access rules.** Getter only → read-only; writing raises `attempt to write Read-Only property '...'`. Setter only → write-only; reading raises `attempt to read Write-Only property '...'`. Reading an unknown property yields `nil`.

**Module properties** work the same way, but the getter/setter are free functions or lambdas with no `self` parameter (a module has no instance). A module getter must return non-void (enforced by `static_assert`).

## `def`: constants, arrays & embedded instances

`def` publishes read-only-ish data.

```cpp
// classes and modules: a scalar constant or a string
mod.def("version", 3);
mod.def("name", "shelter");

// modules only: a C array becomes a Lua array table
static const int primes[] = { 2, 3, 5, 7 };
mod.def("primes", primes, sizeof(primes)/sizeof(primes[0]));

// modules only: embed a live instance of a bound class into the module table.
// obj = nullptr default-constructs one; pass a deleter to control cleanup.
LuaClass<Cat> cat(L, "Cat"); cat.ctor();
mod.def("mascot", cat);                 // shelter.mascot is a Cat
mod.def("mascot", cat, existingCatPtr); // or embed an existing object
```

**Where a class `def` lands.** `LuaClass::def` puts the constant on the **objects** (`cat.MAX_LIVES`). For something that belongs to the class itself use `constant` and `staticFun`, which go into the class table next to the constructors:

```cpp
LuaClass<Cat>(L, "Cat")
    .ctor<std::string>()
    .def("SPECIES", "felis catus")        // Cat.new("Tom").SPECIES
    .constant("MAX_LIVES", 9)             // Cat.MAX_LIVES
    .staticFun("count", &Cat::count);     // Cat.count()   (function pointer, lambda or lua_CFunction)
```

## Metamethods

You can bind Lua metamethods with `fun`. Most (`__tostring`, `__add`, `__len`, `__eq`, …) are registered as-is:

```cpp
cat.fun("__tostring", &Cat::toString);   // print(obj) / tostring(obj)
```

Three are special because luaaa uses them internally to implement properties and GC: `__index`, `__newindex`, `__gc`. When you bind one of these, luaaa keeps its own dispatcher installed and calls **yours as a fallback**, after built-in method and property lookup. So a custom `__index` handles only keys that aren't a bound method or property; a custom `__gc` runs in addition to the object's destructor.

Property/method lookup order for an instance read (`obj.key`): bound methods & constants → registered getter → your `__index` (function or table) → *(if a setter exists)* write-only error → `nil`.

**The metatable is hidden from scripts.** `getmetatable(obj)` returns `false` and `setmetatable(obj, …)` fails, `obj.__gc` / `obj.__index` are `nil`, and methods live in a table scripts cannot reach — so a script cannot replace a method, a metamethod or the finalizer of a bound class. (Metamethods you bound yourself, e.g. `obj.__tostring`, remain readable.) If your scripts depend on `getmetatable(obj)`, define `LUAAA_PROTECT_METATABLE` as `0`. Objects made by `luaaa:extend` are Lua tables and keep an open metatable.

## Errors & exceptions

Everything that goes wrong inside a bound call surfaces as an ordinary Lua error (catch it with `pcall`):

* a wrong argument — `bad argument #2 to 'feed' (number expected, got string)`, a wrong class, a number out of range;
* a C++ exception thrown by your function, constructor or property accessor — `luaaa: function call failed: <what()>`.

Lua raises errors with `longjmp`, which runs no C++ destructors, so luaaa keeps the two worlds apart: when an argument fails to convert, the arguments converted before it (strings, containers, by-value objects) are destroyed by normal C++ unwinding *before* the Lua error is raised; and a caught C++ exception is fully handled (not left "in flight") before the Lua error is raised. Builds without exceptions (`-fno-exceptions`) skip all of this and raise directly.

Two things to know when you write conversion code yourself:
* Inside a custom `LuaStack<T>::get`, report a bad value with the usual `luaL_error` / `luaL_argerror` — or build on the shipped specializations, which already do the right thing.
* `LuaStack<T>::get` used directly in a raw `lua_CFunction` behaves exactly like the `luaL_check*` family: it raises a Lua error.

## Lua-side inheritance in depth

`luaaa:extend(Base, fields)` returns an ordinary class table: `fields` (or `{}`), plus `new`. Add methods and defaults to it at any time.

```lua
local SpecialCat = luaaa:extend(Cat, { tricks = 0 })
function SpecialCat:learn() self.tricks = self.tricks + 1 end

local a = SpecialCat:new("Felix")     -- every call makes a NEW instance...
local b = SpecialCat:new("Tom")       -- ...with its own C++ Cat
a:learn()                             -- a.tricks == 1, b.tricks == 0, SpecialCat.tricks == 0

local ShowCat = luaaa:extend(SpecialCat, { ribbons = 0 })   -- extended classes can be extended
```

* **Lookup order** on an instance: its own fields → the class table (and the classes it extends, nearest first) → the C++ class's methods and properties → the C++ class's custom `__index`.
* **Writing** `obj.key = v` calls the C++ setter if `key` is a property; otherwise it sets a field on that instance only.
* An instance can be passed wherever C++ expects the bound class — by reference (`void groom(Cat&)`), by pointer (`void groom(Cat*)`), or as a base class of it; `luaaa:base(obj)` returns the underlying C++ object, e.g. to call the method you overrode.
* Metamethods bound on the C++ class (`__tostring`, `__eq`, …) apply to instances. The instance metatable is derived from the C++ class's the first time a class is instantiated, so finish binding the C++ class before scripts start creating instances.
* The C++ object lives exactly as long as the instance table that holds it.

## C++ inheritance

Tell luaaa about a C++ base class with `inherit<Base>()`. The base must already be bound in that state.

```cpp
struct Animal { std::string name() const; virtual std::string speak() const; };
struct Dog : Animal { void learn(); };

LuaClass<Animal>(L, "Animal").ctor<std::string>().fun("name", &Animal::name).fun("speak", &Animal::speak);
LuaClass<Dog>(L, "Dog").inherit<Animal>().ctor<std::string>().fun("learn", &Dog::learn);

void pet(const Animal& a);          // accepts a Dog
```

```lua
local d = Dog.new("Rex")
d:name()        -- Animal's function, found on a Dog
d:speak()       -- virtual: Dog's override runs
pet(d)          -- a Dog where an Animal is expected
```

* An object of the derived class is accepted wherever the base is expected: `Base&`, `const Base&`, `Base*`, `std::shared_ptr<Base>`, and as `self` of the base's functions. The pointer is adjusted as C++ would (`static_cast`), so multiple inheritance works — call `inherit<>()` once per base. The reverse (a base object where the derived class is expected) is rejected.
* The base's functions, constants and properties are found on derived objects. Lookup order: the class itself (functions, then properties) → its bases, in `inherit` order, depth first → the custom `__index`. What the derived class registers under the same name wins.
* Bases of the base come along: with `Puppy → Dog → Animal`, a `Puppy` is an `Animal`.
* A `Base*` / `Base&` returned from C++ is wrapped as a `Base` (luaaa does not inspect the dynamic type), so only the base's functions are visible on it.
* **Without** `inherit`, you can still bind inherited member functions directly on the derived class — `LuaClass<Dog>(L, "Dog").fun("name", &Dog::name)` works even though `name` is declared in `Animal`, and `Animal` need not be bound at all.

This is separate from [Lua-side inheritance](#lua-side-inheritance-in-depth) (`luaaa:extend`), which derives a *Lua* class from a bound class; the two combine freely.

## Smart pointers, `std::optional` & move-only types

**`std::shared_ptr<T>`** (T a bound class). Returned to Lua, the object is *shared*: the Lua object keeps a `shared_ptr` of its own and releases it when collected. Passed back to a `std::shared_ptr<T>` (or `shared_ptr<Base>`) parameter, C++ shares ownership with Lua. It is also accepted by `T&` / `T*` parameters and as `self`, like any other object.

```cpp
std::shared_ptr<Cat> adopt(const std::string& name);
void keep(std::shared_ptr<Cat> cat);            // may store it beyond the call
LuaClass<Cat>(L, "Cat").ctor("create", &Cat::create);   // a shared_ptr factory as constructor
```

Only an object that Lua holds *through a `shared_ptr`* can be passed to a `shared_ptr` parameter. One made by a placement `ctor<>()`, or returned as a raw pointer, raises `std::shared_ptr expected: this object is not held by a shared_ptr` — there is no ownership to share.

**`std::unique_ptr<T, D>`**. Returned to Lua, ownership *moves to Lua*: the deleter runs when the Lua object is collected. Passed to a `std::unique_ptr<T, D>` parameter, ownership *moves back to C++* and the Lua object becomes invalid (using it raises `invalid user data`). Only an object that came from exactly that `unique_ptr` type can be taken this way.

**`nil`** converts to an empty `shared_ptr` / `unique_ptr` and back.

**`std::optional<T>`** (C++17) maps `nil` — or a missing trailing argument — to `std::nullopt`, and anything else to a `T` (a wrong type is still an error, not `nullopt`). It is the safe way to take a nullable argument.

**Nullable `T*` parameters.** By default `nil` passed to a `T*` parameter is an argument error, so a script bug cannot hand your function a null pointer. Define `LUAAA_NIL_AS_NULLPTR` as `1` to accept `nil` (and a missing argument) as `nullptr` — a `nullptr` returned from C++ (which is `nil` in Lua) can then be passed straight back. **Every bound function taking a `T*` must then check for null.**

**Move-only return types.** A bound class with a deleted copy constructor can be returned by value: the result is *moved* into the Lua object.

## Multiple `lua_State`s

luaaa stores each type's Lua name in a **per-state registry**, so the same C++ type can be exported under **different names in different states**:

```cpp
LuaClass<Widget>(A, "Button").ctor().fun("get", &Widget::get);  // state A
LuaClass<Widget>(B, "Slider").ctor().fun("get", &Widget::get);  // state B
// Button.new():get() in A and Slider.new():get() in B both work.
```

Within a **single** state, a C++ type maps to exactly **one** Lua name. Binding the same type under a second name in the same state raises a conflict error. If you genuinely need two Lua views of one type in one state, wrap it in two distinct C++ types (e.g. trivial subclasses) — that is the reliable approach.

## Embedded / no-stdlib builds

For microcontrollers and other freestanding targets, define this **before** including the header:

```cpp
#define LUAAA_WITHOUT_CPP_STDLIB 1
#include "luaaa.hpp"
```

Typically compiled with `-fno-exceptions -fno-rtti`. See `example/embedded.cpp` for a complete, runnable node.

**Not available** in this mode (they need the STL): `std::string`, `std::function`, all STL container conversions, `std::tuple`/`std::pair`, and lambda binding (lambdas rely on `std::function`).

**Use instead:**
* `const char*` for text (fixed buffers in your classes). These are borrowed from the Lua VM — see [String lifetime](#string-lifetime-const-char-vs-stdstring) and copy into your own buffer before the call returns.
* C arrays via `def(name, array, length)` for tables.
* raw **function-pointer** callbacks (see [Callbacks](#callbacks-in-depth)).
* a `lua_State*` parameter on a method to read tables/arguments by hand:
  ```cpp
  void feedAll(lua_State* L) {
      if (lua_istable(L, -1)) {
          lua_pushnil(L);
          while (lua_next(L, -2)) {
              const char* who = LuaStack<const char*>::get(L, lua_gettop(L));
              /* ... */
              lua_pop(L, 1);
          }
      }
  }
  ```

**Provide `operator new`/`delete`.** luaaa uses placement `new` to build objects inside userdata; a freestanding runtime has no allocator. Provide your own (guard them out on hosted toolchains that already define them):
```cpp
#ifdef LUAAA_FREESTANDING
void* operator new(size_t, void* p) noexcept { return p; }
void  operator delete(void*, void*) noexcept {}
void* operator new(size_t n)            { return malloc(n); }
void  operator delete(void* p) noexcept { free(p); }
#endif
```

**RTTI.** With `-fno-rtti` luaaa can't read type names, so `not export`/conflict messages show `?` instead of the C++ type name. Everything still functions; only the diagnostics are terser.

## Feature macros

Define these **before** including `luaaa.hpp`.

| Macro | Default | Effect |
|---|---|---|
| `LUAAA_WITHOUT_CPP_STDLIB` | `0` | Drop all C++ std-lib use (embedded mode). |
| `LUAAA_FEATURE_PROPERTY` | `1` | Enable `get`/`set` properties (the `__index`/`__newindex` machinery). |
| `LUAAA_FEATURE_EXTEND` | `1` | Auto-inject `luaaa:extend` / `luaaa:base` for Lua-side inheritance. |
| `LUAAA_CHECK_CONSTRUCTOR_NAME_CONFLICT` | `1` | Warn when two constructors share a Lua name. |
| `LUAAA_CHECK_INTEGER_RANGE` | `1` | Raise a Lua error when a number does not fit the C++ integer parameter it is passed to; `0` restores the silent wrap-around. |
| `LUAAA_NIL_AS_NULLPTR` | `0` | Accept `nil` (or a missing argument) for a `T*` parameter as `nullptr` instead of raising. Bound functions must then null-check. |
| `LUAAA_PROTECT_METATABLE` | `1` | Hide the metatable of bound objects from scripts (`getmetatable(obj)` returns `false`). |
| `LUAAA_REGISTER_IN_PACKAGE_LOADED` | `1` | Also record every class / module table in `package.loaded`, so `require "name"` returns it. |
| `LUAAA_DEBUG` | `0` | Enable the `LUAAA_DUMP(L)` stack-dump helper. |

## String lifetime: `const char*` vs `std::string`

A `const char*` parameter (and `char*`, which just `const_cast`s it) is **borrowed from the Lua VM**. luaaa reads it with `lua_tostring`, which hands back a pointer into Lua's internal string storage. That pointer is valid only while the Lua value stays alive — in practice, for the duration of this bound call (the Lua arguments stay on the stack, unpopped, until your function returns). The moment the value is popped or garbage-collected, the pointer is stale.

```cpp
// BAD: stores the borrowed pointer past the call -> dangling
static const char* g_name = nullptr;
void set(const char* n) { g_name = n; }      // g_name dangles after set() returns
const char* get()       { return g_name; }   // use-after-free

// GOOD: take std::string to own a copy
void set(std::string n) { g_name = std::move(n); }   // deep-copied, safe to keep
```

**Rule of thumb:** if you need the string to outlive the call, take `std::string` — `LuaStack<std::string>` deep-copies the bytes for you. Use `const char*` only when you consume the text immediately (log it, compare it, copy it yourself) within the call.

**Embedded NUL bytes.** `std::string` (and `std::string_view`) carry their length in both directions, so binary strings such as `"a\0b"` survive the trip. A `const char*` is a C string and ends at its first NUL — in both directions.

**Embedded / no-stdlib builds** have no `std::string`, so the borrow is your only option. Copy into your own fixed buffer before the call returns, exactly as `example/embedded.cpp` does:

```cpp
class Feeder {
    char m_id[32];
public:
    explicit Feeder(const char* id) { strncpy(m_id, id, sizeof(m_id) - 1); m_id[31] = '\0'; }
    //                                                  ^^^^^^^^^^^^^^^ own copy, safe to keep
};
```

The borrow rule applies equally to keys read out of a table with `lua_next` (see [Embedded builds](#embedded--no-stdlib-builds)) and to anything returned by `LuaStack<const char*>::get` directly — consume it before the next `lua_pop`.

## Object lifetime & GC ownership

Who calls the destructor depends on how the object entered Lua:

| Creation | On GC |
|---|---|
| `ctor<Args...>()` (placement) | object lives in the userdata; `~T()` runs |
| `ctor(name, spawner)` | `delete` the returned pointer (no-op if `T` isn't destructible) |
| `ctor(name, spawner, deleter)` | your `deleter(T*)` runs |
| `ctor(name, spawner, nullptr)` | never destroyed by Lua (borrowed / singleton) |
| returned as `T*` or `T&` from a bound function | typed **non-owning alias** (full userdata, class metatable, `dtor=nullptr`); Lua never destroys it |
| returned as `T` (by value) from a bound function | typed **owning** object, moved (or copied) into the userdata; `~T()` runs on GC |
| returned as `std::shared_ptr<T>`, or made by a `shared_ptr` factory `ctor` | Lua holds one reference; released on GC, the object dies with its last owner |
| returned as `std::unique_ptr<T, D>` | owned by Lua; the deleter runs on GC — unless a `unique_ptr` parameter took it back first |

An object is finalized **at most once**. Scripts normally cannot reach `__gc` at all (the metatable is hidden — see [Metamethods](#metamethods)); where they can (through the `debug` library, or with `LUAAA_PROTECT_METATABLE=0`), calling it runs the destructor/deleter right away, the collector's later call is then a no-op, and any further use of that object raises `invalid user data` instead of touching a destroyed object. A custom deleter is stored in the object itself, so it stays callable even if the class table (and its constructors) has been collected first.

A returned `T*`/`T&` wraps a full userdata carrying the class metatable, so methods stay callable on it and the value is type-checked when passed back in (a wrong class is rejected, unlike the old untyped lightuserdata). It aliases the C++ object — keep that object alive yourself; a `nullptr` becomes Lua `nil`. A by-value `T` return instead copy-constructs into the userdata and is collected normally. A pointer/reference whose class is *not* bound in that state still degrades to a plain lightuserdata (the legacy opaque-handle pattern); a by-value object of an unbound class raises instead of dangling.

## Compatibility notes

* **Lua versions.** 5.1, 5.2, 5.3, 5.4, 5.5 and LuaJIT. The few newer API functions luaaa needs on older versions are provided as internal `LUAAA_`-prefixed helpers; luaaa defines **no** `lua_*` / `luaL_*` names of its own, so it can be combined with lua-compat-5.3 and similar libraries.
* **Modules and class tables are plain tables**, built the same way on every Lua version. A name is a path from the globals: `LuaModule(L, "game.util")` is `_G.game.util` (missing tables are created; a non-table in the way is a `name conflict` error), and the default module `"_G"` is the globals table itself. Each table is also recorded in `package.loaded` under its name, so `require "game.util"` returns it (`LUAAA_REGISTER_IN_PACKAGE_LOADED=0` turns that off). luaaa adds **nothing** else to a module table and gives it a metatable only when you register the first property (`get`/`set`) or a custom `__index`/`__newindex` on it — so binding functions into `_G` leaves `_G` untouched. A table that already carries a *foreign* metatable is left alone: module properties stay unavailable there.
* **C++ standard.** C++11 is the floor. C++14+ selects a faster `std::tuple` conversion path; a C++11 fallback is included, so tuples work either way.

## Troubleshooting / FAQ

**`cpp class 'X' not export`** — luaaa was asked to convert a type it doesn't have bound/specialized. Causes: (1) you called a method before `LuaClass<X>` was constructed in *that* state; (2) the bound signature uses a type with no specialization (e.g. a raw struct that was never bound) — bind it, or add a `LuaStack` for it; (3) you're using the object in a different `lua_State` than the one it was bound in.

**`C++ class '...' bind to conflict lua name`** — you bound the same C++ type under two names in one state. Use one name per state, separate states, or distinct wrapper types (see [Multiple states](#multiple-lua_states)).

**`lua name '...' already bound to a different cpp class`** — the reverse conflict: two *different* C++ types tried to share one lua name in one state. They would have silently shared a metatable (and passed each other's type checks), so luaaa rejects the second bind. Give each type its own name.

**A static method call `Class.method()` fails with "nil value"** — static/free functions bound via `LuaClass::fun` are invoked like methods: call `instance:method(args)`. The instance is the skipped `self`. To get a true `Class.method()` (or a constant such as `Class.MAX`), register it with `staticFun` / `constant`: `LuaClass<Cat>(L, "Cat").staticFun("count", &Cat::count).constant("MAX", 9);` — `Cat.count()` then works next to `Cat.new()`.

**`getmetatable(obj)` returns `false`** — by design since 2.0 (see [Metamethods](#metamethods)); define `LUAAA_PROTECT_METATABLE` as `0` if a script needs the real metatable.

**`no overload of '...' matches the given arguments`** — none of the functions registered with `overload` under that name accepts this argument list; see [Overloaded functions](#overloaded-functions--disambiguation) for the matching rules.

**`attempt to read Write-Only` / `write Read-Only property`** — the property has only a setter (write-only) or only a getter (read-only). Register the missing accessor if you need both.

**A lambda won't bind** — in embedded mode lambdas are unavailable (they need `std::function`); use a free function pointer. In normal mode, make sure the lambda is non-generic (no `auto` params) so its signature can be deduced.

**My callback crashes after a while** — a raw function-pointer callback is single-slot per signature and not re-entrant; a `std::function` callback must not outlive its `lua_State`. See [Callbacks](#callbacks-in-depth).

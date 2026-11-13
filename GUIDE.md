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
11. [Multiple `lua_State`s](#multiple-lua_states)
12. [Embedded / no-stdlib builds](#embedded--no-stdlib-builds)
13. [Feature macros](#feature-macros)
14. [String lifetime: `const char*` vs `std::string`](#string-lifetime-const-char-vs-stdstring)
15. [Object lifetime & GC ownership](#object-lifetime--gc-ownership)
16. [Compatibility notes](#compatibility-notes)
17. [Troubleshooting / FAQ](#troubleshooting--faq)

---

## Type conversion & `LuaStack`

Every value that crosses the C++ ⇄ Lua boundary goes through `LuaStack<T>`, which has two jobs: `get` (Lua → C++) and `put` (C++ → Lua). luaaa ships specializations for:

| Category | Types |
|---|---|
| Floating | `float`, `double`, `long double` |
| Enumerations | any `enum` / `enum class` — converted through its underlying integer type |
| Boolean | `bool` |
| Integers | `int`, `long`, `long long`, `short`, `char` and their `unsigned` forms — and therefore every alias (`size_t`, `int64_t`, `uint32_t`, `ptrdiff_t`, …) |
| Strings | `const char*`, `char*`, `std::string` |
| Sequence containers | `std::array`, `vector`, `deque`, `list`, `forward_list` |
| Sets | `set`, `multiset`, `unordered_set`, `unordered_multiset` |
| Maps | `map`, `multimap`, `unordered_map`, `unordered_multimap` |
| Tuple-like | `std::pair`, `std::tuple` |
| Lua handle | `lua_State*` (receives the current state; consumes no argument) |
| Bound classes | `T`, `T&`, `const T&`, `T*` for any class you exported with `LuaClass` |

Containers map to Lua tables: sequences become array-style tables (`{1,2,3}`), maps become key/value tables (`{a=1}`), `pair`/`tuple` become positional tables (`{first, second}`).

Going the other way, sequences **and sets** are read like `ipairs` walks a table: `t[1]`, `t[2]`, … up to the first `nil`. The order is therefore always the array order, however the table was built, and values stored under non-array keys (`{ 'a', 'b', x = 'c' }`) are not swept in. Maps read every key/value pair.

> ⚠️ **Integer precision across Lua versions.** All integer types are converted through `lua_Integer`. On Lua 5.3/5.4 that's 64-bit, so `int`/`long`/`size_t` round-trip exactly. On **Lua 5.1/5.2 and LuaJIT** numbers are doubles, so any integer beyond 2⁵³ loses precision. Also, an `unsigned` 64-bit value ≥ 2⁶³ maps to a negative `lua_Integer` even on 5.3+. This is an inherent Lua limitation — keep large 64-bit integers as strings if you need them exact on older Lua.
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

A class needs at least one constructor. There are four flavors; all can coexist under different names, and all return a fresh Lua object.

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
```

A spawner is `TCLASS* (*)(ARGS...)`; a deleter is `R (*)(TCLASS*)`. Both may be static members or free functions.

**Missing arguments.** An argument left out of a constructor call reads as `nil`, as everywhere else in Lua: `Cat.new()` against `ctor<std::string>` fails with `bad argument #1 ... (string expected, got nil)`. Extra arguments are ignored.

**Name conflicts.** Two constructors with the same Lua name: the later one overwrites the earlier. When `LUAAA_CHECK_CONSTRUCTOR_NAME_CONFLICT` is `1` (the default), luaaa prints a warning to help you catch it.

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

Give each overload a distinct Lua name (Lua has no overloading).

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
* The whole call — arguments in, the Lua function, the result out — runs under one **protected call**. If the Lua function raises, or an argument/return value can't be converted (e.g. it returns a string where an `int` is expected), the callback returns a default-constructed `RET()` (nothing for `void`); a Lua error never propagates through the C++ frames of whoever invoked the callback. Define `LUAAA_DEBUG` to have the swallowed message printed to `stderr`.

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

## Metamethods

You can bind Lua metamethods with `fun`. Most (`__tostring`, `__add`, `__len`, `__eq`, …) are registered as-is:

```cpp
cat.fun("__tostring", &Cat::toString);   // print(obj) / tostring(obj)
```

Three are special because luaaa uses them internally to implement properties and GC: `__index`, `__newindex`, `__gc`. When you bind one of these, luaaa keeps its own dispatcher installed and calls **yours as a fallback**, after built-in method and property lookup. So a custom `__index` handles only keys that aren't a bound method or property; a custom `__gc` runs in addition to the object's destructor.

Property/method lookup order for an instance read (`obj.key`): bound methods & constants → registered getter → your `__index` (function or table) → *(if a setter exists)* write-only error → `nil`.

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
* An instance can be passed wherever C++ expects the bound class by reference (`void groom(Cat&)`); `luaaa:base(obj)` returns the underlying C++ object, e.g. to call the method you overrode.
* Metamethods bound on the C++ class (`__tostring`, `__eq`, …) apply to instances. The instance metatable is derived from the C++ class's the first time a class is instantiated, so finish binding the C++ class before scripts start creating instances.
* The C++ object lives exactly as long as the instance table that holds it.

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
| returned as `T` (by value) from a bound function | typed **owning copy**; `~T()` runs on GC |

An object is finalized **at most once**. `__gc` is reachable from scripts (`obj:__gc()`); calling it runs the destructor/deleter right away, the collector's later call is then a no-op, and any further use of that object raises `invalid user data` instead of touching a destroyed object. A custom deleter is stored in the object itself, so it stays callable even if the class table (and its constructors) has been collected first.

A returned `T*`/`T&` wraps a full userdata carrying the class metatable, so methods stay callable on it and the value is type-checked when passed back in (a wrong class is rejected, unlike the old untyped lightuserdata). It aliases the C++ object — keep that object alive yourself; a `nullptr` becomes Lua `nil`. A by-value `T` return instead copy-constructs into the userdata and is collected normally. A pointer/reference whose class is *not* bound in that state still degrades to a plain lightuserdata (the legacy opaque-handle pattern); a by-value object of an unbound class raises instead of dangling.

## Compatibility notes

* **Lua versions.** 5.1, 5.2, 5.3, 5.4, 5.5 and LuaJIT. For 5.1/LuaJIT, luaaa supplies the few 5.2+ helpers it needs (`luaL_setfuncs`, `lua_rawgetp`, and `lua_tonumberx`/`lua_tointegerx` wrappers for PUC 5.1, which lacks them — LuaJIT has its own).
* **Module registry.** On Lua > 5.1 without `LUA_COMPAT_MODULE`, modules are created as plain global tables (the modern style); otherwise the legacy `luaL_openlib` path is used. This is automatic. Module properties work on both paths, including modules bound onto a pre-existing table (e.g. the default `_G` module, or a table the script created first); the one exception is a table that already carries a *foreign* metatable installed by someone else — luaaa leaves it untouched and module properties stay unavailable there.
* **C++ standard.** C++11 is the floor. C++14+ selects a faster `std::tuple` conversion path; a C++11 fallback is included, so tuples work either way.

## Troubleshooting / FAQ

**`cpp class 'X' not export`** — luaaa was asked to convert a type it doesn't have bound/specialized. Causes: (1) you called a method before `LuaClass<X>` was constructed in *that* state; (2) the bound signature uses a type with no specialization (e.g. a raw struct that was never bound) — bind it, or add a `LuaStack` for it; (3) you're using the object in a different `lua_State` than the one it was bound in.

**`C++ class '...' bind to conflict lua name`** — you bound the same C++ type under two names in one state. Use one name per state, separate states, or distinct wrapper types (see [Multiple states](#multiple-lua_states)).

**`lua name '...' already bound to a different cpp class`** — the reverse conflict: two *different* C++ types tried to share one lua name in one state. They would have silently shared a metatable (and passed each other's type checks), so luaaa rejects the second bind. Give each type its own name.

**A static method call `Class.method()` fails with "nil value"** — static/free functions bound via `LuaClass::fun` are invoked like methods: call `instance:method(args)`. The instance is the skipped `self`. To get a true `Class.method()` (or a constant such as `Class.MAX`), put it on the class table with a module of the same name: `LuaModule(L, "Cat").fun("count", &Cat::count).def("MAX", 9);` — `Cat.count()` then works next to `Cat.new()`, in either binding order.

**`attempt to read Write-Only` / `write Read-Only property`** — the property has only a setter (write-only) or only a getter (read-only). Register the missing accessor if you need both.

**A lambda won't bind** — in embedded mode lambdas are unavailable (they need `std::function`); use a free function pointer. In normal mode, make sure the lambda is non-generic (no `auto` params) so its signature can be deduced.

**My callback crashes after a while** — a raw function-pointer callback is single-slot per signature and not re-entrant; a `std::function` callback must not outlive its `lua_State`. See [Callbacks](#callbacks-in-depth).

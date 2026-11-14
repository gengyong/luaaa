# Changelog

[English](CHANGELOG.md) | [中文](CHANGELOG.zh-CN.md)

## 2.0

`LUAAA_VER_MAJOR` / `LUAAA_VER_MINOR` are now `2` / `0`. This release collects review rounds 7 – 10 (see `tests/REVIEW.md` for the per-item record). It contains behaviour changes; read the first section before upgrading.

### Incompatible changes

| Change | Before | Now | To get the old behaviour |
|---|---|---|---|
| `T&` / `const T&` returns | a copy of the object | a non-owning alias of it | return by value |
| `luaaa:extend` | every `new()` returned the same table | a new instance per `new()`; extended classes can be extended | — |
| Sequence containers and sets read from Lua | `lua_next` over the table | `t[1]`, `t[2]`, … up to the first `nil` | — |
| Integer arguments | wrapped silently when out of range | `number overflow` error | `LUAAA_CHECK_INTEGER_RANGE=0` |
| `uint64_t` (unsigned, as wide as `lua_Integer`) return values | ≥ 2⁶³ inconsistent: some wrapped, `UINT64_MAX` raised | always passed as the bit pattern, symmetric with arguments (`UINT64_MAX` ⇄ `-1`) | — |
| Metatable of bound objects | `getmetatable(obj)` returned it; `obj.__gc`, `obj.__index` readable; scripts could replace methods | hidden: `getmetatable(obj)` is `false`, methods live in a separate table | `LUAAA_PROTECT_METATABLE=0` |
| `obj:__gc()` from a script | ran the finalizer | `__gc` is not reachable (`nil`) | `LUAAA_PROTECT_METATABLE=0`, then `getmetatable(obj).__gc(obj)` |
| Module / class names containing `.` on Lua 5.2+ | a global literally named `"a.b"` | the nested table `_G.a.b` (as on 5.1) | — |
| `LuaModule` | set a metatable and a `__name` field on every module table, including `_G` | adds neither; a metatable appears only with the first property or custom `__index` / `__newindex` | — |
| Module property accessors | stored in the module table as `"<name"` / `">name"` | not visible in the module table | — |
| `package.loaded` on Lua 5.2+ | untouched | class / module tables are recorded under their names | `LUAAA_REGISTER_IN_PACKAGE_LOADED=0` |
| `LUAAA_FEATURE_PROPERTY=0` with a custom `__index` | the handler replaced method lookup entirely | methods first, then the handler (same as with properties enabled) | — |
| `std::string` with embedded NUL | truncated at the first NUL in both directions | preserved | use `const char*` |
| User `__index` / `__newindex` / `__gc` handlers bound as a raw `lua_CFunction` | called with the metatable as an extra last argument | called with `(self, key)`, `(self, key, value)`, `(self)` | — |
| Global helper names on Lua 5.1 / LuaJIT | the header defined `luaL_setfuncs`, `luaL_setmetatable`, `lua_rawgetp`, `lua_rawsetp` | it defines no `lua_*` / `luaL_*` names | define them yourself if your code relied on them |
| Internal macros | `USE_NEW_MODULE_REGISTRY`, `RTTI_CLASS_NAME`, … leaked to the includer | `LUAAA_`-prefixed and `#undef`'d | — |
| `LuaClass<T>::UserDataDetail` | a per-class struct with a typed `obj` | one shared struct (`luaaa_detail::UserData`) with `void* obj` | — |

### New

* **C++ inheritance** — `LuaClass<Dog>(L, "Dog").inherit<Animal>()`: derived objects are accepted wherever the base is expected (with pointer adjustment, so multiple inheritance works) and see the base's functions, constants and properties. Inherited member functions can also be bound directly on the derived class without binding the base.
* **Overloads** — `overload(name, f)` on classes and modules and `overloadCtor<Args...>(name)` register several functions under one Lua name; the call picks one by argument count and kind.
* **Smart pointers** — `std::shared_ptr<T>` and `std::unique_ptr<T, D>` of bound classes as parameters and return values; `ctor(name, factory)` accepts a factory returning `std::shared_ptr<T>`.
* **Move-only return types** — a bound class with a deleted copy constructor can be returned by value.
* **C++17 types** — `std::optional<T>` (`nil` ⇄ `nullopt`) and `std::string_view`.
* **`enum` / `enum class`** convert through their underlying integer type.
* **Class-table members** — `LuaClass::staticFun(name, f)` and `LuaClass::constant(name, value)` (`Cat.count()`, `Cat.MAX`).
* **Callback error handler** — `luaaa::setCallbackErrorHandler(L, handler, userdata)` is told when a Lua function called back from C++ fails; the handler may throw.
* **`LUAAA_NIL_AS_NULLPTR`** (default `0`) — accept `nil` for `T*` parameters as `nullptr`.
* A luaaa:extend instance can be passed to `T*` parameters (not only `T&`).
* Module `__index` / `__newindex` handlers now also work on Lua 5.1 / LuaJIT.

### Fixed

* Memory safety: idempotent `__gc`; custom deleters stored in the object (use-after-free when the class table was collected first); callbacks run entirely inside one protected call and on the main thread.
* A failed argument conversion unwinds the C++ stack before the Lua error is raised; C++ exceptions are fully handled before `lua_error`.
* Infinity passes through floating-point parameters and return values.
* Constructor calls with missing arguments report `got nil` instead of `got userdata`.
* `T*` parameter errors name the Lua class and the real argument position.
* On LuaJIT, a Lua error raised inside a property accessor or a callback conversion keeps its own message instead of `unknown C++ exception`.
* A `LuaModule` named like a class no longer clobbers the class metatable; numeric keys are no longer converted to strings by `__index` / `__newindex`.
* Arguments are converted strictly left to right before the call. On MSVC below C++17, a by-value argument was not destroyed when a later one failed to convert, and on x64 a failed conversion could crash while the Lua error unwound the stack.
* Lua 5.1 / 5.2 on 32-bit targets: integers convert through `long long` instead of the 32-bit `lua_Integer`, so values beyond 2³¹ are no longer clamped (an `int64_t` argument of 2⁴⁰ used to arrive as 2147483647) and integer range checks follow the same rules as on 64-bit targets.

### Performance

Measured with `-O2`, Lua 5.5, arm64 (ns per call, before → after):

| | 1.5 | 2.0 |
|---|---|---|
| method call, class without properties | 62 | 28 |
| method call, class with properties | 61 | 46 |
| property read | 95 | 57 |
| property write | 92 | 50 |
| free function | 17 – 18 | 15 |
| function taking a bound object | 43 | 23 |

The gains come from recognizing a class by a type tag in its metatable (no name lookup per conversion), from a plain-table `__index` for classes that have no property, and from property accessors found by key instead of a formatted `"<name"` string.

### Engineering

* GitHub Actions CI: gcc and clang on Linux (LuaJIT, 5.1 – 5.4, C++11/14/17/20, ASan + UBSan, TSan), clang on macOS, and every PUC Lua release line built from source on Linux / macOS / Windows (MSVC) through `tests/CMakeLists.txt`.
* `tests/run.sh all` runs every detected Lua × C++11/14/17/20 in parallel and no longer assumes `/opt/homebrew`.
* Tests that assert a known-wrong behaviour are marked `KNOWN-LIMITATION`.
* The header's duplicated invoker and container templates were merged.

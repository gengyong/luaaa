# luaaa —— 进阶指南

[English](GUIDE.md) | [中文](GUIDE.zh-CN.md) · 返回 [README.zh-CN.md](README.zh-CN.md)

本指南是参考手册和常见问题解答，适合已经读过 [README](README.zh-CN.md) 基础内容的朋友。各章节互相独立，按需跳转即可。

1. [类型转换与 `LuaStack`](#类型转换与-luastack)
2. [教会 luaaa 你自己的类型](#教会-luaaa-你自己的类型)
3. [构造函数详解](#构造函数详解)
4. [重载函数与消歧](#重载函数与消歧)
5. [回调详解](#回调详解)
6. [属性详解](#属性详解)
7. [`def`：常量、数组与内嵌实例](#def常量数组与内嵌实例)
8. [元方法](#元方法)
9. [错误与异常](#错误与异常)
10. [Lua 侧继承详解](#lua-侧继承详解)
11. [C++ 继承](#c-继承)
12. [智能指针、`std::optional` 与只能移动的类型](#智能指针stdoptional-与只能移动的类型)
13. [多个 `lua_State`](#多个-lua_state)
14. [嵌入式 / 无标准库构建](#嵌入式--无标准库构建)
15. [特性宏](#特性宏)
16. [字符串生命周期：`const char*` 与 `std::string`](#字符串生命周期const-char-与-stdstring)
17. [对象生命周期与 GC 所有权](#对象生命周期与-gc-所有权)
18. [兼容性说明](#兼容性说明)
19. [排错 / FAQ](#排错--faq)

---

## 类型转换与 `LuaStack`

所有在 C++ 和 Lua 之间传递的值，都要经过 `LuaStack<T>` 来处理。它只做两件事：`get`（从 Lua 取数据转成 C++ 类型）和 `put`（从 C++ 推数据到 Lua）。luaaa 已经内置了以下类型的转换：

| 类别 | 类型 |
|---|---|
| 浮点数 | `float`、`double`、`long double` |
| 枚举 | 任意 `enum` / `enum class` —— 按其底层整数类型转换 |
| 布尔 | `bool` |
| 整数 | `int`、`long`、`long long`、`short`、`char` 以及它们对应的 `unsigned` 版本 —— 所以各种别名（`size_t`、`int64_t`、`uint32_t`、`ptrdiff_t` 等）也都天然支持 |
| 字符串 | `const char*`、`char*`、`std::string`、`std::string_view`（C++17） |
| 顺序容器 | `std::array`、`vector`、`deque`、`list`、`forward_list` |
| 集合 | `set`、`multiset`、`unordered_set`、`unordered_multiset` |
| 映射 | `map`、`multimap`、`unordered_map`、`unordered_multimap` |
| 元组类 | `std::pair`、`std::tuple` |
| Lua 状态机指针 | `lua_State*`（用来获取当前 state 的指针；不消耗函数参数） |
| 已绑定的类 | 任何用 `LuaClass` 导出的类的 `T`、`T&`、`const T&`、`T*` —— 也包括它的派生类（见 [C++ 继承](#c-继承)） |
| 智能指针 | 已绑定类的 `std::shared_ptr<T>`、`std::unique_ptr<T, D>`（见[智能指针](#智能指针stdoptional-与只能移动的类型)） |
| 可选值 | `std::optional<T>`（C++17）：`nil` ⇄ `std::nullopt` |

容器的转换规则：顺序容器在 Lua 侧表现为数组式表（`{1,2,3}`），映射变为键值表（`{a=1}`），`pair`/`tuple` 变为按位置排列的表（`{first, second}`）。

反方向读取时，顺序容器**和集合**都按 `ipairs` 的方式读表：`t[1]`、`t[2]`…… 直到第一个 `nil`。所以无论表是怎么构造出来的，顺序永远是数组顺序；存在非数组键下的值（`{ 'a', 'b', x = 'c' }` 里的 `'c'`）也不会被混进来。映射则读取全部键值对。

> ⚠️ **不同 Lua 版本的整数精度。** 所有整数类型都经过 `lua_Integer` 转换。Lua 5.3+ 上它是 64 位，`int`/`long`/`size_t` 可以精确往返；**Lua 5.1/5.2 和 LuaJIT** 的数字是 double，超过 2⁵³ 的整数会丢精度 —— 在这些版本上需要精确的大整数请用字符串传递。
>
> ⚠️ **`uint64_t` 按位传递。** 与 `lua_Integer` 等宽的无符号类型有一半的值 `lua_Integer` 表示不了，所以**两个方向**都原样传递二进制位：C++ 的 `UINT64_MAX` 到 Lua 里是 `-1`，`uint64_t(1) << 63` 是 `math.mininteger`，把 `-1` 传给 `uint64_t` 参数得到 `UINT64_MAX`。这样任何值都能往返；Lua 侧需要按无符号解释时用 `math.ult` / `string.format('%u', v)`。只有比 `lua_Integer` 更宽的整数类型在出参时才可能溢出，此时会报错。
>
> ⚠️ **整数入参会做范围检查。** 如果 Lua 数值放不进它要传给的 C++ 整数参数，会抛 Lua 错误（`number overflow`），而不是静默回绕：比如把 `300` 传给 `uint8_t`、把 `2^40` 传给 `int`、把 `-1` 传给 `unsigned int`。有一个刻意保留的例外：**与 `lua_Integer` 等宽的无符号类型**（5.3+ 上的 `uint64_t`）仍然接受负值并按位解释 —— 因为脚本只能这样写出 `0xFFFFFFFFFFFFFFFF` 这类 64 位掩码（它作为 `lua_Integer` 就是 `-1`）。把 `LUAAA_CHECK_INTEGER_RANGE` 定义为 `0` 可以恢复原先的静默 `static_cast` 回绕。
>
> ⚠️ **浮点溢出会被检测。** 每个浮点值都会经过 `lua_Number` 来转换。如果值超出了目标类型能表示的范围 —— 比如把一个很大的 `long double` 塞进 `double` 型的 `lua_Number`，或者把 double 值的 Lua number 读到 `float` 里 —— luaaa 会直接报 Lua 错误，而不是悄悄生成一个 `inf`。只有「有限但放不下」的值才算溢出：真正的无穷大（`math.huge`，或者 C++ 返回的 `inf`）是每种浮点类型都能表示的合法值，两个方向都会原样通过，NaN 也一样。对于范围以内但精度有损的情况（比如尾数截断，以及在 Lua 5.1/5.2/LuaJIT 上的任何浮点操作），这是 Lua 本身决定的，不会额外报错。
>
> **枚举**在 Lua 侧就是普通数字。枚举类型的参数接收一个数字，并沿用底层类型的检查（传入非数字是类型错误；把 `300` 传给 `enum class Mode : uint8_t` 会报 `number overflow`）。但落在底层类型范围内、却不对应任何枚举项的值不会被拒绝 —— C++ 本身也不拒绝。枚举项可以用 `def` 导出为常量：`LuaModule(L, "Mode").def("Auto", Mode::Auto)`。如果你自己写了 `LuaStack<MyEnum>` 特化（比如想接受字符串形式的名字），仍然以你的为准。
>
> 其他非算术类型的标量（如裸结构体）没有内置特化，需要自己写 `LuaStack`（见下一节）。

## 教会 luaaa 你自己的类型

如果要传递 luaaa 不认识的类型，就为它写一个 `LuaStack<你的类型>` 的模板特化。**这个特化必须放在 `namespace luaaa` 里面**（GCC 强制要求，其他编译器不写也不影响）。通常你可以在已有的特化基础上搭建。

```cpp
struct Color { int r, g, b; };   // 自定义类型；Lua 侧看到的是 { r=.., g=.., b=.. }

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

这样，任何接收或返回 `Color` 的绑定函数都可以直接使用了。`example/example.cpp` 里的项圈颜色就是按这种方式实现的。

## 构造函数详解

每个类必须要至少有一个构造函数。构造函数分为五种形态，彼此可以用不同名字共存，而且每种都会返回一个新的 Lua 对象。

```cpp
LuaClass<Cat> cat(L, "Cat");

// 1) placement 构造 —— 对象直接在 Lua 的 userdata 内存块里创建，
//    Lua 回收时自动调用 ~Cat()。模板参数 = C++ 构造函数的参数类型。
cat.ctor<std::string>("new");            // Cat.new("Tom") -> new Cat("Tom")

// 2) 工厂  / spawner —— 静态函数或自由函数返回一个堆上的对象。
//    默认情况下，luaaa 在 GC 时直接 delete 它。
cat.ctor("fromShelter", &Cat::adopt);    // Cat* adopt() { return new Cat(...); }

// 3) spawner + 自定义析构器 —— GC 时调用你的析构器。
cat.ctor("managed", &Cat::adopt, &Cat::release);   // void release(Cat*)

// 4) spawner + nullptr 析构器 —— Lua 永不销毁它。用于单例
//    或由别处持有的对象。
cat.ctor("shared", &Cat::instance, nullptr);

// 5) 返回 std::shared_ptr 的工厂函数 —— 对象由 C++ 与 Lua 共享，
//    可以传给 std::shared_ptr 参数（见「智能指针」）。
cat.ctor("create", &Cat::create);        // std::shared_ptr<Cat> create(...)
```

spawner 的类型是 `TCLASS* (*)(ARGS...)`；析构器是 `R (*)(TCLASS*)`。两者都可以是静态成员或自由函数。

**缺少参数。** 调用构造函数时少传的参数按 `nil` 处理，和 Lua 里其他地方一样：对 `ctor<std::string>` 调用 `Cat.new()` 会报 `bad argument #1 ... (string expected, got nil)`。多传的参数会被忽略。

**命名冲突。** 用 `ctor` 注册的两个构造函数如果用了同一个 Lua 名字，后注册的会覆盖先注册的。`LUAAA_CHECK_CONSTRUCTOR_NAME_CONFLICT` 为 `1`（默认）时，luaaa 会打印一条警告帮你发现它。想让多个构造函数共用**一个**名字，改用 `overloadCtor` 注册 —— 调用时按实参挑选（见[重载函数](#重载函数与消歧)）：

```cpp
cat.overloadCtor<>("new").overloadCtor<std::string>("new").overloadCtor<std::string, int>("new");
```

## 重载函数与消歧

C++ 的函数名被重载以后，编译器没法单凭一个 `&函数名` 判断你要的是哪个版本。这时候需要显式地把函数指针转成确定的签名：

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

消歧之后有两种做法：像上面那样用 `fun` 给每个版本起一个不同的 Lua 名字，或者用 `overload` 把它们注册到**同一个**名字下，由 luaaa 在调用时挑选：

```cpp
LuaModule(L, "m")
    .overload("parse", (bool(*)(const std::string&)) parse)
    .overload("parse", (void(*)(int))               parse);

LuaClass<Calc>(L, "Calc")
    .overloadCtor<>().overloadCtor<int>()                 // Calc.new() / Calc.new(3)
    .overload("add", (int(Calc::*)(int,int)) &Calc::add)
    .overload("add", (int(Calc::*)(int))     &Calc::add); // c:add(1) / c:add(1, 2)
```

调用时执行**第一个**与实参相符的重载（按注册顺序）：

* **个数** —— 实参不多于形参；只有缺的那几个形参可以是 `nil` 时才允许更少（`std::optional`、智能指针，以及开启 `LUAAA_NIL_AS_NULLPTR` 时的 `T*`）。
* **种类** —— 每个实参的 Lua 类型要是该形参能接收的：number / string / boolean / table（各种容器、`pair`、`tuple`）/ function / 对应绑定类（或其派生类）的对象。整数形参只接收值为整数的 number，所以 `f(int)` 和 `f(double)` 对 `1.5` 能分开；对 `2` 两者都符合，先注册的胜出。这里字符串形参只接收 string（不做数字到字符串的隐式转换）。
* luaaa 不认识的形参类型（你自己写的 `LuaStack` 特化）与任何实参都相符，这类重载请放在最后注册。

都不相符时报 `no overload of 'name' matches the given arguments`。同一个名字下 `fun` 与 `overload` 不能混用：之后再调 `fun` 会替换掉整组重载。

## 回调详解

绑定的 C++ 函数可以接收 Lua 函数作为回调。声明参数时，可以选 `std::function` 或裸函数指针 —— 但这两种方式的差异非常大。

```cpp
void onEach(std::function<int(int)> cb);   // (a) 推荐的方式
void onEvent(int (*cb)(const char*));      // (b) 裸函数指针
```

**(a) `std::function` —— 推荐。** 每个回调会各自独立地持有对 Lua 函数的引用（背后用 `shared_ptr` 维护）。这也意味着它可以：
* 存起来反复调用；
* 和其他同签名的回调同时存在，互不干扰；
* 支持重入（一个回调内部再触发另一个也完全没问题）。

只有当回调对象本身和它的所有副本都被销毁后，对应的 Lua 引用才会被释放。**有一点要注意：回调绝对不能比它所在的 `lua_State` 活得更久**（因为析构时要调 `luaL_unref`）。

**(b) 裸函数指针 —— 功能受限。** 无捕获的函数指针没办法携带额外的上下文，所以 luaaa 只能在内部用*按签名索引*的 `static` 槽位来存放 `(state, ref)` 信息。这会带来几个限制：
* 同一个签名在同一时刻最多只能有**一个**有效回调 —— 再注册一个新的就会把旧的覆盖掉；
* **不支持**重入，也不是线程安全的；
* Lua 的引用要等到整个 state 关闭时才会释放（在此之前算泄漏）。

**两种形式**都遵循下面几条规则：
* Lua 函数运行在虚拟机的**主线程**上，而不是恰好把它传进来的那个协程 —— 所以在协程里注册的回调，在该协程结束并被回收之后依然有效。（Lua 5.1/LuaJIT 的 registry 里没有主线程槽位，luaaa 会在你绑定 `LuaClass`/`LuaModule` 时记下主线程；请在主 state 上完成绑定。）
* 如果参数是已绑定类的引用（`std::function<void(Cat&)>`、`const Cat&`），Lua 拿到的是该 C++ 对象的**非拥有别名** —— Lua 函数里做的修改调用方看得见，回调返回之后不能再保留这个别名。想让 Lua 拿到自己的一份拷贝，就按值传（`Cat`）。
* 整个调用过程 —— 实参转换、Lua 函数本身、返回值转换 —— 都在同一次**保护调用**里完成。如果 Lua 函数抛错，或者某个实参/返回值无法转换（例如该返回 `int` 的地方返回了字符串），回调会返回默认构造的 `RET()`（`void` 则什么都不返回）；Lua 错误绝不会穿过调用回调的那段 C++ 代码的栈帧。
* 按值传递的参数（`std::function<void(Cat)>`）是回调自己的那份拷贝，会被**移动**进 Lua。

**获知回调失败。** 默认情况下这类失败是静默的（定义 `LUAAA_DEBUG` 时会打印到 `stderr`）。可以给每个 `lua_State` 注册一个处理函数：

```cpp
luaaa::setCallbackErrorHandler(L, [](lua_State*, const char* message, void* userdata) {
    log_error("lua callback failed: %s", message);
}, /*userdata=*/nullptr);
```

处理函数在调用回调的那个线程上运行，而且是在失败的保护调用**结束之后** —— 所以它也可以**抛出 C++ 异常**，异常会传给调用回调的那段 C++ 代码。处理函数正常返回时回调仍然返回 `RET()`。传 `nullptr` 可移除处理函数。

除非你只需要一个简单的、长期有效的回调（或者在使用[嵌入式构建](#嵌入式--无标准库构建)模式，那是唯一选择），否则一律推荐用 `std::function`。

## 属性详解

`get`/`set` 支持多种写法。下面的 `TCLASS` 表示被绑定的类。

**Getter**（返回属性值；返回值不能是 void）：
```cpp
cat.get("name", &Cat::name);                       // 成员函数：  P (Cat::*)() const
cat.get("total", &globalCount);                    // 自由函数：  P (*)()
cat.get("label", &describe);                       // 自由函数：  P (*)(const Cat&)
cat.get("w", [](Cat& c){ return c.weight(); });    // lambda：   []( [const] Cat& )->P
cat.get("k", []{ return 3.14; });                  // lambda：   []()->P
```

**Setter**（接收新值；返回值会被忽略）：
```cpp
cat.set("name", &Cat::setName);                    // 成员函数：  R (Cat::*)(P)
cat.set("flag", &setGlobalFlag);                   // 自由函数：  R (*)(P)
cat.set("w", &applyWeight);                        // 自由函数：  R (*)(Cat&, P)
cat.set("w", [](Cat& c, float v){ c.setW(v); });   // lambda：   [](Cat&, P)
cat.set("k", [](float v){ /*...*/ });              // lambda：   [](P)
```

**访问规则。** 只有 getter → 只读；写入会直接报错 `attempt to write Read-Only property '...'`。只有 setter → 只写；读取会直接报错 `attempt to read Write-Only property '...'`。访问未定义的属性返回 `nil`。

**模块属性**的绑定方式跟类一样，区别在于 getter/setter 不带 `self` 参数（模块不像对象有实例）。模块的 getter 返回值不能是 void（由 `static_assert` 在编译期强制保证）。

## `def`：常量、数组与内嵌实例

`def` 用来发布一些“准只读”的数据。

```cpp
// 类和模块都可以用：标量常量或字符串
mod.def("version", 3);
mod.def("name", "shelter");

// 仅模块可用：把一个 C 数组变成 Lua 数组表
static const int primes[] = { 2, 3, 5, 7 };
mod.def("primes", primes, sizeof(primes)/sizeof(primes[0]));

// 仅模块可用：把一个绑定类的实例内嵌到模块表中。
// obj 填 nullptr 会默认构造一个；传入析构器可以控制清理行为。
LuaClass<Cat> cat(L, "Cat"); cat.ctor();
mod.def("mascot", cat);                 // shelter.mascot 是一个 Cat 对象
mod.def("mascot", cat, existingCatPtr); // 也可以内嵌一个已有的对象
```

**类的 `def` 放在哪里。** `LuaClass::def` 把常量放在**对象**上（`cat.MAX_LIVES`）。属于类本身的东西用 `constant` 和 `staticFun`，它们与构造函数一起放在类表里：

```cpp
LuaClass<Cat>(L, "Cat")
    .ctor<std::string>()
    .def("SPECIES", "felis catus")        // Cat.new("Tom").SPECIES
    .constant("MAX_LIVES", 9)             // Cat.MAX_LIVES
    .staticFun("count", &Cat::count);     // Cat.count()   （函数指针、lambda 或 lua_CFunction）
```

## 元方法

你可以通过 `fun` 来绑定 Lua 的元方法。绝大多数（`__tostring`、`__add`、`__len`、`__eq` 等）直接注册就行：

```cpp
cat.fun("__tostring", &Cat::toString);   // print(obj) / tostring(obj)
```

但 `__index`、`__newindex` 和 `__gc` 这三个比较特别，因为 luaaa 内部依赖它们来实现属性和 GC。当你绑定了这几个元方法时，luaaa 会保留自己的分发逻辑，然后把你提供的实现**作为后备**，在内置方法和属性查找执行完毕之后再调用。也就是说，你自定义的 `__index` 只处理那些既不是绑定方法也不是注册属性的键；而你自定义的 `__gc` 会在对象的析构逻辑之外额外执行一遍。

实例读取（`obj.key`）时的查找顺序为：绑定方法与常量 → 已注册的 getter → 你的 `__index`（函数或表）→ *（如果有 setter）* 只写报错 → `nil`。

**metatable 对脚本隐藏。** `getmetatable(obj)` 返回 `false`，`setmetatable(obj, …)` 会失败，`obj.__gc` / `obj.__index` 是 `nil`，方法存放在脚本拿不到的表里 —— 因此脚本无法替换已绑定类的方法、元方法或终结器。（你自己绑定的元方法，如 `obj.__tostring`，仍然可以读到。）如果脚本依赖 `getmetatable(obj)`，把 `LUAAA_PROTECT_METATABLE` 定义为 `0`。`luaaa:extend` 创建的对象是 Lua 表，metatable 保持开放。

## 错误与异常

绑定调用里出的任何问题，最终都表现为普通的 Lua 错误（用 `pcall` 捕获）：

* 实参不对 —— `bad argument #2 to 'feed' (number expected, got string)`、传错了类、数值超出范围；
* 你的函数、构造函数或属性访问器抛出了 C++ 异常 —— `luaaa: function call failed: <what()>`。

Lua 用 `longjmp` 抛错，而 `longjmp` 不会执行 C++ 析构函数，所以 luaaa 把两边隔开处理：某个实参转换失败时，排在它前面、已经转换好的实参（字符串、容器、按值对象）会先经正常的 C++ 栈展开被析构，**然后**才抛 Lua 错误；捕获到的 C++ 异常也会先被完整处理掉（不会一直处于“处理中”状态），然后才抛 Lua 错误。不带异常的构建（`-fno-exceptions`）没有这些步骤，直接抛错。

自己写转换代码时需要知道两点：
* 在自定义的 `LuaStack<T>::get` 里，用常规的 `luaL_error` / `luaL_argerror` 报告错误值即可 —— 或者直接基于库里已有的特化来写，它们已经处理好了。
* 在裸 `lua_CFunction` 里直接使用 `LuaStack<T>::get`，行为和 `luaL_check*` 系列完全一样：抛 Lua 错误。

## Lua 侧继承详解

`luaaa:extend(Base, fields)` 返回一张普通的类表：`fields`（或 `{}`）再加上 `new`。之后可以随时往上加方法和默认值。

```lua
local SpecialCat = luaaa:extend(Cat, { tricks = 0 })
function SpecialCat:learn() self.tricks = self.tricks + 1 end

local a = SpecialCat:new("Felix")     -- 每次调用都得到一个【新】实例……
local b = SpecialCat:new("Tom")       -- ……各自拥有自己的 C++ Cat
a:learn()                             -- a.tricks == 1, b.tricks == 0, SpecialCat.tricks == 0

local ShowCat = luaaa:extend(SpecialCat, { ribbons = 0 })   -- 扩展出来的类还可以继续扩展
```

* 实例上的**查找顺序**：实例自己的字段 → 类表（以及它所扩展的各级类，由近及远）→ C++ 类的方法和属性 → C++ 类的自定义 `__index`。
* **写入** `obj.key = v`：如果 `key` 是属性就调用 C++ setter；否则只在这个实例上设置字段。
* 凡是 C++ 接收该绑定类的地方都可以传实例 —— 按引用（`void groom(Cat&)`）、按指针（`void groom(Cat*)`），或者作为它的基类；`luaaa:base(obj)` 返回底层的 C++ 对象，例如用来调用被你覆盖的方法。
* 绑定在 C++ 类上的元方法（`__tostring`、`__eq` 等）对实例同样生效。实例的 metatable 是在某个类第一次实例化时从 C++ 类的 metatable 派生出来的，所以请在脚本开始创建实例之前完成 C++ 类的绑定。
* C++ 对象的生命周期与持有它的实例表完全一致。

## C++ 继承

用 `inherit<Base>()` 告诉 luaaa 某个类的 C++ 基类。基类必须已经在同一个 state 里绑定。

```cpp
struct Animal { std::string name() const; virtual std::string speak() const; };
struct Dog : Animal { void learn(); };

LuaClass<Animal>(L, "Animal").ctor<std::string>().fun("name", &Animal::name).fun("speak", &Animal::speak);
LuaClass<Dog>(L, "Dog").inherit<Animal>().ctor<std::string>().fun("learn", &Dog::learn);

void pet(const Animal& a);          // 可以接收 Dog
```

```lua
local d = Dog.new("Rex")
d:name()        -- Animal 的函数，在 Dog 上能找到
d:speak()       -- 虚函数：执行 Dog 的实现
pet(d)          -- 需要 Animal 的地方传 Dog
```

* 派生类的对象可以用在任何需要基类的地方：`Base&`、`const Base&`、`Base*`、`std::shared_ptr<Base>`，以及基类函数的 `self`。指针会像 C++ 那样调整（`static_cast`），所以多重继承也能用 —— 每个基类调用一次 `inherit<>()`。反过来（需要派生类的地方传基类对象）会被拒绝。
* 基类的函数、常量和属性在派生类对象上都能找到。查找顺序：类自己（先函数、后属性）→ 各基类（按 `inherit` 的顺序，深度优先）→ 自定义的 `__index`。派生类用同名注册的内容优先。
* 基类的基类一并继承：`Puppy → Dog → Animal` 时，`Puppy` 也是 `Animal`。
* C++ 返回的 `Base*` / `Base&` 按 `Base` 包装（luaaa 不检查动态类型），所以在它上面只能看到基类的函数。
* **不用** `inherit` 时，仍然可以直接在派生类上绑定继承来的成员函数 —— `LuaClass<Dog>(L, "Dog").fun("name", &Dog::name)` 可以工作，即使 `name` 声明在 `Animal` 里，而且 `Animal` 完全不必绑定。

这与 [Lua 侧继承](#lua-侧继承详解)（`luaaa:extend`，从已绑定的类派生一个 *Lua* 类）是两回事，两者可以组合使用。

## 智能指针、`std::optional` 与只能移动的类型

**`std::shared_ptr<T>`**（T 为已绑定的类）。返回给 Lua 时对象是*共享*的：Lua 对象自己持有一份 `shared_ptr`，被回收时释放。传回 `std::shared_ptr<T>`（或 `shared_ptr<Base>`）参数时，C++ 与 Lua 共享所有权。它同样可以传给 `T&` / `T*` 参数、作为 `self`，与其他对象没有区别。

```cpp
std::shared_ptr<Cat> adopt(const std::string& name);
void keep(std::shared_ptr<Cat> cat);            // 可以保存到调用结束之后
LuaClass<Cat>(L, "Cat").ctor("create", &Cat::create);   // 用返回 shared_ptr 的工厂函数作构造函数
```

只有 Lua *通过 `shared_ptr` 持有*的对象才能传给 `shared_ptr` 参数。由原位构造的 `ctor<>()` 创建、或以裸指针返回的对象会报 `std::shared_ptr expected: this object is not held by a shared_ptr` —— 没有可以共享的所有权。

**`std::unique_ptr<T, D>`**。返回给 Lua 时所有权*转移给 Lua*：Lua 对象被回收时执行删除器。传给 `std::unique_ptr<T, D>` 参数时所有权*交回 C++*，Lua 侧的对象随即失效（再使用会报 `invalid user data`）。只有来自完全相同的 `unique_ptr` 类型的对象才能这样取走。

**`nil`** 与空的 `shared_ptr` / `unique_ptr` 互相转换。

**`std::optional<T>`**（C++17）把 `nil` —— 或者省略的末尾实参 —— 映射为 `std::nullopt`，其他值按 `T` 转换（类型不对仍然报错，不会当成 `nullopt`）。接收可空参数时这是安全的做法。

**可空的 `T*` 参数。** 默认情况下把 `nil` 传给 `T*` 参数是参数错误，这样脚本的 bug 不会把空指针交给你的函数。把 `LUAAA_NIL_AS_NULLPTR` 定义为 `1` 后，`nil`（以及缺省的实参）作为 `nullptr` 传入 —— C++ 返回的 `nullptr`（在 Lua 里是 `nil`）就可以原样传回。**此时每个接收 `T*` 的被绑函数都必须判空。**

**只能移动的返回类型。** 拷贝构造函数被删除的已绑定类可以按值返回：返回值会被*移动*进 Lua 对象。

## 多个 `lua_State`

luaaa 会把每个类型的 Lua 名字保存在一个**与 lua_State 绑定的注册表**里。这意味着同一个 C++ 类型可以在**不同的 state 当中用不同的名字**导出：

```cpp
LuaClass<Widget>(A, "Button").ctor().fun("get", &Widget::get);  // state A
LuaClass<Widget>(B, "Slider").ctor().fun("get", &Widget::get);  // state B
// state A 里 Button.new():get() 和 state B 里 Slider.new():get() 都能正常工作。
```

但在**同一个** state 内部，一个 C++ 类型只能对应**一个** Lua 名字。如果在同一个 state 里把同一个类型绑定到第二个名字，会直接触发冲突错误。如果确实需要在一个 state 里为同一个类型提供两种不同的 Lua 视角，那就把它封装成两个不同的 C++ 类型（比如写两个简单的子类）—— 这才是靠谱的做法。

## 嵌入式 / 无标准库构建

面向单片机等无操作系统的自由（freestanding）运行环境时，在引入头文件**之前**定义以下宏：

```cpp
#define LUAAA_WITHOUT_CPP_STDLIB 1
#include "luaaa.hpp"
```

通常配合 `-fno-exceptions -fno-rtti` 一起编译。完整的可运行示例参见 `example/embedded.cpp`。

在这个模式下，**下面的特性不可用**（因为它们依赖 STL）：`std::string`、`std::function`、所有的 STL 容器转换、`std::tuple`/`std::pair`，以及 lambda 绑定（lambda 底层依赖 `std::function`）。

**替代方案：**
* 用 `const char*` 处理文本（在你的类里用定长缓冲区存放）。这些是指向 Lua VM 的借用 —— 参见[字符串生命周期](#字符串生命周期const-char-与-stdstring)，需在调用返回前拷贝到自有缓冲区。
* 用 `def(name, array, length)` 把 C 数组变成 Lua 表。
* 用**裸函数指针**接收回调（参见[回调详解](#回调详解)）。
* 在方法参数里加上 `lua_State*`，手动从栈上读取表和参数：
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

**自行提供 `operator new`/`delete`。** luaaa 用 placement `new` 在 userdata 内存块里构造对象；但无标准库的环境里没有自带的分配器。你需要自己实现（在已有标准库的环境里用宏把它们隔离掉，避免重定义冲突）：
```cpp
#ifdef LUAAA_FREESTANDING
void* operator new(size_t, void* p) noexcept { return p; }
void  operator delete(void*, void*) noexcept {}
void* operator new(size_t n)            { return malloc(n); }
void  operator delete(void* p) noexcept { free(p); }
#endif
```

**关于 RTTI。** 用 `-fno-rtti` 编译时，luaaa 无法读取类型名称，所以 `not export` 和类型冲突的错误信息里，类型名会显示为 `?` 而不是 C++ 的具体名字。功能上完全不受影响，只是错误提示会简略一些。

## 特性宏

以下宏需要在 `#include "luaaa.hpp"` **之前**定义。

| 宏 | 默认值 | 作用 |
|---|---|---|
| `LUAAA_WITHOUT_CPP_STDLIB` | `0` | 去掉所有 C++ 标准库的依赖（启用嵌入式模式）。 |
| `LUAAA_FEATURE_PROPERTY` | `1` | 启用 `get`/`set` 属性机制（即 `__index`/`__newindex` 底层）。 |
| `LUAAA_FEATURE_EXTEND` | `1` | 自动注入 `luaaa:extend` 和 `luaaa:base` 以支持 Lua 侧的类继承。 |
| `LUAAA_CHECK_CONSTRUCTOR_NAME_CONFLICT` | `1` | 两个构造函数使用了相同的 Lua 名字时打印警告。 |
| `LUAAA_CHECK_INTEGER_RANGE` | `1` | Lua 数值放不进目标 C++ 整数参数时抛 Lua 错误；设为 `0` 恢复静默回绕。 |
| `LUAAA_NIL_AS_NULLPTR` | `0` | `nil`（或缺省的实参）传给 `T*` 参数时得到 `nullptr` 而不是报错。开启后被绑函数必须判空。 |
| `LUAAA_PROTECT_METATABLE` | `1` | 对脚本隐藏已绑定对象的 metatable（`getmetatable(obj)` 返回 `false`）。 |
| `LUAAA_REGISTER_IN_PACKAGE_LOADED` | `1` | 把每个类表 / 模块表同时登记到 `package.loaded`，这样 `require "name"` 能取到它。 |
| `LUAAA_DEBUG` | `0` | 启用 `LUAAA_DUMP(L)` 栈内容转储的辅助功能。 |

## 字符串生命周期：`const char*` 与 `std::string`

`const char*` 参数（以及 `char*`，它只是对其做了 `const_cast`）是**向 Lua VM 借来的**。luaaa 用 `lua_tostring` 读取它，返回的是指向 Lua 内部字符串存储的指针。该指针仅在对应的 Lua 值存活期间有效 —— 实际上就是本次绑定调用的持续时间内（Lua 参数会留在栈上不弹出，直到你的函数返回）。一旦该值被弹出或被 GC 回收，指针就失效了。

```cpp
// 错误：把借来的指针存到调用之外 -> 悬垂
static const char* g_name = nullptr;
void set(const char* n) { g_name = n; }      // set() 返回后 g_name 即悬垂
const char* get()       { return g_name; }   // use-after-free

// 正确：用 std::string 拥有一份拷贝
void set(std::string n) { g_name = std::move(n); }   // 深拷贝，可安全保留
```

**经验法则**：若字符串需要存活到调用结束之后，就用 `std::string` —— `LuaStack<std::string>` 会替你深拷贝字节。只有当你在调用内**立即**消费该文本（打印、比较、自行拷贝）时，才用 `const char*`。

**嵌入的 NUL 字节。** `std::string`（以及 `std::string_view`）在两个方向上都带着长度传递，所以 `"a\0b"` 这样的二进制字符串可以完整往返。`const char*` 是 C 字符串，在第一个 NUL 处结束 —— 两个方向都是。

**嵌入式 / 无标准库构建**没有 `std::string`，借用是你唯一的选择。请在调用返回前拷贝到自有的定长缓冲区，正如 `example/embedded.cpp` 所做：

```cpp
class Feeder {
    char m_id[32];
public:
    explicit Feeder(const char* id) { strncpy(m_id, id, sizeof(m_id) - 1); m_id[31] = '\0'; }
    //                                                  ^^^^^^^^^^^^^^^ 自有拷贝，可安全保留
};
```

这条借用规则同样适用于用 `lua_next` 从表里读出的 key（见[嵌入式构建](#嵌入式--无标准库构建)）以及任何直接由 `LuaStack<const char*>::get` 返回的值 —— 在下一次 `lua_pop` 之前消费掉它。

## 对象生命周期与 GC 所有权

对象的析构由谁负责，取决于它是怎么进入 Lua 的：

| 创建方式 | GC 时的行为 |
|---|---|
| `ctor<Args...>()`（placement 构造） | 对象存在于 userdata 内部；GC 时自动调用 `~T()` |
| `ctor(name, spawner)`（生成器） | `delete` 生成器返回的指针（如果 `T` 不可析构则什么事都不做） |
| `ctor(name, spawner, deleter)`（生成器 + 自定义析构器） | GC 时调用你的 `deleter(T*)` |
| `ctor(name, spawner, nullptr)`（生成器 + 空析构器） | Lua 永远不销毁它（借用/单例场景） |
| 绑定函数以 `T*` 或 `T&` 返回 | 带类型的**非拥有别名**（full userdata，挂类 metatable，`dtor=nullptr`）；Lua 永远不销毁它 |
| 绑定函数以 `T`（按值）返回 | 带类型的**拥有**对象，移动（或拷贝）进 userdata；GC 时调用 `~T()` |
| 以 `std::shared_ptr<T>` 返回，或由返回 `shared_ptr` 的工厂 `ctor` 创建 | Lua 持有一份引用；GC 时释放，对象随最后一个所有者销毁 |
| 以 `std::unique_ptr<T, D>` 返回 | 由 Lua 拥有；GC 时执行删除器 —— 除非先被 `unique_ptr` 参数取走 |

一个对象**最多只会被终结一次**。脚本通常根本拿不到 `__gc`（metatable 是隐藏的，见[元方法](#元方法)）；在能拿到的情况下（通过 `debug` 库，或 `LUAAA_PROTECT_METATABLE=0`），调用它会立刻执行析构函数/自定义析构器，之后 GC 的那次调用什么都不做，再使用该对象会报 `invalid user data`，而不是访问已销毁的对象。自定义析构器保存在对象自身里，即使类表（及其构造函数）先被回收，它仍然可以调用。

返回 `T*`/`T&` 会包装成携带类 metatable 的 full userdata：方法可以继续调用，回传时也会做类型检查（传错类会被拒绝，不再像过去的无类型 lightuserdata 那样照单全收）。它别名到 C++ 对象本体 —— 对象的存活需要你自己保证；`nullptr` 会变成 Lua 的 `nil`。按值返回 `T` 则拷贝构造进 userdata，由 GC 正常回收。指针/引用的类如果**没有**在该 state 绑定，仍退化为普通 lightuserdata（传统的透明句柄模式）；而未绑定类按值返回则会直接报错（而不是产生悬垂指针）。

## 兼容性说明

* **Lua 版本。** 支持 5.1、5.2、5.3、5.4、5.5 以及 LuaJIT。旧版本上缺少的少数几个新 API 由带 `LUAAA_` 前缀的内部函数提供；luaaa 自己**不定义**任何 `lua_*` / `luaL_*` 名字，因此可以与 lua-compat-5.3 之类的库一起使用。
* **模块表与类表都是普通的表**，在所有 Lua 版本上建法相同。名字是从全局表出发的路径：`LuaModule(L, "game.util")` 就是 `_G.game.util`（缺少的表会自动创建；路径上已有非表的值时报 `name conflict`），默认模块 `"_G"` 就是全局表本身。每个表还会以它的名字登记到 `package.loaded`，所以 `require "game.util"` 能取到它（`LUAAA_REGISTER_IN_PACKAGE_LOADED=0` 可关闭）。除此之外 luaaa **不往**模块表里加任何东西，只有在你注册第一个属性（`get`/`set`）或自定义的 `__index`/`__newindex` 时才给它挂 metatable —— 所以往 `_G` 里绑定函数不会改动 `_G`。已经带有*外来* metatable 的表保持原样：其上的模块属性不可用。
* **C++ 标准。** 最低要求 C++11。C++14 及以上会启用更快的 `std::tuple` 转换路径；同时也带了 C++11 的备选方案，所以 tuple 在两种标准下都能用。

## 排错 / FAQ

**`cpp class 'X' not export`** —— luaaa 被要求转换一个它还没有绑定或特化的类型。常见原因：(1) 你在当前 state 里的 `LuaClass<X>` 构造完成之前就调用了方法；(2) 绑定的函数签名中用到了没有特化的类型（比如从未绑定过的裸结构体）—— 把它绑定出去，或者给它补一个 `LuaStack`；(3) 你把这个对象用在了跟绑定时不同的 `lua_State` 里。

**`C++ class '...' bind to conflict lua name`** —— 你在同一个 state 里把同一个 C++ 类型绑定到了两个不同的 Lua 名字。解决方法：每个 state 用一个名字；或者用不同的 state；或者把它封装成不同的包装类型（参见[多个 lua_State](#多个-lua_state)）。

**`lua name '...' already bound to a different cpp class`** —— 反过来的冲突：两个**不同**的 C++ 类型试图在同一个 state 里共用同一个 Lua 名字。那样它们会静默共享同一个 metatable（并且互相通过对方的类型检查），所以 luaaa 直接拒绝第二次绑定。给每个类型各起一个名字即可。

**静态方法用 `Class.method()` 调用报 "nil value"** —— 通过 `LuaClass::fun` 绑定的静态/自由函数要像方法那样调用：`instance:method(args)`，实例就是被跳过的 `self`。想要真正的 `Class.method()`（或 `Class.MAX` 这样的常量），用 `staticFun` / `constant` 注册：`LuaClass<Cat>(L, "Cat").staticFun("count", &Cat::count).constant("MAX", 9);` —— 这样 `Cat.count()` 就能和 `Cat.new()` 并列使用。

**`getmetatable(obj)` 返回 `false`** —— 2.0 起的既定行为（见[元方法](#元方法)）；脚本确实需要真正的 metatable 时，把 `LUAAA_PROTECT_METATABLE` 定义为 `0`。

**`no overload of '...' matches the given arguments`** —— 用 `overload` 注册在该名字下的函数没有一个能接收这组实参；匹配规则见[重载函数](#重载函数与消歧)。

**`attempt to read Write-Only` / `write Read-Only property`** —— 这个属性你只注册了 setter（只写）或只注册了 getter（只读）。如果两个方向都需要，把缺的那个访问器也注册上就好。

**lambda 绑不上去** —— 嵌入式模式下 lambda 不可用（因为需要 `std::function`），请改用自由函数指针。普通模式下，注意 lambda 不能是泛型的（参数不能用 `auto`），否则编译器推导不出它的签名。

**回调跑一阵就崩了** —— 裸函数指针的回调是按签名单槽的，且不支持重入；`std::function` 回调则需要确保它不会比对应的 `lua_State` 活得更久。详见[回调详解](#回调详解)。

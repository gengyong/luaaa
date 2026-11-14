# 变更记录

[English](CHANGELOG.md) | [中文](CHANGELOG.zh-CN.md)

## 2.0

`LUAAA_VER_MAJOR` / `LUAAA_VER_MINOR` 现为 `2` / `0`。本版本汇总了第七到第十轮评审的修改（逐项记录见 `tests/REVIEW.md`），其中有行为变化，升级前请先看第一节。

### 不兼容的改动

| 改动 | 之前 | 现在 | 恢复旧行为 |
|---|---|---|---|
| 返回 `T&` / `const T&` | 返回对象的拷贝 | 返回它的非拥有别名 | 改为按值返回 |
| `luaaa:extend` | 每次 `new()` 返回同一张表 | 每次 `new()` 一个新实例；扩展类可以再扩展 | — |
| 从 Lua 读取顺序容器和集合 | 用 `lua_next` 遍历表 | 读 `t[1]`、`t[2]`…… 到第一个 `nil` 为止 | — |
| 整数入参 | 越界时静默回绕 | 报 `number overflow` | `LUAAA_CHECK_INTEGER_RANGE=0` |
| `uint64_t`（与 `lua_Integer` 等宽的无符号类型）返回值 | ≥ 2⁶³ 时不一致：有的回绕，`UINT64_MAX` 报错 | 一律按位传递，与入参对称（`UINT64_MAX` ⇄ `-1`） | — |
| 已绑定对象的 metatable | `getmetatable(obj)` 能拿到；`obj.__gc`、`obj.__index` 可读；脚本可以替换方法 | 隐藏：`getmetatable(obj)` 为 `false`，方法在独立的表里 | `LUAAA_PROTECT_METATABLE=0` |
| 脚本里调用 `obj:__gc()` | 执行终结器 | 取不到 `__gc`（`nil`） | `LUAAA_PROTECT_METATABLE=0` 后用 `getmetatable(obj).__gc(obj)` |
| Lua 5.2+ 上带 `.` 的模块名 / 类名 | 一个字面名为 `"a.b"` 的全局 | 嵌套表 `_G.a.b`（与 5.1 一致） | — |
| `LuaModule` | 给每个模块表（包括 `_G`）挂 metatable、写入 `__name` 字段 | 两者都不加；注册第一个属性或自定义 `__index` / `__newindex` 时才挂 metatable | — |
| 模块属性的访问器 | 以 `"<name"` / `">name"` 存在模块表里 | 模块表里看不到 | — |
| Lua 5.2+ 上的 `package.loaded` | 不改动 | 类表 / 模块表按名字登记进去 | `LUAAA_REGISTER_IN_PACKAGE_LOADED=0` |
| `LUAAA_FEATURE_PROPERTY=0` 且有自定义 `__index` | 自定义处理函数完全顶掉方法查找 | 先查方法，再走处理函数（与开启属性时一致） | — |
| 含 NUL 的 `std::string` | 两个方向都在第一个 NUL 处截断 | 完整保留 | 用 `const char*` |
| 以裸 `lua_CFunction` 绑定的 `__index` / `__newindex` / `__gc` 处理函数 | 末尾多传一个 metatable 参数 | 参数为 `(self, key)`、`(self, key, value)`、`(self)` | — |
| Lua 5.1 / LuaJIT 上的全局辅助函数名 | 头文件定义了 `luaL_setfuncs`、`luaL_setmetatable`、`lua_rawgetp`、`lua_rawsetp` | 不再定义任何 `lua_*` / `luaL_*` 名字 | 你的代码依赖它们时请自行定义 |
| 内部宏 | `USE_NEW_MODULE_REGISTRY`、`RTTI_CLASS_NAME` 等泄漏给包含者 | 加 `LUAAA_` 前缀并在末尾 `#undef` | — |
| `LuaClass<T>::UserDataDetail` | 每个类一个结构体，`obj` 带类型 | 统一为一个结构体（`luaaa_detail::UserData`），`obj` 为 `void*` | — |

### 新增

* **C++ 继承** —— `LuaClass<Dog>(L, "Dog").inherit<Animal>()`：派生类对象可用于任何需要基类的地方（会调整指针，多重继承可用），并能访问基类的函数、常量和属性。不绑定基类时，也可以直接在派生类上绑定继承来的成员函数。
* **重载** —— 类和模块上的 `overload(name, f)`，以及 `overloadCtor<Args...>(name)`，把多个函数注册到同一个 Lua 名字下，调用时按实参个数与种类挑选。
* **智能指针** —— 已绑定类的 `std::shared_ptr<T>`、`std::unique_ptr<T, D>` 可作参数和返回值；`ctor(name, factory)` 接受返回 `std::shared_ptr<T>` 的工厂函数。
* **只能移动的返回类型** —— 拷贝构造被删除的已绑定类可以按值返回。
* **C++17 类型** —— `std::optional<T>`（`nil` ⇄ `nullopt`）和 `std::string_view`。
* **`enum` / `enum class`** 按底层整数类型转换。
* **类表成员** —— `LuaClass::staticFun(name, f)` 与 `LuaClass::constant(name, value)`（`Cat.count()`、`Cat.MAX`）。
* **回调错误处理函数** —— `luaaa::setCallbackErrorHandler(L, handler, userdata)`：C++ 回调的 Lua 函数失败时得到通知，处理函数里可以抛异常。
* **`LUAAA_NIL_AS_NULLPTR`**（默认 `0`）—— `nil` 传给 `T*` 参数时作为 `nullptr`。
* `luaaa:extend` 的实例可以传给 `T*` 参数（此前只能传给 `T&`）。
* 模块的 `__index` / `__newindex` 处理函数在 Lua 5.1 / LuaJIT 上也生效了。

### 修复

* 内存安全：`__gc` 幂等；自定义析构器存入对象本身（类表先被回收时的 use-after-free）；回调全程在一次保护调用内、在主线程上运行。
* 参数转换失败时先展开 C++ 栈再抛 Lua 错误；C++ 异常处理完毕后才调用 `lua_error`。
* 无穷大可以通过浮点参数和返回值。
* 构造函数缺参时报 `got nil` 而不是 `got userdata`。
* `T*` 参数的报错里是 Lua 类名和真实的参数位置。
* LuaJIT 上，属性访问器或回调转换里抛出的 Lua 错误保留原始信息，不再变成 `unknown C++ exception`。
* 与类同名的 `LuaModule` 不再覆盖类的 metatable；`__index` / `__newindex` 不再把数字键转成字符串。

### 性能

`-O2`、Lua 5.5、arm64 下实测（每次调用的纳秒数）：

| | 1.5 | 2.0 |
|---|---|---|
| 方法调用（类没有属性） | 62 | 28 |
| 方法调用（类有属性） | 61 | 46 |
| 读属性 | 95 | 57 |
| 写属性 | 92 | 50 |
| 自由函数 | 17 – 18 | 15 |
| 以已绑定对象为参数的函数 | 43 | 23 |

提升来自三处：用 metatable 上的类型标记识别类（每次转换不再查名字）；没有属性的类直接用表作 `__index`；属性访问器按键查找，不再拼 `"<name"` 字符串。

### 工程

* GitHub Actions CI：Linux 上 gcc 与 clang（LuaJIT、5.1 – 5.4，C++11/14/17/20，ASan + UBSan，TSan），macOS 上 clang，以及通过 `tests/CMakeLists.txt` 在 Linux / macOS / Windows（MSVC）上从源码构建各条 PUC Lua 版本线。
* `tests/run.sh all` 并行运行探测到的全部 Lua × C++11/14/17/20，不再假定 `/opt/homebrew`。
* 把已知的错误行为写成预期的测试断言统一标记为 `KNOWN-LIMITATION`。
* 合并了头文件里重复的调用器模板和容器模板。

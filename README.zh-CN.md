# luaaa

[English](README.md) | **中文** · 进阶指南：[GUIDE.md](GUIDE.md) | [GUIDE.zh-CN.md](GUIDE.zh-CN.md)

**只需一个头文件，就能把 C++ 类和函数绑定到 Lua —— 不用生成代码，不用写包装层。**

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

核心就这么简单。下面我们来一次由浅入深的完整导览。

---

## 目录

1. [为什么用 luaaa](#为什么用-luaaa)
2. [环境要求](#环境要求)
3. [安装](#安装)
4. [快速开始](#快速开始)
5. [绑定成员函数与静态函数](#绑定成员函数与静态函数)
6. [构造函数](#构造函数)
7. [属性](#属性)
8. [模块与全局](#模块与全局)
9. [在 Lua 中继承](#在-lua-中继承)
10. [把 Lua 函数当作 C++ 回调](#把-lua-函数当作-c-回调)
11. [运行示例](#运行示例)
12. [下一步](#下一步)
13. [许可证](#许可证)

---

## 为什么用 luaaa

* **单头文件。** 把 `luaaa.hpp` 拷贝到项目里，然后 `#include` 就行。没有额外的构建步骤、没有 `.cpp`、不需要任何外部工具。
* **不用包装。** 直接绑定你**已有的**类和函数，不用为了让它们能对接 Lua 而改写代码。
* **接口极简。** 三个名字几乎覆盖所有场景：`LuaClass`、`LuaModule`，以及（自定义类型时用到的）`LuaStack`。
* **自动类型转换。** 数字、字符串、`std::string` 以及所有标准容器，都会在 C++ 和 Lua 之间自动互转。（`const char*` 参数是向 Lua 借用的，仅在本次调用内有效 —— 需保留字符串请用 `std::string`；详见进阶指南 → 字符串生命周期。）
* **跨平台可移植。** 支持 Lua 5.1 – 5.5 和 LuaJIT。还提供了一个不依赖 C++ 标准库的嵌入式模式。

## 环境要求

* 支持 C++11 的编译器（C++14 及以上会自动启用更快的 `std::tuple` 转换路径，但不强制要求）。
* Lua 5.1、5.2、5.3、5.4、5.5 或 LuaJIT —— 编译时能找到对应的头文件和库即可。

## 安装

luaaa 是纯头文件库，把 `luaaa.hpp` 放进你的项目然后引入就行：

```cpp
#include "luaaa.hpp"
using namespace luaaa;   // 可选，但本文示例都默认写了这一行
```

它只依赖 Lua 和 C++ 标准库，除此之外什么都不需要。

## 快速开始

假设你已经有这样一个普通的 C++ 类 —— 它完全不知道 Lua 的存在：

```cpp
class Cat {
public:
    explicit Cat(const std::string& name) : m_name(name), m_age(1) {}
    void eat(const std::list<std::string>& foods);   // 接收一个列表
    const std::string& name() const;
    void setName(const std::string& n);
private:
    std::string m_name;
    int m_age;
};
```

把它绑定到一个已经创建好的 `lua_State* L` 上：

```cpp
#include "luaaa.hpp"
using namespace luaaa;

void bind(lua_State* L) {
    LuaClass<Cat>(L, "Cat")          // 把 C++ 的 Cat 类以 "Cat" 这个名字暴露给 Lua
        .ctor<std::string>()          // 一个接收 string 的构造函数
        .fun("eat", &Cat::eat)        // 一个成员方法
        .get("name", &Cat::name)      // 一个可读属性……
        .set("name", &Cat::setName);  // ……同时也可写
}
```

现在 Lua 里就可以直接用这个类了 —— 注意传给 `eat` 的 Lua 数组会自动转换成 `std::list<std::string>`：

```lua
local c = Cat.new("Bingo")     -- 调用 C++ 构造函数
c:eat{ "fish", "milk" }        -- Lua 表自动转换为 std::list<std::string>
print(c.name)                  --> Bingo
c.name = "Bingo the Brave"     -- 调用 setName()
```

对象的生命周期完全由 Lua 的垃圾回收（GC）来管理：当 `c` 被回收时，`Cat` 的析构函数会自动调用。

## 绑定成员函数与静态函数

`fun` 几乎什么都能绑：成员函数、`static` 成员函数、普通自由函数、lambda 表达式。

```cpp
class Cat {
public:
    void eat(const std::list<std::string>&);   // 成员函数
    static void meow(const std::string& who);  // 静态函数
    std::string toString() const;              // 供 __tostring 使用
};

LuaClass<Cat>(L, "Cat")
    .ctor<std::string>()
    .fun("eat",  &Cat::eat)
    .fun("meow", &Cat::meow)              // 静态成员，绑定方式跟普通方法一样
    .fun("__tostring", &Cat::toString)    // Lua 元方法也一样可以绑
    .fun("play", [](int minutes) {        // lambda 也没问题
        printf("玩了 %d 分钟\n", minutes);
    });
```

```lua
local c = Cat.new("Bingo")
c:eat{ "fish" }
c:meow("Bingo")        -- 静态函数也通过实例来调用
print(c)               -- __tostring：输出 "Bingo (1y)"
c:play(10)
```

> 注意：用 `fun` 绑定的 `static` 或自由函数，在 Lua 里是**当作方法来调用**的 —— 写法是 `c:meow("Bingo")`。冒号前面的实例会被当成隐藏的 `self` 参数传入并自动跳过，剩下的实参会依次传给函数的形参。想在类本身上调用 —— `Cat.meow("Bingo")` —— 改用 `staticFun` 绑定；`constant` 则把常量放在类表上（`Cat.MAX_LIVES`）。

## 构造函数

每个类至少要注册一个构造函数。模板参数填 C++ 构造函数需要的参数类型，字符串是它在 Lua 端的方法名（默认叫 `"new"`）。

```cpp
LuaClass<Cat>(L, "Cat")
    .ctor()                       // Cat.new()          -> new Cat()
    .ctor<std::string>("create"); // Cat.create("Tom")  -> new Cat("Tom")
```

```lua
local a = Cat.new()
local b = Cat.create("Tom")
```

你可以用不同的名字注册多个构造函数。工厂函数、单例模式、自定义析构器这些进阶用法，见[进阶指南](GUIDE.zh-CN.md#构造函数详解)。

## 属性

`get` 和 `set` 能把 C++ 的取值/赋值方法变成 Lua 的“字段”，让 Lua 用普通的 `.字段名` 语法就能读写。

```cpp
LuaClass<Cat>(L, "Cat")
    .ctor<std::string>()
    .get("name", &Cat::name).set("name", &Cat::setName)   // 可读可写
    .get("age",  &Cat::age ).set("age",  &Cat::setAge);
```

```lua
local c = Cat.new("Bingo")
print(c.name)      -- 实际调用的是 name()
c.age = 3          -- 实际调用的是 setAge(3)
```

* 只配了 getter → **只读**（写入会报 Lua 错误）。
* 只配了 setter → **只写**（读取会报 Lua 错误）。
* 两者都配了 → 可读可写。

getter 和 setter 也可以用自由函数或 lambda（第一个参数可以是 `Cat&`，也可以没有）。详见[属性详解](GUIDE.zh-CN.md#属性详解)。

## 模块与全局

`LuaModule` 把一组自由函数和常量组织到一个 Lua 表里 —— 很适合放那些跟某个对象无关的东西。

```cpp
void adopt(const std::string& name, std::function<void(const std::string&)> onDone);

LuaModule(L, "shelter")
    .def("city",     "Catville")   // 一个常量
    .def("capacity", 50)
    .fun("adopt", adopt);          // 一个自由函数
```

```lua
print(shelter.city)            --> Catville
shelter.adopt("Bingo", function(who) print(who .. " 被领养了！") end)
```

如果不给模块起名字（或者名字用 `"_G"`），里面的东西会直接放进 Lua 的全局空间：

```cpp
LuaModule(L).def("pi", 3.14159);   // 全局的 pi
```

## 在 Lua 中继承

只要你绑定了任意一个类，luaaa 就会自动往当前 `lua_State` 中注入两个辅助函数 —— 不需要你自己写任何胶水代码：

* `luaaa:extend(Base, fields)` —— 基于一个已导出的类来创建子类，`fields` 是可选的新字段。
* `luaaa:base(obj)` —— 获取某个实例底层的 C++ 对象，方便在重写方法后继续调用父类的实现。

```lua
local SpecialCat = luaaa:extend(Cat, { tricks = 0 })

function SpecialCat:learn() self.tricks = self.tricks + 1 end

function SpecialCat:meow(who)          -- 重写 meow……
    print(self.name .. " 先发出咕噜声")
    luaaa:base(self):meow(who)         -- ……再调用 C++ 原版的方法
end

local felix = SpecialCat:new("Felix")
felix:learn()
felix:meow("Felix")
```

每次 `SpecialCat:new(...)` 都会创建一个独立的实例，各自拥有自己的 C++ `Cat`；扩展出来的类还可以继续扩展。详见 [Lua 侧继承详解](GUIDE.zh-CN.md#lua-侧继承详解)。

## 把 Lua 函数当作 C++ 回调

C++ 函数可以直接接收 Lua 端传过来的函数作为回调。把对应参数的类型声明为 `std::function`（推荐）或者普通的函数指针即可：

```cpp
void onEach(std::function<int(int)> cb);   // 推荐：用 std::function
void onEvent(int (*cb)(const char*));      // 限制较多：裸函数指针
```

```lua
onEach(function(x) return x * 2 end)
```

建议优先用 `std::function` 形式。因为它会自己持有对 Lua 函数的引用，所以可以保存起来、拷贝多份、反复调用，还支持重入。两种形式的详细区别以及裸函数指针的注意事项，见[回调详解](GUIDE.zh-CN.md#回调详解)。

## 运行示例

[`example/`](example/) 目录里有一个完整可运行的故事 —— *小小猫咪收容所*，另外还有一个不依赖标准库的嵌入式版本 —— *喂食器节点*。

```bash
bash example/build.sh            # 编译并运行两个版本
bash example/build.sh desktop    # 只跑标准库版本
bash example/build.sh embedded   # 只跑嵌入式版本
```

构建脚本会通过 `pkg-config` 自动探测 Lua。如果找不到，手动指定路径即可：

```bash
LUA_CFLAGS=-I/usr/include/lua5.4 LUA_LIBS=-llua5.4 bash example/build.sh
```

或者直接手写编译命令：

```bash
cd example
c++ -std=c++11 example.cpp -I/usr/include/lua5.4 -llua5.4 -lm -o example && ./example
```

## 下一步

到这里，日常使用需要掌握的内容你已经都了解了。**[进阶指南](GUIDE.zh-CN.md)** 覆盖了剩下的内容，可以作为参考手册或 FAQ 来查阅：

* 自动类型转换，以及完整的容器 / `pair` / `tuple` 支持列表
* 用 `LuaStack` 教会 luaaa 如何处理你自己的类型
* 构造函数的各种形态：工厂、单例、自定义析构器、命名冲突
* 重载函数：消歧，以及用 `overload` / `overloadCtor` 把多个函数注册到同一个 Lua 名字下
* 用 `inherit<Base>()` 声明 C++ 继承
* `std::shared_ptr` / `std::unique_ptr`、`std::optional`、`std::string_view`、只能移动的返回类型
* 回调详解（`std::function` vs 裸指针）
* 元方法（`__index`、`__newindex`、`__gc`、`__tostring`）
* 同时使用多个 `lua_State`
* 面向单片机等环境的**嵌入式 / 无标准库**构建
* 特性宏、GC 所有权规则，以及常见问题排查

从 1.x 升级？[CHANGELOG.zh-CN.md](CHANGELOG.zh-CN.md) 列出了 2.0 的全部变化，包括不兼容的改动。

## 许可证

MIT。详见 [LICENSE](LICENSE)。

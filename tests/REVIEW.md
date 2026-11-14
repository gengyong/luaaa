# luaaa 工程级测试与代码 Review 报告

- 被测对象：`luaaa.hpp`（v1.4，header-only，3362 行）
- 测试环境：macOS (arm64)、g++ / clang++、Lua 5.5.0、`-std=c++11`、`-fsanitize=address,undefined`
- 方法：C++ 侧完成绑定 + Lua 侧断言，覆盖正常功能；再用受控输入触发疑似缺陷路径；全程 ASan/UBSan 监控。
- 测试资产：`tests/test_functional.cpp`（24 项功能断言）、`tests/test_bugs.cpp`（缺陷触发）、`tests/test_edge.cpp`（多 state / 继承）、`tests/run.sh`（一键运行）。

> 以上是第一次报告时的状态。此后经过十轮修复，`luaaa.hpp` 现为 4849 行（版本 2.0），测试矩阵为 LuaJIT / 5.4 / 5.5 × C++11/14/17/20 × ASan+UBSan 共 6732 项。第九轮留下的清单已在第十轮处理完，见[第十轮](#第十轮遗留清单其余各项与功能性能工程类建议)；仍未验证的部分见[第十轮之后仍未验证或未做的](#第十轮之后仍未验证或未做的)；文末的「总体结论」「缺陷清单」是第一次报告的原文，其中各项均已修复。

## 修复记录（本轮已修 + 回归验证）

| 缺陷 | 修复内容 | 位置 | 验证 |
|---|---|---|---|
| H3 | `std::pair::get` 改为用 `lua_rawgeti` 从 table 内按下标读取，与 `put` 对称 | `luaaa.hpp:3229` 附近 | `test_bugs` B5：`echoPair({10,20})→{11,21}` ✅ |
| H4 | module 的 `f_internal_index`/`f_internal_newindex` 中 3 处 `lua_getmetatable(state,1)` 改为 `lua_pushvalue(state,1)`（property 闭包实际存在 module table 自身） | `luaaa.hpp:2182/2209/2241` 附近 | 功能 24/24；`test_bugs` B15、newindex 三路径全部 ✅ |
| M1 | `luaL_error` 补齐缺失的 `key` 实参，消除格式串 UB 并修正语义（属性名/模块名） | `luaaa.hpp:2217` | 读只写属性正确报 `Write-Only property 'wo' of 'MOD'`，无崩溃 ✅ |

### 第二轮（除 H1/H2 外全部修复）

| 缺陷 | 修复内容 | 位置 | 验证 |
|---|---|---|---|
| M2 | 存储 `F`（可能是 `std::function`）改用 placement new（`new (funPtr) F(f)`），消除对未构造存储 `memset`+`operator=` 的 UB；新增 `#include <new>` | `luaaa.hpp` 三处 `_registerClassFunction`/`_registerModuleFunction` | `test_fixes` M2：无捕获/值捕获/返回 string 的 lambda 均正常；`memset` 警告消失 |
| M3 | **per-state registry 方案**：类型→lua 名的映射从 C++ 全局 static 改为存进**每个 state 的 registry**（以 `&s_typeKey` 为键）。`klassName` 由 static 变量改为 `klassName(lua_State*)` 静态方法（从当前 state 取名）。移除 `name$` class-metatable、占位 userdata、`"$"` 字段、`f__clsgc` 与进程级 `new[]`；补 `lua_rawgetp/rawsetp` 的 5.1 polyfill | `luaaa.hpp` 顶部 polyfill + 构造函数 + 成员定义 + 约 30 处 `klassName`→`klassName(state)` | `test_edge` 14/14：并存 state **同名**、并存 state **不同名**（La=Animal / Lb=Beast 同一类型）、L1 关闭后 L2 仍可用、同一 state 内异名冲突被拒；embedded(no-stdlib) 与 example(full) 均编译运行通过 |
| L1 | `std::array::get` 改为 `lua_rawgeti` 按下标读取（顺序确定、无栈残留、短表遇 nil 停止、超长表安全截断）；`Container result{}` 值初始化消除短表未定义尾部 | `luaaa.hpp` array 特化 | `test_fixes` L1：正常/短表/超长表/连续调用均正确 |
| L2 | 无效 `static_assert` 补 `typename std::decay<...>::type`（原比较模板本身，形同虚设） | `luaaa.hpp` 两处 getter/setter | 编译通过，断言恢复语义 |
| L3 | getter/setter 与用户 `__index`/`__newindex` 的 `lua_pcall` 补返回码检查并 `lua_error` 传播（`__gc` 里的 pcall 保持静默）；read-only/write-only 错误信息补全模块名；`"use use"` 文案修正；清理 unused parameter 警告 | `luaaa.hpp` 多处 | `test_fixes` L3：用户 `__index` 抛错正确传播；零编译警告 |

回归：`bash tests/run.sh`，功能 24/24 + fixes 9/9 + edge 8/8 全部通过，ASan/UBSan 无内存错误，零编译警告。

### M3 增强（per-state registry，支持并存 state 不同名）

第二轮的 M3 曾用「进程级持久 `klassName`」实现，只支持并存 state **同名**（不同名需 `TAG`）。本轮进一步重构为 **per-state registry**：
- 每个 `LuaClass<TCLASS>` 有一个唯一静态地址 `&s_typeKey` 作类型标识；lua 名存到**该 state** 的 `registry[&s_typeKey]`。
- `klassName(lua_State*)` 从当前 state 取名——因此**同一 C++ 类型可在不同（甚至并存）state 中用不同 lua 名绑定**，各 state 只认自己的名字。
- 冲突判定变为 per-state：仅同一 state 内对同类型用两个不同名才算冲突。
- 顺带**消除**了上一版的进程级 `new[]` 泄漏；`lua_rawgetp/rawsetp` 为 5.1/luajit 提供 polyfill。
- 代价：`LuaStack::get/put` 每次多一次 O(1) registry 查找。

> **后续（TAG 移除）**：模板此前还带一个 `int TAG` 参数（`LuaClass<T, TAG>`），本意是让同一类型在同一 state 内绑定多个名字。但 `LuaStack<T>::get`（解析 `self`/参数）恒用 `LuaClass<T, 0>`，非零 `TAG` 的实例方法在调用时必然找不到 `self`——该特性从未真正工作。已彻底删除 `TAG` 模板参数；"不同 state 不同名"由 per-state registry 提供，与 `TAG` 无关。

**遗留 / 未修**：
- ~~M2 的遗留：存储的 `std::function` 在 holder userdata 回收时其析构仍未被调用（一次性 setup 期泄漏，非本轮 UB 问题；彻底修复需为 holder 加 `__gc`）。~~ **已修复（第四轮）**：见下。

### 第四轮（M2 遗留：holder `__gc`）

第二轮 M2 用 placement new（`new (funPtr) F(f)`）消除了 `memset`+`operator=` 的 UB，但只构造未析构——存 `F` 的 holder userdata 无 `__gc`，回收时不调用 `~F()`，`std::function` 的堆捕获随 state 生命周期泄漏。

| 缺陷 | 修复内容 | 位置 | 验证 |
|---|---|---|---|
| M2 遗留 | 新增命名空间级 `AttachHolderFinalizer<F>(L)`：仅对**非平凡析构** `F`（如 `std::function`）给 holder userdata 挂一个调用 `p->~F()` 的 `__gc`（`__gc` 在 `lua_setmetatable` 前即写入 metatable，满足 5.4+ 终结器登记规则）；平凡 `F`（函数指针）跳过——no-stdlib/embedded 零开销。在 3 处 placement new 后各插一行调用。另补 `#include <new>`（placement new 本应自带，原先靠 includer 传递性引入，no-stdlib 下脆弱） | `luaaa.hpp` `AttachHolderFinalizer` + `_registerClassFunction` + `_registerModuleFunction`（新/旧注册表两路径） | `test_fixes` M2b：按值捕获带存活计数 `Tracker` 的 `std::function`（类 + 模块两条存储路径），`lua_close` 后 `Tracker::alive` 归零；未修复版该断言 FAIL（已实测）。fixes 9→11 全通过 |

### 第三轮（H1/H2 回调机制重构）

回调把 Lua 函数转成 C++ 可调用体。原实现（函数指针 / `std::function` 各 `RET`/`void` 共 4 处）都用一对 per-signature `static cacheLuaState/cacheLuaFuncId` 存 ref，且**每次 `pcall` 后 `luaL_unref`**，导致：**H1** 回调只能调一次；**H2** 同签名回调共享 static 槽、互相踩踏、不可重入。

| 缺陷 | 修复内容 | 位置 | 验证 |
|---|---|---|---|
| **H1+H2**（`std::function` 回调，彻底修复） | `get` 重写为**捕获 lambda 携带 `(L, ref)`**，`shared_ptr` guard 在回调对象及其所有拷贝销毁时才 `luaL_unref`；移除 `static` 槽与调用后 `unref`；补 `lua_pop` 修正原有栈不平衡。新增 `#include <memory>` | `luaaa.hpp` 两处 `LuaStack<std::function<...>>` 特化 | `test_callbacks` 7/7：多次调用、同签名多回调并存、可重入、存储后延迟调用、连调 50 次栈不增长、void 副作用 |
| **H1**（函数指针回调，部分修复） | 删除调用后 `luaL_unref`（单回调可多次调用），补 `lua_pop` 平衡栈；`void` 分支 `pcall` 改 0 返回值 | `luaaa.hpp` `IMPLEMENT_CALLBACK_INVOKER` 宏 | `test_callbacks`：函数指针回调连调两次都生效 |
| **H2**（函数指针回调，语言限制） | 无捕获函数指针无法携带上下文，保留 `static` 槽；宏上方补注释说明限制，README 引导用 `std::function` | 同上 + `README.md` | 文档化；`test_bugs` B2/B3 由缺陷转为 OK（结论「0 个缺陷」） |

回归：`bash tests/run.sh` 全绿（functional 24 + fixes 9 + callbacks 7 + edge 14，test_bugs「0 个缺陷」）；ASan/UBSan 无报错（重点确认多次/重入调用后无栈失衡、无 use-after-free）；embedded(no-stdlib) 与 example(full) 编译运行通过；零编译警告。

**H1/H2 遗留（文档注明）**：
- 函数指针回调 H2 是 C++ 语言限制，无依赖前提下无法根治 —— 引导用 `std::function`。
- ~~回调内 `pcall` 失败仍经 `lua_error` longjmp（可能跳过调用者 C++ 析构）；彻底解决需 C++ 异常桥接，但 embedded 为 `-fno-exceptions`，超出范围。~~ **已修复（第七轮 R7-3）**：整个回调在一次保护调用内完成，失败返回 `RET()`，不再 longjmp。
- `std::function` 回调不应在其 `lua_State` 关闭后再销毁（`shared_ptr` 析构会 `luaL_unref(L,...)`）。

### 第七轮（高危 4 项：`__gc` 幂等 / deleter 生命周期 / 回调保护调用 / 回调主线程）

| 缺陷 | 修复内容 | 位置 | 验证 |
|---|---|---|---|
| **R7-1** 脚本可调 `obj:__gc()`，析构后不置空 → GC 时二次析构；对外来 userdata 调 `__gc` 会被当成 `UserDataDetail` 解释 | `f__objgc` 只信任「full userdata 且 metatable 带本类型 tag（`&s_typeKey`）」的对象；终结后清空 `dtor`/`obj`，最多终结一次；已终结对象经 `LuaStack::get` 报 `invalid user data` | `luaaa.hpp` `LuaClass` 构造函数内 `f__objgc` | `test_review7` H1：手动 `__gc` + GC 后 ctor=dtor=1；已终结对象调方法报错；用户 `__gc` 只跑一次；`getmetatable(o).__gc(io.stdout)` 被忽略 |
| **R7-2** `ctor(name, spawner, deleter)` 的对象只存了指向 deleter holder userdata 的指针，holder 仅由构造闭包保活 → `Class = nil` 后对象回收时 heap-use-after-free | `UserDataDetail::free_func` 由 `void*` 改为泛型函数指针 `void(*)()`，deleter **按值**存入对象（`reinterpret_cast` 往返）；`LuaModule::def` 的 deleter 路径同步，不再做函数指针↔`void*` 转换 | `luaaa.hpp` `UserDataDetail`、三参 `ctor`、`LuaModule::def` | `test_review7` H2：类表先回收后对象再回收，deleter 恰好一次，ASan 无报错（修复前实测 UAF） |
| **R7-3** 回调内 `LuaStack<RET>::get` / `LuaStack<ARGS>::put` 失败时直接 `luaL_error`：跨 C++ 栈帧 longjmp，纯 C++ 调用时 PANIC/abort | 新增 `luaaa_detail::CallbackRunner` + `RunProtected`：取函数、压实参、调用、取返回值**全部**在一次保护调用内完成（5.1/LuaJIT 用 `lua_cpcall`，5.2+ 用 `lua_pcall`）；失败返回 `RET()`，栈保持平衡。四处回调实现（函数指针 RET/void、`std::function` RET/void）统一走该路径 | `luaaa.hpp` `luaaa_detail`（callback plumbing）、`IMPLEMENT_CALLBACK_INVOKER`、两处 `LuaStack<std::function<...>>` | `test_review7` H3：返回类型不符 / 实参无法转换均不 panic、返回默认值、连续 50 次失败后栈平衡；正常回调回归通过 |
| **R7-4** 回调捕获的是注册时的 `lua_State*`，在协程里注册、协程被回收后即悬垂 | 新增 `luaaa_detail::MainThread`：5.2+ 取 `LUA_RIDX_MAINTHREAD`；5.1/LuaJIT 在绑定 `LuaClass`/`LuaModule` 时记录主线程。回调与 `luaL_unref` 都改用主线程 state | `luaaa.hpp` `luaaa_detail::MainThread`、`LuaClass`/`LuaModule` 构造函数 | `test_review7` H4：协程内注册、协程回收后调用，回调运行在主线程（两种回调形式） |

回归：`bash tests/run.sh all` —— LuaJIT / 5.4 / 5.5 × ASan+UBSan 共 978 通过、0 失败（原 909 + `test_review7` 23×3），无 sanitizer 报告；TSan 并发 3/3；C++11/14/17/20/23 下 `-Wall -Wextra -Wpedantic -Wshadow` 零警告；`example`（full）与 `embedded`（no-stdlib、`-fno-exceptions -fno-rtti`，含 LuaJIT 5.1 路径）编译运行通过。

**本轮未处理（已在第八轮修复，见下）**：`T&`/`const T&` 返回实际为拷贝而非别名（`put_ref` 的 `remove_cv` 漏 `::type`）；`luaaa:extend` 所有实例共用同一张表；与类同名的 `LuaModule` 覆盖类 metatable；顺序容器用 `lua_next` 读表（顺序不保证、混入哈希键）；整数入参无范围检查；数字键被原地转成字符串；参数转换失败时 longjmp 跳过实参析构；在 `catch` 块内 `luaL_error` 导致异常对象残留。

### 第八轮（中危 8 项）

| 缺陷 | 修复内容 | 验证（`test_review8`） |
|---|---|---|
| **R8-1** `T&`/`const T&` 返回实为拷贝 | `put_ref` 补 `::type`，并改用主模板标记 `LUAAA_BOUND_CLASS_STACK` 判定（避免 `const bool&` 被当指针）；C++14 tuple 路径按元素类型 put（拥有拷贝）。回调的类引用实参随之成为别名 | M1 |
| **R8-2** `luaaa:extend` 所有实例同表、不支持多级 | 每次 `new` 新建实例表 `{["@"]=obj}`，实例 metatable 为 C++ 类 metatable 的按类副本（`__index` 先查类表链再回落 C++）；类表间以 `__index` 链接；`LuaStack<T>::get` 对表改查类型 tag | M2 |
| **R8-3** 同名 `LuaModule` 覆盖类 metatable | 模块 metatable 改为每 state 一张共享表，以私有地址为 registry 键，不再用模块名 | M3 |
| **R8-4** 顺序容器/集合用 `lua_next` | 改为 `t[1..]` 读到首个 nil；vector 预留容量 | M4 |
| **R8-5** 整数入参无范围检查 | 窄于 `lua_Integer` 的类型越界报 `number overflow`；等宽无符号仍按位接受负值；`LUAAA_CHECK_INTEGER_RANGE=0` 可恢复回绕。这取代了 `test_numeric_edge2` B7 中「经用户确认保持现状」的旧决定（用户于 2026-11-13 确认默认开启），该测试已改为断言新默认行为，旧行为由 `test_features_no_intrange` 覆盖 | M5 |
| **R8-6** 数字键被原地转成字符串 | 类/模块的 `__index`/`__newindex` 不再转换键；数字键原样交给用户处理器或原样存储；模块表非字符串键赋值不再被丢弃 | M6 |
| **R8-7** 转换失败 longjmp 跳过实参析构 | 调用边界内的转换失败改为抛 `ConversionError`，栈展开后在边界处重放原生 `luaL_*` 检查；边界以 upvalue 1 的地址自我标识，边界之外（裸 `lua_CFunction`、属性访问器、回调返回值）仍直接抛 lua 错误 | M7 |
| **R8-8** `catch` 块内 `luaL_error` 致异常残留 | 统一为 `LUAAA_TRY`/`LUAAA_CATCH`：在 catch 内只压入消息，离开处理块后再 `lua_error` | M8 |

顺带修复：`installExtendHelper` 中 `lua_rawgetp` 返回值的用法（5.2 上返回 void）；5.1 路径各 `ctor` 注册后 `luaL_openlib` 的栈残留；`tests/run.sh` 失败数解析的贪婪匹配（"10 failed" 被读成 0）。

回归：`bash tests/run.sh all` —— LuaJIT / 5.4 / 5.5 × ASan+UBSan 共 1209 通过、0 失败，无 sanitizer 报告；C++14（tuple 快路径）与 C++20/LuaJIT 全套通过；TSan 3/3；示例与嵌入式（无标准库、`-fno-exceptions`）构建运行正常。调用开销：成员/自由函数每次调用约增加 1–5 ns（边界登记一次 thread-local 写），`vector` 入参约快 15%。

### 第九轮（第一批低危：R9-1 / R9-5 / R9-10 / R9-11、`enum`、内部宏、`.gitignore`）

编号 R9-x 见下文「遗留问题与改进建议」。

| 项 | 修复内容 | 验证（`test_review9`） |
|---|---|---|
| R9-1：`math.huge` 传给浮点参数报溢出 | 浮点 `get` / `put` 的范围检查放行 ±inf，只拦「有限但超出目标类型范围」的值（`1e300` → `float` 仍报溢出）。C++ 返回的 inf 现在得到 `math.huge` | L1 |
| R9-5：构造函数缺参报 `got userdata` | 新增 `PadMissingArgs<ARGS...>`：在压入对象的 userdata 之前，把缺少的实参位置补成 nil（`lua_State*` 形参不占位）。四种构造形式（placement、spawner、spawner + deleter、spawner + `nullptr`）都走这一步；报错变为 `number expected, got nil`，参数序号正确 | L5 |
| R9-10：`T*` 参数报错里是 mangled 名 | 类已绑定时报 `<lua 类名> expected, got <实参类型>`，并使用实参的真实序号（原先固定报 `#1`）；未绑定的指针类型仍退回 `cpp pointer ... expected` | L10 |
| R9-11：拼写 | 16 处 `faild to alloc mem` 改为 `failed to allocate memory`（只在分配失败时出现，没有对应测试） | — |
| `enum` / `enum class` | 新增 `LuaStack` 偏特化：按 `std::underlying_type` 转换，沿用底层整数类型的类型检查和范围检查；用户自己写的 `LuaStack<MyEnum>` 全特化仍然优先 | ENUM |
| 内部宏泄漏 | `USE_NEW_MODULE_REGISTRY`、`RTTI_CLASS_NAME`、`IMPLEMENT_CALLBACK_INVOKER`、`IMPLEMENT_FUNCTION_CALLER`、`luaaa_check_constructor_name_conflict`、`_NOTHING` 全部加 `LUAAA_` 前缀（最后两个分别改名为 `LUAAA_REPORT_CTOR_NAME_CONFLICT`、`LUAAA_NOTHING`），并在文件末尾 `#undef`；公开的配置宏不受影响 | MACRO |
| `tests/results.txt` | 加入 `.gitignore` | — |

**随之改动的旧断言**（都是把旧行为写成预期的测试，没有「经用户确认」的记录）：

- `test_cast`、`test_float_edge`：原断言「`float` get +inf 应报溢出」，改为断言 ±inf 透传。
- `test_numeric`：`big_ld()` 原为 `1e300L * 1e300L`。在 `long double` 与 `double` 等宽的平台（arm64、MSVC）上它其实就是 +inf，靠「inf 也算溢出」才通过。改为返回 `long double` 的最大有限值，并按平台分支：`long double` 更宽时断言报溢出，等宽时断言可以原样返回。

`tests/run.sh` 的汇总行只认 `=== name: N passed, M failed ===` 这一种格式，新测试的输出已按此对齐（写成四个等号时不会计入总数）。

回归：`bash tests/run.sh all` —— LuaJIT / 5.4 / 5.5 × ASan+UBSan 共 1356 通过、0 失败（原 1209 + `test_review9` 48×3 + `test_float_edge` 新增 1×3），无 sanitizer 报告；TSan 3/3；C++14 / 5.5 与 C++20 / LuaJIT 各 452 通过；示例与嵌入式（无标准库、`-fno-exceptions -fno-rtti`）在 5.5 和 LuaJIT 下构建运行正常；带标准库的 `-fno-exceptions` 构建通过；C++11/17/23 下 `-Wall -Wextra -Wpedantic -Wshadow` 零警告。

### 第十轮（遗留清单其余各项与功能、性能、工程类建议）

编号 R9-x 见下文「遗留问题与改进建议」。版本号升为 2.0，不兼容改动列在 `CHANGELOG.zh-CN.md`。

**遗留低危项**

| 项 | 修复内容 | 验证 |
|---|---|---|
| R9-2：`uint64` 出参在 2⁶³ 附近不一致 | 整数出参的范围检查改为纯整数比较。规则定为：与 `lua_Integer` 等宽的无符号类型在**两个方向**都按位传递（`UINT64_MAX` ⇄ `-1`），任何值都能往返；只有比 `lua_Integer` 更宽的类型出参时才可能报溢出 | `test_review10` R9-2 |
| R9-3：nil 不能传给 `T*` | 新增开关 `LUAAA_NIL_AS_NULLPTR`，**默认关闭**：开启后 nil / 缺省实参得到 `nullptr`。默认关闭是因为 `test_cast`（M4）和 `test_review9` 里的被绑函数都不判空，默认开启会把脚本错误变成宿主崩溃 | `test_features_nil_nullptr`（开）；`test_cast` / `test_review9` / `test_review10`（关） |
| R9-4：extend 实例不能传给 `T*` | `T*`、`T&`、`self`、智能指针参数共用一个取对象的函数 `luaaa_detail::to_object`，都认 extend 实例 | `test_review10` R9-4 |
| R9-6：回调出错被静默吞掉 | `luaaa::setCallbackErrorHandler(L, handler, userdata)`，按 state 存 registry；处理函数在保护调用结束后运行，可以抛 C++ 异常。未注册时行为不变 | `test_review10` R9-6 |
| R9-7：`LuaModule(L)` 改动 `_G` | 模块表不再写入 `__name`，只在注册第一个 property 或用户 `__index` / `__newindex` 时才挂 metatable（每个模块一张，名字存在 metatable 里）；访问器存在 metatable 的表里，不再出现在模块表中 | `test_review10` R9-7 |
| R9-8：类 metatable 对脚本开放 | 方法、常量放进独立的方法表；metatable 设 `__metatable = false`（`LUAAA_PROTECT_METATABLE=0` 可关）；extend 实例的 metatable 副本去掉 `__metatable`，其 `__index` 每次访问时取 C++ 类当前的 `__index` | `test_review10` R9-8；`test_review7` H1 改从 `debug.getmetatable` 取 `__gc` |
| R9-9：字符串在 NUL 处截断 | `std::string` 改用 `lua_tolstring` / `lua_pushlstring`；`const char*` 仍在 NUL 处结束（C 字符串的固有限制，已标 `KNOWN-LIMITATION`） | `test_strings` |

**功能**

| 建议 | 实现 | 验证（`test_review10`） |
|---|---|---|
| C++ 继承 | `inherit<Base>()`：metatable 里记基类 metatable 与指针转换函数，取对象时沿基类链做 `static_cast`（多重继承的指针调整正确），`__index` / `__newindex` 沿基类链查方法、属性。另外，在派生类上绑定继承来的成员函数不再要求基类已绑定 | 继承 |
| 智能指针 | `std::shared_ptr<T>`（userdata 内存一份 `shared_ptr<void>`，取出时用别名构造，可转成 `shared_ptr<Base>`）、`std::unique_ptr<T, D>`（所有权可交给 Lua 也可取回）；`ctor` 接受返回 `shared_ptr` 的工厂 | 智能指针 |
| 只能移动的返回类型 | `LuaStackReturn` 改为转发，主模板增加 `put(T&&)` | 只能移动 |
| C++17 类型 | `std::optional<T>`、`std::string_view` | C++17（`-std=c++17` 及以上才编译） |
| 重载分发 | `overload(name, f)`（类、模块）与 `overloadCtor<Args...>(name)`，按实参个数与种类取第一个相符的 | 重载 |
| 类表上的静态函数与常量 | `LuaClass::staticFun` / `LuaClass::constant` | R9-8 |
| 嵌套模块名与 `require` | 所有版本统一为显式建表：`"a.b"` 即 `_G.a.b`；并登记到 `package.loaded`（`LUAAA_REGISTER_IN_PACKAGE_LOADED=0` 可关）。不再使用 `luaL_openlib` | R9-7 |

**性能**（`-O2`、Lua 5.5、arm64，每次调用纳秒数，五次取最小）

| | 改动前 | 改动后 |
|---|---|---|
| 方法调用（类没有 property） | 62 | 28 |
| 方法调用（类有 property） | 61 | 46 |
| 读 property | 95 | 57 |
| 写 property | 92 | 50 |
| 自由函数 | 17–18 | 15 |
| 以已绑定对象为参数 | 43 | 23 |

- 取对象时只比较 metatable 上的类型 tag（一次 raw 查找），不再取类名、不再经 `luaL_checkudata`；metatable 另存一份在 `registry[&s_metaKey]`，压对象时不按名字查。
- 没有 property、没有基类、没有用户 `__index` 的类，`__index` 直接是方法表；需要时才换成 C 函数。
- property 访问器按键从表里取，不再用 `snprintf` 拼 `"<name"`。

**工程**

- CI：`.github/workflows/ci.yml`（Linux gcc / clang × 发行版自带的 LuaJIT、5.1–5.4；macOS；Linux / macOS / Windows 从源码构建 PUC 5.1–5.5，其中 Windows 用 MSVC），配套 `tests/CMakeLists.txt`。
- `tests/run.sh`：`all` 模式把 C++11/14/17/20 纳入矩阵，并行编译运行（432 个任务约 40 秒）；Lua 一律经 pkg-config 探测，有 Homebrew 时自动加入各 keg 的 pkgconfig 目录，不再写死 `/opt/homebrew`；没有汇总行且退出码非 0 的测试计为崩溃，sanitizer 报告条数计入失败。
- 把已知的错误行为写成预期的断言统一加 `KNOWN-LIMITATION` 前缀（`grep -rn KNOWN-LIMITATION tests/`，现有 5 条）。
- 5.1 下的全局 polyfill（`luaL_setfuncs`、`luaL_setmetatable`、`lua_rawgetp`、`lua_rawsetp`）全部去掉，改为 `LUAAA_` 前缀的内部函数。
- 重复代码：10 个调用器合并为 `FunctionInvoker` / `MethodInvoker`（返回值与 `void` 由 `ReturnPusher` 统一），12 个 property 访问器合并为 `PropGet` / `PropSet`，4 个构造函数共用 `_spawn`，12 个容器特化合并为 `SequenceStack` / `MapStack`，回调的返回值 / `void` 两套合并。`__gc`、`__index`、`__newindex` 不再是每个类一份的模板代码。
- `LUAAA_FEATURE_PROPERTY=0` 且 `-Wextra` 时的 12 条 `unused-parameter` 警告已消除。
- 版本号升为 2.0，新增 `CHANGELOG.md` / `CHANGELOG.zh-CN.md`。

**顺带修复**：LuaJIT 用外来异常实现 lua 错误，`catch (...)` 会把它截住并改写成 `unknown C++ exception`（属性访问器、回调的转换路径）；现在在 GCC / Clang 上识别外来异常并保留原始错误信息。

**随之改动的旧断言**：`test_edge`（手写的旧式 extend 依赖 `getmetatable(o)`，该文件定义 `LUAAA_PROTECT_METATABLE 0`）、`test_review7`（`o:__gc()` 改为 `debug.getmetatable(o).__gc(o)`）、`test_strings`（NUL 截断改为断言保留）、`test_features_no_property`（用户 `__index` 不再顶掉方法查找）、`test_property`（模块 `__index` 在所有版本上断言）、`test_cast`（只改了一条断言的说明文字：该用例其实是在入参处被拒绝）。

回归：`bash tests/run.sh all` —— LuaJIT / 5.4 / 5.5 × C++11/14/17/20 × ASan+UBSan 共 6732 通过、0 失败，无 sanitizer 报告；C++23 下 1695 通过；TSan 并发 36/36（12 个配置各 3 项）；C++11/14/17/20/23 下 `-Wall -Wextra -Wpedantic -Wshadow -Werror` 通过；示例与嵌入式（无标准库、`-fno-exceptions -fno-rtti`，5.5 与 LuaJIT）构建运行正常；带标准库的 `-fno-exceptions` 构建通过；`tests/CMakeLists.txt` 在本机（Ninja、Lua 5.5、C++17）36 个测试全部通过。

**PUC Lua 5.1.5 / 5.2.4 / 5.3.6 实测**（2026-11-14，源码取自 lua.org，经 `tests/CMakeLists.txt -DLUA_SRC_DIR=` 构建，Lua 与测试都开 ASan+UBSan，C++11）：三个版本各 36 个测试、557 项断言全部通过，luaaa 自身没有 sanitizer 报告（剩下的 UBSan 报告都在 Lua 5.2 自己的 `ltable.c` 里）。这是这三个版本第一次被实测，过程中发现并修了：

| 问题 | 版本 | 修复 |
|---|---|---|
| 超出 `lua_Integer` 范围的数字（如 `'1e30'`）传给整数参数时，转换交给了 `lua_tointegerx`，而 PUC 5.2 的实现是直接做 double → 整数的强制转换，越界时是未定义行为（UBSan 在 `lapi.c:355` 报出） | 5.2 | `LUAAA_tointegerx` 的钳位分支从「5.1」扩大到「5.3 之前」，与 5.1 / LuaJIT 的行为一致（钳到 `lua_Integer` 的范围）；顺带给 NaN 加了保护 |
| `test_numeric_edge2`、`test_overload` 的 `lua_isinteger` 垫片只对 5.1 生效，5.2 上编译不过 | 5.2 | 垫片条件改为 `< 503` |
| `test_cast` 两处按「是否 5.1」分支的断言，实际区分的是「数字是否都是 double」 | 5.2 | 条件改为 `< 503` |

5.1.5 和 5.3.6 无需改动即通过。

#### 第十轮之后仍未验证或未做的

- **CI 还没有在 GitHub 上跑过，MSVC（含各调用约定分支）至今没有实测**，第一次推送后可能要按报错再修。PUC Lua 5.1 / 5.2 / 5.3 已在本机从源码实测，见下。
- 重载分发只按「个数 + 种类」匹配，不做隐式转换打分；`fun` 与 `overload` 不能在同一个名字下混用。
- `shared_ptr` 参数只接受由 `shared_ptr` 持有的对象，不支持 `enable_shared_from_this`。
- C++17 的 `noexcept` 成员函数指针类型没有单独处理（此前也没有）。

## 遗留问题与改进建议（第九轮时的清单，已在第十轮全部处理）

第七、八轮修了审阅中的高危 4 项和中危 8 项，第九轮修了第一批低危项，其余各项在第十轮处理，处理方式见上面的[第十轮](#第十轮遗留清单其余各项与功能性能工程类建议)。下面保留清单原文备查。

- 「遗留低危项」一节的现象于 2026-11-13 在当前代码上用探针重新实测过（Lua 5.5，arm64，ASan+UBSan）。
- 「改进建议」中标注「未复核」的条目来自最初的代码阅读，没有单独写探针验证。
- 编号用 `R9-` 前缀，与本文件后面第一次报告里的 L1–L3 无关。

### 遗留低危项

| 编号 | 现象（实测） | 位置 | 建议 |
|---|---|---|---|
| ~~**R9-1**~~ **已修复（第九轮）** `math.huge` 传给浮点参数报溢出，NaN 却通过 | `dbl(math.huge)`、`dbl(-math.huge)` 报 `number overflow: value does not fit target floating type`；`dbl(0/0)` 正常返回 `nan` | 浮点 `LuaStack::get` 的范围比较 | 无穷大是合法的 `double`/`float` 值：比较前先用 `std::isinf` 放行，只拦「有限但超出目标类型范围」的值（如 `1e300` → `float`） |
| ~~**R9-2**~~ **已修复（第十轮）** `uint64` 出参在 2⁶³ 附近不一致 | 返回 `uint64_t(1) << 63` 得到 `-9223372036854775808`，返回 `UINT64_MAX` 却报 `integer value out of lua_Integer range`；入参 `-1` 被接受为 `UINT64_MAX`，原样返回时报错（往返不对称） | 整数 `LuaStack::put` 的范围检查：用 `long double` 比较，而 arm64 / MSVC 上 `long double` 就是 `double`，`INT64_MAX` 被舍入成 2⁶³ | 整数范围检查改用整数比较（按有无符号分支），不经过浮点。再定一个统一规则：等宽无符号要么两个方向都按位解释，要么两个方向都报错，并同步 GUIDE 里的说明 |
| ~~**R9-3**~~ **已修复（第十轮）** nil 不能传给 `T*` 参数 | C++ 返回的 `nullptr` 在 Lua 里是 nil，但 `ptrGet(nil)` 报 `cpp pointer ... expected`，返回值不能原样传回 | `LuaStack<T*>::get` | `T*` 入参接受 nil → `nullptr`。这会让原本报错的调用变成空指针，建议加开关宏（如 `LUAAA_NIL_AS_NULLPTR`）并在 GUIDE 里提醒被绑函数要判空 |
| ~~**R9-4**~~ **已修复（第十轮）** extend 实例不能传给 `T*` 参数 | `luaaa:extend` 的实例可以当 `self`，也可以传给 `T&` / `const T&` 参数，但传给 `T*` 参数报错，必须写 `luaaa:base(d)` | `LuaStack<T*>::get` 没有主模板 `get` 的表分支 | `T*` 的 `get` 同样识别带类型 tag 的实例表，取其 `"@"` |
| ~~**R9-5**~~ **已修复（第九轮）** 构造函数缺参的报错信息误导 | `Cat.new()` 报 `bad argument #1 (number expected, got userdata)`；双参构造少传一个报 `#2 ... got userdata` | placement 构造：对象的 userdata 先于取参入栈，缺失的参数位读到了它 | 取参前用 `lua_gettop` 校验参数个数，或取完参数再压 userdata，使报错为 `got no value` |
| ~~**R9-6**~~ **已修复（第十轮）** Lua 回调出错被静默吞掉 | 回调里 `error('cb boom')`，C++ 侧拿到 `RET()`（如 0），没有任何提示；只有 `LUAAA_DEBUG=1` 时打印到 stderr | `HandleCallbackError` | 提供可注册的错误钩子（按 state 存 registry），默认行为不变；钩子里可记日志或转成 C++ 异常 |
| ~~**R9-7**~~ **已修复（第十轮）** `LuaModule(L)` 改动 `_G` | 绑定默认模块后 `_G` 被挂上 metatable，且多出全局 `__name = "_G"`；此后每次读写未定义全局都要走一次 C 函数 | `LuaModule` 构造函数、`_ensureMetaTable` | 只在该模块第一次注册 property 时才挂 metatable；模块名放进 registry 或 metatable，不写进模块表 |
| ~~**R9-8**~~ **已修复（第十轮）** 类 metatable 对脚本完全开放 | `o.__gc` 能取到（调用已安全，见 R7-1）；`getmetatable(o)` 返回真表，脚本可以改写方法（实测 `mt.get = ...` 后 `o:get()` 返回新值）；同理应能改掉 `__gc`、`__index`（未实测） | 方法直接存在 metatable 里，未设 `__metatable` | 方法放进独立的方法表，`__index` 指向它；metatable 设 `__metatable` 字段。注意 extend 的实例 metatable 是从类 metatable 拷贝的，要一起调整 |
| ~~**R9-9**~~ **已修复（第十轮）** 字符串在 NUL 处截断（双向） | Lua 的 `"a\0b"` 传给 `std::string` 参数长度为 1；C++ 返回长度 3 的 `std::string("a\0b", 3)`，Lua 侧 `#s` 为 1 | `LuaStack<std::string>` 经 `const char*` 转换（`c_str()` + `lua_pushstring` / `lua_tostring`） | `std::string` 改用 `lua_tolstring` / `lua_pushlstring`。`tests/test_strings.cpp` 把截断断言为「既定限制」，修的时候要同时改那几条断言 |
| ~~**R9-10**~~ **已修复（第九轮，仅 `T*` 参数的报错）** 报错里的 C++ 类型名不可读 | `cpp pointer 'P3Cat' expected`（mangled 名）；关闭 RTTI 时是 `?` | `RTTI_CLASS_NAME` | 类已绑定时用它在当前 state 的 lua 名，未绑定时再退回 RTTI 名 |
| ~~**R9-11**~~ **已修复（第九轮）** 拼写 | 内存分配失败的报错写成 `faild to alloc mem`（全文件 16 处 `faild`） | `ctor` / `fun` / `def` 注册路径 | 改为 `failed to allocate memory` |

### 改进建议

**功能缺口**

| 建议 | 现状 | 备注 |
|---|---|---|
| ~~C++ 继承~~ **已支持（第十轮）** | ~~实测：`Dog : Animal` 两个类都绑定后，`Dog` 实例传给 `const Animal&` 报 `Animal expected, got Dog`，也调不到 `Animal` 的方法~~ | ~~需要在绑定时声明基类（如 `LuaClass<Dog>(L, "Dog").inherit<Animal>()`），`get` 沿基类链做指针转换，`__index` 链到基类方法表。工作量最大的一项~~ |
| ~~`enum` / `enum class`~~ **已支持（第九轮）** | 原先能编译，但被当成未绑定的类，调用时报 `cpp class '5Color' not export` | 已按 `std::underlying_type` 加偏特化 |
| ~~智能指针~~ **已支持（第十轮）** | ~~不支持 `std::shared_ptr<T>` / `std::unique_ptr<T>` 作参数或返回值~~ | ~~userdata 里存智能指针本体，`__gc` 调其析构；与现有 `UserDataDetail` 的所有权标记合并设计~~ |
| ~~只能移动的返回类型~~ **已支持（第十轮）** | ~~实测：返回拷贝构造被删除的已绑定类编译失败（`LuaStack<T>::put` 调用了拷贝构造）；`LuaStackReturn(lua_State*, T t)` 也是按值接收~~ | ~~`put` 增加右值重载，返回路径用转发引用 + `std::move`~~ |
| ~~C++17 类型~~ **已支持（第十轮）** | ~~没有 `std::optional`（nil ↔ `nullopt`）、`std::string_view`~~ | ~~用 `__cplusplus` 条件编译；`optional` 也能顺带解决 R9-3 的部分需求~~ |
| ~~重载分发~~ **已支持（第十轮）** | ~~同名重载必须用不同 lua 名注册~~ | ~~可按参数个数和类型在运行时挑选，属于可选增强~~（只按个数 + 种类匹配，见「第十轮之后仍未验证或未做的」） |
| ~~类表上的静态函数与常量~~ **已支持（第十轮）** | ~~第八轮后可用 `LuaModule(L, "Cat")` 挂到类表上~~ | ~~可以加语法糖 `LuaClass::staticFun` / `LuaClass::constant`，省去再构造一个模块~~ |
| ~~嵌套模块名与 `require`~~ **已修复（第十轮）** | ~~5.1 路径（`luaL_openlib`）与 5.2+ 路径（`USE_NEW_MODULE_REGISTRY`）对 `"a.b"` 这类名字和 `package.loaded` 的处理不一致（未复核）~~ | ~~统一成显式建表，并可选写入 `package.loaded`~~ |

**性能**（基线：`-O2`、Lua 5.5、arm64，成员方法约 64 ns/次，自由函数约 16 ns/次）

- ~~`klassName(L)` 每次都查一次 registry，全文件 42 处调用，一次带 `self` 的方法调用要查 2–4 次。同一函数内缓存到局部变量，或直接比较 metatable 上的类型 tag，不取名字。~~ **已修复（第十轮）**：取对象只比较类型 tag。
- ~~每次方法调用都经过 C 实现的 `__index`（为了 property）。没有注册任何 property 的类可以直接用表做 `__index`，省掉一次 C 调用。成员方法比自由函数慢的部分有多少来自这里还没有单独测过，动手前先做一次剖析。~~ **已修复（第十轮）**：无 property / 基类 / 用户 `__index` 的类直接以方法表为 `__index`。

**工程**

- ~~**没有 CI。** 建议 GitHub Actions 跑 gcc / clang / MSVC × LuaJIT / PUC 5.1–5.5。MSVC 的调用约定分支（`__cdecl` / `__stdcall` / `__fastcall` 等）和 PUC Lua 5.1 / 5.2 / 5.3 至今从未实测，其中 5.2 上曾有编译不过的写法（第八轮顺带修了一处）。~~ **已建（第十轮）**：`.github/workflows/ci.yml`；PUC 5.1 / 5.2 / 5.3 已在本机实测。**CI 尚未在 GitHub 上跑过，MSVC 仍未实测。**
- ~~**测试矩阵只覆盖 `-std=c++11`。** C++14 的 tuple 快路径和 C++20 目前靠手工加 `STD=` 跑。`tests/run.sh all` 应把 C++ 标准也纳入矩阵。~~ **已修复（第十轮）**
- ~~**`tests/run.sh` 写死了 `/opt/homebrew`**（Lua 5.4 的 pkg-config 路径和回退的 include / lib 路径），Linux 和 Intel Mac 上探测不到 5.4。~~ **已修复（第十轮）**
- ~~**`tests/results.txt` 是运行产物但未加入 `.gitignore`**~~ 已加入（第九轮）。
- ~~**部分测试把已知的错误行为断言为「既定限制」**（如 `test_strings` 的 NUL 截断）。这类断言应单独标记，修复对应问题时好找。~~ **已修复（第十轮）**：统一加 `KNOWN-LIMITATION` 前缀。
- ~~**宏命名。** 内部宏没有 `LUAAA_` 前缀、不 `#undef`，`_NOTHING` 是保留标识符。~~ 已修复（第九轮）。
- ~~**全局命名空间的 polyfill。** 5.1 下的 `luaL_setfuncs`、`luaL_setmetatable`、`lua_rawgetp`、`lua_rawsetp` 定义在全局，和其他同样做兼容的库（如 lua-compat-5.3）一起用会重定义。改名为 `LUAAA_` 前缀的内部函数。~~ **已修复（第十轮）**
- ~~**重复代码。** 约 15 个近乎相同的 `Invoke` 辅助结构和 13 个容器特化可以各自合并成通用模板，头文件（现 4879 行）能明显缩短，也能避免像第八轮那样同一个修复要改 8 处。~~ **已修复（第十轮）**
- ~~**关闭 property 时有 12 条 `unused-parameter` 警告**（`LUAAA_FEATURE_PROPERTY=0` 且 `-Wextra`），默认配置下零警告。~~ **已修复（第十轮）**
- ~~**版本与变更说明。** 第七、八轮含行为变化（引用返回变别名、extend 实例模型、容器按 ipairs 语义读取、整数范围检查默认开启），版本号还没有升，也没有 CHANGELOG；建议升版本时把这些列为不兼容变更。~~ **已修复（第十轮）**：升为 2.0，新增 `CHANGELOG.md` / `CHANGELOG.zh-CN.md`。

### 建议的处理顺序

1. ~~改动小、收益直接的：R9-1、R9-5、R9-10、R9-11、`enum`、`.gitignore`、宏前缀与 `#undef`。~~ 已在第九轮完成。
2. ~~需要定规则或加开关的：R9-2、R9-3、R9-4、R9-9、R9-6（错误钩子）。~~ 已在第十轮完成。
3. ~~结构性改动，建议各自单独一轮：R9-8（独立方法表，可与「表 `__index`」的性能项一起做）、R9-7、C++ 继承、智能指针、合并重复模板。~~ 已在第十轮完成。
4. ~~CI 最好在第 3 步之前建好，否则 MSVC 和旧版 Lua 上的回归没有东西兜底。~~ CI 配置已建（第十轮），但尚未在 GitHub 上跑过。

## 总体结论

核心能力（类导出、构造/析构与 GC、成员/静态/lambda 函数、**类** property、容器与 tuple 往返、Lua 侧继承）工作正常，析构生命周期平衡（ctor=dtor=11），功能路径下 ASan/UBSan 无内存错误。

但存在 **3 个高危缺陷**（回调不可重复调用、Module property 完全失效、`std::pair` 入参不可用）和若干中低危问题。这些缺陷都不在 `example` 覆盖范围内，因此长期未被发现。

---

## 缺陷清单（按严重程度）

### ~~【高危 H1】传入 C++ 的 Lua 回调只能被调用一次~~ **已修复（第三轮）**
- 位置：`IMPLEMENT_CALLBACK_INVOKER` 及 `std::function` 特化，`luaaa.hpp:624 / 667 / 708 / 752`
- 根因：回调包装体在每次 `lua_pcall` 之后立即 `luaL_unref(...cacheLuaFuncId)`，把注册在注册表里的 Lua 函数释放掉。第二次调用时 `lua_rawgeti` 取回的是 `nil`，退化为返回 `nil`，随后 `LuaStack<RET>::get(nil)` 类型检查失败并 `lua_error`。
- 复现（`test_bugs.cpp` B2a/B2b）：C++ 侧 `f(10); f(20)` → 第二次报 `bad argument ... number expected, got nil`。对照组「只调用一次」正常。
- 影响：任何「多次触发的回调 / 事件处理器 / 比较器」用法都会失败。更严重的是错误以 `lua_error` 从 C++ 调用栈深处 `longjmp` 抛出，会**跳过途中 C++ 对象的析构**（异常构建下的资源泄漏/未定义行为）。
- 建议：不要在调用后 `unref`；应把 ref 的生命周期绑定到回调包装对象（例如用带 `shared_ptr`/析构 unref 的仿函数持有 ref），并在真正不再使用时释放。

### ~~【高危 H2】回调实现依赖 per-type `static` 槽，无法并发/重入/多回调~~ **已修复（第三轮，`std::function` 回调；函数指针回调为语言限制，保留为 `KNOWN-LIMITATION`）**
- 位置：同上，`static lua_State* cacheLuaState; static int cacheLuaFuncId;`（`luaaa.hpp:610-611 / 653-654 / 694-695 / 738-739`）
- 根因：同一函数签名的所有回调共用一对静态变量。`get()` 在取参数阶段就把 `cacheLuaFuncId` 覆盖为最后一个回调，导致同签名的多个回调互相踩踏；也不具备可重入性与线程安全。
- 复现（`test_bugs.cpp` B3）：`callTwoCallbacks(f, g)` 报错/结果错误。
- 影响：一次调用中传入两个同签名回调、或在多线程/多 state 下使用回调，行为不可预期。
- 建议：改为把 Lua 函数 ref 存入 upvalue / 用户数据随包装体传递，彻底移除 `static` 缓存。H1 与 H2 应一并重构。

### ~~【高危 H3】`std::pair` 作为入参完全不可用（get 与 put 不对称）~~ **已修复（第一轮）**
- 位置：`LuaStack<std::pair<U,V>>::get`，`luaaa.hpp:3229-3230`
- 根因：`get` 从**栈的绝对位置 `idx+1`、`idx+2`** 读取两个元素，而 `put` 是生成 `{[1],[2]}` 的 table。二者不对称——`get` 根本没有从传入的 table 里取值，而是去读 table 之后的栈槽。
- 复现（`test_bugs.cpp` B5）：`echoPair({10,20})` 报 `bad argument #2 ... got no value`；对照组「pair 仅作返回值（put）」正常。
- 影响：任何以 `std::pair` 为参数的导出函数不可用；若 `idx+1/idx+2` 恰好落在其它有效栈槽，还会读到错误数据而不报错。
- 建议：`get` 改为 `first = LuaStack<...>::get(rawgeti(idx,1)); second = LuaStack<...>::get(rawgeti(idx,2));`，与 `put` 对齐。

### ~~【高危 H4】LuaModule 的 property（get/set）完全失效~~ **已修复（第一轮）**
- 位置：注册端 `_registerModuleFunction` 的 `lua_rawset(m_state, -4)`（`luaaa.hpp:2334`）把 `<name>` / `>name` 闭包写入 **module table 自身**；查找端 `f_internal_index` / `f_internal_newindex` 却从 **metatable** 读取（`luaaa.hpp:2182/2184`、`2241/2243`）。两者层级不一致。
- 复现（`test_functional.cpp` “module prop get” 失败；`test_bugs.cpp` B15）：`MOD.ro` 恒为 `nil`，getter 从不触发。
- 连带影响：由于 setter 也存错层级，只写属性的写保护、只读属性的写保护（`attempt to write Read-Only ...`）等 Module 侧属性语义全部不生效（`test_bugs.cpp` B1 中读只写属性未报错）。注意 **类（LuaClass）的 property 是正常的**，仅 Module 受影响。
- 建议：统一存取层级——要么 getter/setter 也写入 metatable，要么查找改为查 module table。

---

### ~~【中危 M1】`luaL_error` 格式串参数不匹配（潜在 UB）~~ **已修复（第一轮）**
- 位置：`luaaa.hpp:2217`
```cpp
luaL_error(state, "attempt to read Write-Only property '%s' of '%s'", luaL_optstring(state, -1, "?"));
```
- 问题：格式串有 **两个 `%s` 只提供了一个实参**，第二个 `%s` 读取未提供的可变参数 → 未定义行为（可能崩溃/输出乱码）。且唯一实参取的是 `__name`（模块名），而第一个 `%s` 语义应为属性名 `key`，参数含义也错位。
- 现状：该分支因 H4 目前走不到，属「潜伏」缺陷；一旦修复 H4，此处会立即成为真实崩溃点。
- 建议：`luaL_error(state, "attempt to read Write-Only property '%s' of '%s'", key, <module name>);`

### ~~【中危 M2】对 non-trivially-copyable 类型（`std::function`）做 `memset`~~ **已修复（第二、四轮）**
- 位置：`luaaa.hpp:1680 / 2327 / 2347`，`memset(funPtr, 0, sizeof(F))` 后紧跟 `*funPtr = f`
- 问题：`F` 可能是 `std::function`。对一块**未构造**的存储 `memset` 置零，再用 `operator=` 赋值，属未定义行为（编译器已给出 `-Wnontrivial-memcall` 警告）。当前 libc++ 实现下“恰好能跑”，但不可依赖。
- 建议：改用 placement new：`new (funPtr) F(f);`，并在对象析构/GC 时显式调用析构（若需要）。

### ~~【中危 M3】同一 C++ 类无法在两个并存的 `lua_State` 中绑定~~ **已修复（第二轮，per-state registry）**
- 位置：`LuaClass<TCLASS>::klassName` 为 `static`（`luaaa.hpp:2121`）
- 复现（`test_edge.cpp` B13）：在 L1 绑定 `Animal` 后，L2 再绑定同类型直接报冲突错误。
- 影响：多 VM 场景（脚本沙箱、每线程一个 state）默认失败；这是隐式的全局状态耦合。（已由上文 M3 增强的 per-state registry 修复。）
- 建议：文档中显著说明该限制；或将 `klassName` 与注册信息改为按 `lua_State` 存储（如放入注册表）。

---

### ~~【低危 L1】`std::array<K,N>` 传入超长表：静默截断且内部栈残留~~ **已修复（第二轮）**
- 位置：`luaaa.hpp:2808`，`while (0 != lua_next(L, idx) && index < N)`
- 问题：当表元素多于 `N`，循环在 `index==N` 时退出，但最后一次 `lua_next` 压入的 key/value **未被 `lua_pop`**，造成局部栈残留；同时多余元素被静默丢弃。另外用 `lua_next` 取值依赖遍历顺序（对纯数组段通常有序，但非契约）。
- 复现（`test_bugs.cpp` B4）：未观察到崩溃（Lua 栈自动增长，函数返回时整体清理），属健壮性/整洁性问题。
- 建议：改用 `lua_rawgeti(idx, i)` 按下标读取 `N` 个元素，避免顺序依赖与栈残留。

### ~~【低危 L2】无效的 `static_assert`（语义写错）~~ **已修复（第二轮）**
- 位置：`luaaa.hpp:1878 / 1918`
```cpp
static_assert(std::is_same<std::decay<TCLASS>, std::decay<FCLASS>>::value, ...);
```
- 问题：比较的是 `std::decay<...>` **模板本身**而非其 `::type`。该断言实际等价于 `is_same<TCLASS,FCLASS>`（漏了 `typename ...::type`），既没有实现“忽略 cv/引用”的本意，也几乎总是恰好为真而形同虚设。
- 建议：`std::is_same<typename std::decay<TCLASS>::type, typename std::decay<FCLASS>::type>::value`。

### ~~【低危 L3】文案/健壮性琐项~~ **已修复（第二轮；`internal_name` 缓冲随第十轮按键取访问器一并删除）**
- `luaaa.hpp:1147` 错误信息 “use use LuaClass...” 重复单词。
- `f_internal_index` / `f_internal_newindex` 内部多处 `lua_pcall` 未检查返回码，getter/setter 内部报错会被静默吞掉（`luaaa.hpp:1204/1214` 等）。
- `internal_name[256]` 固定缓冲，超长属性名会被 `snprintf` 截断（一般无害，属边界）。

---

## 已验证正常的功能

（按第十轮之后的测试重写。第一次报告时只验证了下表中较少的项，其中回调只验证了单次调用。）

| 分类 | 用例 | 主要测试 |
|---|---|---|
| 类与构造 | 默认 / 带参 ctor、spawner、spawner + deleter、返回 `shared_ptr` 的工厂；缺参报错序号正确；`overloadCtor` | `test_functional`、`test_review9`、`test_review10` |
| 成员函数 | 可变 / const 成员、`T&` / `const T&` 返回为别名、只能移动的返回类型 | `test_functional`、`test_review8`、`test_review10` |
| 静态函数与常量 | `staticFun` / `constant`；类上 `def` 常量、模块数组常量 | `test_review10`、`test_static_const` |
| property | 类与模块的 getter / setter、只读 / 只写保护、访问器内抛异常 | `test_property`、`test_property_except` |
| 容器 | vector / list / forward_list / deque / set / map / array / pair / tuple 往返，按 `t[1..]` 读取，嵌套容器 | `test_containers`、`test_containers_edge`、`test_forward_list`、`test_fixes`、`test_review3`、`test_review8` |
| 数值与字符串 | 整数范围检查、等宽无符号按位往返、浮点 ±inf、`enum`；`std::string` 保留 NUL | `test_numeric`、`test_numeric_edge2`、`test_float_edge`、`test_cast`、`test_strings` |
| C++17 类型 | `std::optional`、`std::string_view` | `test_review10` |
| Module | 常量、函数、property、`def` 显式对象、嵌套名 `"a.b"` 与 `package.loaded`、不改动 `_G` | `test_functional`、`test_property`、`test_def_singleton`、`test_review10` |
| 生命周期 | GC 触发析构且 ctor=dtor；`__gc` 最多终结一次；deleter 在类表回收后仍有效；`shared_ptr` / `unique_ptr` 所有权 | `test_lifetime`、`test_gc_except`、`test_review7`、`test_review10` |
| 继承 | C++ 继承 `inherit<Base>()`（含多重继承指针调整）；Lua 侧 `luaaa:extend` 多级继承、override、`base` 调用 | `test_review10`、`test_extend`、`test_review8` |
| 回调 | `std::function` 回调多次调用、同签名并存、可重入、协程回收后仍可调用、出错不 longjmp、错误钩子 | `test_callbacks`、`test_review7`、`test_review10` |
| 错误与异常 | 转换失败时实参正常析构、C++ 异常转为 Lua 错误、metatable 受保护 | `test_exception`、`test_review8`、`test_review10` |
| 多 state 与并发 | 同一类型在并存 state 中用不同名字绑定；每线程一个 state（TSan） | `test_edge`、`test_concurrency` |
| 配置开关 | 关闭 property / extend / 名字冲突检查 / 整数范围检查，开启 nil 作 `nullptr` | `test_features*` |
| 内存 | 全部测试在 ASan+UBSan 下无报告 | `bash tests/run.sh all` |

## 总体评价

（按第十轮之后的代码重写；第一次报告时的评价见文末缺陷清单。）

设计简洁、接口友好，定位是「零 wrapper、单头文件」的绑定库。第一次报告中的高危、中危缺陷，以及第七至第十轮审阅提出的问题，都已修复并有回归测试。功能上补齐了 C++ 继承、智能指针、只能移动的返回类型、`enum`、C++17 类型、重载分发、类上的静态函数与常量。`std::function` 回调可以多次调用、可重入，出错时不会 longjmp 穿过 C++ 栈帧。

测试方面，`tests/` 现有 36 个测试文件，`bash tests/run.sh all` 在 LuaJIT / 5.4 / 5.5 × C++11/14/17/20 × ASan+UBSan 下共 6732 项通过。PUC Lua 5.1 / 5.2 / 5.3 已在本机从源码实测。另有 TSan 并发测试，以及无标准库、`-fno-exceptions -fno-rtti` 的嵌入式构建。第一次报告时的根本问题「缺乏针对性的自动化测试」已经解决。

**仍然存在的限制**（测试里以 `KNOWN-LIMITATION` 标出的，或在「第十轮之后仍未验证或未做的」中列出的）：

- **平台验证**：CI 还没在 GitHub 上跑过；MSVC（含各调用约定分支）没有实测。
- **函数指针回调**：同签名的回调共用一个槽位，会互相覆盖。这是 C++ 语言限制，需要多个回调时应改用 `std::function`。
- **`std::function` 回调的生命周期**：回调对象不能在其 `lua_State` 关闭之后才销毁，因为销毁时会调用 `luaL_unref`。
- **`const char*`**：在 NUL 处截断，也不能跨调用保存。要保留完整内容请用 `std::string`。
- **重载分发**：只按「个数 + 种类」匹配，不做隐式转换打分；`fun` 与 `overload` 不能在同一个名字下混用。同名 `ctor` 后注册的覆盖先注册的，只打印提示，需要并存时用 `overloadCtor`。
- **其他**：`shared_ptr` 参数不支持 `enable_shared_from_this`；C++17 的 `noexcept` 成员函数指针没有单独处理；模块上的运算符元方法（如 `__add`）存在模块表里，不起作用。

**优先级建议**：先把 CI 推到 GitHub 跑通，并补上 MSVC 实测，否则 Windows 上的回归没有东西兜底。其余各项都是功能增强或已文档化的限制，可以按需处理。

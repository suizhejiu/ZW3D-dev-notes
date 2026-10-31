# ZW3D 二次开发实战笔记

> 记录一次用 ZW3D 官方 C/C++ 插件 API 做批量建模时踩过的坑与验证过的方法。
> **本笔记不含任何厂商文件、头文件副本、软件二进制，也不含任何具体项目/赛题的几何数据。**
> 示例代码均为通用几何（圆柱、方块、圆的轮廓），仅用于说明 API 用法。
>
> 环境：ZW3D 2027 SP1 教育版 / Windows x64 / MSVC 2022+ / 中文界面
> 本文所有结论均在本机实测验证过，未验证的会明确标注。

---

## 目录

1. [环境与工具链](#一环境与工具链)
2. [插件基本结构：Init 只注册，不干活](#二插件基本结构init-只注册不干活)
3. [远程命令通道：不用点界面就能执行代码](#三远程命令通道不用点界面就能执行代码)
4. [建模 API 实战](#四建模-api-实战)
5. [草图：本系列最大的坑](#五草图本系列最大的坑)
6. [放样（Loft）](#六放样loft)
7. [批量生成框架](#七批量生成框架)
8. [错误码对照表](#八错误码对照表)
9. [编译与环境坑](#九编译与环境坑)
10. [稳定性坑](#十稳定性坑)
11. [未解决的问题](#十一未解决的问题)
12. [附录：最小可运行骨架](#十二附录最小可运行骨架)

---

## 一、环境与工具链

### 1.1 SDK 位置

```
<ZW3D 安装目录>\api\inc\        // 200+ 个 zwapi_*.h
<ZW3D 安装目录>\ZW3D.lib        // 导入库
<ZW3D 安装目录>\ZW3dRemote.exe  // 远程命令工具（官方自带）
```

### 1.2 编译命令（MSVC）

```bat
call "<VS>\VC\Auxiliary\Build\vcvars64.bat"
cd /d <你的工程目录>
cl /nologo /LD /utf-8 /MT /EHsc /O2 /D_CRT_SECURE_NO_WARNINGS ^
   /I"<ZW3D 安装目录>\api\inc" MyPlugin.cpp ^
   /link /LIBPATH:"<ZW3D 安装目录>" ZW3D.lib /OUT:MyPlugin.dll
```

**关键参数 `/utf-8`** —— 见第九节，不加会出玄学编译错误。

### 1.3 DLL 放置位置

```
%APPDATA%\ZWSOFT\ZW3D\<版本名>\custom\apilibs\
```

**不是**安装目录下的 `custom\apilibs`，也**不是**安装目录下的 `plugins\`（那是 Qt 插件目录）。

### 1.4 手动加载

界面：`应用程序管理器` → `插件应用程序` 页 → `加载Dll` → 选 DLL。

**注意**：这个按钮只在主界面（无文档或空文档）的 Ribbon 上出现，有实体文档时 Ribbon 切成"造型"页，按钮就没了。

---

## 二、插件基本结构：Init 只注册，不干活

### 2.1 这是最重要的一条

**DLL 的 Init 函数在 `loadApiModules` 阶段被调用，此时 UI 和文档系统都还没就绪。**

在 Init 里做任何建模动作（建文件、建实体、存盘、输出消息）都会触发 `c0000005` 访问违例，且崩溃栈会指向你自己的 DLL，看起来像"插件写错了"，实际上是"调用的时机不对"。

### 2.2 正确结构

```cpp
static void myBuild(void)   // 真正干活的函数
{
    // 建文件、建几何、保存、导出……
}

int MyPluginInit(void)
{
    cvxCmdFunc("myBuild", (void*)myBuild, 0.0);   // 只注册
    return 0;
}

int MyPluginExit(void)
{
    cvxCmdFuncUnload("myBuild");
    return 0;
}
```

导出的函数名必须匹配：`<DllName>Init` / `<DllName>Exit`。`MyPlugin.dll` → `MyPluginInit`。

### 2.3 判断依据

崩溃日志里能看到时间线：

```
####[Init:loadApiModules]
=== 你的消息 begin ===        ← Init 里的 cvxMsgDisp
Set scene anti-alias to off   ← 开始碰 UI
（崩溃）
```

只要你的消息出现在 `loadApiModules` 之后、紧接着就崩，基本就是这个原因。

---

## 三、远程命令通道：不用点界面就能执行代码

### 3.1 原理

ZW3D 支持以 server 模式启动并监听本地端口，配套的 `ZW3dRemote.exe` 可以向它发送命令，包括调用插件注册的命令。

### 3.2 用法

```bat
:: 1) 启动（后台监听，默认 8000 端口）
zw3d.exe -server

:: 2) 触发插件里注册的命令（命令名前加 ~）
ZW3dRemote.exe /R local "cmd=~myBuild"
```

返回值：成功时输出 `OK`。

### 3.3 注意

- 这是**官方自带功能**，不是破解或注入
- 但"批量自动化建模"这个用法属于超出常规交互的用法，**公开讨论前先评估适用性**
- 命令是异步的，发完要等几秒再检查产物文件

---

## 四、建模 API 实战

### 4.1 建文件与根对象

```cpp
// 设置工作目录（影响"另存为"的默认位置）
vxPath dir; strcpy_s(dir, "D:\\work\\out");
cvxFileDirectory(dir);

// 新建单对象零件文件（文件名不能带路径！）
vxLongPath pz; strcpy_s(pz, "demo.z3prt");
cvxFileNewSingle(pz, VX_FILE_PART, VX_NORMAL_PART, NULL, NULL);
```

**坑**：`cvxFileNew` 传完整路径会返回 `-241`（文件名含非法字符，因为 `\` 是非法字符）。

**根对象**：单对象文件创建后**自带一个 root，名字就是文件名**（不含扩展名）。

```cpp
char root[128] = {0};
cvxRootInqActive(root, sizeof(root));   // 拿到 root 名
```

**不要**再调 `cvxRootAdd`——单对象文件上会返回 `-7`（单/多对象类型冲突）。

### 4.2 基本几何

```cpp
// 圆柱（沿 Z，Center 是底面圆心）
svxCylData c; cvxPartCylInit(&c);
c.Combine = VX_BOOL_NONE;   // 第一刀用 NONE，之后用 ADD / REMOVE
c.Center.x = 0; c.Center.y = 0; c.Center.z = 0;
c.Radius = 10; c.Length = 20;
int id = 0;
cvxPartCyl(&c, &id);

// 圆锥（Radius1 底、Radius2 顶）
svxConeData co; memset(&co, 0, sizeof(co));
co.Combine = VX_BOOL_ADD;
co.Center.z = 20; co.Radius1 = 10; co.Radius2 = 5; co.Length = 10;
cvxPartCone(&co, &id);

// 方块（Center 是体中心，不是角点）
svxBoxData b; cvxPartBoxInit(&b);
b.Combine = VX_BOOL_NONE;
b.Center.x = 0; b.Center.y = 0; b.Center.z = 5;
b.X = 40; b.Y = 30; b.Z = 10;
cvxPartBox(&b, &id);
```

**布尔类型**：`VX_BOOL_NONE` / `VX_BOOL_ADD` / `VX_BOOL_REMOVE` / `VX_BOOL_INTERSECT`。

### 4.3 任意方向的孔（重点）

**问题**：`cvxPartCyl` 的 `useAxis` / `axis.Dir` 实测**不生效**，圆柱永远沿 Z。

**解法**：在目标平面上建草图，画圆，沿法向拉伸切除。

```cpp
// 例：在 XZ 平面（法向 = 世界 Y）画孔
svxMatrix m;
memset(&m, 0, sizeof(m));
m.xx = 1; m.mx_yx = 0; ...  // 见下方矩阵构造
```

**矩阵构造规则（关键）**：`svxMatrix` 是**列主序**，字段含义：

```
xx, yx, zx, xt    ← 第一列：草图 X 轴在世界坐标中的方向 + 平移 X
xy, yy, zy, yt    ← 第二列：草图 Y 轴
xz, yz, zz, zt    ← 第三列：法向（拉伸方向）
                   ← 第四列：平移（xt, yt, zt）
```

| 目标平面 | 草图X → 世界 | 草图Y → 世界 | 法向 → 世界 | 矩阵赋值 |
|---|---|---|---|---|
| XY（默认） | X | Y | Z | `xx=yy=zz=1`，其余 0 |
| XZ | X | Z | Y | `xx=1, zy=1, yz=1`，其余 0 |
| YZ | Y | Z | X | `yx=1, zy=1, xz=1`，其余 0 |

**草图原点平移**：`xt` / `yt` / `zt` 对应世界 XYZ 的偏移。

代码示例（XZ 平面，原点抬到 z=37）：

```cpp
svxMatrix m; memset(&m, 0, sizeof(m));
m.xx = 1;                       // 草图X → 世界X
m.zy = 1;                       // 草图Y → 世界Z
m.yz = 1;                       // 法向 → 世界Y
m.zt = 37.0;                    // 原点抬到 z=37
```

### 4.4 径向孔（轴上的孔）

同上思路：在 XZ 或 YZ 平面的对应位置画圆，沿法向拉伸切除。

**范围要给够**——拉伸的 Start/End 必须覆盖整个零件厚度，否则孔切不透。实测中我给 `-12..12`，而零件厚度 14，结果孔只切到 12，体积少减 21%。

### 4.5 键槽

键槽不需要草图，直接用方块减：

```cpp
// 宽 12、深 3.3、长 26 的键槽，从孔壁 r=21.5 处向外挖
box(VX_BOOL_REMOVE, 0.0, 23.15, 2.0, 12.0, 3.3, 26.0);
```

**坑**：切除盒的深度不要给成"键槽宽度"，那是两个概念。给大了会把孔壁切穿（实测减量从应有的 594 变成 2541）。

---

## 五、草图：本系列最大的坑

### 5.1 创建与进入（关键！）

```cpp
// 建草图
svxMatrix m; memset(&m, 0, sizeof(m));
m.identity = 1; m.xx = 1; m.yy = 1; m.zz = 1;
int sk = 0;
cvxPartSkNew(&m, &sk);

// ★ 进入草图（这一步最容易漏！）
cvxSkActivate(sk, 0);

// 画几何……
cvxSkAddPolyline(n, pts, 0);

// 退出
cvxRootExit();
cvxSkActivate(0, 0);
```

**`cvxSkActivate(sk, 0)` 与 `cvxSkActivate(0, 0)` 的区别**：

- `cvxSkActivate(sk, 0)` —— **进入**指定草图（`sk` 是草图 id）
- `cvxSkActivate(0, 0)` —— **退出**到零件级

**只调 `cvxSkActivate(0,0)` 而不调带 id 的那个，几何画不进去**，返回 `-10000`。

### 5.2 `cvxRootExit()` 会弹对话框

`cvxRootExit()` 内部会检查"最后一张草图是否有几何"，没有就弹「是否删除空草图」的对话框。

**问题**：在自动化流程里这个对话框会**阻塞执行**，而且如果点了"否"，草图几何会被丢弃，后续拉伸返回 `-152`。

**对策**：
1. 保证草图里确实画上了几何再调 `cvxRootExit()`
2. 或者在无对话框需求时，**只调 `cvxSkActivate(0,0)` 退出**（实测可行）

### 5.3 闭合样条 vs 多段线：能不能放样

| 场景 | 能用多段线 | 必须用样条 |
|---|---|---|
| 拉伸（Extrude） | ✅ | ✅ |
| 放样（Loft） | ❌ | ✅ |

**闭合样条的正确写法**：

```cpp
svxSplineData sd; memset(&sd, 0, sizeof(sd));
cvxSkSplineInit(&sd, n);        // n = 点数
sd.SplineTyp     = 0;           // 0 = 通过点
sd.Degree        = 1;           // ★ 1 = 直线段，保留尖角（默认 3 会圆滑掉尖角）
sd.CreateCrvOpen = 0;           // ★ 0 = 闭合（默认 1 是开放）
sd.Count         = n;
for (int i = 0; i < n; i++) {
    sd.Pnts[i].Pnt.x = ...; sd.Pnts[i].Pnt.y = ...; sd.Pnts[i].Pnt.z = 0;
    sd.Pnts[i].UseCons = 0;
}
int sp = 0;
cvxSkSpline(&sd, &sp);          // sp 返回轮廓 id
```

**两个参数缺一不可**：
- `Degree = 1`：默认 3 次会把折线的尖角磨圆，齿轮齿形就废了
- `CreateCrvOpen = 0`：默认是开放曲线，不闭合就无法围成面

### 5.4 同一零件里画多张草图

**规则**：所有草图必须在**第一次拉伸之前**全部建好。

### 5.4.1 任意平面（含 YZ）的正确建法 —— 2026-10-31 修订

**旧结论（错误）**：手填 `svxMatrix` 的 12 个分量，YZ 平面（法向世界 X）怎么也建不出来，
曾判定为"厂商 API 限制，不再深挖"。

**正确做法**：不要手填矩阵，用官方 `zwapi_matrix.h` 的构造函数：

```c
#include "zwapi_matrix.h"

szwPoint  origin = {0, 0, 0};
szwVector zAxis  = {1, 0, 0};        /* 法向指向世界 X，即 YZ 平面 */
szwMatrix m;
ZwMatrixGetByPointAndZAxis(origin, zAxis, &m);

int sk = 0;
svxMatrix sm;                        /* szwMatrix 多出 ox/oy/oz/scale，显式拷贝更安全 */
memset(&sm, 0, sizeof(sm));
sm.identity = m.identity;
sm.xx = m.xx; sm.yx = m.yx; sm.zx = m.zx; sm.xt = m.xt;
sm.xy = m.xy; sm.yy = m.yy; sm.zy = m.zy; sm.yt = m.yt;
sm.xz = m.xz; sm.yz = m.yz; sm.zz = m.zz; sm.zt = m.zt;

cvxPartSkNew(&sm, &sk);
```

**实测**：XY / XZ / YZ 三个平面全部正确。Ø20 圆柱拉伸 10，包围盒分别为
`20×20×10`、`20×10×20`、`10×20×20`。

**教训**：`zwapi_matrix.h` 有 33 个函数，包括 `ZwMatrixInitByTranslation`、
`ZwMatrixInitByRotation`、`ZwMatrixGetByPointAndXYAxis`、`ZwMatrixCoordinateSystemGet`、
`ZwMatrixNormalize`、`ZwPointTransform` 等。**先读这个头文件，再动手填矩阵。**

**原因**：拉伸一旦生成实体，活动根从"零件"变成"实体"，此时再 `cvxPartSkNew` + `cvxSkAddPolyline` 会返回 `-10000`。

**正确顺序**：

```cpp
// 1) 草图 A
cvxPartSkNew(&mA, &skA);
cvxSkActivate(skA, 0);
cvxSkAddPolyline(...);
cvxRootExit();

// 2) 草图 B（此时零件根仍然活动）
cvxPartSkNew(&mB, &skB);
cvxSkActivate(skB, 0);
cvxSkSpline(...);
cvxRootExit();
cvxSkActivate(0, 0);

// 3) 拉伸 A
cvxPartExtrude(&extA, &shA);

// 4) 拉伸 B（切除）
cvxPartExtrude(&extB, &shB);
```

**不要**在两次拉伸之间建草图。

### 5.5 带孔的轮廓（环形）

**错误做法**：一条曲线走"外圈一圈 + 桥接 + 内圈一圈"（8 字形）——ZW3D 判为自交，拉伸体积为 0。

**正确做法**：在同一草图里画**两条独立的闭合曲线**（外圈 + 内圈），然后拉伸时 `idProfile` 传**草图 id**（不是曲线 id）。内圈会被自动识别成孔。

```cpp
int sk = 0; cvxPartSkNew(&m, &sk);
cvxSkActivate(sk, 0);
cvxSkSpline(&outer, &sp1);     // 外圈闭合
cvxSkSpline(&inner, &sp2);     // 内圈闭合，完全在外圈内部
cvxRootExit(); cvxSkActivate(0, 0);

svxExtrudeData e; cvxPartExtrudeInit(&e);
e.Combine = VX_BOOL_REMOVE;
e.idProfile = sk;               // ★ 草图 id
e.Start = -5; e.End = 0;
cvxPartExtrude(&e, &sh);
```

**约束**：两圈不能相交、不能相切，内圈必须完全在外圈内。

---

## 六、放样（Loft）

### 6.1 用途

做锥形、变截面、锥齿轮齿形等"多层轮廓过渡"的实体。

### 6.2 结构

```cpp
svxLoftProfile lp[3];
memset(lp, 0, sizeof(lp));
for (int i = 0; i < 3; i++) {
    lp[i].idParent  = sketchId[i];   // ★ 草图 id
    lp[i].idProfile = splineId[i];   // ★ 轮廓曲线 id（不是 0，不是草图 id）
}

svxLoftData ld;
cvxPartLoftInit(&ld);
ld.combine  = VX_BOOL_NONE;
ld.type     = VX_LOFT_PROFS;
ld.numProfs = 3;
ld.profs    = lp;
ld.endCap   = 0;      // 0 = 两端封盖
ld.close    = 0;
int sh = 0;
cvxPartLoft(&ld, &sh);
```

**关键**：`idParent` 是草图 id，`idProfile` 是**轮廓曲线**的 id。两者不是同一个东西，混淆了会失败。

### 6.3 层间对应

多层放样时，各层的点必须**同起点、同绕向**，否则放样体会扭曲。

建议：所有层用同一个参数化函数生成（同一角度步长、同一方向），只改半径/高度。

### 6.4 验证放样是否成实体

看导出的 STEP 里有没有 `MANIFOLD_SOLID_BREP`：

```python
s = open('out.stp', encoding='utf-8', errors='ignore').read()
print('SOLID:', s.count('MANIFOLD_SOLID_BREP'))   # 0 = 没成实体
```

如果体积是 0 而文件不小，通常是"只有曲线没有面"。

---

## 七、批量生成框架

### 7.1 结构

一个 DLL 里注册多个命令，每个零件一个函数：

```cpp
static void partA(void) { ... }
static void partB(void) { ... }
static void buildAll(void) { partA(); partB(); }

int MyPluginInit(void)
{
    cvxCmdFunc("partA",    (void*)partA,    0.0);
    cvxCmdFunc("partB",    (void*)partB,    0.0);
    cvxCmdFunc("buildAll", (void*)buildAll, 0.0);
    return 0;
}
```

### 7.2 每个零件建完必须关文档

```cpp
cvxFileClose();   // 放在每个零件函数的末尾
```

**不关的后果**（实测）：连续建 13 个零件不关，进程内存涨到 1.2 GB，之后远程命令**不再响应**——端口还在监听，但命令发过去没反应，表现为"假活"。

关掉之后，连续跑 14 个零件，内存只涨 24 MB。

### 7.3 写日志到文件，而不是只靠界面输出

`cvxMsgDisp` 的输出在 server 模式下看不到。建议：

```cpp
static FILE *g_log = NULL;
static void lprintf(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap); va_end(ap);
    fflush(g_log);
}

// 每个零件函数开头
if (!g_log) fopen_s(&g_log, "D:\\work\\log\\build.txt", "w");
```

**并且**：每个零件建完往 CSV 追加一行"零件名,体积"，方便单独触发时也能核对：

```cpp
FILE *f = NULL;
fopen_s(&f, "D:\\work\\log\\parts.csv", "a");
if (f) { fprintf(f, "%s,%.3f\n", name, vol); fclose(f); }
```

### 7.4 体积自检

```cpp
double vol = 0, mass = 0;
cvxPartVolumAndMassGet(root, &vol, &mass);
```

**用途**：和理论值对比，能快速判断几何是否建对。实测中这个手段抓出过好几处尺寸错误（比如键槽切除盒给大了）。

---

## 八、错误码对照表

| 码 | 名称 | 常见原因 |
|---|---|---|
| `-5` | `ZW_API_ROOT_OBJ_ACT_FAIL` | 没有活动根对象；或建几何前没激活文件 |
| `-6` | `ZW_API_ROOT_SINGLE_MULTI_TYPE_ERROR` | 单对象文件上调了多对象接口 |
| `-7` | 同上（另一种表述） | 单对象文件调 `cvxRootAdd` |
| `-31` | `ZW_API_INVALID_INPUT` | 参数传了 NULL（如 `cvxFileExport` 的 STEP 数据） |
| `-152` | `ZW_API_CMMD_EXEC_ERROR` | 命令执行失败——**最常见的是草图里没几何** |
| `-241` | 文件名含非法字符 | `cvxFileNew` 传了带 `\` 的完整路径 |
| `-242` | 文件格式不被接受 | 手工拼的 xlsx 喂给 `ZwConfigTableImport` |
| `-10000` | 未公开 | **草图未激活**时调 `cvxSkAddPolyline` / `cvxSkSpline` |

**`-10000` 值得单独说**：这个码不在标准 `evxErrors` 列表里，官方文档没写。实测出现的唯一场景是"在非草图环境中画草图几何"。

---

## 九、编译与环境坑

### 9.1 必须加 `/utf-8`

**现象**：源码里有中文注释时，编译报出莫名其妙的错误：

```
error C2267: "buildAll": 具有块范围的静态函数非法
note: 此行有一个"{"没有匹配项
```

用脚本逐字符扫描确认大括号**完全平衡**，但编译器坚持报错。

**原因**：MSVC 默认按系统代码页（简体中文是 936/GBK）解析源文件。UTF-8 编码的中文注释在 GBK 下解码时会错位，某些字节被误判成 ASCII 的 `{` 或 `}`，编译器"看到"了不存在的括号。

**解法**：编译加 `/utf-8`。或者把源文件存成 GBK。

### 9.2 路径不能有中文/空格问题

安装路径含中文会导致 `Start-Process` 报 `DirectoryNotFoundException`，尤其在 PowerShell 里拼接路径时容易出错。

**建议**：所有路径用变量存，不要手写拼接。

### 9.3 中文输出乱码

`cvxMsgDisp` 按 GBK 解读字符串，UTF-8 源码里的中文会变成乱码（如"寤堕暱鏉"）。

**建议**：日志和消息全用英文。

---

## 十、稳定性坑

### 10.1 文档累积 → 命令假死

见 7.2。症状：进程在、端口在监听、但命令不响应。解决：加 `cvxFileClose()`，或者每 N 个零件重启一次 server。

### 10.2 弹窗阻塞自动化

除了 `cvxRootExit()` 的空草图确认，还有：

- 另存为对话框：`cvxFileSave3` 会弹，改用 `cvxFileSaveAs(完整路径)`
- 首次启动的引导弹窗

**通用对策**：自动化流程里避免调用任何"可能弹框"的接口；如果必须调，确保前置条件满足（比如草图里确实有几何）。

### 10.3 崩溃日志的位置

```
%APPDATA%\ZWSOFT\ZW3D\ZW3DCrashReport\
├─ BugReport\<时间戳>.zip     // 含 dump + TraceStack.txt + crash.log
└─ RuntimeRecord\*.xml        // 每次启动的 Status：Crash / None
```

**`RuntimeRecord` 很有用**：能快速区分"崩溃"和"正常退出"。如果全是 `None`，说明进程是正常退出的，"没反应"是别的原因（比如文档累积导致的假死）。

### 10.4 崩溃栈的读法

`TraceStack.txt` 是倒序的（#1 是最外层调用）。找你自己的 DLL：

```
#22 <你的DLL>.dll + 0x11FE  →  ZW3D.dll  →  ZW3D_CAD.dll  →  ...
```

如果是你的 DLL 出现在栈里，且 `crash.log` 里你的消息紧挨着崩溃，那基本就是插件的问题。

---

## 十一、未解决的问题

### 11.1 配置表导出（变量能建，但进不了配置表）

**目标**：让 ZW3D 导出一份含变量的配置表 xlsx。

**试过的方法**：

| 方法 | 返回 | 结果 |
|---|---|---|
| `ZwVariableListSet(count, list, working)` 批量建 | `-10000` | 失败 |
| `ZwVariableCreate(单个)` | `0` | 变量建进了模型 |
| `ZwConfigTableExport(path, name)` | `0` | 导出的表**只有表头，没有变量** |
| `ZwConfigTableImport(手搓 xlsx)` | `-242` | 格式被拒 |
| `ZwConfigTableImport(软件空表填好后导入)` | `0` | 读进去了，但内容**不生效** |

**结论**：配置表需要三层东西——变量定义、配置（config）实体、变量与配置的绑定。`ZwVariableCreate` 只完成第一层，后两层没有公开 API。

**2026-10-31 追加证据**：用 `ZwVariableListGet` 读那个"建过 11 个变量"的文件，返回 `count = 0`。

```c
int count = 0;
szwVariableData *list = NULL;
ZwVariableListGet(NULL, &count, &list);   /* 返回 0，但 count = 0 */
```

**说明 `ZwVariableCreate` 返回 0 并不代表变量真的写进了文件**。和 `ZW3dRemote` 返回 `OK` 一样，
"调用没报错" ≠ "真的生效"。这是本系列踩得最深的一个坑。

**另一个坑**：`ZwVariableDelete` 会弹模态对话框，在自动化流程里调用会**直接卡死插件**，
只能强杀 ZW3D 进程。不要在脚本里调它。

**正确用法（2026-10-31 验证通过）**：变量要在**建几何之前**设定，并让尺寸引用变量名，
而不是"建完几何再改变量"。见第 13 节。

**目前只能界面操作**：变量管理器建变量 → 配置表加变量 → 导出。

### 11.2 几何尺寸引用变量

API 建的几何，尺寸是硬编码的 `double`（`c.Radius = 10.5`），不是变量表达式。没有找到"把半径设成表达式"的接口。

**含义**：用 API 建的模型是"静态"的，改变量不会让几何变。真正的参数化需要在界面里交互建模（标注时选变量）。

### 11.2.1 旋转/平移：`cvxPartMoveTransform` 已废弃，不要用

**现象**（2026-10-31 实测）：

```c
szwMatrix m;
ZwMatrixInitByRotation(rad, axis, &m);          /* 注意：角度单位是弧度 */
cvxPartMoveTransform(entityId, (svxMatrix*)&m); /* 返回 0 */
```

- 返回 0（看起来成功）
- 但旋转后包围盒完全不变
- 平移则变成"复制 + 移动"：原位置的体仍在，包围盒翻倍

**原因**：该函数标记 `@deprecated`，官方注释原文：

> This function will not be updated anymore, please use ZwFeatureMoveCreateByTransform()

**替代**：`ZwFeatureMoveCreateByTransform(int count, const szwEntityHandle *handleList, szwMatrix matrix)`
需要 `szwEntityHandle`（不是 int id），未验证。

**建议**：**在目标位置直接建模**，不要"建完再转"。旋转/平移类需求用"按方向建草图"绕过
（见 4.3 任意方向的孔）。

### 11.3 圆周阵列（`cvxPartPatternGeom`）

返回 `ret=0`，但实测**复制的是整个 part**而不是选中的几何：

- `combine=REMOVE`：体积 187,433（预期 93,717）
- `combine=NONE`：体积 206,283（正好是 100k + 100k + 6k）

推测：布尔运算后整个 part 成了一个 body，阵列按 body 复制。

**替代方案**：多层放样（见第六节），或者手动循环创建。

### 11.4 `svxPtnData` 的 union 访问

头文件里是：

```c
union PtnData { ... struct svxCircular {...} circular; ... };
```

带标签的 union 在 C++ 下无法直接访问成员（`pd.circular` 编译不过）。

**绕法**：定义一份布局等价的镜像结构，强转指针：

```cpp
struct MIRROR_PTN { evxBoolType combine; evxPatternType type; union { MIRROR_CIRCULAR circular; } u; };
MIRROR_PTN *mp = (MIRROR_PTN*)&pd;
mp->u.circular.num[0] = 8;
```

---

## 十二、附录：最小可运行骨架

```cpp
// demo.cpp —— 最小可运行示例（建一个带通孔的方块）
#include <stdio.h>
#include <string.h>
#include "zwapi_root.h"
#include "zwapi_cmd.h"
#include "zwapi_cmd_shape.h"
#include "zwapi_cmd_shape_data.h"
#include "zwapi_file.h"
#include "zwapi_file_data.h"
#include "zwapi_part_attribute.h"

#ifdef __cplusplus
extern "C" {
#endif
__declspec(dllexport) int DemoInit(void);
__declspec(dllexport) int DemoExit(void);
#ifdef __cplusplus
}
#endif

static FILE *g_log = NULL;

static void scenario(void)
{
    // 打开日志
    fopen_s(&g_log, "D:\\work\\log\\demo.txt", "w");
    if (g_log) fprintf(g_log, "start\n"), fflush(g_log);

    // 1) 建文件
    vxPath dir; memset(dir, 0, sizeof(dir));
    strcpy_s(dir, "D:\\work\\out");
    cvxFileDirectory(dir);

    vxLongPath fn; memset(fn, 0, sizeof(fn));
    strcpy_s(fn, "demo.z3prt");                    // 不能带路径
    cvxFileNewSingle(fn, VX_FILE_PART, VX_NORMAL_PART, NULL, NULL);

    // 2) 拿 root 名（单对象文件自带，不需要 cvxRootAdd）
    char root[128] = {0};
    cvxRootInqActive(root, sizeof(root));

    // 3) 方块
    svxBoxData b; cvxPartBoxInit(&b);
    b.Combine = VX_BOOL_NONE;
    b.Center.x = 0; b.Center.y = 0; b.Center.z = 10;
    b.X = 40; b.Y = 30; b.Z = 20;
    int id = 0;
    cvxPartBox(&b, &id);

    // 4) 中心通孔（沿 Z，直接减即可）
    svxCylData c; cvxPartCylInit(&c);
    c.Combine = VX_BOOL_REMOVE;
    c.Center.x = 0; c.Center.y = 0; c.Center.z = -1;
    c.Radius = 5; c.Length = 22;
    cvxPartCyl(&c, &id);

    // 5) 体积自检
    double vol = 0, mass = 0;
    cvxPartVolumAndMassGet(root, &vol, &mass);
    if (g_log) fprintf(g_log, "volume = %.3f\n", vol), fflush(g_log);

    // 6) 保存 + 导出
    vxLongPath pz; memset(pz, 0, sizeof(pz));
    strcpy_s(pz, "D:\\work\\out\\demo.z3prt");
    cvxFileSaveAs(pz);

    vxLongPath ps; memset(ps, 0, sizeof(ps));
    strcpy_s(ps, "D:\\work\\out\\demo.stp");
    svxSTEPData sd; memset(&sd, 0, sizeof(sd));
    sd.AppProtocol = 1; sd.OutPut = 0; sd.ExportType = 0;
    cvxFileExport(VX_EXPORT_TYPE_STEP, ps, &sd);

    // 7) 关文档（防累积）
    cvxFileClose();

    if (g_log) fclose(g_log), g_log = NULL;
}

int DemoInit(void)     { cvxCmdFunc("demoScenario", (void*)scenario, 0.0); return 0; }
int DemoExit(void)     { cvxCmdFuncUnload("demoScenario"); return 0; }
```

**触发**：

```bat
zw3d.exe -server
ZW3dRemote.exe /R local "cmd=~demoScenario"
```

---

## 十三、参数化：让尺寸引用变量（2026-10-31 验证通过）

这一节是本系列最实用的内容——**让 API 建的几何受变量驱动**。

### 13.1 关键 API

```c
cvxSkAddConsDim(...)          /* 在草图里加约束尺寸，拿到尺寸 id */
cvxDimValMod(idDim, "D")      /* 把尺寸值设成变量名（不是数字） */
```

缺了这两个，API 建的几何只能是硬编码的 `double`。

### 13.2 完整流程

```c
/* 1) 先建变量（注意：必须在建几何之前） */
szwVariableData v;
memset(&v, 0, sizeof(v));
strcpy_s(v.name, "D");
v.type = ZW_VARIABLE_NUMBER;
v.value.numberValue.subType = ZW_VARIABLE_DISTANCE;
v.value.numberValue.number  = 80.0;
ZwVariableCreate(v);          /* 返回 0 不代表写进文件，但内存里有效 */

/* 2) 画圆 */
svxPoint2 c = {0, 0};
cvxSkCirRad(&c, 40.0, &idCircle);

/* 3) 加直径尺寸 */
svxConsDim cd;
memset(&cd, 0, sizeof(cd));
cd.type = VX_SK_CONSDIM_RADIAL;
/* ... 填 cd 的实体引用 ... */
cvxSkAddConsDim(VX_SK_CONSDIM_RADIAL, &cd);

/* 4) 把尺寸值设成变量名 —— 关键一步 */
cvxDimValMod(idDim, "D");

/* 5) 拉伸 */
cvxPartExtrude(...);
```

### 13.3 实测数据

| 场景 | 结果 |
|---|---|
| D=80, d=25, t=5，圆画 r=40/12.5，尺寸填 `D`/`d` | 体积 **22,678.372** = π(40²−12.5²)×5 |
| 先建 D=100，圆仍画 r=40/12.5，尺寸填 `D`/`d` | 几何**自动跳到 r=50**，体积 **36,815.539** = π(50²−12.5²)×5 |

**第二行是关键**：说明"尺寸框里填变量名"不是摆设，几何会跟着变量走。

### 13.4 三个必须知道的限制

**限制一：事后改变量不会自动重建**

```c
ZwVariableListSet(1, &v, 0);   /* D=100，返回 0 */
/* 但体积不变 */
```

文档说这个函数会 "regenerate the root object"，实测不动。`working` 参数的含义是
"1 显示 working 消息，0 不显示"，`-1` 才是"禁止重建"——也就是说传 0 本来就该重建，
但几何没动。原因可能是尺寸表达式缓存了求值结果。

**结论：不要指望"改变量触发重建"。**

**限制二：`ZwVariableDelete` 会弹模态框**

在自动化脚本里调用会直接卡死插件，只能强杀 ZW3D 进程。**不要在批量流程里调它。**

**限制三：变量必须在建几何之前建**

如果先建几何、后建变量，再想让尺寸引用变量，会因为没有尺寸 id 而失败。

### 13.5 对批量参数化的正确做法

**不要**：建一个零件 → 循环改参数 → 指望它自动变

**要**：

```
for 每个参数组合:
    新建零件
    设变量（这个配置的值）
    建几何（尺寸用 cvxDimValMod 绑到变量名）
    拉伸
    导出 STEP
    关闭文档
```

**每个配置完整重跑一遍**。慢，但可靠。

---

## 十四、官方资源的正确用法（2026-10-31 补充）

### 14.1 安装目录里有一整套资料，别去网上找

```
<ZW3D 安装目录>\api\
├── ApiExample.zip              官方示例，20 个 VS 工程
├── ApiHelpDoc.chm              英文 API 帮助（22 MB）
├── ApiHelpDoc_chs.chm          中文 API 帮助（23 MB）
├── ZW3D API introduction.pdf   英文介绍
├── ZW3D API introduction_Chs.pdf  中文介绍
└── DevelopmentTemplate\        工程模板
```

**20 个示例的目录**（每一个都对应一类常见需求）：

```
1.BaseInquiry          基础查询
2.TopoInquiry          拓扑查询（拿实体 id）
3.ViewTool             视图/图层
4.FilletBox            倒圆角
5.FileOperation        文件操作（带示例 .Z3）
6.Pattern              阵列
7.CurveCreate          曲线创建
8.NurbsSurface         NURBS 曲面
9.FileExport           导出
10.IgsOperation        IGES 操作
11.OptAuxframe         辅助框架
12.DllRegister         插件注册
13.TableSet            表格
14.ColoredBox          着色
15.EditVariable        变量编辑  ← 配置表相关
16.EntityName          实体命名
17.ExTrudewithPreview  带预览的拉伸
18.LayerEntityManager  图层/实体管理
19.MatrixOperations    矩阵操作  ← 矩阵相关
20.ChamferAddAndDelete 倒角增删
```

### 14.2 插件注册有官方工具，不用手动拷 DLL

`12.DllRegister` 是一个独立的命令行工具：

```
DllRegister -n <KeyName> -a <PluginName.dll> -d "<描述>" -l <所在目录> [-r <资源文件>]
```

对比：手动把 DLL 拷到 `%APPDATA%\...\custom\apilibs\` 再在界面「加载Dll」——
**这是绕远路**，而且容易漏掉注册信息。

### 14.3 CHM 里的坑

`.chm` 是编译过的帮助文件，用 `hh.exe` 打开。示例源码的编码**不统一**：
`19.MatrixOperations.cpp` 是纯 ASCII，`15.EditVariable.cpp` 是 UTF-16 变体
（BOM 是 `FF FE` 但按 LE 解出来仍然乱码，疑似非标准保存）。

**建议**：直接用 Visual Studio 打开 `.sln`，不要用文本编辑器硬解。

### 14.4 这个教训值得单列

本项目前期花了大量时间"试错"，而答案一直在安装目录里。教训：

1. **拿到 SDK 第一件事：清点 `api` 目录**
2. **遇到 API 问题，先搜 CHM，再动手试**
3. **"试出来的结论"只能写"实测如此"，不能写"不支持"**

本项目就因为没做第 2 条，把 `plane 2` 误判成"厂商限制"，还在笔记里写了错误结论。

---

## 免责声明

- 本文仅记录个人开发过程中验证过的方法与观察，**不构成对任何软件的使用授权**
- 文中不含任何厂商的头文件内容、二进制文件或商业文档
- 使用 ZW3D 的 API 需遵守其官方许可协议
- 示例代码为通用几何演示，与任何具体项目、赛题、作品无关

---

**版本**：v1 · 2026-10-31

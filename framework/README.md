# framework — 代码骨架

把 ZW3D 二次开发中踩过的坑，封装成可复用的函数。

**目标**：让你写业务代码时，不用再操心「草图进没进去」「文档关没关」「矩阵怎么填」。

---

## 文件

| 文件 | 行数 | 说明 |
|---|---|---|
| `qjw_core.h` | ~155 | API 声明，21 个函数 |
| `qjw_core.cpp` | ~490 | 实现 |

---

## 包含什么

### 生命周期

```c
qjwPartNew("my_part");        // 新建零件（内部处理 root）
qjwPartFinish("my_part");     // 导出 + 体积 + cvxFileClose
```

### 草图（最大的坑区）

```c
int sk = 0;
qjwSketchBegin(0, 0, 0, 0, &sk);   // plane 0=XY 1=XZ 2=YZ
qjwSketchCircle(0, 0, 10);         // 内部用 36 边形（cvxSkCirRad 不闭合）
qjwSketchPolyline(n, pts, 1);      // 闭合折线
qjwSketchClosedSpline(n, pts);     // 内部处理「点数<8 走多段线」
qjwSketchEnd();
```

**内部已处理**：
- `cvxSkActivate(sk, 0)` 进入草图（不调就返回 `-10000`）
- 用 `ZwMatrixGetByPointAndZAxis` 构造平面矩阵（**不要手填 `svxMatrix`**）
- `szwMatrix` → `svxMatrix` 的安全拷贝（前者多 4 个字段）

### 拉伸 / 放样

```c
svxExtrudeData e;
cvxPartExtrudeInit(&e);
e.Combine = VX_BOOL_NONE;
e.idProfile = sk;       // 传草图 id，不是曲线 id
e.Start = 0; e.End = 10;
int sh = 0;
cvxPartExtrude(&e, &sh);
```

### 参数化

```c
/* 先建变量，再建几何，尺寸用 cvxDimValMod 绑变量名 */
qjwVariableSet("D", 80.0);

cvxSkAddConsDim(VX_SK_CONSDIM_RADIAL, &cd);
cvxDimValMod(idDim, "D");      // 尺寸值 = 变量名（不是数字）
```

**注意**：事后改 `ZwVariableListSet` **不会**自动重建。要换参数就整个重跑。

### 错误码翻译

```c
qjwErrText(-10000);   // -> "草图未激活（未调 cvxSkActivate(sk,0)）"
```

---

## 编译

```bat
call "<VS>\VC\Auxiliary\Build\vcvars64.bat"
cl /nologo /LD /utf-8 /MT /EHsc /O2 ^
   /I"<ZW3D>\api\inc" ^
   qjw_core.cpp my_plugin.cpp ^
   /link /LIBPATH:"<ZW3D>" ZW3D.lib /OUT:MyPlugin.dll
```

**必须加 `/utf-8`**：否则中文注释会被 MSVC 按 GBK 解析，报出 `C2267` / `C2601` 这类假错误。

---

## 部署

```
%APPDATA%\ZWSOFT\ZW3D\<版本名>\custom\apilibs\MyPlugin.dll
```

**不是**安装目录下的 `custom\apilibs`。

---

## 触发

```bat
zw3d.exe -server
ZW3dRemote.exe /R local "cmd=~myCommand"
```

---

## 已知限制

| 功能 | 状态 |
|---|---|
| 改变量触发重建 | ❌ 不通（`ZwVariableListSet` 返回 0 但几何不动） |
| 旋转/平移 | ❌ `cvxPartMoveTransform` 已废弃，行为异常 |
| 零件库脚本化 | ❌ `~CdShowPartFamily` 是交互式对话框 |

**建议**：在目标位置直接建模，不要「建完再转」。

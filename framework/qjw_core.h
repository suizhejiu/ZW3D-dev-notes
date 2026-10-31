/*
 * QianJiWorks — 千机工坊
 * ZW3D 参数化批量建模套件
 *
 * qjw_core.h — 基础几何与零件生命周期
 *
 * 设计要点（均为实测结论）：
 *   1. Init 只注册命令，不做任何几何操作
 *   2. 单对象文件自带 root，不要调 cvxRootAdd
 *   3. 草图必须 cvxSkActivate(sk, 0) 显式进入
 *   4. 闭合样条 Degree=1 + CreateCrvOpen=0
 *   5. 所有草图在第一次拉伸前建好
 *   6. 每个零件结束 cvxFileClose()
 */
#ifndef QJW_CORE_H
#define QJW_CORE_H

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#include "zwapi_root.h"
#include "zwapi_cmd.h"
#include "zwapi_cmd_sk_create.h"
#include "zwapi_cmd_sk_cmds.h"
#include "zwapi_sk_objs.h"
#include "zwapi_cmd_shape.h"
#include "zwapi_cmd_shape_data.h"
#include "zwapi_message.h"
#include "zwapi_file.h"
#include "zwapi_file_data.h"
#include "zwapi_part_attribute.h"
#include "zwapi_matrix.h"        /* ZwMatrix* 系列，用于构造草图平面和变换矩阵 */

#define QJW_VERSION "1.0.0"

/* ---------- 全局配置 ---------- */

typedef struct {
    char outDir[512];      /* 输出目录，默认 %TEMP%\QianJiWorks */
    char logPath[512];     /* 日志文件，空则不写 */
    int  exportStep;       /* 是否导出 STEP，1=是 */
    int  exportStl;        /* 是否导出 STL，1=是 */
} qjwConfig;

/* 取全局配置（首次调用时初始化默认值） */
qjwConfig *qjwCfg(void);

/* 设置输出目录（下次 newPart 生效） */
int qjwSetOutDir(const char *dir);

/* ---------- 日志 ---------- */

void qjwLogOpen(const char *path);
void qjwLogClose(void);
void qjwLog(const char *fmt, ...);

/* ---------- 基础几何 ---------- */

/* 圆柱，Center 为底面圆心，沿 +Z */
int qjwCyl(int combine, double cx, double cy, double cz, double r, double len);

/* 圆锥，Center 为底面圆心，沿 +Z */
int qjwCone(int combine, double cx, double cy, double cz,
            double r1, double r2, double len);

/* 方块，Center 为体中心 */
int qjwBox(int combine, double cx, double cy, double cz,
           double X, double Y, double Z);

/* ---------- 孔 ---------- */

/* 轴向孔（沿 Z），用于端面打孔 */
int qjwHoleZ(int combine, double cx, double cy, double z, double r, double len);

/* 均布孔组：n 个孔，分布圆 pcd，起始角 degStart */
int qjwHoleRing(int combine, double pcd, int n, double r,
                double z, double len, double degStart);

/* 径向孔：通过在 XZ 或 YZ 平面画圆 + 拉伸切除实现
 * dirAxis = 0 法向为 +X；dirAxis = 1 法向为 +Y
 * z 为孔中心在草图坐标系里的 Y（即世界 Z）
 * x0 为孔中心在草图坐标系里的 X（世界 X 或 Y，取决于 dirAxis）
 */
int qjwHoleRadial(double z, double r, int dirAxis, double x0);

/* ---------- 草图 ---------- */

/* 在 XY 平面建草图并进入（自动 cvxSkActivate）
 * tx/ty/tz 为草图原点平移
 */
int qjwSketchBegin(double tx, double ty, double tz, int *skOut);

/* 在指定平面建草图并进入
 * plane: 0=XY (法向Z) 1=XZ (法向Y) 2=YZ (法向X)
 */
int qjwSketchBeginPlane(int plane, double tx, double ty, double tz, int *skOut);

/* 画闭合样条（Degree=1、闭合），pts 为 x,y 序列，n 个点
 * 返回样条 id，失败返回 -1
 */
int qjwSketchClosedSpline(const double *pts, int n);

/* 画多段线（点数少时比样条可靠，保证直线段）
 * closed=1 时自动闭合
 */
int qjwSketchPolyline(const double *pts, int n, int closed);

/* 画圆（内部用 36 边形多段线近似） */
int qjwSketchCircle(double cx, double cy, double r);

/* 退出草图（cvxRootExit + cvxSkActivate(0,0)） */
void qjwSketchEnd(void);

/* ---------- 拉伸 ---------- */

/* 以草图 id 为轮廓拉伸
 * combine: 0=NONE 1=ADD 2=REMOVE 3=INTERSECT
 */
int qjwExtrude(int sketchId, int combine, double start, double end);

/* ---------- 放样 ---------- */

typedef struct {
    int sketchId;    /* 草图 id */
    int profileId;   /* 该草图里闭合样条的 id */
} qjwLoftProfile;

/* 多层放样，n 层 */
int qjwLoft(const qjwLoftProfile *profs, int n, int combine, int endCap);

/* ---------- 变换 ---------- */

/* 绕任意轴旋转实体（Rodrigues），deg 为角度 */
int qjwPartRotate(int entityId, double ax, double ay, double az, double deg);

/* 平移实体 */
int qjwPartTranslate(int entityId, double dx, double dy, double dz);

/* ---------- 零件生命周期 ---------- */

/* 新建零件文件并记录 root 名
 * 成功后 g_qjwRoot 有效
 */
int qjwPartNew(const char *fileNoExt);

/* 保存 + 导出 + 体积记录 + 关闭文档
 * 返回体积值，失败返回 -1
 */
double qjwPartFinish(const char *fileNoExt);

/* 查询当前零件的体积 */
double qjwPartVolume(void);

/* 当前 root 名（qjwPartNew 之后有效） */
const char *qjwCurrentRoot(void);

/* ---------- 错误码辅助 ---------- */

const char *qjwErrText(int code);

#endif /* QJW_CORE_H */

/*
 * QianJiWorks — qjw_core.cpp
 * 基础几何、草图、拉伸、放样、零件生命周期
 */
#define _USE_MATH_DEFINES
#include "qjw_core.h"
#include <stdlib.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---------- 全局状态 ---------- */

static qjwConfig g_cfg;
static int       g_cfgReady = 0;
static FILE     *g_log = NULL;
static char      g_root[128] = {0};

static void ensureCfg(void)
{
    if (g_cfgReady) return;
    memset(&g_cfg, 0, sizeof(g_cfg));

    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = ".";

    sprintf_s(g_cfg.outDir, sizeof(g_cfg.outDir), "%s\\QianJiWorks", tmp);
    g_cfg.exportStep = 1;
    g_cfg.exportStl  = 0;
    g_cfgReady = 1;
}

qjwConfig *qjwCfg(void)
{
    ensureCfg();
    return &g_cfg;
}

int qjwSetOutDir(const char *dir)
{
    if (!dir || !dir[0]) return -1;
    ensureCfg();
    strncpy_s(g_cfg.outDir, sizeof(g_cfg.outDir), dir, _TRUNCATE);
    return 0;
}

/* ---------- 日志 ---------- */

void qjwLogOpen(const char *path)
{
    if (g_log) { fclose(g_log); g_log = NULL; }
    if (!path || !path[0]) return;
    fopen_s(&g_log, path, "w");
}

void qjwLogClose(void)
{
    if (g_log) { fclose(g_log); g_log = NULL; }
}

void qjwLog(const char *fmt, ...)
{
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

/* szwMatrix 比 svxMatrix 多出 ox/oy/oz/scale 四个字段，
 * 直接强转虽然前 13 个字段对齐，但显式拷贝更安全。 */
static void szwToSvxMatrix(const szwMatrix *in, svxMatrix *out)
{
    memset(out, 0, sizeof(*out));
    out->identity = in->identity;
    out->xx = in->xx; out->yx = in->yx; out->zx = in->zx; out->xt = in->xt;
    out->xy = in->xy; out->yy = in->yy; out->zy = in->zy; out->yt = in->yt;
    out->xz = in->xz; out->yz = in->yz; out->zz = in->zz; out->zt = in->zt;
}

/* ---------- 基础几何 ---------- */

int qjwCyl(int combine, double cx, double cy, double cz, double r, double len)
{
    svxCylData c;
    cvxPartCylInit(&c);
    c.Combine  = (evxBoolType)combine;
    c.Center.x = cx;
    c.Center.y = cy;
    c.Center.z = cz;
    c.Radius   = r;
    c.Length   = len;
    int id = 0;
    int ret = (int)cvxPartCyl(&c, &id);
    qjwLog("  cyl(combine=%d r=%.3f len=%.3f) = %d\n", combine, r, len, ret);
    return ret;
}

int qjwCone(int combine, double cx, double cy, double cz,
            double r1, double r2, double len)
{
    svxConeData c;
    memset(&c, 0, sizeof(c));
    c.Combine  = (evxBoolType)combine;
    c.Center.x = cx;
    c.Center.y = cy;
    c.Center.z = cz;
    c.Radius1  = r1;
    c.Radius2  = r2;
    c.Length   = len;
    int id = 0;
    int ret = (int)cvxPartCone(&c, &id);
    qjwLog("  cone(combine=%d r1=%.3f r2=%.3f len=%.3f) = %d\n", combine, r1, r2, len, ret);
    return ret;
}

int qjwBox(int combine, double cx, double cy, double cz,
           double X, double Y, double Z)
{
    svxBoxData b;
    cvxPartBoxInit(&b);
    b.Combine  = (evxBoolType)combine;
    b.Center.x = cx;
    b.Center.y = cy;
    b.Center.z = cz;
    b.X = X;
    b.Y = Y;
    b.Z = Z;
    int id = 0;
    int ret = (int)cvxPartBox(&b, &id);
    qjwLog("  box(combine=%d X=%.3f Y=%.3f Z=%.3f) = %d\n", combine, X, Y, Z, ret);
    return ret;
}

/* ---------- 孔 ---------- */

int qjwHoleZ(int combine, double cx, double cy, double z, double r, double len)
{
    return qjwCyl(combine, cx, cy, z, r, len);
}

int qjwHoleRing(int combine, double pcd, int n, double r,
                double z, double len, double degStart)
{
    if (n <= 0) return -1;
    double R = pcd * 0.5;
    int ok = 0;
    for (int i = 0; i < n; i++)
    {
        double a = (degStart + 360.0 * i / n) * M_PI / 180.0;
        if (qjwCyl(combine, R * cos(a), R * sin(a), z, r, len) == 0)
            ok++;
    }
    qjwLog("  holeRing(n=%d pcd=%.3f) ok=%d/%d\n", n, pcd, ok, n);
    return (ok == n) ? 0 : -1;
}

int qjwHoleRadial(double z, double r, int dirAxis, double x0)
{
    svxMatrix m;
    memset(&m, 0, sizeof(m));

    /* 矩阵为列主序：
     *   第1列 (xx,yx,zx) = 草图X轴在世界中的方向
     *   第2列 (xy,yy,zy) = 草图Y轴
     *   第3列 (xz,yz,zz) = 法向（拉伸方向）
     * 未赋值的分量已在 memset 中清零，无需再显式写 0。
     */
    if (dirAxis)
    {
        /* 法向 = 世界Y：草图X->世界X，草图Y->世界Z */
        m.xx = 1.0;   /* 草图X -> 世界X */
        m.zy = 1.0;   /* 草图Y -> 世界Z */
        m.yz = 1.0;   /* 法向  -> 世界Y */
    }
    else
    {
        /* 法向 = 世界X：草图X->世界Y，草图Y->世界Z */
        m.yx = 1.0;   /* 草图X -> 世界Y */
        m.zy = 1.0;   /* 草图Y -> 世界Z */
        m.xz = 1.0;   /* 法向  -> 世界X */
    }

    int sk = 0;
    if (cvxPartSkNew(&m, &sk) != 0) { qjwLog("  holeRadial: SkNew fail\n"); return -1; }

    int id = 0;
    svxPoint2 c = { x0, z };
    cvxSkCirRad(&c, r, &id);

    cvxRootExit();
    cvxSkActivate(0, 0);

    svxExtrudeData e;
    cvxPartExtrudeInit(&e);
    e.Combine   = VX_BOOL_REMOVE;
    e.idProfile = sk;
    e.Start     = -50.0;
    e.End       =  50.0;
    int out = 0;
    int ret = (int)cvxPartExtrude(&e, &out);
    qjwLog("  holeRadial(z=%.3f r=%.3f axis=%d) = %d\n", z, r, dirAxis, ret);
    return ret;
}

/* ---------- 草图 ---------- */

int qjwSketchBegin(double tx, double ty, double tz, int *skOut)
{
    svxMatrix m;
    memset(&m, 0, sizeof(m));
    m.identity = 1;
    m.xx = 1; m.yy = 1; m.zz = 1;
    m.xt = tx; m.yt = ty; m.zt = tz;

    int sk = 0;
    int ret = (int)cvxPartSkNew(&m, &sk);
    if (ret != 0) { qjwLog("  sketchBegin: SkNew = %d\n", ret); return ret; }

    /* 关键：必须显式进入草图，否则后续画几何返回 -10000 */
    int ra = (int)cvxSkActivate(sk, 0);
    qjwLog("  sketchBegin(sk=%d) skActivate = %d\n", sk, ra);
    if (skOut) *skOut = sk;
    return ra;
}

int qjwSketchBeginPlane(int plane, double tx, double ty, double tz, int *skOut)
{
    /* plane: 0=XY(法向Z) 1=XZ(法向Y) 2=YZ(法向X)
     * 用官方的 ZwMatrixGetByPointAndZAxis 构造，避免手填矩阵出错 */
    szwPoint  origin;
    szwVector zAxis;
    origin.x = tx; origin.y = ty; origin.z = tz;

    switch (plane)
    {
    case 0: zAxis.x = 0; zAxis.y = 0; zAxis.z = 1; break;  /* XY */
    case 1: zAxis.x = 0; zAxis.y = 1; zAxis.z = 0; break;  /* XZ */
    case 2: zAxis.x = 1; zAxis.y = 0; zAxis.z = 0; break;  /* YZ */
    default: return -1;
    }

    szwMatrix m;
    memset(&m, 0, sizeof(m));
    int mr = (int)ZwMatrixGetByPointAndZAxis(origin, zAxis, &m);
    qjwLog("  matrixByPointZAxis(plane=%d) = %d\n", plane, mr);
    if (mr != 0) return mr;

    svxMatrix sm;
    szwToSvxMatrix(&m, &sm);

    int sk = 0;
    int ret = (int)cvxPartSkNew(&sm, &sk);
    if (ret != 0) { qjwLog("  sketchBeginPlane: SkNew = %d\n", ret); return ret; }

    int ra = (int)cvxSkActivate(sk, 0);
    qjwLog("  sketchBeginPlane(plane=%d sk=%d) skActivate = %d\n", plane, sk, ra);
    if (skOut) *skOut = sk;
    return ra;
}

int qjwSketchClosedSpline(const double *pts, int n)
{
    if (!pts || n < 3) return -1;

    /* 实测：点数少于 8 时 cvxSkSpline 会明显外扩（4 点的正方形被撑到 1.5 倍），
     *       因此少量点强制走多段线路径。 */
    if (n < 8)
    {
        qjwLog("  closedSpline: n=%d < 8, fallback to polyline\n", n);
        return qjwSketchPolyline(pts, n, 1);
    }

    svxSplineData sd;
    memset(&sd, 0, sizeof(sd));
    cvxSkSplineInit(&sd, n);
    sd.SplineTyp     = 0;   /* 通过点 */
    sd.Degree        = 1;   /* 直线段，保留尖角 */
    sd.CreateCrvOpen = 0;   /* 闭合 */
    sd.Count         = n;

    for (int i = 0; i < n; i++)
    {
        sd.Pnts[i].Pnt.x = pts[2 * i];
        sd.Pnts[i].Pnt.y = pts[2 * i + 1];
        sd.Pnts[i].Pnt.z = 0.0;
        sd.Pnts[i].UseCons = 0;
    }

    int sp = 0;
    int ret = (int)cvxSkSpline(&sd, &sp);
    qjwLog("  closedSpline(n=%d) = %d id=%d\n", n, ret, sp);
    return (ret == 0) ? sp : -1;
}

/* 多段线：点数少时更可靠，保证直线段、不外扩
 * closed=1 时补一个回到起点的点
 */
int qjwSketchPolyline(const double *pts, int n, int closed)
{
    if (!pts || n < 2) return -1;

    int total = closed ? (n + 1) : n;
    svxPoint2 *p2 = (svxPoint2 *)calloc((size_t)total, sizeof(svxPoint2));
    if (!p2) return -1;

    for (int i = 0; i < n; i++)
    {
        p2[i].x = pts[2 * i];
        p2[i].y = pts[2 * i + 1];
    }
    if (closed) { p2[n] = p2[0]; }

    int ret = (int)cvxSkAddPolyline(total, p2, 0);
    free(p2);
    qjwLog("  polyline(n=%d closed=%d) = %d\n", total, closed, ret);
    return (ret == 0) ? 0 : -1;
}

int qjwSketchCircle(double cx, double cy, double r)
{
    /* 实测：cvxSkCirRad 建出的圆在拉伸后 bbox 不足直径（疑似只成了部分弧），
     *       因此改用 36 边形多段线近似整圆，结果与理论值一致。 */
    const int kSeg = 36;
    double pts[2 * kSeg];
    for (int i = 0; i < kSeg; i++)
    {
        double a = 2.0 * M_PI * i / kSeg;
        pts[2 * i]     = cx + r * cos(a);
        pts[2 * i + 1] = cy + r * sin(a);
    }
    qjwLog("  sketchCircle(r=%.3f) via %d-gon\n", r, kSeg);
    return qjwSketchPolyline(pts, kSeg, 1);
}

void qjwSketchEnd(void)
{
    cvxRootExit();
    cvxSkActivate(0, 0);
    qjwLog("  sketchEnd\n");
}

/* ---------- 拉伸 ---------- */

int qjwExtrude(int sketchId, int combine, double start, double end)
{
    svxExtrudeData e;
    cvxPartExtrudeInit(&e);
    e.Combine   = (evxBoolType)combine;
    e.idProfile = sketchId;
    e.Start     = start;
    e.End       = end;
    int sh = 0;
    int ret = (int)cvxPartExtrude(&e, &sh);
    qjwLog("  extrude(sk=%d combine=%d %.1f..%.1f) = %d\n",
           sketchId, combine, start, end, ret);
    return ret;
}

/* ---------- 变换 ---------- */



/* 绕任意轴旋转一个实体（Rodrigues 公式）
 * entityId: cvxPartExtrude / cvxPartLoft 等返回的实体 id
 * axis: 旋转轴方向（无需归一化）；deg: 角度（度）
 */
int qjwPartRotate(int entityId, double ax, double ay, double az, double deg)
{
    if (entityId <= 0) return -1;

    szwAxis axis;
    memset(&axis, 0, sizeof(axis));
    axis.point.x     = 0.0; axis.point.y     = 0.0; axis.point.z     = 0.0;
    axis.direction.x = ax;  axis.direction.y = ay;  axis.direction.z = az;

    szwMatrix m;
    memset(&m, 0, sizeof(m));
    /* 角度单位待验证：先按度传入（上一版按弧度传，实测只转了约 5 度） */
    int mr = (int)ZwMatrixInitByRotation(deg, axis, &m);
    qjwLog("  matrixByRotation(deg=%.2f) = %d\n", deg, mr);
    if (mr != 0) return mr;

    svxMatrix sm;
    szwToSvxMatrix(&m, &sm);
    int ret = (int)cvxPartMoveTransform(entityId, &sm);
    qjwLog("  rotate(id=%d axis=(%.2f,%.2f,%.2f) deg=%.2f) = %d\n",
           entityId, ax, ay, az, deg, ret);
    return ret;
}

/* 平移一个实体 */
int qjwPartTranslate(int entityId, double dx, double dy, double dz)
{
    if (entityId <= 0) return -1;

    szwMatrix m;
    memset(&m, 0, sizeof(m));
    int mr = (int)ZwMatrixInitByTranslation(dx, dy, dz, &m);
    if (mr != 0) return mr;

    svxMatrix sm;
    szwToSvxMatrix(&m, &sm);
    int ret = (int)cvxPartMoveTransform(entityId, &sm);
    qjwLog("  translate(id=%d d=(%.2f,%.2f,%.2f)) = %d\n", entityId, dx, dy, dz, ret);
    return ret;
}

/* ---------- 放样 ---------- */

int qjwLoft(const qjwLoftProfile *profs, int n, int combine, int endCap)
{
    if (!profs || n < 2) return -1;

    svxLoftProfile *lp = (svxLoftProfile *)calloc((size_t)n, sizeof(svxLoftProfile));
    if (!lp) return -1;

    for (int i = 0; i < n; i++)
    {
        lp[i].idParent  = profs[i].sketchId;
        lp[i].idProfile = profs[i].profileId;
    }

    svxLoftData ld;
    cvxPartLoftInit(&ld);
    ld.combine  = (evxBoolType)combine;
    ld.type     = VX_LOFT_PROFS;
    ld.numProfs = n;
    ld.profs    = lp;
    ld.endCap   = endCap;
    ld.close    = 0;

    int sh = 0;
    int ret = (int)cvxPartLoft(&ld, &sh);
    free(lp);
    qjwLog("  loft(n=%d combine=%d) = %d\n", n, combine, ret);
    return ret;
}

/* ---------- 零件生命周期 ---------- */

int qjwPartNew(const char *fileNoExt)
{
    if (!fileNoExt || !fileNoExt[0]) return -1;
    ensureCfg();

    vxPath dir;
    memset(dir, 0, sizeof(dir));
    strncpy_s(dir, sizeof(dir), g_cfg.outDir, _TRUNCATE);
    cvxFileDirectory(dir);

    char fn[256];
    sprintf_s(fn, sizeof(fn), "%s.z3prt", fileNoExt);

    vxLongPath pz;
    memset(pz, 0, sizeof(pz));
    strncpy_s(pz, sizeof(pz), fn, _TRUNCATE);

    int r = (int)cvxFileNewSingle(pz, VX_FILE_PART, VX_NORMAL_PART, NULL, NULL);
    qjwLog("  partNew(%s) = %d\n", fn, r);
    if (r != 0) return r;

    /* 单对象文件自带 root，不要调 cvxRootAdd */
    memset(g_root, 0, sizeof(g_root));
    cvxRootInqActive(g_root, sizeof(g_root));
    qjwLog("  root = '%s'\n", g_root);

    if (g_root[0] == 0)
    {
        qjwLog("  WARN: root name empty\n");
        return -2;
    }
    return 0;
}

const char *qjwCurrentRoot(void)
{
    return g_root;
}

double qjwPartVolume(void)
{
    if (g_root[0] == 0) return -1.0;
    double vol = 0.0, mass = 0.0;
    int rv = (int)cvxPartVolumAndMassGet(g_root, &vol, &mass);
    qjwLog("  volume(root=%s) ret=%d vol=%.3f\n", g_root, rv, vol);
    return (rv == 0) ? vol : -1.0;
}

double qjwPartFinish(const char *fileNoExt)
{
    if (!fileNoExt || !fileNoExt[0]) return -1.0;
    ensureCfg();

    double vol = qjwPartVolume();

    char pz[1024];
    sprintf_s(pz, sizeof(pz), "%s\\%s.z3prt", g_cfg.outDir, fileNoExt);
    vxLongPath pt;
    memset(pt, 0, sizeof(pt));
    strncpy_s(pt, sizeof(pt), pz, _TRUNCATE);
    qjwLog("  saveAs = %d\n", (int)cvxFileSaveAs(pt));

    if (g_cfg.exportStep)
    {
        char ps[1024];
        sprintf_s(ps, sizeof(ps), "%s\\%s.stp", g_cfg.outDir, fileNoExt);
        vxLongPath pstp;
        memset(pstp, 0, sizeof(pstp));
        strncpy_s(pstp, sizeof(pstp), ps, _TRUNCATE);

        svxSTEPData sd;
        memset(&sd, 0, sizeof(sd));
        sd.AppProtocol = 1;
        sd.OutPut      = 0;
        sd.ExportType  = 0;
        qjwLog("  exportStep = %d\n",
               (int)cvxFileExport(VX_EXPORT_TYPE_STEP, pstp, &sd));
    }

    /* 关键：建完立即关文档，防止累积导致进程假死 */
    cvxFileClose();
    qjwLog("  fileClose\n");

    memset(g_root, 0, sizeof(g_root));
    return vol;
}

/* ---------- 错误码 ---------- */

const char *qjwErrText(int code)
{
    switch (code)
    {
    case 0:      return "OK";
    case -5:     return "ROOT_OBJ_ACT_FAIL (无活动根对象)";
    case -6:     return "ROOT_SINGLE_MULTI_TYPE_ERROR";
    case -7:     return "单对象文件调用了多对象接口";
    case -31:    return "INVALID_INPUT (参数为 NULL)";
    case -152:   return "CMMD_EXEC_ERROR (命令执行失败，常见于草图无几何)";
    case -241:   return "文件名含非法字符";
    case -242:   return "文件格式不被接受";
    case -10000: return "草图未激活 (未调用 cvxSkActivate(sk,0))";
    default:     return "未知错误";
    }
}

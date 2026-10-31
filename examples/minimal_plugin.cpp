/*
 * minimal_plugin.cpp — ZW3D 插件最小可运行骨架
 *
 * 这是从「踩坑笔记」里提炼出的最小结构。
 * 照着这个写，可以避开最常见的几个坑。
 *
 * 编译：
 *   cl /nologo /LD /utf-8 /MT /EHsc /O2 ^
 *      /I"<ZW3D>\api\inc" ^
 *      minimal_plugin.cpp ^
 *      /link /LIBPATH:"<ZW3D>" ZW3D.lib /OUT:minimal_plugin.dll
 *
 * 放置：
 *   %APPDATA%\ZWSOFT\ZW3D\<版本名>\custom\apilibs\minimal_plugin.dll
 *
 * 触发：
 *   zw3d.exe -server
 *   ZW3dRemote.exe /R local "cmd=~demoBox"
 */

#include <stdio.h>
#include <string.h>

#include "zwapi_root.h"
#include "zwapi_cmd.h"
#include "zwapi_cmd_sk_create.h"
#include "zwapi_cmd_sk_cmds.h"
#include "zwapi_sk_objs.h"
#include "zwapi_cmd_shape.h"
#include "zwapi_cmd_shape_data.h"
#include "zwapi_file.h"
#include "zwapi_file_data.h"
#include "zwapi_matrix.h"
#include "zwapi_message.h"

/* ------------------------------------------------------------------ */
/* 演示命令：建一个 20x20x10 的方块                                     */
/* ------------------------------------------------------------------ */

static void demoBox(void)
{
    /* 1) 新建零件 */
    vxLongPath p;
    memset(p, 0, sizeof(p));
    strcpy_s(p, "D:\\tmp\\minimal_box.z3prt");

    if (cvxFileNewEx(p, NULL) != 0)
    {
        cvxMsgDisp("cvxFileNewEx failed");
        return;
    }

    char root[128] = {0};
    cvxRootInqActive(root, sizeof(root));

    /* 2) 草图：XY 平面 */
    /* 关键：用 ZwMatrixGetByPointAndZAxis 构造矩阵，不要手填 12 个分量 */
    szwPoint  origin = {0.0, 0.0, 0.0};
    szwVector zAxis  = {0.0, 0.0, 1.0};       /* 法向 Z = XY 平面 */

    szwMatrix m;
    memset(&m, 0, sizeof(m));
    if (ZwMatrixGetByPointAndZAxis(origin, zAxis, &m) != 0)
    {
        cvxMsgDisp("ZwMatrixGetByPointAndZAxis failed");
        return;
    }

    /* szwMatrix 比 svxMatrix 多 4 个字段，显式拷贝更安全 */
    svxMatrix sm;
    memset(&sm, 0, sizeof(sm));
    sm.identity = m.identity;
    sm.xx = m.xx; sm.yx = m.yx; sm.zx = m.zx; sm.xt = m.xt;
    sm.xy = m.xy; sm.yy = m.yy; sm.zy = m.zy; sm.yt = m.yt;
    sm.xz = m.xz; sm.yz = m.yz; sm.zz = m.zz; sm.zt = m.zt;

    int sk = 0;
    if (cvxPartSkNew(&sm, &sk) != 0)
    {
        cvxMsgDisp("cvxPartSkNew failed");
        return;
    }

    /* 关键：必须显式进入草图，否则后续画图返回 -10000 */
    if (cvxSkActivate(sk, 0) != 0)
    {
        cvxMsgDisp("cvxSkActivate failed");
        return;
    }

    /* 画一个 20x20 的正方形（用闭合折线，简单可靠） */
    svxPoint2 pts[5] = {
        {-10.0, -10.0},
        { 10.0, -10.0},
        { 10.0,  10.0},
        {-10.0,  10.0},
        {-10.0, -10.0}
    };
    if (cvxSkAddPolyline(5, pts, 1) != 0)     /* 1 = 闭合 */
    {
        cvxMsgDisp("cvxSkAddPolyline failed");
        return;
    }

    /* 退出草图（这一步不会弹空草图对话框，因为有几何了） */
    cvxRootExit();
    cvxSkActivate(0, 0);

    /* 3) 拉伸 10 */
    svxExtrudeData e;
    cvxPartExtrudeInit(&e);
    e.Combine   = VX_BOOL_NONE;
    e.idProfile = sk;       /* 传草图 id，不是曲线 id */
    e.Start     = 0.0;
    e.End       = 10.0;

    int sh = 0;
    if (cvxPartExtrude(&e, &sh) != 0)
    {
        cvxMsgDisp("cvxPartExtrude failed");
        return;
    }

    /* 4) 体积校验 */
    double vol = 0.0, mass = 0.0;
    cvxPartVolumAndMassGet(root, &vol, &mass);
    /* 预期 20*20*10 = 4000 */

    char msg[256];
    sprintf_s(msg, sizeof(msg), "demoBox: vol = %.3f (expect 4000.000)", vol);
    cvxMsgDisp(msg);

    /* 5) 每个零件建完必须关文档，否则进程内存累积、命令假死 */
    cvxFileClose();
}

/* ------------------------------------------------------------------ */
/* Init / Exit：只注册命令，不做任何几何操作                             */
/* ------------------------------------------------------------------ */

int MinimalInit(void)
{
    cvxCmdFunc("demoBox", (void *)demoBox, 0.0);
    return 0;
}

int MinimalExit(void)
{
    cvxCmdFuncUnload("demoBox");
    return 0;
}

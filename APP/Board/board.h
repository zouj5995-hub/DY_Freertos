/*******************************************************************************
 * 文件   ：board.h
 * 功能   ：板级硬件描述层（Board Layer）对外接口
 * 说明   ：本层只回答一个问题——“哪台设备，对应哪个引脚，什么电平算开”。
 *          业务代码只允许使用 DEV_xxx 这样的逻辑设备名，
 *          不允许直接出现 GPIO 端口名和引脚号；换板子只改 board.c 里的表。
 ******************************************************************************/
#ifndef __BOARD_H
#define __BOARD_H

#include "main.h"
#include <stdbool.h>

/*******************************************************************************
 * 名称  ：dev_id_t
 * 功能  ：受控设备的逻辑编号
 * 说明  ：序号到具体引脚的对应关系写在 board.c 的引脚表里，这里只是“名字”
 ******************************************************************************/
typedef enum
{
    DEV_IPC = 0,        // 工控机   24V
    DEV_SONAR,          // 声纳     24V
    DEV_BD,             // 北斗     24V
    DEV_BK1,            // 备用一   24V
    DEV_RADAR,          // 雷达     12V
    DEV_CAMERA,         // 摄像头   12V
    DEV_BK2,            // 备用二   12V
    DEV_BK3,            // 备用三   12V
    DEV_CNT             // 设备总数
} dev_id_t;


void board_init(void);//把所有设备置于“断电”状态

void board_power_set(dev_id_t dev, bool on);//给指定设备供电或断电

bool board_power_get(dev_id_t dev);//读回指定设备当前的输出状态

const char *board_dev_name(dev_id_t dev);//取设备名（打印/日志用）


#endif /* __BOARD_H */

/*******************************************************************************
 * 文件   ：rule_store.h
 * 功能   ：供电策略（35 条规则）存储层对外接口
 * 说明   ：负责规则表与 EEPROM 之间的转换与读写；
 *          EEPROM 布局保持与旧版本完全一致，升级固件不会让已下发的规则失效
 ******************************************************************************/
#ifndef __RULE_STORE_H
#define __RULE_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/
#define RULE_COUNT                35U       // 规则条数（与协议帧一致）
#define RULE_EEPROM_ADDR          0xDAU     // 规则区起始地址（与旧版本一致）
#define RULE_RECORD_SIZE          9U        // 每条规则在 EEPROM 中占用 9 字节
#define RULE_DEVICE_INVALID       0xFFU     // 无效规则的目标设备码

/* 单设备目标码（与协议定义一致） */
#define RULE_DEVICE_PC            0x00U     // 工控机
#define RULE_DEVICE_RADAR         0x01U     // 雷达
#define RULE_DEVICE_SONAR         0x02U     // 声纳
#define RULE_DEVICE_CAMERA        0x03U     // 摄像头
#define RULE_DEVICE_BD            0x04U     // 北斗
#define RULE_DEVICE_BK1           0x05U     // 备用一
#define RULE_DEVICE_BK2           0x06U     // 备用二
#define RULE_DEVICE_BK3           0x07U     // 备用三

/* 组合设备目标码 */
#define RULE_DEVICE_PC_RADAR_SONAR_CAMERA   0x0AU   // 工控机+雷达+声纳+摄像头
#define RULE_DEVICE_PC_RADAR_CAMERA         0x0BU   // 工控机+雷达+摄像头
#define RULE_DEVICE_PC_RADAR                0x0CU   // 工控机+雷达
#define RULE_DEVICE_PC_SONAR                0x0DU   // 工控机+声纳

/* Exported types ------------------------------------------------------------*/

/*******************************************************************************
 * 名称  ：rule_item_t
 * 功能  ：一条供电策略规则（应用层使用的形式）
 ******************************************************************************/
typedef struct
{
    uint8_t  start_h;           // 开始时（0 - 23）
    uint8_t  start_m;           // 开始分（0 - 59）
    uint8_t  start_s;           // 开始秒（0 - 59）
    uint16_t work_sec;          // 工作时长（秒）
    uint8_t  target_device;     // 目标设备码，0xFF 表示本条无效
} rule_item_t;

/* Exported functions prototypes ---------------------------------------------*/
void rule_store_init(void);                                     // 初始化规则存储（内部初始化 EEPROM）
bool rule_store_load(rule_item_t *rules, uint8_t count);        // 从 EEPROM 载入规则表
bool rule_store_save(const rule_item_t *rules, uint8_t count);  // 把规则表写入 EEPROM
void rule_store_set_default(rule_item_t *rules, uint8_t count); // 生成安全默认规则（全部置为无效）

#ifdef __cplusplus
}
#endif

#endif /* __RULE_STORE_H */

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



/* Exported constants --------------------------------------------------------*/

/* ===== EEPROM（AT24C256）软件 I2C 引脚与参数 ===== */
#define BOARD_EEPROM_SCL_PORT   GPIOG       // SCL 端口
#define BOARD_EEPROM_SCL_PIN    GPIO_PIN_3  // SCL 引脚
#define BOARD_EEPROM_SDA_PORT   GPIOG       // SDA 端口
#define BOARD_EEPROM_SDA_PIN    GPIO_PIN_2  // SDA 引脚
#define BOARD_EEPROM_ADDR       0xA0U       // 器件地址（写操作）
#define BOARD_EEPROM_PAGE_SIZE  64U         // 页大小（字节），AT24C256 为 64
#define BOARD_EEPROM_WRITE_MS   5U          // 写周期（毫秒）

/* ===== SP3485 收发器使能脚（低电平有效：给收发器供电） ===== */
#define BOARD_U2_EN_PORT         GPIOA                    // USART2 收发器使能端口
#define BOARD_U2_EN_PIN          GPIO_PIN_1               // USART2 收发器使能（PA1）
#define BOARD_U3_EN_PORT         GPIOG                    // USART3 收发器使能端口
#define BOARD_U3_EN_PIN          GPIO_PIN_1               // USART3 收发器使能（PG1）
#define BOARD_485_EN_LEVEL       GPIO_PIN_RESET           // 使能有效电平（低电平使能）

/* ===== RS485 方向控制引脚（发送时切向、发完切回） ===== */
#define BOARD_U2_DIR_PORT        GPIOF        // USART2 方向脚端口（协议链路）
#define BOARD_U2_DIR_PIN         GPIO_PIN_12  // USART2 方向脚（PF12）
#define BOARD_U3_DIR_PORT        GPIOE        // USART3 方向脚端口（工控机链路）
#define BOARD_U3_DIR_PIN         GPIO_PIN_15  // USART3 方向脚（PE15）
#define BOARD_485_DIR_TX_LEVEL   GPIO_PIN_SET // 方向脚为发送时的电平

/* ===== 12V 总控（不参与供电决策，但硬件若仍在回路中须保持接通） ===== */
#define BOARD_MAIN12V_ENABLE     1           // 1=处理总控引脚；0=板上已无总控则跳过
#define BOARD_MAIN12V_PORT       GPIOE       // 总控继电器端口
#define BOARD_MAIN12V_PIN        GPIO_PIN_12 // 总控继电器引脚（PE12）
#define BOARD_MAIN12V_ON_LEVEL   GPIO_PIN_RESET // 接通电平（低电平接通，沿用旧板）

/* ===== LoRa 模块（A39C）软件模拟串口：引脚与参数 ===== */
#define BOARD_LORA_TX_PORT       GPIOB          // 软串口 TX 端口（接模块 RXD）
#define BOARD_LORA_TX_PIN        GPIO_PIN_6     // 软串口 TX（PB6）
#define BOARD_LORA_RX_PORT       GPIOB          // 软串口 RX 端口（接模块 TXD）
#define BOARD_LORA_RX_PIN        GPIO_PIN_5     // 软串口 RX（PB5）
#define BOARD_LORA_MD0_PORT      GPIOB          // 模块 MD0 模式脚端口
#define BOARD_LORA_MD0_PIN       GPIO_PIN_8     // MD0（PB8）
#define BOARD_LORA_MD1_PORT      GPIOB          // 模块 MD1 模式脚端口
#define BOARD_LORA_MD1_PIN       GPIO_PIN_7     // MD1（PB7）
#define BOARD_LORA_AUX_PORT      GPIOB          // 模块 AUX 忙闲指示脚端口
#define BOARD_LORA_AUX_PIN       GPIO_PIN_4     // AUX（PB4）
#define BOARD_LORA_BAUD          9600U          // 软串口波特率（与模块一致）
#define BOARD_LORA_MD0_LEVEL     GPIO_PIN_SET   // 一般工作模式：MD0 为高
#define BOARD_LORA_MD1_LEVEL     GPIO_PIN_RESET // 一般工作模式：MD1 为低

/* Exported functions prototypes ---------------------------------------------*/
void board_init(void);//把所有设备置于“断电”状态

void board_power_set(dev_id_t dev, bool on);//给指定设备供电或断电

bool board_power_get(dev_id_t dev);//读回指定设备当前的输出状态

const char *board_dev_name(dev_id_t dev);//取设备名（打印/日志用）


#endif /* __BOARD_H */

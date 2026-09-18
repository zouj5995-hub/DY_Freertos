/*******************************************************************************
 * 文件   ：eeprom.h
 * 功能   ：AT24Cxx 系列 EEPROM 读写对外接口
 * 说明   ：基于软件 I2C；器件地址、页大小、写周期在 board.h 中定义；
 *          写入按页拆分，既避免跨页回卷，也比逐字节写快几十倍
 ******************************************************************************/
#ifndef __EEPROM_H
#define __EEPROM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported functions prototypes ---------------------------------------------*/
void eeprom_init(void);                                              // 初始化 EEPROM（含软件 I2C 引脚）
bool eeprom_write(uint16_t addr, const uint8_t *data, uint16_t len); // 从指定地址写入若干字节
bool eeprom_read(uint16_t addr, uint8_t *data, uint16_t len);        // 从指定地址读出若干字节
bool eeprom_self_test(void);                                         // 读写自检：写测试模式再读回比对

#ifdef __cplusplus
}
#endif

#endif /* __EEPROM_H */

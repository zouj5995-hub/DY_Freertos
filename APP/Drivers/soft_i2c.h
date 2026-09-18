/*******************************************************************************
 * 文件   ：soft_i2c.h
 * 功能   ：软件模拟 I2C 对外接口
 * 说明   ：引脚与器件地址在 board.h 中定义；本层只实现时序，不认识具体器件
 ******************************************************************************/
#ifndef __SOFT_I2C_H
#define __SOFT_I2C_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported functions prototypes ---------------------------------------------*/
void    soft_i2c_init(void);                    // 初始化 I2C 引脚（SDA/SCL 均置高）
void    soft_i2c_start(void);                   // 产生起始条件
void    soft_i2c_stop(void);                    // 产生停止条件
bool    soft_i2c_write_byte(uint8_t byte);      // 写一个字节，返回 true 表示收到应答
uint8_t soft_i2c_read_byte(bool ack);           // 读一个字节，ack 表示是否回应答

#ifdef __cplusplus
}
#endif

#endif /* __SOFT_I2C_H */

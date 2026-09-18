/*******************************************************************************
 * 文件   ：log.h
 * 功能   ：日志服务对外接口（多任务安全）
 * 说明   ：任务里用 LOG_xxx() 打日志——它们只是把日志投递到队列，瞬间返回；
 *          真正往串口写的是日志任务 TaskLog，所以输出永远不会交错。
 *          ⚠ 中断里禁止调用 LOG_xxx
 ******************************************************************************/
#ifndef __LOG_H
#define __LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/

/* ===== 日志等级（数字越小越重要） ===== */
#define LOG_LEVEL_ERROR     0       // 错误：必须上报
#define LOG_LEVEL_WARNING   1       // 警告：异常但可运行
#define LOG_LEVEL_INFO      2       // 信息：常规运行事件
#define LOG_LEVEL_DEBUG     3       // 调试：发布版可整体编译掉

#define LOG_COMPILE_LEVEL   LOG_LEVEL_DEBUG     // 编译期开关：发布固件时改成 LOG_LEVEL_INFO
#define LOG_DEFAULT_LEVEL   LOG_LEVEL_INFO      // 运行期默认等级
#define LOG_TEXT_MAX        256                 // 单条日志最大长度（含前缀与换行）
#define LOG_QUEUE_LEN       6                   // 日志队列深度（满则丢弃，保护业务）

/* 文件名（去掉路径），供日志前缀使用 */
#define __FILENAME__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : \
                     (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__))

/* Exported functions prototypes ---------------------------------------------*/
void     log_init(void);                                    // 创建日志队列与串口互斥量（须在启动调度器前调用）
void     log_set_level(uint8_t level);                      // 设置运行期日志等级
uint8_t  log_get_level(void);                               // 读取当前运行期日志等级
uint32_t log_get_drop_count(void);                          // 读取因队列满而丢弃的日志条数
void     log_printf(uint8_t level, const char *file, const char *fmt, ...);  // 生产一条日志（投递到队列，瞬间返回）
void     log_write_sync(const char *text, uint16_t len);    // 直接同步输出一段文本（互斥量保护串口）
void     TaskLog(void *argument);                           // 日志任务：从队列取日志并输出

/* Exported macro ------------------------------------------------------------*/
/* 任务中打日志统一用这四个宏；中断里禁止使用 */

#if (LOG_COMPILE_LEVEL >= LOG_LEVEL_ERROR)
#define LOG_ERROR(fmt, ...)   log_printf(LOG_LEVEL_ERROR,   __FILENAME__, fmt, ##__VA_ARGS__)
#else
#define LOG_ERROR(fmt, ...)
#endif

#if (LOG_COMPILE_LEVEL >= LOG_LEVEL_WARNING)
#define LOG_WARNING(fmt, ...) log_printf(LOG_LEVEL_WARNING, __FILENAME__, fmt, ##__VA_ARGS__)
#else
#define LOG_WARNING(fmt, ...)
#endif

#if (LOG_COMPILE_LEVEL >= LOG_LEVEL_INFO)
#define LOG_INFO(fmt, ...)    log_printf(LOG_LEVEL_INFO,    __FILENAME__, fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...)
#endif

#if (LOG_COMPILE_LEVEL >= LOG_LEVEL_DEBUG)
#define LOG_DEBUG(fmt, ...)   log_printf(LOG_LEVEL_DEBUG,   __FILENAME__, fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __LOG_H */

/*******************************************************************************
 * 文件   ：rtc_port.c
 * 功能   ：RTC 驱动封装层实现
 * 说明   ：封装 HAL 的 RTC 读写，处理 F1 上"先读时间再读日期"的限制，
 *          并用备份寄存器记录"是否曾被正确校时"
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "rtc_port.h"
#include "rtc.h"
#include "stm32f1xx_hal_rtc_ex.h"    // 提供 RTC_BKP_DRx 与 HAL_RTCEx_BKUPRead/Write
#include <stddef.h>

/* Private define ------------------------------------------------------------*/
#define RTC_MIN_VALID_YEAR   2024U          // 小于该年份视为尚未校时
#define RTC_SYNC_MAGIC       0xA5A5U        // 备份寄存器"已校时"标记值
#define RTC_SYNC_BKP_REG     RTC_BKP_DR2    // 存放标记的备份寄存器（DR1 已被 rtc.c 用作"已初始化"标记）

/* Exported functions --------------------------------------------------------*/

/*******************************************************************************
 * 函数名：rtc_port_get
 * 功  能：读取 RTC 当前的日期与时间
 * 参  数：t —— 接收时间的输出指针
 * 返回值：true 表示读取成功，false 表示失败
 * 说  明：F1 的 RTC 必须先读时间再读日期，否则影子寄存器不会被锁存
 ******************************************************************************/
bool rtc_port_get(rtc_port_time_t *t)
{
    RTC_TimeTypeDef s_time = {0};
    RTC_DateTypeDef s_date = {0};

    /*==============================
     *  #1. 参数检查
     *==============================*/
    if (t == NULL)
    {
        return false;                                   // 空指针保护
    }

    /*==============================
     *  #2. 先读时间（顺序不可颠倒）
     *==============================*/
    if (HAL_RTC_GetTime(&hrtc, &s_time, RTC_FORMAT_BIN) != HAL_OK)
    {
        return false;                                   // 读时间失败
    }

    /*==============================
     *  #3. 再读日期（同时解锁影子寄存器）
     *==============================*/
    if (HAL_RTC_GetDate(&hrtc, &s_date, RTC_FORMAT_BIN) != HAL_OK)
    {
        return false;                                   // 读日期失败
    }

    /*==============================
     *  #4. 填充输出结构，年份补足 2000
     *==============================*/
    t->year   = (uint16_t)(2000U + (uint16_t)s_date.Year);  // 完整年份
    t->month  = s_date.Month;                               // 月
    t->day    = s_date.Date;                                // 日
    t->hour   = s_time.Hours;                               // 时
    t->minute = s_time.Minutes;                             // 分
    t->second = s_time.Seconds;                             // 秒

    return true;                                        // 读取成功
}

/*******************************************************************************
 * 函数名：rtc_port_set
 * 功  能：设置 RTC 的日期与时间
 * 参  数：t —— 要设置的时间
 * 返回值：true 表示设置成功，false 表示失败
 * 说  明：写入前会做合理性校验，防止把无效时间写进 RTC
 ******************************************************************************/
bool rtc_port_set(const rtc_port_time_t *t)
{
    RTC_TimeTypeDef s_time = {0};
    RTC_DateTypeDef s_date = {0};

    /*==============================
     *  #1. 参数检查与合法性校验
     *==============================*/
    if (t == NULL)
    {
        return false;                                   // 空指针保护
    }

    if (rtc_port_time_valid(t) == false)
    {
        return false;                                   // 时间不合理，拒绝写入
    }

    /*==============================
     *  #2. 先写时间
     *==============================*/
    s_time.Hours   = t->hour;                           // 时
    s_time.Minutes = t->minute;                         // 分
    s_time.Seconds = t->second;                         // 秒

    if (HAL_RTC_SetTime(&hrtc, &s_time, RTC_FORMAT_BIN) != HAL_OK)
    {
        return false;                                   // 写时间失败
    }

    /*==============================
     *  #3. 再写日期（年份减去 2000，星期不影响业务）
     *==============================*/
    s_date.Year    = (uint8_t)(t->year - 2000U);        // 年
    s_date.Month   = t->month;                          // 月
    s_date.Date    = t->day;                            // 日
    s_date.WeekDay = RTC_WEEKDAY_MONDAY;                // 星期固定填，不参与业务

    if (HAL_RTC_SetDate(&hrtc, &s_date, RTC_FORMAT_BIN) != HAL_OK)
    {
        return false;                                   // 写日期失败
    }

    return true;                                        // 设置成功
}

/*******************************************************************************
 * 函数名：rtc_port_time_valid
 * 功  能：判断时间是否落在合理范围内
 * 参  数：t —— 待判断的时间
 * 返回值：true 表示合理，false 表示明显异常
 * 说  明：用于识别"上电后 RTC 里还没有被校过的有效时间"
 ******************************************************************************/
bool rtc_port_time_valid(const rtc_port_time_t *t)
{
    /*==============================
     *  #1. 参数检查
     *==============================*/
    if (t == NULL)
    {
        return false;                                   // 空指针保护
    }

    /*==============================
     *  #2. 各字段范围检查
     *==============================*/
    if ((t->year < RTC_MIN_VALID_YEAR) || (t->year > 2099U))    // 年份范围
    {
        return false;
    }

    if ((t->month < 1U) || (t->month > 12U))                    // 月份范围
    {
        return false;
    }

    if ((t->day < 1U) || (t->day > 31U))                        // 日期范围
    {
        return false;
    }

    if (t->hour > 23U)                                          // 小时范围
    {
        return false;
    }

    if (t->minute > 59U)                                        // 分钟范围
    {
        return false;
    }

    if (t->second > 59U)                                        // 秒范围
    {
        return false;
    }

    return true;                                        // 各字段均合理
}

/*******************************************************************************
 * 函数名：rtc_port_was_synced
 * 功  能：判断 RTC 是否曾被正确校时过
 * 参  数：无
 * 返回值：true 表示曾被校时（时间可信），false 表示从未校时
 * 说  明：备份寄存器由 VBAT 供电，电池耗尽时标记会丢失，正好可用于识别
 ******************************************************************************/
bool rtc_port_was_synced(void)
{
    /*==============================
     *  #1. 读取备份寄存器并比对魔数
     *==============================*/
    return (HAL_RTCEx_BKUPRead(&hrtc, RTC_SYNC_BKP_REG) == RTC_SYNC_MAGIC);   // 标记是否匹配
}

/*******************************************************************************
 * 函数名：rtc_port_mark_synced
 * 功  能：标记 RTC 已被正确校时
 * 参  数：无
 * 返回值：无
 * 说  明：校时成功后调用，掉电也不会丢失（由 VBAT 维持）
 ******************************************************************************/
void rtc_port_mark_synced(void)
{
    /*==============================
     *  #1. 向备份寄存器写入魔数
     *==============================*/
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_SYNC_BKP_REG, RTC_SYNC_MAGIC);   // 写标记
}

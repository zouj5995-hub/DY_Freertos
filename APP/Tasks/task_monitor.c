/*******************************************************************************
 * 文件   ：task_monitor.c
 * 功能   ：监控任务——周期性采集电压与三路温度并打印
 * 说明   ：本任务优先级放低，因为采集是阻塞式的；
 *          “低优先级任务里可以放心用阻塞式 API”正是 RTOS 的价值所在
 ******************************************************************************/

#include "tasks.h"
#include "sense.h"
#include <stdio.h>
#include "log.h"
#include "wdg_service.h"
#include "monitor_service.h"
#include "bd_time.h"

#define MONITOR_PERIOD_MS 1000  //采集周期（毫秒）
#define MONITOR_SIMULATE_ENABLE   0     // 0=真实采集，1=模拟数据（测完必须改回 0）
#define MONITOR_LOG_COUNT 5
/*******************************************************************************
 * 函数名：TaskMonitor
 * 功  能：监控任务主体：每秒采集一次并打印
 * 参  数：argument —— 创建任务时传入的参数（本任务不用）
 * 返回值：无（永不返回）
 * 说  明：打印用“整数放大 10 倍”的方式，避免 MicroLIB 不支持浮点打印的问题
 ******************************************************************************/
void TaskMonitor(void *argument)
{
    sense_data_t       data;
    monitor_snapshot_t snapshot;
    TickType_t         last_wake_tick;
    uint32_t           last_alarm = MONITOR_ALARM_NONE;
    uint8_t            log_count  = 0;

    last_wake_tick = xTaskGetTickCount();                       // 记录本周期起点

    bd_time_init();                                             // 启动北斗主动校时状态机

    /*==============================
     *  #1. 任务主循环（永不退出）
     *==============================*/
    for (;;)
    {
        /*==============================
         *  #2. 采集并更新监控服务
         *==============================*/
        #if (MONITOR_SIMULATE_ENABLE == 1)
        data.voltage  = 24.0f;      // 24.0V，门槛内
        data.temp_env = 25.0f;
        data.temp_ipc = 75.0f;      // 模拟工控机过温
        data.temp_pcb = 30.0f;
        #else
        sense_read_all(&data);
        #endif                                                  // 读电压与三路温度
        monitor_service_update(&data);                          // 更新告警与快照

        /*==============================
         *  #3. 读取快照并处理告警变化
         *==============================*/
        if (monitor_service_get_snapshot(&snapshot))
        {
            if (snapshot.alarm_flags != last_alarm)             // 告警状态发生变化
            {
                LOG_WARNING("温度告警变化：0x%02lX -> 0x%02lX",
                            (unsigned long)last_alarm,
                            (unsigned long)snapshot.alarm_flags);

                last_alarm = snapshot.alarm_flags;              // 记录本次状态
            }

            /*==============================
             *  #4. 每 5 秒打印一次常规数据
             *==============================*/
            log_count++;

            if (log_count >= MONITOR_LOG_COUNT)
            {
                log_count = 0;

                LOG_INFO("Vx10=%ld Env=%ld Ipc=%ld Pcb=%ld valid=0x%02lX alarm=0x%02lX gate=%u",
                         (long)(snapshot.data.voltage * 10.0f),
                         (long)(snapshot.data.temp_env * 10.0f),
                         (long)(snapshot.data.temp_ipc * 10.0f),
                         (long)(snapshot.data.temp_pcb * 10.0f),
                         (unsigned long)snapshot.valid_flags,
                         (unsigned long)snapshot.alarm_flags,
                         (unsigned)snapshot.power_on_allowed);
            }
        }
        else
        {
            LOG_ERROR("读取监控快照失败");                       // 取锁失败
        }

        /*==============================
         *  #5. 北斗校时查询（开机 60 秒后一次 + 每天 11:50 后一次）
         *==============================*/
        bd_time_poll();                                     // 驱动北斗校时状态机

        /*==============================
         *  #6. 等到下一个固定采集周期
         *==============================*/
        wdg_kick(WDG_BIT_MONITOR);                          // 上报心跳（监控任务）
        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(MONITOR_PERIOD_MS));
    }
}


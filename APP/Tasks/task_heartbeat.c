/*******************************************************************************
 * 文件   ：task_heartbeat.c
 * 功能   ：心跳任务——闪 LED + 周期性打印任务列表
 * 说明   ：任务函数的固定长相：void 函数 + void* 参数 + for(;;) 永不返回；
 *          每个循环都必须“让出 CPU”（vTaskDelay 或等事件），否则会饿死别的任务
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "tasks.h"
#include "main.h"
#include <stdio.h>
#include "log.h"
#include "wdg_service.h"
#include "time_service.h"
/* Private define ------------------------------------------------------------*/
#define LED_TOGGLE_PERIOD_MS   500      // LED 翻转周期（毫秒）
#define TASKLIST_PERIOD_MS     60000    // 打印任务列表周期（毫秒，调试用，平时不必频繁）

/* Exported functions --------------------------------------------------------*/

/*******************************************************************************
 * 函数名：TaskHeartbeat
 * 功  能：心跳任务主体
 * 参  数：argument —— 创建任务时传入的参数（本任务不用）
 * 返回值：无（永不返回）
 * 说  明：LED0 每 500ms 翻转一次；每 5s 打印一次任务列表，用来查看各任务的剩余栈
 ******************************************************************************/
void TaskHeartbeat(void *argument)
{
    static char task_buf[512];                          // static：任务栈很小，大数组不能放栈上
    TickType_t  last_list_tick = 0;                     // 上次打印任务列表的时刻

    /*************
     *   #1. 开机只做一次：打印系统基础信息
     *************/
    LOG_INFO("\r\n===== 系统启动信息 =====\r\n");
    LOG_INFO("固件编译时间：" __DATE__ " " __TIME__);   // 用于确认烧录的是否为最新固件

    /* #1.1 上次复位原因（读完必须清零，否则下次开机会重复上报） */
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET)          // 看门狗复位
    {
        LOG_INFO("上次复位：独立看门狗（说明程序曾卡死过）\r\n");
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != RESET)      // 软件复位
    {
        LOG_INFO("上次复位：软件复位/重启命令\r\n");
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != RESET)      // 上电/掉电复位
    {
        LOG_INFO("上次复位：上电或掉电（正常）\r\n");
    }
    else                                                        // 其他
    {
        LOG_INFO("上次复位：其他/未知\r\n");
    }
    __HAL_RCC_CLEAR_RESET_FLAGS();                              // 清零复位标志

    /* #1.2 主频是否符合预期 */
    LOG_INFO("HCLK = %lu Hz\r\n", (unsigned long)HAL_RCC_GetHCLKFreq());

    /* #1.3 剩余堆（验证 heap 大小配置生效） */
    LOG_INFO("剩余堆 = %u 字节\r\n", (unsigned) xPortGetFreeHeapSize());

    /* #1.4 RTC 时间与时间可信状态 */
    {
        app_time_t now;

        if (time_service_get(&now))
        {
            LOG_INFO("RTC = %04u-%02u-%02u %02u:%02u:%02u  可信=%u\r\n",
                     now.year, now.month, now.day,
                     now.hour, now.minute, now.second,
                     (unsigned)time_service_is_reliable());
        }
        else
        {
            LOG_ERROR("RTC 读取失败\r\n");
        }
    }


    /*************
     *   #2. 任务主循环（永不退出）
     *************/
    for (;;)
    {
        /*************
         *   #2.1 翻转运行指示灯，然后睡 500ms 让出 CPU
         *************/
        HAL_GPIO_TogglePin(LED0_GPIO_Port, LED0_Pin);           // 翻转 LED0
        wdg_kick(WDG_BIT_HEARTBEAT);                        // 上报心跳（心跳任务）
        vTaskDelay(pdMS_TO_TICKS(LED_TOGGLE_PERIOD_MS));        // 睡觉，CPU 交给别人

        /*************
         *   #2.2 每 5 秒打印一次任务列表（看剩余栈是否够用）
         *************/
        if ((xTaskGetTickCount() - last_list_tick) >= pdMS_TO_TICKS(TASKLIST_PERIOD_MS))
        {
            last_list_tick = xTaskGetTickCount();               // 记录本次时刻
            vTaskList(task_buf);                                 // 生成任务列表到缓冲区           
            LOG_DEBUG("\r\n任务\t状态\t优先级\t剩余栈\t序号\r\n%s\r\n", task_buf);  // 周期任务列表（DEBUG 级）
            LOG_DEBUG("剩余堆 = %u 字节\r\n", (unsigned) xPortGetFreeHeapSize());
        }
    }
}

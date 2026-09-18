/*******************************************************************************
 * 文件   ：task_comm.c
 * 功能   ：通信任务——负责服务器链路的收帧、命令执行与定时上报
 * 说明   ：接收由中断搬运到缓冲区，本任务负责取帧、解析与应答；
 *          同时负责 $REST 重启请求的最终执行（等应答发出后再复位）
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "tasks.h"
#include "uart485.h"
#include "usart.h"
#include "protocol.h"
#include "log.h"
#include "FreeRTOS.h"
#include "task.h"

/* Private define ------------------------------------------------------------*/
#define COMM_POLL_MS        20U     // 收帧与上报节拍检查周期（毫秒）
#define COMM_RESTART_MS     300U    // 重启前等待应答发出的时间（毫秒）

/* Exported functions --------------------------------------------------------*/

/*******************************************************************************
 * 函数名：TaskComm
 * 功  能：通信任务主体：取帧 → 处理 → 上报节拍检查 → 重启请求处理
 * 参  数：argument —— 创建任务时传入的参数，本任务不使用
 * 返回值：无（永不返回）
 * 说  明：接收缓冲由 485 驱动管理，本任务只负责取走完整帧
 ******************************************************************************/
void TaskComm(void *argument)
{
    uint8_t    frame[UART485_RX_BUF_SIZE];
    uint16_t   len;
    TickType_t last_wake_tick;
    TickType_t restart_tick;
    TickType_t stat_tick;                               // 诊断统计打印节拍
    bool       restart_waiting;

    LOG_INFO("通信任务已启动");
    last_wake_tick = xTaskGetTickCount();               // 记录周期起点
    restart_waiting = false;                            // 尚未收到重启请求
    stat_tick       = xTaskGetTickCount();

    /*==============================
     *  #0. 上电自检：主动发一帧，验证 485 发送方向是否正常
     *==============================*/
    {
        static const uint8_t test_msg[] = "DY-PWR-485-TEST\r\n";

        if (uart485_send(&huart2, test_msg, (uint16_t)(sizeof(test_msg) - 1U)))
        {
            LOG_INFO("已发送 485 自检帧，请确认 485 侧是否收到 DY-PWR-485-TEST");
        }
        else
        {
            LOG_ERROR("485 自检帧发送失败");
        }
    }
    restart_tick    = 0;

    /*==============================
     *  #1. 任务主循环（永不退出）
     *==============================*/
    for (;;)
    {
        /*==============================
         *  #2. 取走完整帧并交给协议层处理
         *==============================*/
        if (uart485_rx_ready())
        {
            len = uart485_rx_take(frame, (uint16_t)sizeof(frame));

            if (len > 0U)
            {
                protocol_handle_frame(frame, len);      // 解析并执行命令
            }
        }

        /*==============================
         *  #3. 定时上报节拍检查（每小时第 53 分）
         *==============================*/
        /*==============================
         *  #3.1 每 5 秒打印 485 收发统计（现场排查用）
         *==============================*/
        if ((xTaskGetTickCount() - stat_tick) >= pdMS_TO_TICKS(5000))
        {
            stat_tick = xTaskGetTickCount();
            LOG_INFO("485统计 收字节=%lu 空闲=%lu 发送=%lu 帧就绪=%u",
                     (unsigned long)uart485_get_rx_bytes(),
                     (unsigned long)uart485_get_idle_count(),
                     (unsigned long)uart485_get_tx_count(),
                     (unsigned)(uart485_rx_ready() ? 1U : 0U));
        }

        protocol_report_check();

        /*==============================
         *  #4. 重启请求：等应答发出后再复位
         *==============================*/
        if (protocol_is_restart_pending())
        {
            if (restart_waiting == false)               // 第一次发现请求
            {
                restart_waiting = true;                 // 进入等待
                restart_tick    = xTaskGetTickCount();  // 记录起始时刻
            }
            else if ((xTaskGetTickCount() - restart_tick) >=
                     pdMS_TO_TICKS(COMM_RESTART_MS))    // 等待结束
            {
                LOG_WARNING("执行系统复位");
                vTaskDelay(pdMS_TO_TICKS(50));          // 给日志留出发送时间
                NVIC_SystemReset();                     // 复位系统
            }
        }

        /*==============================
         *  #5. 等到下一个处理周期
         *==============================*/
        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(COMM_POLL_MS));
    }
}

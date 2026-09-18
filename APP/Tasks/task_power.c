/*******************************************************************************
 * 文件   ：task_power.c
 * 功能   ：供电决策任务——周期收集输入、仲裁、输出决策结果
 * 说明   ：本阶段只打印决策结果，不操作 GPIO；
 *          执行动作（board_power_set）在下一阶段由本任务接入
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "tasks.h"
#include "power.h"
#include "power_apply.h"
#include "time_service.h"
#include "monitor_service.h"
#include "log.h"
#include "wdg_service.h"

/* Private define ------------------------------------------------------------*/
#define POWER_PERIOD_MS     100U        // 决策周期（毫秒）

/* Private functions prototypes ----------------------------------------------*/
static void power_log_changes(const power_decision_t *now, const power_decision_t *prev);   // 打印决策变化

/* Private functions ---------------------------------------------------------*/

/*******************************************************************************
 * 函数名：power_log_changes
 * 功  能：打印本次决策相对上一轮发生的变化
 * 参  数：now  —— 本次决策结果
 *         prev —— 上一次决策结果
 * 返回值：无
 * 说  明：只在变化时打印，避免每 100ms 刷屏
 ******************************************************************************/
static void power_log_changes(const power_decision_t *now, const power_decision_t *prev)
{
    /*==============================
     *  #1. 准入状态变化先提示
     *==============================*/
    if (now->allowed != prev->allowed)
    {
        if (now->allowed)
        {
            LOG_INFO("上电准入：允许（电压合格且时间可信）");
        }
        else
        {
            LOG_WARNING("上电准入：禁止（电压不合格或时间不可信）→ 全部设备断电");
        }
    }

    /*==============================
     *  #2. 逐台比较设备目标状态
     *==============================*/
    for (uint8_t i = 0U; i < DEV_CNT; i++)
    {
        if (now->target[i] == prev->target[i])
        {
            continue;                                       // 状态未变，不打印
        }

        LOG_INFO("供电决策 %s : %s  req=0x%04X",
                 board_dev_name((dev_id_t)i),
                 now->target[i] ? "ON " : "OFF",
                 (unsigned)now->req_mask[i]);
    }
}

/* Exported functions --------------------------------------------------------*/

/*******************************************************************************
 * 函数名：TaskPower
 * 功  能：供电决策任务主体：取输入 → 仲裁 → 打印决策变化
 * 参  数：argument —— 创建任务时传入的参数，本任务不使用
 * 返回值：无（永不返回）
 * 说  明：使用 vTaskDelayUntil 保持 100ms 稳定周期
 ******************************************************************************/
void TaskPower(void *argument)
{
    power_input_t      in;
    power_decision_t   dec;
    power_decision_t   prev;
    app_time_t         t;
    monitor_snapshot_t snap;
    TickType_t         last_wake_tick;

    memset(&prev, 0, sizeof(prev));                     // 上一轮决策清零
    last_wake_tick = xTaskGetTickCount();               // 记录周期起点

    /*==============================
     *  #1. 任务主循环（永不退出）
     *==============================*/
    for (;;)
    {
        /*==============================
         *  #2. 取时间与运行时长
         *==============================*/
        memset(&in, 0, sizeof(in));

        if (time_service_get(&t))
        {
            in.hour   = t.hour;                         // 当前时
            in.minute = t.minute;                       // 当前分
            in.second = t.second;                       // 当前秒
        }

        in.uptime_sec    = time_service_uptime_sec();    // 上电运行秒数
        in.time_reliable = time_service_is_reliable();   // 时间是否可信

        /*==============================
         *  #3. 取监控快照（温度告警与电压门槛）
         *==============================*/
        if (monitor_service_get_snapshot(&snap))
        {
            in.alarm_flags      = snap.alarm_flags;         // 温度告警
            in.power_on_allowed = snap.power_on_allowed;    // 电压门槛
        }

        /*==============================
         *  #4. 仲裁
         *==============================*/
        power_decide(&in, &dec);

        /*==============================
         *  #5. 把决策结果应用到硬件输出
         *==============================*/
        power_apply(&dec);

        /*==============================
         *  #6. 打印决策变化并记录本轮结果
         *==============================*/
        power_log_changes(&dec, &prev);
        prev = dec;                                     // 记录本轮结果

        /*==============================
         *  #7. 等到下一个决策周期
         *==============================*/
        wdg_kick(WDG_BIT_POWER);                            // 上报心跳（供电决策任务）
        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(POWER_PERIOD_MS));
    }
}

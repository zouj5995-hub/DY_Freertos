/*******************************************************************************
 * 文件   ：power_apply.c
 * 功能   ：供电执行层实现（含工控机 30 秒安全关机流程）
 * 说明   ：设备输出只在"目标状态发生变化"时才写 GPIO；
 *          工控机从开变关时，先发关机命令，等 30 秒再断电，
 *          且命令一经发出即不可逆（流程结束前不会重新上电）
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "power_apply.h"
#include "board.h"
#include "log.h"
#include "uart485.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>
#include <string.h>

/* Private define ------------------------------------------------------------*/
#define PC_OFF_DELAY_MS     30000U      // 工控机关机命令发出后等待断电的时间（毫秒）

/* Private types -------------------------------------------------------------*/

/*******************************************************************************
 * 名称  ：pc_state_t
 * 功能  ：工控机输出状态机
 ******************************************************************************/
typedef enum
{
    PC_STATE_IDLE = 0,      // 空闲：输出跟随目标状态
    PC_STATE_SHUTDOWN,      // 已发关机命令，等待 30 秒后断电
} pc_state_t;

/* Private variables ---------------------------------------------------------*/
static bool       s_output[DEV_CNT];            // 已写入硬件的输出状态
static pc_state_t s_pc_state = PC_STATE_IDLE;   // 工控机状态机
static TickType_t s_pc_shutdown_tick = 0;       // 工控机关机流程起始时刻

/* Private functions prototypes ----------------------------------------------*/
static void power_send_pc_off_cmd(void);        // 给工控机发送关机命令
static void power_apply_pc(bool target_on);     // 工控机输出处理（含安全关机流程）

/* Private functions ---------------------------------------------------------*/

/*******************************************************************************
 * 函数名：power_send_pc_off_cmd
 * 功  能：给工控机发送关机命令
 * 参  数：无
 * 返回值：无
 * 说  明：命令内容为 $OFF\nCC60$OVER（经串口3 输出），
 *         方向脚由 485 驱动自动切换，发完立即切回接收
 ******************************************************************************/
static void power_send_pc_off_cmd(void)
{
    /*==============================
     *  #1. 关机命令内容：$OFF\nCC60$OVER（12 字节，沿用旧版本格式）
     *==============================*/
    static const uint8_t s_off_cmd[12] = {
        0x24U, 0x4FU, 0x46U, 0x46U, 0x0AU, 0xCCU, 0x60U, 0x24U, 0x4FU, 0x56U, 0x45U, 0x52U
    };

    /*==============================
     *  #2. 通过串口3（485 半双工）发出
     *==============================*/
    if (uart485_send(&huart3, s_off_cmd, (uint16_t)sizeof(s_off_cmd)))
    {
        LOG_INFO("工控机关机命令已通过串口3 发出");
    }
    else
    {
        LOG_ERROR("工控机关机命令发送失败（串口3）");
    }
}

/*******************************************************************************
 * 函数名：power_apply_pc
 * 功  能：处理工控机输出：目标为开则直接上电，目标由开变关则走安全关机流程
 * 参  数：target_on —— 本次决策给出的工控机目标状态
 * 返回值：无
 * 说  明：关机流程期间忽略目标变化（命令已发出，不可逆）
 ******************************************************************************/
static void power_apply_pc(bool target_on)
{
    /*==============================
     *  #1. 按状态机处理
     *==============================*/
    switch (s_pc_state)
    {
        case PC_STATE_IDLE:
            if (target_on)
            {
                if (s_output[DEV_IPC] == false)             // 需要上电
                {
                    board_power_set(DEV_IPC, true);         // 输出供电
                    s_output[DEV_IPC] = true;               // 记录输出状态
                    LOG_INFO("设备输出 IPC -> ON");
                }
            }
            else if (s_output[DEV_IPC])                     // 由开变关
            {
                power_send_pc_off_cmd();                    // 先发关机命令
                s_pc_state         = PC_STATE_SHUTDOWN;     // 进入等待
                s_pc_shutdown_tick = xTaskGetTickCount();   // 记录起始时刻
                LOG_INFO("工控机进入安全关机流程：%u 秒后断电",
                         (unsigned)(PC_OFF_DELAY_MS / 1000U));
            }
            else
            {
                /* 本来就是关的，无需任何动作 */
            }
            break;

        case PC_STATE_SHUTDOWN:
            if ((xTaskGetTickCount() - s_pc_shutdown_tick) >=
                pdMS_TO_TICKS(PC_OFF_DELAY_MS))             // 延时结束
            {
                board_power_set(DEV_IPC, false);            // 执行断电
                s_output[DEV_IPC] = false;                  // 记录输出状态
                s_pc_state        = PC_STATE_IDLE;          // 回到空闲
                LOG_INFO("工控机安全关机流程结束，已断电");
            }
            break;

        default:                                            // 异常状态自恢复
            s_pc_state = PC_STATE_IDLE;
            break;
    }
}

/* Exported functions --------------------------------------------------------*/

/*******************************************************************************
 * 函数名：power_apply_init
 * 功  能：初始化供电执行层
 * 参  数：无
 * 返回值：无
 * 说  明：清空输出状态记录，使软件记录与硬件实际状态一致
 ******************************************************************************/
void power_apply_init(void)
{
    /*==============================
     *  #1. 输出状态清零（与 board_init 后的硬件状态一致）
     *==============================*/
    memset(s_output, 0, sizeof(s_output));                  // 全部记为断电
    s_pc_state         = PC_STATE_IDLE;                     // 状态机复位
    s_pc_shutdown_tick = 0;
}

/*******************************************************************************
 * 函数名：power_apply
 * 功  能：把决策结果应用到硬件输出
 * 参  数：dec —— 本次决策结果
 * 返回值：无
 * 说  明：只有目标状态发生变化时才写 GPIO；工控机单独走安全关机流程
 ******************************************************************************/
void power_apply(const power_decision_t *dec)
{
    /*==============================
     *  #1. 参数检查
     *==============================*/
    if (dec == NULL)
    {
        return;                                             // 参数无效
    }

    /*==============================
     *  #2. 逐台设备比对并输出
     *==============================*/
    for (uint8_t i = 0U; i < DEV_CNT; i++)
    {
        if (i == (uint8_t)DEV_IPC)                          // 工控机单独处理
        {
            power_apply_pc(dec->target[i]);
            continue;
        }

        if (dec->target[i] != s_output[i])                  // 仅状态变化时写引脚
        {
            board_power_set((dev_id_t)i, dec->target[i]);   // 写硬件输出
            s_output[i] = dec->target[i];                   // 记录输出状态

            LOG_INFO("设备输出 %s -> %s",
                     board_dev_name((dev_id_t)i),
                     dec->target[i] ? "ON " : "OFF");
        }
    }
}

/*******************************************************************************
 * 函数名：power_is_pc_shutting_down
 * 功  能：查询工控机是否正在安全关机流程中
 * 参  数：无
 * 返回值：true 表示正在关机流程中
 * 说  明：供其他模块判断"工控机是否处于不可立即上电的状态"
 ******************************************************************************/
bool power_is_pc_shutting_down(void)
{
    /*==============================
     *  #1. 直接由状态机判断
     *==============================*/
    return (s_pc_state == PC_STATE_SHUTDOWN);               // 是否处于等待断电状态
}

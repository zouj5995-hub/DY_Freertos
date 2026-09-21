/*******************************************************************************
 * 文件   ：bd_time.c
 * 功能   ：北斗主动校时实现
 * 说明   ：本板的北斗模块为查询式，需主动发送 $BDRNS,1*54 它才会回一帧
 *          $BDRMC 时间数据。本模块用状态机管理“何时发、发几次、何时放弃”：
 *            ① 开机后等 60 秒（留给北斗搜星）发起首次查询；
 *            ② 之后每天 11:50 之后查询一次；
 *            ③ 发出后 30 秒未收到 $BDRMC 则重发，最多重试 3 次；
 *            ④ 按“年-月-日”去重，同一天不重复查询。
 *          $BDRMC 的解析与 RTC 写入由 protocol.c 的 proto_cmd_bd_time 负责，
 *          本模块只负责“把查询指令发出去”；校时成功后由对方回调
 *          bd_time_on_synced() 通知本轮完成。
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "bd_time.h"
#include "time_service.h"
#include "uart485.h"
#include "usart.h"
#include "log.h"
#include "FreeRTOS.h"
#include "task.h"

/* Private define ------------------------------------------------------------*/
/* 北斗查询指令：ASCII 8 字节，按旧版程序原样使用（'*54' 为 NMEA 校验和），
   不加回车换行；发送长度取 sizeof-1，以去掉结尾的 '\0' */
#define BD_QUERY_CMD            "$BDRNS,1*54"

#define BD_BOOT_WAIT_MS         60000U  // 开机后等待北斗搜星的时间（毫秒）
#define BD_REPLY_TIMEOUT_MS     30000U  // 发出查询后等待 $BDRMC 回复的超时（毫秒）
#define BD_MAX_RETRY            3U      // 超时后的最大重发次数

#define BD_DAILY_HOUR           11U     // 每日校时时刻：11:50 之后
#define BD_DAILY_MINUTE         50U

/* Private types -------------------------------------------------------------*/

/*******************************************************************************
 * 名称  ：bd_state_t
 * 功能  ：北斗校时查询状态机的状态
 ******************************************************************************/
typedef enum
{
    BD_ST_WAIT_BOOT = 0,    // 开机等待搜星（等够时间后发起首次查询）
    BD_ST_WAITING,          // 已发出查询，等待 $BDRMC 回复
    BD_ST_IDLE,             // 空闲：等待“每日一次”的触发条件
} bd_state_t;

/* Private variables ---------------------------------------------------------*/
static bd_state_t  s_state       = BD_ST_WAIT_BOOT;     // 当前状态
static TickType_t  s_boot_tick   = 0U;                  // 开机时刻（用于开机延时）
static TickType_t  s_send_tick   = 0U;                  // 上次发出查询的时刻
static uint8_t     s_retry       = 0U;                  // 本轮已重发次数
static bool        s_ever_synced = false;               // 是否成功校时过至少一次
static uint16_t    s_last_year   = 0U;                  // 最近一次校时成功的日期
static uint8_t     s_last_month  = 0U;
static uint8_t     s_last_day    = 0U;

/* Private functions prototypes ----------------------------------------------*/
static void bd_time_send(void);                                 // 发送一次北斗查询指令
static bool bd_time_synced_today(const app_time_t *now);        // 判断今天是否已校时

/* Private functions ---------------------------------------------------------*/

/*******************************************************************************
 * 函数名：bd_time_send
 * 功  能：向北斗模块发送一次校时查询指令
 * 参  数：无
 * 返回值：无
 * 说  明：指令经 USART2（北斗所在的 485 链路）发出，收发方向由 uart485_send
 *         自动切换；发送后记录时刻，供超时判定使用
 ******************************************************************************/
static void bd_time_send(void)
{
    static const char s_cmd[] = BD_QUERY_CMD;                   // 编译期常量

    /*==============================
     *  #1. 经 USART2 发出查询指令（长度去掉结尾 '\0'）
     *==============================*/
    (void)uart485_send(&huart2, (const uint8_t *)s_cmd, (uint16_t)(sizeof(s_cmd) - 1U));

    s_send_tick = xTaskGetTickCount();                          // 记录本次发送时刻
    LOG_INFO("已向北斗发送校时查询指令：第 %u 次", (unsigned)(s_retry + 1U));
}

/*******************************************************************************
 * 函数名：bd_time_synced_today
 * 功  能：判断“今天是否已经成功校过时”
 * 参  数：now —— 当前时间
 * 返回值：true 表示今天已校过时（本日不再查询）
 * 说  明：按“年-月-日”三元组比较，避免只用“日”在跨月/跨年时误判
 ******************************************************************************/
static bool bd_time_synced_today(const app_time_t *now)
{
    return (s_ever_synced &&
            (s_last_year  == now->year) &&
            (s_last_month == now->month) &&
            (s_last_day   == now->day));
}

/* Exported functions --------------------------------------------------------*/

/*******************************************************************************
 * 函数名：bd_time_init
 * 功  能：初始化北斗校时模块
 * 参  数：无
 * 返回值：无
 * 说  明：记录开机时刻并进入“等待搜星”状态；应在调度器启动后的任务里调用
 ******************************************************************************/
void bd_time_init(void)
{
    s_state       = BD_ST_WAIT_BOOT;                            // 从开机等待开始
    s_boot_tick   = xTaskGetTickCount();                        // 记录开机时刻
    s_retry       = 0U;
    s_ever_synced = false;

    LOG_INFO("北斗校时已启用：开机 %u 秒后发起首次查询，之后每天 %02u:%02u 后查询一次",
             (unsigned)(BD_BOOT_WAIT_MS / 1000U),
             (unsigned)BD_DAILY_HOUR,
             (unsigned)BD_DAILY_MINUTE);
}

/*******************************************************************************
 * 函数名：bd_time_poll
 * 功  能：北斗校时查询状态机（周期调用）
 * 参  数：无
 * 返回值：无
 * 说  明：建议每 1 秒调用一次；三个状态依次为
 *         ① WAIT_BOOT：开机等够 BD_BOOT_WAIT_MS 后发起首次查询；
 *         ② WAITING  ：等回复，超时未收到则重发（最多 BD_MAX_RETRY 次）；
 *         ③ IDLE     ：每天 BD_DAILY_HOUR:BD_DAILY_MINUTE 之后查询一次
 ******************************************************************************/
void bd_time_poll(void)
{
    app_time_t now;
    bool       time_ok;
    TickType_t elapsed;

    time_ok = time_service_get(&now);                           // 读当前时间（可能尚不可信）

    switch (s_state)
    {
        /*==============================
         *  #1. 开机等待搜星：够时间就发起首次查询
         *==============================*/
        case BD_ST_WAIT_BOOT:
        {
            elapsed = xTaskGetTickCount() - s_boot_tick;

            if (elapsed >= pdMS_TO_TICKS(BD_BOOT_WAIT_MS))
            {
                s_retry = 0U;                                   // 新一轮，重发计数清零
                bd_time_send();                                 // 发出首次查询
                s_state = BD_ST_WAITING;                        // 转入等待回复
            }
            break;
        }

        /*==============================
         *  #2. 等待回复：超时则重发，超过次数则放弃本轮
         *==============================*/
        case BD_ST_WAITING:
        {
            elapsed = xTaskGetTickCount() - s_send_tick;

            if (elapsed >= pdMS_TO_TICKS(BD_REPLY_TIMEOUT_MS))
            {
                if (s_retry < BD_MAX_RETRY)
                {
                    s_retry++;                                  // 累计重发次数
                    bd_time_send();                             // 重发查询指令
                }
                else
                {
                    LOG_WARNING("北斗校时查询无应答，本轮放弃（已尝试 %u 次）",
                                (unsigned)(s_retry + 1U));
                    s_retry = 0U;
                    s_state = BD_ST_IDLE;                       // 放弃本轮，等下个触发点
                }
            }
            break;
        }

        /*==============================
         *  #3. 空闲：每天 11:50 之后查询一次（当天不重复）
         *==============================*/
        case BD_ST_IDLE:
        default:
        {
            if (time_ok && (bd_time_synced_today(&now) == false))
            {
                bool passed_daily = (now.hour > BD_DAILY_HOUR) ||
                                    ((now.hour == BD_DAILY_HOUR) && (now.minute >= BD_DAILY_MINUTE));

                if (passed_daily)                               // 已过当日校时时刻且今天未校时
                {
                    s_retry = 0U;
                    bd_time_send();                             // 发起当日查询
                    s_state = BD_ST_WAITING;
                }
            }
            break;
        }
    }
}

/*******************************************************************************
 * 函数名：bd_time_on_synced
 * 功  能：收到 $BDRMC 并完成校时时由 protocol 层回调，通知本轮已完成
 * 参  数：无
 * 返回值：无
 * 说  明：记录成功校时的日期，用于“同一天不重复查询”；并回到空闲状态
 ******************************************************************************/
void bd_time_on_synced(void)
{
    app_time_t now;

    s_retry       = 0U;
    s_state       = BD_ST_IDLE;
    s_ever_synced = true;

    if (time_service_get(&now))
    {
        s_last_year  = now.year;                                // 记录校时成功时的日期
        s_last_month = now.month;
        s_last_day   = now.day;
    }
}

/*******************************************************************************
 * 函数名：bd_time_is_waiting
 * 功  能：查询当前是否正在等待北斗回复
 * 参  数：无
 * 返回值：true 表示正在等待回复
 ******************************************************************************/
bool bd_time_is_waiting(void)
{
    return (s_state == BD_ST_WAITING);
}

/*******************************************************************************
 * 文件   ：power_decide.c
 * 功能   ：供电决策层实现（请求收集 + 优先级仲裁）
 * 说明   ：仲裁顺序固定为 保护否决 > 保持窗口 > 跟随 > 手动 > 任务策略；
 *          每轮决策都从输入全量重算，不依赖上一轮结果，因此与调用顺序无关
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "power.h"
#include "monitor_service.h"
#include <stddef.h>
#include <string.h>

/* Private define ------------------------------------------------------------*/

/* 北斗保持窗口参数 */
#define BD_BOOT_KEEP_SEC        600U                            // 开机后保持北斗 10 分钟
#define BD_CLOCK_START_SEC      (11U * 3600U + 50U * 60U)       // 11:50:00
#define BD_CLOCK_END_SEC        (12U * 3600U)                   // 12:00:00
#define BD_REPORT_START_MIN     52U                             // 每小时 52 分
#define BD_REPORT_END_MIN       54U                             // 每小时 54 分

/* 一天的秒数 */
#define POWER_SEC_PER_DAY       (24UL * 3600UL)

/* Private variables ---------------------------------------------------------*/

/* 规则表：初始化时从 EEPROM 载入，之后只在策略更新时刷新 */
static rule_item_t s_rules[RULE_COUNT];

/* 每台设备接受的"否决类"来源（保护） */
static const uint16_t s_veto_mask[DEV_CNT] =
{
    [DEV_IPC]    = RQ_PROT_IPC_TEMP | RQ_PROT_PCB_TEMP,     // 工控机受两类温度保护
    [DEV_SONAR]  = RQ_PROT_PCB_TEMP,                        // 其余设备只受 PCB 保护
    [DEV_BD]     = RQ_PROT_PCB_TEMP,
    [DEV_BK1]    = RQ_PROT_PCB_TEMP,
    [DEV_RADAR]  = RQ_PROT_PCB_TEMP,
    [DEV_CAMERA] = RQ_PROT_PCB_TEMP,
    [DEV_BK2]    = RQ_PROT_PCB_TEMP,
    [DEV_BK3]    = RQ_PROT_PCB_TEMP,
};

/* 每台设备接受的"保持类"来源（强制供电） */
static const uint16_t s_keep_mask[DEV_CNT] =
{
    [DEV_BD]     = RQ_KEEP_BD_BOOT | RQ_KEEP_BD_CLOCK | RQ_KEEP_BD_REPORT,   // 只有北斗有保持需求
};

/* 每台设备接受的"跟随类"来源 */
static const uint16_t s_follow_mask[DEV_CNT] =
{
    [DEV_BD]     = RQ_FOLLOW_PC,                            // 北斗跟随工控机
};

/* 手动控制目标状态（调试通道使用，默认为关） */
static bool s_manual_target[DEV_CNT];

/* Private functions prototypes ----------------------------------------------*/
static void power_expand_device(uint8_t code, uint16_t *req);                   // 设备码展开成请求位
static void power_collect_schedule(const power_input_t *in, uint16_t *req);     // 收集任务策略请求
static void power_collect_bd_keep(const power_input_t *in, uint16_t *req);      // 收集北斗保持请求
static void power_collect_protect(const power_input_t *in, uint16_t *req);      // 收集温度保护请求
static bool power_decide_device(dev_id_t dev, const uint16_t *req, bool pc_target);  // 单台设备仲裁

/* Private functions ---------------------------------------------------------*/

/*******************************************************************************
 * 函数名：power_expand_device
 * 功  能：把规则中的目标设备码展开成各设备的请求位
 * 参  数：code —— 目标设备码（单设备或组合设备）
 *         req  —— 各设备的请求位数组（就地修改）
 * 返回值：无
 * 说  明：未知设备码不做任何动作
 ******************************************************************************/
static void power_expand_device(uint8_t code, uint16_t *req)
{
    /*==============================
     *  #1. 单设备码直接置位
     *==============================*/
    switch (code)
    {
        case RULE_DEVICE_PC:     req[DEV_IPC]    |= RQ_SCHEDULE; break;      // 工控机
        case RULE_DEVICE_RADAR:  req[DEV_RADAR]  |= RQ_SCHEDULE; break;      // 雷达
        case RULE_DEVICE_SONAR:  req[DEV_SONAR]  |= RQ_SCHEDULE; break;      // 声纳
        case RULE_DEVICE_CAMERA: req[DEV_CAMERA] |= RQ_SCHEDULE; break;      // 摄像头
        case RULE_DEVICE_BD:     req[DEV_BD]     |= RQ_SCHEDULE; break;      // 北斗
        case RULE_DEVICE_BK1:    req[DEV_BK1]    |= RQ_SCHEDULE; break;      // 备用一
        case RULE_DEVICE_BK2:    req[DEV_BK2]    |= RQ_SCHEDULE; break;      // 备用二
        case RULE_DEVICE_BK3:    req[DEV_BK3]    |= RQ_SCHEDULE; break;      // 备用三

        /*==============================
         *  #2. 组合设备码逐台置位
         *==============================*/
        case RULE_DEVICE_PC_RADAR_SONAR_CAMERA:                              // 工控机+雷达+声纳+摄像头
            req[DEV_IPC]    |= RQ_SCHEDULE;
            req[DEV_RADAR]  |= RQ_SCHEDULE;
            req[DEV_SONAR]  |= RQ_SCHEDULE;
            req[DEV_CAMERA] |= RQ_SCHEDULE;
            break;

        case RULE_DEVICE_PC_RADAR_CAMERA:                                    // 工控机+雷达+摄像头
            req[DEV_IPC]    |= RQ_SCHEDULE;
            req[DEV_RADAR]  |= RQ_SCHEDULE;
            req[DEV_CAMERA] |= RQ_SCHEDULE;
            break;

        case RULE_DEVICE_PC_RADAR:                                           // 工控机+雷达
            req[DEV_IPC]   |= RQ_SCHEDULE;
            req[DEV_RADAR] |= RQ_SCHEDULE;
            break;

        case RULE_DEVICE_PC_SONAR:                                           // 工控机+声纳
            req[DEV_IPC]   |= RQ_SCHEDULE;
            req[DEV_SONAR] |= RQ_SCHEDULE;
            break;

        default:                                                             // 未知设备码
            break;
    }
}

/*******************************************************************************
 * 函数名：power_collect_schedule
 * 功  能：收集任务策略请求：遍历规则表，命中的规则给对应设备置请求位
 * 参  数：in  —— 决策输入（提供当前时间）
 *         req —— 各设备的请求位数组（就地修改）
 * 返回值：无
 * 说  明：区间按当日秒数比较，支持跨天（结束时间超过一天时按跨天判断）
 ******************************************************************************/
static void power_collect_schedule(const power_input_t *in, uint16_t *req)
{
    uint32_t now_sec;

    /*==============================
     *  #1. 把当前时间换算成当日秒数
     *==============================*/
    now_sec = ((uint32_t)in->hour * 3600UL +
               (uint32_t)in->minute * 60UL +
               (uint32_t)in->second);

    /*==============================
     *  #2. 遍历规则表，判断是否命中
     *==============================*/
    for (uint8_t i = 0U; i < RULE_COUNT; i++)
    {
        uint32_t start_sec;
        uint32_t end_sec;
        bool     hit;

        if (s_rules[i].target_device == RULE_DEVICE_INVALID)        // 无效规则跳过
        {
            continue;
        }

        start_sec = ((uint32_t)s_rules[i].start_h * 3600UL +
                     (uint32_t)s_rules[i].start_m * 60UL +
                     (uint32_t)s_rules[i].start_s);
        end_sec   = start_sec + (uint32_t)s_rules[i].work_sec;      // 结束时刻

        /*==============================
         *  #3. 区分普通区间与跨天区间
         *==============================*/
        if (end_sec <= POWER_SEC_PER_DAY)                           // 不跨天
        {
            hit = ((now_sec >= start_sec) && (now_sec < end_sec));
        }
        else                                                        // 跨天（跨过零点）
        {
            hit = ((now_sec >= start_sec) ||
                   (now_sec < (end_sec - POWER_SEC_PER_DAY)));
        }

        /*==============================
         *  #4. 命中则展开为各设备的请求位
         *==============================*/
        if (hit)
        {
            power_expand_device(s_rules[i].target_device, req);
        }
    }
}

/*******************************************************************************
 * 函数名：power_collect_bd_keep
 * 功  能：收集北斗的保持类请求（开机搜星、校时窗口、整点上报窗口）
 * 参  数：in  —— 决策输入
 *         req —— 请求位数组（就地修改）
 * 返回值：无
 * 说  明：这三个窗口与任务策略无关，属于北斗设备自身的功能需求
 ******************************************************************************/
static void power_collect_bd_keep(const power_input_t *in, uint16_t *req)
{
    uint32_t now_sec;

    /*==============================
     *  #1. 开机搜星窗口：上电 10 分钟内保持北斗
     *==============================*/
    if (in->uptime_sec < BD_BOOT_KEEP_SEC)
    {
        req[DEV_BD] |= RQ_KEEP_BD_BOOT;
    }

    /*==============================
     *  #2. 校时窗口：每天 11:50 - 12:00
     *==============================*/
    now_sec = ((uint32_t)in->hour * 3600UL +
               (uint32_t)in->minute * 60UL +
               (uint32_t)in->second);

    if ((now_sec >= BD_CLOCK_START_SEC) && (now_sec < BD_CLOCK_END_SEC))
    {
        req[DEV_BD] |= RQ_KEEP_BD_CLOCK;
    }

    /*==============================
     *  #3. 整点上报窗口：每小时 52 - 54 分
     *==============================*/
    if ((in->minute >= BD_REPORT_START_MIN) && (in->minute <= BD_REPORT_END_MIN))
    {
        req[DEV_BD] |= RQ_KEEP_BD_REPORT;
    }
}

/*******************************************************************************
 * 函数名：power_collect_protect
 * 功  能：收集温度保护请求（由监控告警标志决定）
 * 参  数：in  —— 决策输入（提供告警标志）
 *         req —— 请求位数组（就地修改）
 * 返回值：无
 * 说  明：电压不参与保护（按既定决策仅采集上报）
 ******************************************************************************/
static void power_collect_protect(const power_input_t *in, uint16_t *req)
{
    /*==============================
     *  #1. 工控机过温：只否决工控机
     *==============================*/
    if ((in->alarm_flags & MONITOR_ALARM_IPC_OVERTEMP) != 0U)
    {
        req[DEV_IPC] |= RQ_PROT_IPC_TEMP;
    }

    /*==============================
     *  #2. PCB 过温：否决全部设备
     *==============================*/
    if ((in->alarm_flags & MONITOR_ALARM_PCB_OVERTEMP) != 0U)
    {
        for (uint8_t i = 0U; i < DEV_CNT; i++)
        {
            req[i] |= RQ_PROT_PCB_TEMP;
        }
    }
}

/*******************************************************************************
 * 函数名：power_decide_device
 * 功  能：单台设备的仲裁：按固定优先级算出目标状态
 * 参  数：dev       —— 设备编号
 *         req       —— 各设备的请求位数组
 *         pc_target —— 工控机的最终目标状态（供跟随使用）
 * 返回值：true 表示该设备应供电，false 表示应断电
 * 说  明：顺序为 保护否决 > 保持窗口 > 跟随工控机 > 手动 > 任务策略
 ******************************************************************************/
static bool power_decide_device(dev_id_t dev, const uint16_t *req, bool pc_target)
{
    uint16_t m = req[dev];

    /*==============================
     *  #1. 保护类一票否决
     *==============================*/
    if ((m & s_veto_mask[dev]) != 0U)
    {
        return false;                                       // 保护优先，直接断电
    }

    /*==============================
     *  #2. 保持类强制供电
     *==============================*/
    if ((m & s_keep_mask[dev]) != 0U)
    {
        return true;                                        // 窗口内必须供电
    }

    /*==============================
     *  #3. 跟随类：取被跟随设备的最终结论
     *==============================*/
    if ((m & s_follow_mask[dev]) != 0U)
    {
        return pc_target;                                   // 跟随工控机
    }

    /*==============================
     *  #4. 手动控制（调试通道）
     *==============================*/
    if ((m & RQ_MANUAL) != 0U)
    {
        return s_manual_target[dev];                        // 手动指定的状态
    }

    /*==============================
     *  #5. 基线：任务策略
     *==============================*/
    return ((m & RQ_SCHEDULE) != 0U);
}

/* Exported functions --------------------------------------------------------*/

/*******************************************************************************
 * 函数名：power_init
 * 功  能：初始化供电决策层
 * 参  数：无
 * 返回值：无
 * 说  明：从 EEPROM 载入规则表；读取失败时使用安全默认（全部规则无效）
 ******************************************************************************/
void power_init(void)
{
    /*==============================
     *  #1. 清空手动控制状态
     *==============================*/
    memset(s_manual_target, 0, sizeof(s_manual_target));

    /*==============================
     *  #2. 从 EEPROM 载入规则表
     *==============================*/
    if (rule_store_load(s_rules, RULE_COUNT) == false)
    {
        rule_store_set_default(s_rules, RULE_COUNT);        // 读取失败：安全默认
    }
}

/*******************************************************************************
 * 函数名：power_reload_rules
 * 功  能：重新从 EEPROM 载入规则表
 * 参  数：无
 * 返回值：true 表示载入成功，false 表示失败（保留原规则表）
 * 说  明：上位机下发新策略并写入 EEPROM 后调用
 ******************************************************************************/
bool power_reload_rules(void)
{
    rule_item_t pending[RULE_COUNT];                        // 先读到临时表

    /*==============================
     *  #1. 读到临时表，成功后才替换（双缓冲）
     *==============================*/
    if (rule_store_load(pending, RULE_COUNT) == false)
    {
        return false;                                       // 读取失败，保留原表
    }

    memcpy(s_rules, pending, sizeof(s_rules));              // 整表替换

    return true;                                            // 载入成功
}

/*******************************************************************************
 * 函数名：power_set_manual
 * 功  能：设置某台设备的手动目标状态
 * 参  数：dev —— 设备编号
 *         on  —— true 手动开，false 手动关
 * 返回值：无
 * 说  明：调用后该设备会带上 RQ_MANUAL 请求位，需由调用方保证置位
 ******************************************************************************/
void power_set_manual(dev_id_t dev, bool on)
{
    /*==============================
     *  #1. 参数检查后记录手动状态
     *==============================*/
    if (dev >= DEV_CNT)
    {
        return;                                             // 编号非法
    }

    s_manual_target[dev] = on;                              // 记录手动目标
}

/*******************************************************************************
 * 函数名：power_clear_manual
 * 功  能：清除全部手动目标状态
 * 参  数：无
 * 返回值：无
 * 说  明：调试结束后调用，让设备回到按策略运行
 ******************************************************************************/
void power_clear_manual(void)
{
    /*==============================
     *  #1. 全部手动目标置为关
     *==============================*/
    memset(s_manual_target, 0, sizeof(s_manual_target));    // 清零
}

/*******************************************************************************
 * 函数名：power_decide
 * 功  能：收集所有请求并仲裁出 8 台设备的目标状态
 * 参  数：in  —— 决策输入
 *         out —— 决策输出
 * 返回值：无
 * 说  明：每轮全量重算；准入条件不满足时所有设备强制关闭
 ******************************************************************************/
void power_decide(const power_input_t *in, power_decision_t *out)
{
    /*==============================
     *  #1. 输入检查与输出清零
     *==============================*/
    if ((in == NULL) || (out == NULL))
    {
        return;                                             // 参数无效
    }

    memset(out, 0, sizeof(power_decision_t));

    /*==============================
     *  #2. 收集各类请求
     *==============================*/
    power_collect_schedule(in, out->req_mask);              // 任务策略
    power_collect_bd_keep(in, out->req_mask);               // 北斗保持窗口
    power_collect_protect(in, out->req_mask);               // 温度保护

    /*==============================
     *  #3. 先算工控机（北斗要跟随它的最终结论）
     *==============================*/
    out->target[DEV_IPC] = power_decide_device(DEV_IPC, out->req_mask, false);

    /*==============================
     *  #4. 其余设备逐台仲裁
     *==============================*/
    for (uint8_t i = 0U; i < DEV_CNT; i++)
    {
        if (i == (uint8_t)DEV_IPC)
        {
            continue;                                       // 工控机已算过
        }

        out->target[i] = power_decide_device((dev_id_t)i, out->req_mask, out->target[DEV_IPC]);
    }

    /*==============================
     *  #5. 合并准入条件：电压合格 且 时间可信
     *==============================*/
    out->allowed = (in->power_on_allowed && in->time_reliable);

    /*==============================
     *  #6. 准入不通过：全部设备强制关闭
     *==============================*/
    if (out->allowed == false)
    {
        for (uint8_t i = 0U; i < DEV_CNT; i++)
        {
            out->target[i] = false;                         // 一台都不给电
        }
    }
}

/*******************************************************************************
 * 文件   ：task_selftest.c
 * 功能   ：板级自检任务——当前阶段检查 EEPROM 读写与规则表载入
 * 说明   ：自检项随驱动逐个补齐；本阶段点亮"EEPROM"一项，
 *          并打印从 EEPROM 读到的有效规则，便于与上位机下发内容核对
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "tasks.h"
#include "rule_store.h"
#include "eeprom.h"
#include "log.h"

/* Private define ------------------------------------------------------------*/
#define SELFTEST_REPEAT_MS   600000U    // 只读巡检周期（毫秒）：EEPROM 写寿命有限，运行期不再写

/* Private variables ---------------------------------------------------------*/
static rule_item_t s_rules[RULE_COUNT];         // 规则表（static：大数组不能放任务栈上）

/* Private functions prototypes ----------------------------------------------*/
static const char *selftest_device_name(uint8_t code);      // 目标设备码转名字
static void        selftest_report_rules(void);             // 打印已载入的规则表

/* Private functions ---------------------------------------------------------*/

/*******************************************************************************
 * 函数名：selftest_device_name
 * 功  能：把目标设备码转换成可读名字
 * 参  数：code —— 目标设备码
 * 返回值：设备名字符串
 ******************************************************************************/
static const char *selftest_device_name(uint8_t code)
{
    /*==============================
     *  #1. 单设备与组合设备名称映射
     *==============================*/
    switch (code)
    {
        case RULE_DEVICE_PC:     return "工控机";                   // 0x00
        case RULE_DEVICE_RADAR:  return "雷达";                     // 0x01
        case RULE_DEVICE_SONAR:  return "声纳";                     // 0x02
        case RULE_DEVICE_CAMERA: return "摄像头";                   // 0x03
        case RULE_DEVICE_BD:     return "北斗";                     // 0x04
        case RULE_DEVICE_BK1:    return "备用一";                   // 0x05
        case RULE_DEVICE_BK2:    return "备用二";                   // 0x06
        case RULE_DEVICE_BK3:    return "备用三";                   // 0x07

        case RULE_DEVICE_PC_RADAR_SONAR_CAMERA:                     // 0x0A
            return "工控机+雷达+声纳+摄像头";
        case RULE_DEVICE_PC_RADAR_CAMERA:                           // 0x0B
            return "工控机+雷达+摄像头";
        case RULE_DEVICE_PC_RADAR:                                  // 0x0C
            return "工控机+雷达";
        case RULE_DEVICE_PC_SONAR:                                  // 0x0D
            return "工控机+声纳";

        default:                                                    // 其它码
            return "无效";
    }
}

/*******************************************************************************
 * 函数名：selftest_report_rules
 * 功  能：打印当前载入的有效规则
 * 参  数：无
 * 返回值：无
 * 说  明：无效规则（0xFF）不打印，避免刷屏
 ******************************************************************************/
static void selftest_report_rules(void)
{
    uint8_t valid_count = 0U;

    /*==============================
     *  #1. 逐条检查并打印有效规则
     *==============================*/
    for (uint8_t i = 0U; i < RULE_COUNT; i++)
    {
        if (s_rules[i].target_device == RULE_DEVICE_INVALID)        // 无效规则跳过
        {
            continue;
        }

        valid_count++;                                              // 有效计数

        LOG_INFO("规则%02u: %02u:%02u:%02u 起 %us -> %s",
                 (unsigned)(i + 1U),
                 s_rules[i].start_h, s_rules[i].start_m, s_rules[i].start_s,
                 (unsigned)s_rules[i].work_sec,
                 selftest_device_name(s_rules[i].target_device));
    }

    /*==============================
     *  #2. 打印有效规则总数
     *==============================*/
    LOG_INFO("规则表载入完成：有效 %u 条 / 共 %u 条",
             (unsigned)valid_count, (unsigned)RULE_COUNT);
}

/* Exported functions --------------------------------------------------------*/

/*******************************************************************************
 * 函数名：TaskSelfTest
 * 功  能：自检任务主体：检查 EEPROM 读写并载入规则表
 * 参  数：argument —— 创建任务时传入的参数，本任务不使用
 * 返回值：无（永不返回）
 * 说  明：开机做一次写测试，运行期只做只读巡检，避免消耗 EEPROM 写寿命
 ******************************************************************************/
void TaskSelfTest(void *argument)
{
    /*==============================
     *  #1. 开机一次性完整自检（含 EEPROM 写测试）
     *==============================*/
    if (eeprom_self_test())
    {
        LOG_INFO("EEPROM 读写自检：通过");
    }
    else
    {
        LOG_ERROR("EEPROM 读写自检：失败（请检查 SDA/SCL 接线与上拉电阻）");
    }

    if (rule_store_load(s_rules, RULE_COUNT))
    {
        selftest_report_rules();                            // 开机只打印一次规则表
    }
    else
    {
        LOG_ERROR("规则表载入失败（EEPROM 读取异常）");
    }

    /*==============================
     *  #2. 任务主循环（永不退出）
     *==============================*/
    for (;;)
    {
        /*==============================
         *  #3. 只读巡检：能读出规则表即视为正常（运行期不写 EEPROM，避免磨损）
         *==============================*/
        if (rule_store_load(s_rules, RULE_COUNT) == false)
        {
            LOG_ERROR("EEPROM 只读巡检失败（读取异常）");
        }

        /*==============================
         *  #4. 等待下一个巡检周期
         *==============================*/
        vTaskDelay(pdMS_TO_TICKS(SELFTEST_REPEAT_MS));
    }
}

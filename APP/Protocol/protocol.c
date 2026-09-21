/*******************************************************************************
 * 文件   ：protocol.c
 * 功能   ：服务器通信协议实现（帧解析 + 命令处理 + 打包应答）
 * 说明   ：帧格式与旧版本完全一致，命令处理采用表驱动；
 *          CRC16 使用 Modbus 多项式，按大端存放（与旧版本一致）
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "protocol.h"
#include "uart485.h"
#include "lora_softuart.h"
#include "board.h"
#include "rule_store.h"
#include "time_service.h"
#include "bd_time.h"
#include "monitor_service.h"
#include "power.h"
#include "log.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Private define ------------------------------------------------------------*/

/* 状态上报包中的传感器（设备开关）状态位（沿用旧版本定义，0x20 新增给备用三） */
#define PROTO_SENSOR_IPC       0x80U    // 工控机
#define PROTO_SENSOR_SONAR     0x40U    // 声纳
#define PROTO_SENSOR_BK3       0x20U    // 备用三（新增位）
#define PROTO_SENSOR_RADAR     0x10U    // 雷达
#define PROTO_SENSOR_CAMERA    0x08U    // 摄像头
#define PROTO_SENSOR_BD        0x04U    // 北斗
#define PROTO_SENSOR_BK1       0x02U    // 备用一
#define PROTO_SENSOR_BK2       0x01U    // 备用二

/* 上报包中的错误码位（沿用旧版本定义） */
#define PROTO_ERR_TEMP_HIGH    0x04U    // 温度过高

/* 重启前等待应答发出的时间（毫秒） */
#define PROTO_RESTART_DELAY_MS 200U

/* Private types -------------------------------------------------------------*/

/*******************************************************************************
 * 名称  ：proto_star_t
 * 功能  ：状态上报包结构（30 字节，与旧版本一致）
 ******************************************************************************/
typedef struct __attribute__((packed))
{
    uint8_t  start[5];      // "$STAR"
    uint8_t  device;        // 船只编号
    uint16_t year;          // 年
    uint8_t  month;         // 月
    uint8_t  day;           // 日
    uint8_t  hour;          // 时
    uint8_t  minute;        // 分
    uint8_t  second;        // 秒
    uint16_t voltage;       // 电池电压（放大 10 倍）
    uint16_t ntc1;          // 环境温度（放大 10 倍）
    uint16_t ntc2;          // 工控机鳍片温度（放大 10 倍）
    uint16_t pcb_ntc;       // 电源板温度（放大 10 倍）
    uint8_t  sensor_state;  // 设备开关状态位
    uint8_t  error_code;    // 错误码
    uint16_t crc16;         // CRC16 校验码（大端）
    uint8_t  end[5];        // "$OVER"
} proto_star_t;

/*******************************************************************************
 * 名称  ：proto_str_t
 * 功能  ：规则包结构（222 字节，与旧版本一致）
 ******************************************************************************/
typedef struct __attribute__((packed))
{
    uint8_t  start[4];                  // "$STR"
    uint8_t  device;                    // 船只编号
    uint8_t  rule[PROTO_RULE_COUNT][PROTO_RULE_BYTES];   // 35 条 × 6 字节
    uint16_t crc16;                     // CRC16 校验码（大端）
    uint8_t  end[5];                    // "$OVER"
} proto_str_t;

/*******************************************************************************
 * 名称  ：proto_entry_t
 * 功能  ：协议表的一项
 ******************************************************************************/
typedef struct
{
    const char *header;                             // 协议头
    void (*handler)(const uint8_t *buf, uint16_t len);   // 处理函数
    uint8_t need_crc;                               // 是否需要校验
    uint8_t has_boat;                               // 是否带船只编号（船号紧跟协议头之后）
} proto_entry_t;

/* Private variables ---------------------------------------------------------*/
static proto_star_t s_star;                             // 上报包缓冲
static proto_str_t  s_str;                              // 规则包缓冲
static uint8_t      s_last_report_minute = 0xFFU;       // 上次上报的分钟（防重复）
static TickType_t   s_last_period_tick = 0U;            // 工控机开启期间的周期上报计时
static bool         s_restart_pending   = false;        // 是否有重启请求
static proto_channel_t s_reply_channel = PROTO_CH_485;  // 当前应答走哪条链路（收帧时记录）

/* Private functions prototypes ----------------------------------------------*/
static uint16_t proto_crc16(const uint8_t *ptr, uint16_t len);                  // 计算 CRC16
static uint8_t  proto_sensor_state(void);                                       // 读取设备开关状态位
static void     proto_reply(const uint8_t *data, uint16_t len);                 // 从“收到帧的那条链路”发回应答
static void     proto_pack_star(void);                                          // 打包状态上报包
static void     proto_pack_str(void);                                           // 打包规则包
static void     proto_cmd_bd_time(const uint8_t *buf, uint16_t len);            // 北斗校时
static void     proto_cmd_set_rules(const uint8_t *buf, uint16_t len);          // 下发策略
static void     proto_cmd_read_state(const uint8_t *buf, uint16_t len);         // 读设备状态
static void     proto_cmd_get_rules(const uint8_t *buf, uint16_t len);          // 读控制策略
static void     proto_cmd_restart(const uint8_t *buf, uint16_t len);            // 重启系统

/* 协议表：协议头 -> 处理函数（沿用旧版本的表驱动设计）
   has_boat=1 表示该命令帧在协议头之后紧跟 1 字节船只编号，需按船号过滤 */
static const proto_entry_t s_proto_table[] =
{
    { "$BDRMC",  proto_cmd_bd_time,    0U, 0U },    // 北斗校时数据（无校验、无船号）
    { "$STR",    proto_cmd_set_rules,  1U, 1U },    // 下发控制策略（CRC16 + 船号）
    { "$READ",   proto_cmd_read_state, 1U, 1U },    // 读取设备状态（CRC16 + 船号）
    { "$GETSTR", proto_cmd_get_rules,  1U, 1U },    // 读取控制策略（CRC16 + 船号）
    { "$REST",   proto_cmd_restart,    0U, 1U },    // 重启系统（无校验 + 船号）
    { NULL,      NULL,                 0U, 0U }     // 结束标记
};

/* Private functions ---------------------------------------------------------*/

/*******************************************************************************
 * 函数名：proto_crc16
 * 功  能：计算 Modbus CRC16 校验码
 * 参  数：ptr —— 数据指针；len —— 数据长度
 * 返回值：CRC16 校验码
 * 说  明：与旧版本算法一致，调用方按大端放入帧中
 ******************************************************************************/
static uint16_t proto_crc16(const uint8_t *ptr, uint16_t len)
{
    uint16_t crc = 0xFFFFU;

    /*==============================
     *  #1. 逐字节按位异或计算
     *==============================*/
    for (uint16_t i = 0U; i < len; i++)
    {
        crc = (uint16_t)((crc & 0xFF00U) | ((crc & 0x00FFU) ^ ptr[i]));

        for (uint8_t j = 0U; j < 8U; j++)
        {
            if ((crc & 0x0001U) != 0U)
            {
                crc = (uint16_t)(crc >> 1);
                crc ^= 0xA001U;                             // Modbus 多项式
            }
            else
            {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }

    return crc;                                             // 返回计算结果
}

/*******************************************************************************
 * 函数名：proto_sensor_state
 * 功  能：读取 8 台设备的开关状态，组装成状态位
 * 参  数：无
 * 返回值：设备状态字节
 * 说  明：直接回读输出引脚电平，反映真实输出状态
 ******************************************************************************/
static uint8_t proto_sensor_state(void)
{
    uint8_t bits = 0U;

    /*==============================
     *  #1. 逐台设备回读并置位
     *==============================*/
    if (board_power_get(DEV_IPC))    { bits |= PROTO_SENSOR_IPC;    }   // 工控机
    if (board_power_get(DEV_SONAR))  { bits |= PROTO_SENSOR_SONAR;  }   // 声纳
    if (board_power_get(DEV_BK3))    { bits |= PROTO_SENSOR_BK3;    }   // 备用三
    if (board_power_get(DEV_RADAR))  { bits |= PROTO_SENSOR_RADAR;  }   // 雷达
    if (board_power_get(DEV_CAMERA)) { bits |= PROTO_SENSOR_CAMERA; }   // 摄像头
    if (board_power_get(DEV_BD))     { bits |= PROTO_SENSOR_BD;     }   // 北斗
    if (board_power_get(DEV_BK1))    { bits |= PROTO_SENSOR_BK1;    }   // 备用一
    if (board_power_get(DEV_BK2))    { bits |= PROTO_SENSOR_BK2;    }   // 备用二

    return bits;                                            // 返回状态字节
}

/*******************************************************************************
 * 函数名：proto_reply
 * 功  能：从“收到本帧的那条链路”把数据发回去
 * 参  数：data —— 待发数据；len —— 字节数
 * 返回值：无
 * 说  明：485 链路与 LoRa 链路跑同一套协议，应答必须原路返回，
 *          否则现场用 LoRa 调试时收不到应答
 ******************************************************************************/
static void proto_reply(const uint8_t *data, uint16_t len)
{
    /*==============================
     *  #1. 按收帧时记录的链路选择发送通道
     *==============================*/
    if (s_reply_channel == PROTO_CH_LORA)
    {
        lora_send(data, len);                               // 无线调试链路
    }
    else
    {
        (void)uart485_send(&huart2, data, len);             // 北斗/服务器 485 链路
    }
}

/*******************************************************************************
 * 函数名：proto_pack_star
 * 功  能：组装状态上报包
 * 参  数：无
 * 返回值：无
 * 说  明：数据取自时间服务与监控快照，不额外触发采集
 ******************************************************************************/
static void proto_pack_star(void)
{
    app_time_t         t;
    monitor_snapshot_t snap;

    /*==============================
     *  #1. 固定字段
     *==============================*/
    memcpy(s_star.start, "$STAR", 5U);                      // 开始符号
    s_star.device = PROTO_BOAT_NUMBER;                      // 船只编号

    /*==============================
     *  #2. 时间字段
     *==============================*/
    memset(&t, 0, sizeof(t));

    if (time_service_get(&t))
    {
        s_star.year   = t.year;                             // 年
        s_star.month  = t.month;                            // 月
        s_star.day    = t.day;                              // 日
        s_star.hour   = t.hour;                             // 时
        s_star.minute = t.minute;                           // 分
        s_star.second = t.second;                           // 秒
    }

    /*==============================
     *  #3. 电压与温度（放大 10 倍）
     *==============================*/
    s_star.voltage  = 0U;
    s_star.ntc1     = 0U;
    s_star.ntc2     = 0U;
    s_star.pcb_ntc  = 0U;
    s_star.error_code = 0U;

    if (monitor_service_get_snapshot(&snap))
    {
        s_star.voltage = (uint16_t)(snap.data.voltage * 10.0f);         // 电池电压
        s_star.ntc1    = (uint16_t)(snap.data.temp_env * 10.0f);        // 环境温度
        s_star.ntc2    = (uint16_t)(snap.data.temp_ipc * 10.0f);        // 工控机温度
        s_star.pcb_ntc = (uint16_t)(snap.data.temp_pcb * 10.0f);        // 电源板温度

        /*==============================
         *  #4. 告警映射到协议错误码
         *==============================*/
        if ((snap.alarm_flags & (MONITOR_ALARM_IPC_OVERTEMP |
                                 MONITOR_ALARM_PCB_OVERTEMP)) != 0U)
        {
            s_star.error_code |= PROTO_ERR_TEMP_HIGH;       // 温度过高
        }
    }

    /*==============================
     *  #5. 设备开关状态、校验与结束符
     *==============================*/
    s_star.sensor_state = proto_sensor_state();             // 回读设备状态
    s_star.crc16 = proto_crc16((const uint8_t *)&s_star, PROTO_STAR_LEN - 7U);  // 校验码
    memcpy(s_star.end, "$OVER", 5U);                        // 结束符号
}

/*******************************************************************************
 * 函数名：proto_pack_str
 * 功  能：组装规则包（从 EEPROM 读取当前策略）
 * 参  数：无
 * 返回值：无
 * 说  明：只发送每条规则的前 6 字节（结束时间可由开始时间与工作时长算出）
 ******************************************************************************/
static void proto_pack_str(void)
{
    rule_item_t rules[PROTO_RULE_COUNT];

    /*==============================
     *  #1. 从 EEPROM 读取规则表
     *==============================*/
    memset(&s_str, 0, sizeof(s_str));

    if (rule_store_load(rules, PROTO_RULE_COUNT) == false)
    {
        return;                                             // 读取失败则保持全 0
    }

    /*==============================
     *  #2. 固定字段
     *==============================*/
    memcpy(s_str.start, "$STR", 4U);                        // 开始符号
    s_str.device = PROTO_BOAT_NUMBER;                       // 船只编号

    /*==============================
     *  #3. 逐条规则按 6 字节打包
     *==============================*/
    for (uint8_t i = 0U; i < PROTO_RULE_COUNT; i++)
    {
        s_str.rule[i][0] = rules[i].start_h;                            // 开始时
        s_str.rule[i][1] = rules[i].start_m;                            // 开始分
        s_str.rule[i][2] = rules[i].start_s;                            // 开始秒
        s_str.rule[i][3] = (uint8_t)(rules[i].work_sec & 0x00FFU);      // 工作时长低字节
        s_str.rule[i][4] = (uint8_t)((rules[i].work_sec >> 8) & 0x00FFU); // 工作时长高字节
        s_str.rule[i][5] = rules[i].target_device;                      // 目标设备
    }

    /*==============================
     *  #4. 校验与结束符
     *==============================*/
    s_str.crc16 = proto_crc16((const uint8_t *)&s_str, PROTO_STR_LEN - 7U);   // 校验码
    memcpy(s_str.end, "$OVER", 5U);                         // 结束符号
}

/*******************************************************************************
 * 函数名：proto_cmd_bd_time
 * 功  能：处理北斗校时数据（$BDRMC,yy-mm-dd,hh:mm:ss，UTC 时间）
 * 参  数：buf —— 帧数据；len —— 帧长度
 * 返回值：无
 * 说  明：内部转换成北京时间（UTC+8）后写入 RTC
 ******************************************************************************/
static void proto_cmd_bd_time(const uint8_t *buf, uint16_t len)
{
    int yy, mo, dd, hh, mi, ss;

    /*==============================
     *  #1. 解析时间字段
     *==============================*/
    if (sscanf((const char *)buf, "$BDRMC,%d-%d-%d,%d:%d:%d",
               &yy, &mo, &dd, &hh, &mi, &ss) != 6)
    {
        LOG_WARNING("北斗校时数据解析失败");
        return;                                             // 解析失败
    }

    /*==============================
     *  #2. 合理性检查
     *==============================*/
    if ((yy < 0) || (yy > 99) || (mo < 1) || (mo > 12) || (dd < 1) || (dd > 31) ||
        (hh < 0) || (hh > 23) || (mi < 0) || (mi > 59) || (ss < 0) || (ss > 59))
    {
        LOG_WARNING("北斗校时数据越界");
        return;                                             // 数据非法
    }

    /*==============================
     *  #3. 转换为北京时间（UTC+8）
     *==============================*/
    hh += 8;                                                // 时区偏移

    if (hh >= 24)                                           // 跨天
    {
        hh -= 24;
        dd += 1;                                            // 日期顺延（月末细节由校时源保证）
    }

    /*==============================
     *  #4. 写入时间服务
     *==============================*/
    {
        app_time_t t;

        t.year   = (uint16_t)(2000 + yy);                   // 年
        t.month  = (uint8_t)mo;                             // 月
        t.day    = (uint8_t)dd;                             // 日
        t.hour   = (uint8_t)hh;                             // 时
        t.minute = (uint8_t)mi;                             // 分
        t.second = (uint8_t)ss;                             // 秒

        time_service_set(&t);                               // 校时成功后会标记时间可信
    }

    bd_time_on_synced();                                    // 通知北斗校时模块：本轮查询已完成，当天不再重复查询

    LOG_INFO("北斗校时完成：20%02d-%02d-%02d %02d:%02d:%02d", yy, mo, dd, hh, mi, ss);
}

/*******************************************************************************
 * 函数名：proto_cmd_set_rules
 * 功  能：处理下发控制策略（$STR，含 35 条规则）
 * 参  数：buf —— 帧数据；len —— 帧长度
 * 返回值：无
 * 说  明：解析后写入 EEPROM 并让决策层重新载入，最后应答结果
 ******************************************************************************/
static void proto_cmd_set_rules(const uint8_t *buf, uint16_t len)
{
    rule_item_t rules[PROTO_RULE_COUNT];
    bool ok;

    /*==============================
     *  #1. 长度检查
     *==============================*/
    if (len != PROTO_STR_LEN)
    {
        LOG_WARNING("下发策略包长度错误：%u", (unsigned)len);   // 不回 $ACK（任何情况都不回应答）
        return;
    }

    /*==============================
     *  #2. 解析 35 条规则
     *==============================*/
    for (uint8_t i = 0U; i < PROTO_RULE_COUNT; i++)
    {
        const uint8_t *p = &buf[4U + 1U + (uint16_t)i * PROTO_RULE_BYTES];   // 跳过头部与设备号

        rules[i].start_h       = p[0];                                  // 开始时
        rules[i].start_m       = p[1];                                  // 开始分
        rules[i].start_s       = p[2];                                  // 开始秒
        rules[i].work_sec      = (uint16_t)p[3] | ((uint16_t)p[4] << 8); // 工作时长（小端）
        rules[i].target_device = p[5];                                  // 目标设备
    }

    /*==============================
     *  #3. 写入 EEPROM 并让决策层重新载入
     *==============================*/
    ok = rule_store_save(rules, PROTO_RULE_COUNT);          // 持久化

    if (ok)
    {
        ok = power_reload_rules();                          // 让新策略立即生效
    }

    /*==============================
     *  #4. 结果只记日志，不回 $ACK（任何情况都不回应答）
     *==============================*/
    LOG_INFO("下发策略处理%s", ok ? "成功" : "失败");
}

/*******************************************************************************
 * 函数名：proto_cmd_read_state
 * 功  能：处理读设备状态请求（$READ）
 * 参  数：buf —— 帧数据；len —— 帧长度
 * 返回值：无
 * 说  明：只回状态包（$STAR），任何情况都不回 $ACK
 ******************************************************************************/
static void proto_cmd_read_state(const uint8_t *buf, uint16_t len)
{
    /*==============================
     *  #1. 组装并回状态包（不回 $ACK，避免占用发送机会）
     *==============================*/
    proto_pack_star();                                      // 组装状态包
    proto_reply((const uint8_t *)&s_star, PROTO_STAR_LEN);  // 从收到请求的那条链路回

    LOG_INFO("$READ 应答：已回 $ACK 与 $STAR（%u 字节）", (unsigned)PROTO_STAR_LEN);   // 现场排查用，仅日志
}

/*******************************************************************************
 * 函数名：proto_cmd_get_rules
 * 功  能：处理读取控制策略请求（$GETSTR）
 * 参  数：buf —— 帧数据；len —— 帧长度
 * 返回值：无
 * 说  明：从 EEPROM 读取当前策略并整包返回
 ******************************************************************************/
static void proto_cmd_get_rules(const uint8_t *buf, uint16_t len)
{
    /*==============================
     *  #1. 组装规则包
     *==============================*/
    proto_pack_str();                                       // 从 EEPROM 读取并打包

    /*==============================
     *  #2. 发送整个规则包
     *==============================*/
    proto_reply((const uint8_t *)&s_str, PROTO_STR_LEN);    // 从收到请求的那条链路回

    LOG_INFO("已回复控制策略包");
}

/*******************************************************************************
 * 函数名：proto_cmd_restart
 * 功  能：处理重启系统请求（$REST）
 * 参  数：buf —— 帧数据；len —— 帧长度
 * 返回值：无
 * 说  明：不回 $ACK（任何情况都不回应答），只置重启请求；
 *         真正的复位由通信任务执行
 ******************************************************************************/
static void proto_cmd_restart(const uint8_t *buf, uint16_t len)
{
    /*==============================
     *  #1. 置重启请求
     *==============================*/
    s_restart_pending = true;                               // 由通信任务执行复位

    LOG_WARNING("收到重启命令，准备复位系统");
}

/* Exported functions --------------------------------------------------------*/

/*******************************************************************************
 * 函数名：protocol_init
 * 功  能：初始化协议层
 * 参  数：无
 * 返回值：无
 * 说  明：复位上报节拍与重启请求状态
 ******************************************************************************/
void protocol_init(void)
{
    /*==============================
     *  #1. 复位内部状态
     *==============================*/
    s_last_report_minute = 0xFFU;                           // 未上报过
    s_restart_pending    = false;                           // 无重启请求
}

/*******************************************************************************
 * 函数名：protocol_handle_frame
 * 功  能：解析并处理一帧数据
 * 参  数：buf —— 帧数据；len —— 帧长度
 * 返回值：无
 * 说  明：表驱动匹配协议头；需要校验的协议先校验 CRC 再执行
 ******************************************************************************/
void protocol_handle_frame(const uint8_t *buf, uint16_t len, proto_channel_t ch)
{
    /*==============================
     *  #1. 参数检查，并记录本帧来自哪条链路（应答要原路返回）
     *==============================*/
    if ((buf == NULL) || (len < 7U))
    {
        return;                                             // 太短不可能是一帧
    }

    s_reply_channel = ch;

    LOG_DEBUG("收到帧：%u 字节，头部=%.8s", (unsigned)len, (const char *)buf);  // 改 DEBUG：避免上位机轮询时刷屏

    /*==============================
     *  #2. 遍历协议表匹配协议头
     *==============================*/
    for (uint8_t i = 0U; s_proto_table[i].header != NULL; i++)
    {
        uint16_t hdr_len = (uint16_t)strlen(s_proto_table[i].header);   // 协议头长度

        if (strncmp((const char *)buf, s_proto_table[i].header, hdr_len) != 0)
        {
            continue;                                       // 协议头不匹配
        }

        /*==============================
         *  #3. 船号过滤：不是发给本船的帧直接忽略
         *==============================*/
        if ((s_proto_table[i].has_boat != 0U) && (len > hdr_len))
        {
            uint8_t boat = buf[hdr_len];                    // 船号紧跟协议头之后

            if (boat != PROTO_BOAT_NUMBER)
            {
                LOG_INFO("船号不匹配（收到 %u，本船 %u），已忽略",
                         (unsigned)boat, (unsigned)PROTO_BOAT_NUMBER);
                return;
            }
        }

        /*==============================
         *  #4. 需要校验的先验 CRC
         *==============================*/
        if (s_proto_table[i].need_crc == 1U)
        {
            uint16_t rx_crc   = (uint16_t)((buf[len - 7U] << 8) | buf[len - 6U]);   // 帧中校验码（大端）
            uint16_t calc_crc = proto_crc16(buf, (uint16_t)(len - 7U));             // 计算校验码

            if (rx_crc != calc_crc)
            {
                LOG_WARNING("校验失败：收到 0x%04X，计算 0x%04X",
                            (unsigned)rx_crc, (unsigned)calc_crc);
                return;                                 // 不回 $ACK（任何情况都不回应答）
            }
        }

        /*==============================
         *  #5. 执行命令处理函数
         *==============================*/
        s_proto_table[i].handler(buf, len);
        return;
    }

    /*==============================
     *  #6. 未匹配到任何协议
     *==============================*/
    LOG_WARNING("收到未知协议，已丢弃");
}

/*******************************************************************************
 * 函数名：protocol_report_state
 * 功  能：立即上报一次设备状态包
 * 参  数：无
 * 返回值：无
 ******************************************************************************/
void protocol_report_state(void)
{
    /*==============================
     *  #1. 打包并发送
     *==============================*/
    proto_pack_star();                                      // 组装状态包
    (void)uart485_send(&huart2, (const uint8_t *)&s_star, PROTO_STAR_LEN);

    LOG_DEBUG("已上报设备状态包");
}

/*******************************************************************************
 * 函数名：protocol_report_check
 * 功  能：定时上报节拍检查：整点窗口上报 + 工控机开启期间每 2 分钟上报
 * 参  数：无
 * 返回值：无
 * 说  明：由通信任务周期调用。授时无效时一律不上报——第一包必须等校时
 *          成功后再发，否则上报出去的日期时间是没意义的
 ******************************************************************************/
void protocol_report_check(void)
{
    app_time_t t;

    /*==============================
     *  #1. 授时无效则不上报（第一包要等校时成功）
     *==============================*/
    if (time_service_is_reliable() == false)
    {
        return;
    }

    if (time_service_get(&t) == false)
    {
        return;                                             // 读时间失败
    }

    /*==============================
     *  #2. 工控机开启期间：每 2 分钟上报一次
     *==============================*/
    if (board_power_get(DEV_IPC))
    {
        if ((xTaskGetTickCount() - s_last_period_tick) >= pdMS_TO_TICKS(PROTO_REPORT_PC_PERIOD_MS))
        {
            s_last_period_tick = xTaskGetTickCount();       // 重新计时
            protocol_report_state();                        // 周期上报一次
        }
    }
    else
    {
        s_last_period_tick = xTaskGetTickCount();           // 工控机关闭，重新开启后从头计时
    }

    /*==============================
     *  #3. 整点窗口：每小时第 53 分、前 10 秒内上报一次
     *==============================*/
    if ((t.minute == PROTO_REPORT_MINUTE) &&
        (t.second <= PROTO_REPORT_WINDOW_SEC))
    {
        if (s_last_report_minute != t.minute)               // 本分钟还没报过
        {
            s_last_report_minute = t.minute;                // 记录已上报
            protocol_report_state();                        // 上报一次
        }
    }
    else if (t.minute != PROTO_REPORT_MINUTE)
    {
        s_last_report_minute = 0xFFU;                       // 离开窗口，允许下次再报
    }
    else
    {
        /* 仍在窗口内且已报过，无需处理 */
    }
}

/*******************************************************************************
 * 函数名：protocol_is_restart_pending
 * 功  能：查询是否有重启请求（供通信任务决定是否复位）
 * 参  数：无
 * 返回值：true 表示需要复位
 ******************************************************************************/
bool protocol_is_restart_pending(void)
{
    /*==============================
     *  #1. 直接返回标志
     *==============================*/
    return s_restart_pending;                               // 由 $REST 命令置位
}

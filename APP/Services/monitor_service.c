/*******************************************************************************
 * 文件   ：monitor_service.c
 * 功能   ：系统监控服务实现
 * 说明   ：温度保护使用"回差 + 时间确认"；电压只做有效性检查与上电门槛，
 *          不产生保护动作。快照通过互斥量供多个任务安全读取。
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "monitor_service.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <string.h>

/* Private define ------------------------------------------------------------*/

/* 采样周期：用于把"确认时间"换算成"确认次数" */
#define MONITOR_SAMPLE_PERIOD_MS        1000U

/* 工控机鳍片温度保护参数 */
#define MONITOR_IPC_TEMP_SET            70.0f
#define MONITOR_IPC_TEMP_CLEAR          50.0f

/* PCB 温度保护参数 */
#define MONITOR_PCB_TEMP_SET            85.0f
#define MONITOR_PCB_TEMP_CLEAR          70.0f

/* 温度确认时间（需求按时间定义，代码换算成次数） */
#define MONITOR_TEMP_SET_CONFIRM_MS     3000U
#define MONITOR_TEMP_CLEAR_CONFIRM_MS   5000U
#define MONITOR_TEMP_SET_COUNT   (MONITOR_TEMP_SET_CONFIRM_MS / MONITOR_SAMPLE_PERIOD_MS)
#define MONITOR_TEMP_CLEAR_COUNT (MONITOR_TEMP_CLEAR_CONFIRM_MS / MONITOR_SAMPLE_PERIOD_MS)

/* 上电门槛：电压必须落在此范围内才允许给设备上电（待 DC-DC 规格确认后定稿） */
#define MONITOR_GATE_VOLT_MIN           20.0f
#define MONITOR_GATE_VOLT_MAX           31.0f

/* 数据有效性范围（只判断传感器是否可信，不产生动作） */
#define MONITOR_VOLTAGE_VALID_MIN       0.1f
#define MONITOR_VOLTAGE_VALID_MAX       40.0f
#define MONITOR_TEMP_VALID_MIN         (-55.0f)
#define MONITOR_TEMP_VALID_MAX          150.0f

/* Private types -------------------------------------------------------------*/

/*******************************************************************************
 * 名称  ：monitor_filter_t
 * 功能  ：一路"数值过高"告警的回差与连续确认状态
 ******************************************************************************/
typedef struct
{
    bool    active;         // 告警当前是否成立
    uint8_t set_count;      // 连续异常计数
    uint8_t clear_count;    // 连续恢复计数
} monitor_filter_t;

/* Private variables ---------------------------------------------------------*/
static SemaphoreHandle_t  s_snapshot_mutex = NULL;      // 快照互斥量
static monitor_snapshot_t s_snapshot;                   // 最新监控快照
static monitor_filter_t   s_ipc_temp_filter;            // 工控机过温过滤器
static monitor_filter_t   s_pcb_temp_filter;            // PCB 过温过滤器
static int8_t             s_gate_decided = -1;          // 上电门槛：-1 未判定，0 不允许，1 允许

/* Private functions prototypes ----------------------------------------------*/
static bool monitor_value_valid(float value, float min_value, float max_value);   // 判断数值是否在合理范围内
static void monitor_update_temp_filter(monitor_filter_t *filter, float value, bool valid,
                                       float set_threshold, float clear_threshold); // 更新温度告警过滤器

/* Private functions ---------------------------------------------------------*/

/*******************************************************************************
 * 函数名：monitor_value_valid
 * 功  能：判断采集值是否落在合理范围内
 * 参  数：value     —— 待判断的数据
 *         min_value —— 合理范围下限
 *         max_value —— 合理范围上限
 * 返回值：true 表示数据可信，false 表示不可信
 * 说  明：只判断传感器是否可信，与告警阈值无关
 ******************************************************************************/
static bool monitor_value_valid(float value, float min_value, float max_value)
{
    /*==============================
     *  #1. 数值必须落在上下限之间
     *==============================*/
    return ((value >= min_value) && (value <= max_value));      // 落在范围内为有效
}

/*******************************************************************************
 * 函数名：monitor_update_temp_filter
 * 功  能：更新一路温度告警的回差与连续确认状态
 * 参  数：filter          —— 告警过滤状态
 *         value           —— 当前温度
 *         valid           —— 当前温度是否可信
 *         set_threshold   —— 触发阈值
 *         clear_threshold —— 恢复阈值
 * 返回值：无
 * 说  明：未告警时连续超阈值 3 次置位；已告警时连续低于恢复阈值 5 次清除；
 *         两阈值之间保持原状态（这就是回差）
 ******************************************************************************/
static void monitor_update_temp_filter(monitor_filter_t *filter, float value, bool valid,
                                       float set_threshold, float clear_threshold)
{
    /*==============================
     *  #1. 参数检查与无效数据处理
     *==============================*/
    if (filter == NULL)
    {
        return;                                                 // 空指针保护
    }

    if (valid == false)
    {
        filter->set_count   = 0;                                // 数据不可信，打断连续确认
        filter->clear_count = 0;
        return;
    }

    /*==============================
     *  #2. 未告警：检查是否连续超过触发阈值
     *==============================*/
    if (filter->active == false)
    {
        filter->clear_count = 0;                                // 未告警时不统计恢复

        if (value > set_threshold)
        {
            if (filter->set_count < MONITOR_TEMP_SET_COUNT)
            {
                filter->set_count++;                            // 累加异常次数
            }

            if (filter->set_count >= MONITOR_TEMP_SET_COUNT)
            {
                filter->active    = true;                       // 达到确认时间，置位
                filter->set_count = 0;
            }
        }
        else
        {
            filter->set_count = 0;                              // 中间断开则重新计数
        }

        return;
    }

    /*==============================
     *  #3. 已告警：检查是否连续低于恢复阈值
     *==============================*/
    filter->set_count = 0;                                      // 已告警时不统计置位

    if (value < clear_threshold)
    {
        if (filter->clear_count < MONITOR_TEMP_CLEAR_COUNT)
        {
            filter->clear_count++;                              // 累加恢复次数
        }

        if (filter->clear_count >= MONITOR_TEMP_CLEAR_COUNT)
        {
            filter->active      = false;                        // 达到确认时间，清除
            filter->clear_count = 0;
        }
    }
    else
    {
        filter->clear_count = 0;                                // 中间断开则重新计数
    }
}

/* Exported functions --------------------------------------------------------*/

/*******************************************************************************
 * 函数名：monitor_service_init
 * 功  能：初始化监控服务
 * 参  数：无
 * 返回值：true 表示成功，false 表示互斥量创建失败
 * 说  明：必须在启动调度器之前调用
 ******************************************************************************/
bool monitor_service_init(void)
{
    /*==============================
     *  #1. 清空快照与过滤器状态
     *==============================*/
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    memset(&s_ipc_temp_filter, 0, sizeof(s_ipc_temp_filter));
    memset(&s_pcb_temp_filter, 0, sizeof(s_pcb_temp_filter));
    s_gate_decided = -1;                                        // 上电门槛尚未判定

    /*==============================
     *  #2. 创建快照互斥量
     *==============================*/
    s_snapshot_mutex = xSemaphoreCreateMutex();

    return (s_snapshot_mutex != NULL);                          // 返回创建结果
}

/*******************************************************************************
 * 函数名：monitor_service_update
 * 功  能：根据一次采集结果更新有效性、温度告警与监控快照
 * 参  数：data —— 最近一次采集的电压与三路温度
 * 返回值：无
 * 说  明：只能由 TaskMonitor 调用，保证快照只有一个写者
 ******************************************************************************/
void monitor_service_update(const sense_data_t *data)
{
    monitor_snapshot_t next;
    bool voltage_valid;
    bool temp_env_valid;
    bool temp_ipc_valid;
    bool temp_pcb_valid;

    /*==============================
     *  #1. 输入参数检查
     *==============================*/
    if (data == NULL)
    {
        return;
    }

    /*==============================
     *  #2. 逐路判断数据是否可信
     *==============================*/
    voltage_valid  = monitor_value_valid(data->voltage,  MONITOR_VOLTAGE_VALID_MIN, MONITOR_VOLTAGE_VALID_MAX);
    temp_env_valid = monitor_value_valid(data->temp_env, MONITOR_TEMP_VALID_MIN,    MONITOR_TEMP_VALID_MAX);
    temp_ipc_valid = monitor_value_valid(data->temp_ipc, MONITOR_TEMP_VALID_MIN,    MONITOR_TEMP_VALID_MAX);
    temp_pcb_valid = monitor_value_valid(data->temp_pcb, MONITOR_TEMP_VALID_MIN,    MONITOR_TEMP_VALID_MAX);

    /*==============================
     *  #3. 更新两路温度告警的确认状态
     *==============================*/
    monitor_update_temp_filter(&s_ipc_temp_filter, data->temp_ipc, temp_ipc_valid,
                               MONITOR_IPC_TEMP_SET, MONITOR_IPC_TEMP_CLEAR);

    monitor_update_temp_filter(&s_pcb_temp_filter, data->temp_pcb, temp_pcb_valid,
                               MONITOR_PCB_TEMP_SET, MONITOR_PCB_TEMP_CLEAR);

    /*======================================================
     *  #4. 上电门槛检查（只在第一次采集时判定一次，结果锁定）
     *======================================================*/
    if (s_gate_decided < 0)
    {
        bool ok = ((data->voltage >= MONITOR_GATE_VOLT_MIN) &&
                   (data->voltage <= MONITOR_GATE_VOLT_MAX));

        s_gate_decided = ok ? 1 : 0;                            // 一旦判定不再改变
    }

    /*==============================
     *  #5. 在局部变量中组装新快照
     *==============================*/
    memset(&next, 0, sizeof(next));
    next.data        = *data;                                   // 复制工程量
    next.update_tick = xTaskGetTickCount();                     // 记录更新时间

    if (voltage_valid)  { next.valid_flags |= MONITOR_VALID_VOLTAGE;  }   // 电压有效
    if (temp_env_valid) { next.valid_flags |= MONITOR_VALID_TEMP_ENV; }   // 环温有效
    if (temp_ipc_valid) { next.valid_flags |= MONITOR_VALID_TEMP_IPC; }   // 机温有效
    if (temp_pcb_valid) { next.valid_flags |= MONITOR_VALID_TEMP_PCB; }   // 板温有效

    if (s_ipc_temp_filter.active) { next.alarm_flags |= MONITOR_ALARM_IPC_OVERTEMP; }  // 工控机过温
    if (s_pcb_temp_filter.active) { next.alarm_flags |= MONITOR_ALARM_PCB_OVERTEMP; }  // PCB 过温

    next.power_on_allowed = (s_gate_decided == 1);               // 上电是否允许

    /*==============================
     *  #6. 短暂持锁替换共享快照
     *==============================*/
    if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        s_snapshot = next;                                      // 锁内只复制结构体
        xSemaphoreGive(s_snapshot_mutex);
    }
}

/*******************************************************************************
 * 函数名：monitor_service_get_snapshot
 * 功  能：线程安全地读取最新监控快照
 * 参  数：snapshot —— 接收快照的输出指针
 * 返回值：true 表示读取成功，false 表示参数错误或取锁超时
 * 说  明：锁内只做结构体复制，调用者使用自己的局部副本
 ******************************************************************************/
bool monitor_service_get_snapshot(monitor_snapshot_t *snapshot)
{
    /*==============================
     *  #1. 参数与初始化状态检查
     *==============================*/
    if ((snapshot == NULL) || (s_snapshot_mutex == NULL))
    {
        return false;
    }

    /*==============================
     *  #2. 取锁并复制快照
     *==============================*/
    if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(10)) != pdTRUE)
    {
        return false;                                           // 取锁超时
    }

    *snapshot = s_snapshot;                                     // 复制成调用者的局部副本
    xSemaphoreGive(s_snapshot_mutex);

    return true;
}

/*******************************************************************************
 * 文件   ：sense.c
 * 功能   ：模拟量采集层实现（ADC 读取 + 工程量换算）
 * 说明   ：ADC 为阻塞式读取；调用本模块的任务优先级应放低，让出 CPU 给别人
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "sense.h"
#include "board.h"
#include "adc.h"
#include <math.h>

/* Private define ------------------------------------------------------------*/
#define SENSE_AVG_TIMES     10      // 每个通道重复采样次数（取平均抑制噪声）
#define SENSE_ADC_TIMEOUT   10      // 单次转换超时（毫秒）

/* Private functions prototypes ----------------------------------------------*/
static uint16_t sense_read_channel(uint32_t channel);
static float    sense_adc_to_voltage(uint16_t adc);
static float    sense_adc_to_temp(uint16_t adc);

/* Private functions ---------------------------------------------------------*/

/*******************************************************************************
 * 函数名：sense_read_channel
 * 功  能：读取指定 ADC 通道并多次平均
 * 参  数：channel —— ADC 通道号（如 ADC_CHANNEL_14）
 * 返回值：平均后的 ADC 原始值（0~4095）
 * 说  明：阻塞式实现；采样时间为 239.5 周期以适应 NTC 高内阻
 ******************************************************************************/
static uint16_t sense_read_channel(uint32_t channel)
{
    ADC_ChannelConfTypeDef cfg = {0};
    uint32_t sum = 0;

    /*==============================
        #1. 把 ADC 指向要读的通道
     ==============================*/
    cfg.Channel      = channel;                        // 目标通道
    cfg.Rank         = ADC_REGULAR_RANK_1;             // 规则组第 1 个
    cfg.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;     // 高内阻信号需要长采样
    HAL_ADC_ConfigChannel(&hadc1, &cfg);

    /*==============================
        #2. 连续采样 N 次并累加
     ==============================*/
    for (uint8_t i = 0; i < SENSE_AVG_TIMES; i++)
    {
        HAL_ADC_Start(&hadc1);                          // 启动一次转换
        HAL_ADC_PollForConversion(&hadc1, SENSE_ADC_TIMEOUT);  // 等待完成（阻塞）
        sum += HAL_ADC_GetValue(&hadc1);                // 读结果并累加
        HAL_ADC_Stop(&hadc1);                           // 停止
    }

    /*==============================
     *   #3. 返回平均值
     ==============================*/
    return (uint16_t)(sum / SENSE_AVG_TIMES);
}

/*******************************************************************************
 * 函数名：sense_adc_to_voltage
 * 功  能：ADC 原始值换算成电池电压
 * 参  数：adc —— ADC 原始值
 * 返回值：电压（V）
 * 说  明：换算系数在 board.h 中定义，换板子需重新标定
 ******************************************************************************/
static float sense_adc_to_voltage(uint16_t adc)
{
    /*================================================
         #1. 原始值 → 引脚电压 → 乘分压系数得到真实电压
     ================================================*/
    float v_pin = ((float)adc / BOARD_ADC_FULLSCALE) * BOARD_ADC_VREF;   // 引脚上的电压
    return v_pin * BOARD_VOLT_DIV_COEFF;                                 // 真实电池电压
}

/*******************************************************************************
 * 函数名：sense_adc_to_temp
 * 功  能：ADC 原始值换算成 NTC 温度（B 值方程）
 * 参  数：adc —— ADC 原始值
 * 返回值：温度（℃）；传感器异常时返回 -100.0f
 * 说  明：公式 T = 1/(1/T0 + ln(R/R0)/B) - 273.15
 ******************************************************************************/
static float sense_adc_to_temp(uint16_t adc)
{
    /*================================
         #1. 原始值 → 引脚电压
     ================================*/
    float v = ((float)adc / BOARD_ADC_FULLSCALE) * BOARD_ADC_VREF;

    /*================================
         #2. 异常保护：电压接近参考电压说明 NTC 开路
     ================================*/
    if (v >= (BOARD_ADC_VREF * 0.99f))                  // 开路/未接
    {
        return -100.0f;                                 // 用明显异常值表示“无效”
    }

    /*================================
         #3. 电压 → NTC 阻值（10k 上拉分压）
     ================================*/
    float r = BOARD_NTC_R0 * v / (BOARD_ADC_VREF - v);   // 欧姆定律

    /*================================
         #4. B 值方程 → 摄氏温度
     ================================*/
    float t_k = 1.0f / ((1.0f / BOARD_NTC_T0_K) + (logf(r / BOARD_NTC_R0) / BOARD_NTC_B));
    return t_k - 273.15f;                               // 开氏转摄氏
}

/* Exported functions --------------------------------------------------------*/

/*******************************************************************************
 * 函数名：sense_read_all
 * 功  能：采集并换算全部模拟量
 * 参  数：d —— 输出结构体指针
 * 返回值：无
 * 说  明：一次读完 4 路（每路 10 次采样），耗时约 1~2 毫秒
 ******************************************************************************/
void sense_read_all(sense_data_t *d)
{
    if (d == NULL) return;                              // 指针保护

    /*================================
         #1. 采集电池电压
     ================================*/
    d->voltage = sense_adc_to_voltage(sense_read_channel(BOARD_ADC_CH_VOLTAGE));

    /*================================
         #2. 采集三路温度
     ================================*/
    d->temp_env = sense_adc_to_temp(sense_read_channel(BOARD_ADC_CH_TEMP_ENV));  // 环境
    d->temp_ipc = sense_adc_to_temp(sense_read_channel(BOARD_ADC_CH_TEMP_IPC));  // 工控机鳍片
    d->temp_pcb = sense_adc_to_temp(sense_read_channel(BOARD_ADC_CH_TEMP_PCB));  // 电源板
}
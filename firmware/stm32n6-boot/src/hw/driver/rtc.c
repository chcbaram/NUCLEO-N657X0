#include "rtc.h"
#include "log.h"
#include "cli.h"
#include <time.h>


#ifdef _USE_HW_RTC


/* 클럭은 보드의 32.768 kHz LSE.
   VBAT 은 배터리 없이 VDDIO 에 물려 있어(SB13) 리셋에는 시각과 백업 레지스터가 남고,
   전원이 끊기면 지워진다. 백업 레지스터는 RTC 가 아니라 TAMP 블록에 있다(TAMP_BKPxR). */


static RTC_HandleTypeDef hrtc;
static bool is_init = false;


#if CLI_USE(HW_RTC)
static void cliCmd(cli_args_t *args);
#endif




bool rtcInit(void)
{
  bool ret = true;


  hrtc.Instance            = RTC;
  hrtc.Init.HourFormat     = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv   = 127;
  hrtc.Init.SynchPrediv    = 255;
  hrtc.Init.OutPut         = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap    = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType     = RTC_OUTPUT_TYPE_OPENDRAIN;
  hrtc.Init.OutPutPullUp   = RTC_OUTPUT_PULLUP_NONE;
  hrtc.Init.BinMode        = RTC_BINARY_NONE;
  hrtc.Init.BinMixBcdU     = RTC_BINARY_MIX_BCDU_0;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    ret = false;
  }

  is_init = ret;

  logPrintf("[%s] rtcInit()\n", ret ? "OK":"NG");

#if CLI_USE(HW_RTC)
  cliAdd("rtc", cliCmd);
#endif
  return ret;
}

bool rtcIsInit(void)
{
  return is_init;
}

bool rtcGetInfo(rtc_info_t *rtc_info)
{
  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};


  // GetDate 를 GetTime 뒤에 불러야 shadow 레지스터 잠금이 풀린다
  if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
    return false;

  if (HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)
    return false;

  rtc_info->time.hours   = sTime.Hours;
  rtc_info->time.minutes = sTime.Minutes;
  rtc_info->time.seconds = sTime.Seconds;

  rtc_info->date.year  = sDate.Year;
  rtc_info->date.month = sDate.Month;
  rtc_info->date.day   = sDate.Date;
  rtc_info->date.week  = 0;

  return true;
}

bool rtcGetTime(rtc_time_t *rtc_time)
{
  rtc_info_t info;


  if (rtcGetInfo(&info) != true)
    return false;

  *rtc_time = info.time;
  return true;
}

bool rtcGetDate(rtc_date_t *rtc_date)
{
  rtc_info_t info;
  struct tm  timeinfo;


  if (rtcGetInfo(&info) != true)
    return false;

  memset(&timeinfo, 0, sizeof(timeinfo));
  timeinfo.tm_year = (2000 + info.date.year) - 1900;
  timeinfo.tm_mon  = info.date.month - 1;
  timeinfo.tm_mday = info.date.day;
  mktime(&timeinfo);

  *rtc_date      = info.date;
  rtc_date->week = timeinfo.tm_wday;
  return true;
}

bool rtcSetTime(rtc_time_t *rtc_time)
{
  RTC_TimeTypeDef sTime = {0};


  sTime.Hours   = rtc_time->hours;
  sTime.Minutes = rtc_time->minutes;
  sTime.Seconds = rtc_time->seconds;

  return HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) == HAL_OK;
}

bool rtcSetDate(rtc_date_t *rtc_date)
{
  RTC_DateTypeDef sDate = {0};


  sDate.Year    = rtc_date->year;
  sDate.Month   = rtc_date->month;
  sDate.Date    = rtc_date->day;
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;

  return HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) == HAL_OK;
}

bool rtcSetReg(uint32_t index, uint32_t data)
{
  if (!IS_RTC_BKP(index))
    return false;

  HAL_RTCEx_BKUPWrite(&hrtc, index, data);
  return true;
}

bool rtcGetReg(uint32_t index, uint32_t *p_data)
{
  if (!IS_RTC_BKP(index))
    return false;

  *p_data = HAL_RTCEx_BKUPRead(&hrtc, index);
  return true;
}

void HAL_RTC_MspInit(RTC_HandleTypeDef* rtcHandle)
{
  if (rtcHandle->Instance == RTC)
  {
    RCC_OscInitTypeDef       osc  = {0};
    RCC_PeriphCLKInitTypeDef clk  = {0};


    HAL_PWR_EnableBkUpAccess();

    // LSE 는 백업 도메인에 있어 리셋 뒤에도 켜져 있다. 꺼져 있을 때만 켠다
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == 0)
    {
      osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
      osc.LSEState       = RCC_LSE_ON;
      HAL_RCC_OscConfig(&osc);
    }

    // RTCSEL 이 이미 LSE 면 HAL 은 백업 도메인을 리셋하지 않는다
    clk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    clk.RTCClockSelection    = RCC_RTCCLKSOURCE_LSE;
    HAL_RCCEx_PeriphCLKConfig(&clk);

    __HAL_RCC_RTCAPB_CLK_ENABLE();
    __HAL_RCC_RTC_CLK_ENABLE();
    __HAL_RCC_RTC_ENABLE();
  }
}


#if CLI_USE(HW_RTC)
void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    rtc_info_t rtc_info;

    cliPrintf("is_init : %d\n", is_init);
    cliPrintf("LSE     : %s\n", __HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) ? "ready" : "not ready");
    if (rtcGetInfo(&rtc_info))
    {
      cliPrintf("Date    : 20%02d-%02d-%02d %02d:%02d:%02d\n",
                rtc_info.date.year,
                rtc_info.date.month,
                rtc_info.date.day,
                rtc_info.time.hours,
                rtc_info.time.minutes,
                rtc_info.time.seconds);
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "get") && args->isStr(1, "info"))
  {
    rtc_info_t rtc_info;

    while(cliKeepLoop())
    {
      rtcGetInfo(&rtc_info);

      cliPrintf("Y:%02d M:%02d D:%02d, H:%02d M:%02d S:%02d\n",
                rtc_info.date.year,
                rtc_info.date.month,
                rtc_info.date.day,
                rtc_info.time.hours,
                rtc_info.time.minutes,
                rtc_info.time.seconds);
      delay(1000);
    }
    ret = true;
  }

  if (args->argc == 5 && args->isStr(0, "set") && args->isStr(1, "time"))
  {
    rtc_time_t rtc_time;

    rtc_time.hours   = args->getData(2);
    rtc_time.minutes = args->getData(3);
    rtc_time.seconds = args->getData(4);

    rtcSetTime(&rtc_time);
    cliPrintf("H:%02d M:%02d S:%02d\n",
              rtc_time.hours,
              rtc_time.minutes,
              rtc_time.seconds);
    ret = true;
  }

  if (args->argc == 5 && args->isStr(0, "set") && args->isStr(1, "date"))
  {
    rtc_date_t rtc_date;

    rtc_date.year  = args->getData(2);
    rtc_date.month = args->getData(3);
    rtc_date.day   = args->getData(4);

    rtcSetDate(&rtc_date);
    cliPrintf("Y:%02d M:%02d D:%02d\n",
              rtc_date.year,
              rtc_date.month,
              rtc_date.day);
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "reg"))
  {
    uint32_t index = args->getData(1);
    uint32_t data  = args->getData(2);

    if (rtcSetReg(index, data))
    {
      rtcGetReg(index, &data);
      cliPrintf("BKP%d : 0x%08X\n", (int)index, (unsigned int)data);
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "reg"))
  {
    uint32_t index = args->getData(1);
    uint32_t data  = 0;

    if (rtcGetReg(index, &data))
    {
      cliPrintf("BKP%d : 0x%08X\n", (int)index, (unsigned int)data);
    }
    ret = true;
  }


  if (ret == false)
  {
    cliPrintf("rtc info\n");
    cliPrintf("rtc get info\n");
    cliPrintf("rtc set time [h] [m] [s]\n");
    cliPrintf("rtc set date [y] [m] [d]\n");
    cliPrintf("rtc reg [index] [data]\n");
  }
}
#endif

#endif

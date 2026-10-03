#include "process/cmd_boot.h"


/*
 * 앱(stm32n6-fw) 쪽 부트 커맨드.
 *
 * 명령 코드와 INFO 응답 구조는 FSBL(stm32n6-boot) 과 같다. 앱은 굽는 주체가 아니다.
 *   INFO       : mode = APP 로 알린다. 호스트 툴은 이것을 보고 FW_UPDATE 를 보낸다
 *   FW_UPDATE  : FSBL 에 머물러 달라고 남기고 리셋한다 (resetToBoot)
 *   FW_JUMP    : 같다 (앱은 이미 실행 중이다)
 *   그 밖의 FW_* : ERR_BOOT_WRONG_CMD — 쓰기·지우기는 FSBL 에서만 한다
 */
#define BOOT_CMD_INFO             0x0000
#define BOOT_CMD_FW_UPDATE        0x0008
#define BOOT_CMD_FW_JUMP          0x0009
#define BOOT_CMD_BAUD             0x0020
#define BOOT_CMD_RESET            0x0021

#define BOOT_CMD_VER              1
#define BOOT_BAUD_MIN             9600
#define BOOT_BAUD_MAX             12500000


// FSBL 의 boot_info_t 와 바이트 단위로 같아야 한다 (호스트 툴이 같은 형식으로 읽는다)
typedef struct
{
  uint32_t magic;
  uint32_t mode;
  uint32_t boot_addr;
  uint32_t boot_size;
  uint32_t firm_addr;
  uint32_t firm_vec_addr;
  uint32_t firm_size;
  uint32_t tag_size;
  uint32_t max_fw_size;
  uint32_t family_id;
  char     name[32];
  char     version[32];

  uint32_t cmd_ver;
  uint32_t boot2_addr;
  uint32_t data_addr;
  uint32_t data_size;
  uint32_t baud;
} __attribute__((packed)) boot_info_t;




bool cmdBootProcess(cmd_t *p_cmd)
{
  uint16_t  cmd      = p_cmd->packet.cmd;
  uint16_t  err_code = OK;
  uint8_t  *p_data   = p_cmd->packet.data;
  uint32_t  length   = p_cmd->packet.length;


  switch (cmd)
  {
    case BOOT_CMD_INFO:
    {
      boot_info_t info;

      memset(&info, 0, sizeof(info));
      info.magic         = MAGIC_NUMBER;
      info.mode          = HW_DEV_MODE;
      info.boot_addr     = FLASH_ADDR_BOOT;
      info.boot_size     = FLASH_SIZE_BOOT;
      info.firm_addr     = FLASH_ADDR_FIRM;
      info.firm_vec_addr = FLASH_ADDR_FIRM_VEC;
      info.firm_size     = FLASH_SIZE_FIRM;
      info.tag_size      = FLASH_SIZE_TAG;
      info.max_fw_size   = 0;                  // 앱은 굽는 주체가 아니다
      info.cmd_ver       = BOOT_CMD_VER;
      info.boot2_addr    = FLASH_ADDR_BOOT2;
      info.data_addr     = FLASH_ADDR_DATA;
      info.data_size     = FLASH_SIZE_DATA;
      info.baud          = cmdGetBaud(p_cmd);
      snprintf(info.name,    sizeof(info.name),    "%s", _DEF_BOARD_NAME);
      snprintf(info.version, sizeof(info.version), "%s", _DEF_FIRMWATRE_VERSION);

      cmdSendResp(p_cmd, cmd, OK, (uint8_t *)&info, sizeof(info));
      break;
    }

    case BOOT_CMD_FW_UPDATE:
    case BOOT_CMD_FW_JUMP:
      // 응답이 나간 뒤 리셋한다. FSBL 은 RTC 백업 레지스터의 부트 모드를 보고 머문다
      cmdSendResp(p_cmd, cmd, OK, NULL, 0);
      delay(50);
      resetToBoot(false);
      break;

    case BOOT_CMD_BAUD:
    {
      uint32_t baud = 0;

      if (length < 4)
      {
        err_code = ERR_BOOT_WRONG_CMD;
      }
      else
      {
        memcpy(&baud, &p_data[0], 4);
        if (cmdGetBaud(p_cmd) == 0)
          err_code = ERR_BOOT_WRONG_CMD;
        else if (baud < BOOT_BAUD_MIN || baud > BOOT_BAUD_MAX)
          err_code = ERR_BOOT_WRONG_RANGE;
      }

      cmdSendResp(p_cmd, cmd, err_code, (uint8_t *)&baud, 4);
      if (err_code == OK)
      {
        cmdSetBaud(p_cmd, baud);
      }
      break;
    }

    case BOOT_CMD_RESET:
      cmdSendResp(p_cmd, cmd, OK, NULL, 0);
      delay(50);
      resetToReset();
      break;

    default:
      cmdSendResp(p_cmd, cmd, ERR_BOOT_WRONG_CMD, NULL, 0);
      break;
  }

  return true;
}

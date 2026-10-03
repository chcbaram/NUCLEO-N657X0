#include "bootrom.h"
#include "log.h"
#include "cli.h"


#ifdef _USE_HW_BOOTROM


/* BootROM 이 남기는 바이너리 트레이스 (UM3234, docs/02-fsbl-loading.md 9절)

     START(0xFFDDBB00) | size | timestamp | level | code | arg ...
     size = 그 뒤 바이트 수 (timestamp + level + code + arg)

   FSBL 이미지(0x3418_0400~)와 겹치지 않는 자리라 언제 읽어도 된다. */
#define TRACE_SEC_ADDR      0x341037F0UL
#define TRACE_NSEC_ADDR     0x241077F0UL
#define TRACE_BUF_SIZE      2048
#define TRACE_START_WORD    0xFFDDBB00UL
#define TRACE_ARGS_MAX      8


typedef struct
{
  const uint32_t *p_word;
  uint32_t        remain;     // 남은 워드 수
} trace_iter_t;

typedef struct
{
  uint32_t ts;
  uint32_t level;
  uint32_t code;
  uint32_t args_cnt;
  uint32_t args[TRACE_ARGS_MAX];
} trace_t;

typedef struct
{
  uint32_t    code;
  const char *name;
} trace_name_t;


/* 다음 단계(Flash boot)에서 볼 만한 코드만 이름을 붙인다. 나머지는 코드로 찍는다.
   이름은 ST 문서/공개 파서의 이름을 따랐다. */
static const trace_name_t trace_name_tbl[] =
{
  {0x00000010, "BOOTCORE_LogicalResetSystem"},
  {0x00000011, "BOOTCORE_LogicalResetStKeyProvisioning"},
  {0x00000012, "BOOTCORE_LogicalResetInvalid"},
  {0x00000020, "BOOTCORE_ChipModeInvalid"},
  {0x00000021, "BOOTCORE_ChipModeStVirgin"},
  {0x00000022, "BOOTCORE_ChipModeStOpen"},
  {0x00000023, "BOOTCORE_ChipModeOpen"},
  {0x00000024, "BOOTCORE_ChipModeStClosedVirgin"},
  {0x00000025, "BOOTCORE_ChipModeClosedUnlocked"},
  {0x00000027, "BOOTCORE_ChipModeClosedLockedUnprovd"},
  {0x00000028, "BOOTCORE_ChipModeClosedLockedProvd"},
  {0x00000030, "BOOTCORE_BootActionInvalid"},
  {0x00000031, "BOOTCORE_BootActionNoBoot"},
  {0x00000032, "BOOTCORE_BootActionStKeyProv"},
  {0x00000033, "BOOTCORE_BootActionSecureBootProcess"},
  {0x00000034, "BOOTCORE_BootActionDevBoot"},
  {0x00000155, "BOOTCORE_BootRomVer"},
  {0x00000156, "BOOTCORE_BootRomChipVer"},
  {0x00000157, "BOOTCORE_BootRomCutVer"},
  {0x00000158, "BOOTCORE_BootRomForRtlVer"},
  {0x00000159, "BOOTCORE_BootRomTargetPlatform"},
  {0x0000015A, "BOOTCORE_BootRomMaskVer"},
  {0x00002500, "BOOTCORE_HwResetPOR"},
  {0x00002501, "BOOTCORE_HwResetBOR"},
  {0x00002502, "BOOTCORE_HwResetPin"},
  {0x00002506, "BOOTCORE_HwResetSft"},
  {0x00002508, "BOOTCORE_HwResetIwdgSys"},
  {0x00002509, "BOOTCORE_HwResetWwdgSys"},
  {0x2C000101, "SECBOOT_AuthWrongMagicNumber"},
  {0x2C000102, "SECBOOT_AuthImageLength"},
  {0x2C000103, "SECBOOT_AuthenticationExtensionHeaderMissing"},
  {0x2C000104, "SECBOOT_AuthEccAlgoP256NIST"},
  {0x2C000105, "SECBOOT_AuthEccAlgoBrainPool256"},
  {0x2C000106, "SECBOOT_AuthEccAlgoP384NIST"},
  {0x2C000107, "SECBOOT_AuthEccAlgoBrainPool384"},
  {0x2C000108, "SECBOOT_AuthEccAlgoUnknown"},
  {0x2C00010A, "SECBOOT_AuthImageNotEncrypted"},
  {0x2C00010B, "SECBOOT_AuthImageEncrypted"},
  {0x2C00010D, "SECBOOT_AuthImageDecryptionFailed"},
  {0x2C00010E, "SECBOOT_AuthImageDecryptionSucceed"},
  {0x2C00010F, "SECBOOT_AuthPubKeyVerifFailed"},
  {0x2C000112, "SECBOOT_AuthImageSignatureKo"},
  {0x2C000113, "SECBOOT_AuthImageSignatureOk"},
  {0x2C000114, "SECBOOT_AuthPubKeyVerifOk"},
  {0x2C000201, "SECBOOT_AuthWrongImageChecksum"},
  {0x2C000202, "SECBOOT_AuthPublicKeyIsRevoked"},
  {0x2C000203, "SECBOOT_AuthPublicKeyNotRevoked"},
  {0x2C000204, "SECBOOT_AuthWrongImageVersion"},
  {0x2C000207, "SECBOOT_AuthPubKHashTableNotEqualToRef"},
  {0x2C000208, "SECBOOT_AuthImageEntryPoint"},
  {0x2C00020B, "SECBOOT_AuthPublicKeyHashVerificationError"},
  {0x2C000302, "SECBOOT_DecryptUsingCrypDriver"},
  {0x2C000303, "SECBOOT_DecryptUsingSaesDriver"},
  {0x2C00030A, "SECBOOT_DecryptHashIntegrityError"},
  {0x2C004008, "SECBOOT_AuthDecisionIsJumpToImage"},
};

static const char *level_str[] = {"INFO ", "WARN ", "ERR  ", "DEBUG"};


static const char *traceName(uint32_t code);
static void        traceIterInit(trace_iter_t *p_iter, uint32_t addr);
static bool        traceIterNext(trace_iter_t *p_iter, trace_t *p_trace);
static void        tracePrint(char sec, const trace_t *p_trace);
static void        bootromSummary(char *p_buf, uint32_t size);
#if CLI_USE(HW_BOOTROM)
static void        cliCmd(cli_args_t *args);
#endif




bool bootromInit(void)
{
  char buf[96];


  bootromSummary(buf, sizeof(buf));
  logPrintf("[OK] bootromInit()\n");
  logPrintf("     %s\n", buf);

#if CLI_USE(HW_BOOTROM)
  cliAdd("bootrom", cliCmd);
#endif
  return true;
}

/* secure / non-secure 두 버퍼를 타임스탬프 순으로 합쳐 출력한다. */
void bootromPrintTrace(void)
{
  trace_iter_t iter[2];
  trace_t      trace[2];
  bool         valid[2];
  const char   sec_ch[2] = {'S', 'N'};
  uint32_t     cnt = 0;


  traceIterInit(&iter[0], TRACE_SEC_ADDR);
  traceIterInit(&iter[1], TRACE_NSEC_ADDR);
  valid[0] = traceIterNext(&iter[0], &trace[0]);
  valid[1] = traceIterNext(&iter[1], &trace[1]);

  cliPrintf("S/N  timestamp level code       name / args\n");
  while (valid[0] || valid[1])
  {
    int i;

    if (valid[0] && valid[1])
      i = (trace[0].ts <= trace[1].ts) ? 0 : 1;
    else
      i = valid[0] ? 0 : 1;

    tracePrint(sec_ch[i], &trace[i]);
    cnt++;
    valid[i] = traceIterNext(&iter[i], &trace[i]);
  }
  cliPrintf("%u traces\n", (unsigned)cnt);
}

static const char *traceName(uint32_t code)
{
  for (uint32_t i=0; i<sizeof(trace_name_tbl)/sizeof(trace_name_tbl[0]); i++)
  {
    if (trace_name_tbl[i].code == code)
    {
      return trace_name_tbl[i].name;
    }
  }
  return NULL;
}

static void traceIterInit(trace_iter_t *p_iter, uint32_t addr)
{
  p_iter->p_word = (const uint32_t *)addr;
  p_iter->remain = TRACE_BUF_SIZE / 4;
}

/* 다음 항목을 읽는다. 버퍼 끝이거나 형식이 깨졌으면 false. */
static bool traceIterNext(trace_iter_t *p_iter, trace_t *p_trace)
{
  uint32_t size;
  uint32_t words;


  while (p_iter->remain > 0 && *p_iter->p_word != TRACE_START_WORD)
  {
    p_iter->p_word++;
    p_iter->remain--;
  }
  if (p_iter->remain < 2)
  {
    return false;
  }

  size  = p_iter->p_word[1];
  words = size / 4;
  if (size < 12 || (size % 4) != 0 || words + 2 > p_iter->remain)
  {
    return false;
  }

  p_trace->ts       = p_iter->p_word[2];
  p_trace->level    = p_iter->p_word[3];
  p_trace->code     = p_iter->p_word[4];
  p_trace->args_cnt = words - 3;
  for (uint32_t i=0; i<p_trace->args_cnt && i<TRACE_ARGS_MAX; i++)
  {
    p_trace->args[i] = p_iter->p_word[5 + i];
  }

  p_iter->p_word += 2 + words;
  p_iter->remain -= 2 + words;
  return true;
}

static void tracePrint(char sec, const trace_t *p_trace)
{
  const char *name;
  char        buf[160];
  int         len;


  name = traceName(p_trace->code);
  len  = snprintf(buf, sizeof(buf), "%c %8u %s 0x%08X %s",
                  sec,
                  (unsigned)p_trace->ts,
                  p_trace->level < 4 ? level_str[p_trace->level] : "?    ",
                  (unsigned)p_trace->code,
                  name != NULL ? name : "");

  for (uint32_t i=0; i<p_trace->args_cnt && i<TRACE_ARGS_MAX && len < (int)sizeof(buf); i++)
  {
    len += snprintf(&buf[len], sizeof(buf) - len, " 0x%08X", (unsigned)p_trace->args[i]);
  }
  cliPrintf("%s\n", buf);
}

/* 부팅 배너에 찍을 요약 : 버전, 부트 동작, 칩 모드, 리셋 원인, 에러 수 */
static void bootromSummary(char *p_buf, uint32_t size)
{
  trace_iter_t iter;
  trace_t      trace;
  uint32_t     ver = 0;
  const char  *action = "?";
  const char  *mode = "?";
  const char  *reset = "?";
  uint32_t     err_cnt = 0;
  uint32_t     cnt = 0;


  for (uint32_t addr_i=0; addr_i<2; addr_i++)
  {
    traceIterInit(&iter, addr_i == 0 ? TRACE_SEC_ADDR : TRACE_NSEC_ADDR);
    while (traceIterNext(&iter, &trace))
    {
      const char *name = traceName(trace.code);

      cnt++;
      if (trace.level == 2) err_cnt++;
      if (trace.code == 0x155 && trace.args_cnt > 0) ver = trace.args[0];
      if (name == NULL) continue;

      if (strncmp(name, "BOOTCORE_BootAction", 19) == 0) action = name + 19;
      if (strncmp(name, "BOOTCORE_ChipMode",   17) == 0) mode   = name + 17;
      if (strncmp(name, "BOOTCORE_HwReset",    16) == 0) reset  = name + 16;
    }
  }

  snprintf(p_buf, size, "v0x%X %s %s reset=%s traces=%u err=%u",
           (unsigned)ver, action, mode, reset, (unsigned)cnt, (unsigned)err_cnt);
}

#if CLI_USE(HW_BOOTROM)
void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    char buf[96];

    bootromSummary(buf, sizeof(buf));
    cliPrintf("%s\n", buf);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "trace"))
  {
    bootromPrintTrace();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("bootrom info\n");
    cliPrintf("bootrom trace\n");
  }
}
#endif


#endif

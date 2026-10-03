#include "module.h"


#define MODULE_MAX    32


typedef struct
{
  int32_t         count;
  const module_t *p_module;
} module_info_t;


#if CLI_USE(HW_MODULE)
static void cliModule(cli_args_t *args);
#endif

static bool moduleBegin(void);

static module_info_t info;
static bool          mod_result[MODULE_MAX];

extern uint32_t _smodule;
extern uint32_t _emodule;




bool moduleInit(void)
{
  bool ret;

  info.count    = ((int)&_emodule - (int)&_smodule) / sizeof(module_t);
  info.p_module = (const module_t *)&_smodule;

  ret = moduleBegin();

  // 결과를 먼저 찍고 모듈별 결과를 들여 쓴다 (다른 드라이버 init 로그와 같은 형식)
  logPrintf("[%s] moduleInit()\n", ret ? "OK" : "E_");
  logPrintf("     count : %d\n", (int)info.count);
  for (int i = 0; i < info.count; i++)
  {
    logPrintf("     %-16s %s\n", info.p_module[i].name, (i < MODULE_MAX && mod_result[i]) ? "OK" : "Fail");
  }

#if CLI_USE(HW_MODULE)
  cliAdd("module", cliModule);
#endif

  return ret;
}

bool moduleUpdate(void)
{
  for (int i = 0; i < info.count; i++)
  {
    if (info.p_module[i].update != NULL)
    {
      info.p_module[i].update(info.p_module[i].arg);
    }
  }

  return true;
}

bool moduleBegin(void)
{
  bool ret = true;

  if (info.count > MODULE_MAX)
  {
    ret = false;      // 표가 넘친 모듈은 결과를 기록하지 못한다
  }

  for (int pri = MODULE_PRI_HIGH; pri < MODULE_PRI_MAX; pri++)
  {
    for (int i = 0; i < info.count; i++)
    {
      const module_t *p_mod = &info.p_module[i];

      //-- 우선순위가 범위 밖이면 영원히 안 불린다. 조용히 넘기지 않는다 (Fail 로 찍힌다)
      if (p_mod->priority < MODULE_PRI_HIGH || p_mod->priority >= MODULE_PRI_MAX)
      {
        ret = false;
        continue;
      }

      if (p_mod->priority == pri)
      {
        bool mod_ret = true;

        if (p_mod->init != NULL)
        {
          mod_ret = p_mod->init();
        }
        ret &= mod_ret;

#ifdef _USE_HW_EVENT
        //-- init() 이 성공한 모듈만 구독자로 올린다
        if (mod_ret == true && p_mod->event_cb != NULL)
        {
          eventSubFunc(p_mod->name, p_mod->event_cb);
        }
#endif
        if (i < MODULE_MAX) mod_result[i] = mod_ret;
      }
    }
  }

  return ret;
}


#if CLI_USE(HW_MODULE)
void cliModule(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("count : %d\n", (int)info.count);
    cliPrintf("%-4s %-16s %-4s %-6s %s\n", "idx", "name", "pri", "update", "init");

    for (int i = 0; i < info.count; i++)
    {
      cliPrintf("%-4d %-16s %-4d %-6s %s\n",
                i,
                info.p_module[i].name,
                (int)info.p_module[i].priority,
                info.p_module[i].update != NULL ? "yes" : "-",
                (i < MODULE_MAX && mod_result[i]) ? "OK" : "Fail");
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("module info\n");
  }
}
#endif

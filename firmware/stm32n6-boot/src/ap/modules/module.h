#ifndef MODULE_H_
#define MODULE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


typedef enum
{
  MODULE_PRI_HIGH = 1,
  MODULE_PRI_1,
  MODULE_PRI_2,
  MODULE_PRI_3,
  MODULE_PRI_4,
  MODULE_PRI_NORMAL,
  MODULE_PRI_LOWEST,
  MODULE_PRI_MAX,
} ModulePriority_t;


typedef struct
{
  const char       name[32];
  ModulePriority_t priority;

  //-- 한 번만 부른다. moduleInit() 이 우선순위 순으로.
  bool           (*init)(void);

  //-- 주기적으로 부른다. moduleUpdate() 가 등록 순으로.
  //   스레드를 따로 만들 만큼 무겁지 않은 일을 여기 둔다.
  void           (*update)(void const *arg);
  void            *arg;

#ifdef _USE_HW_EVENT
  //-- 이벤트 구독자. init() 이 성공하면 moduleInit() 이 자동으로 등록한다.
  //   모듈이 직접 eventSub() 를 부를 필요가 없다.
  event_func_t     event_cb;
#endif

} module_t;


/*
 * 모듈 자기 등록.
 *
 * 디스크립터를 .module 섹션에 심으면 moduleInit() 이 링커가 만든 _smodule ~ _emodule
 * 범위를 훑어 우선순위 순으로 init() 을 부른다. 파일을 추가하는 것만으로 등록되므로
 * 목록을 따로 관리할 필요가 없다.
 *
 * const + used 라 링커가 지우지 않고 .module 섹션에 모인다 (bsp/ldscript 의 .module).
 * FSBL 은 BootROM 이 이미지 통째로 RAM 에 올리므로 따로 복사할 것이 없다.
 */
#define MODULE_DEF(x_name) \
  static const __attribute__((section(".module"), used)) module_t module_##x_name =


bool moduleInit(void);
bool moduleUpdate(void);


#ifdef __cplusplus
}
#endif

#endif

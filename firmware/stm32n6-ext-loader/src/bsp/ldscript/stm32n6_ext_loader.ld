/*
  STM32CubeProgrammer 외부 로더 (.stldr)

  CubeProgrammer 는 ELF 를 읽어
    - SgInfo 세그먼트의 StorageInfo (주소 0, 적재 안 함) 로 메모리 정보를 알고
    - Loader 세그먼트를 RAM 에 그대로 써 넣은 뒤 Init / Write / SectorErase / MassErase 를 부른다.
  startup 코드는 돌지 않는다. .bss 는 Init() 이 직접 지운다.

  주소는 0x34180404 여야 한다. CubeProgrammer 는 0x34180400 에 복귀용 BKPT(4 바이트)를 넣고
  ELF 주소와 상관없이 그 바로 뒤에 Loader 세그먼트를 쓴다. 0x34180400 에 링크하면 코드가
  4 바이트 밀려 절대 주소가 모두 어긋나고 Init 에서 HardFault 가 난다 (ST 의 N6 로더도 0x34180404).
*/

ENTRY(Init)

PHDRS
{
  Loader PT_LOAD;
  SgInfo PT_LOAD;
}

MEMORY
{
  RAM (xrw) : ORIGIN = 0x34180404, LENGTH = 511K - 4
}

SECTIONS
{
  .text :
  {
    . = ALIGN(4);
    KEEP(*Loader_Src.c.obj(.text .text*))   /* CubeProgrammer 가 이름으로 찾는 함수들 */
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.vfp11_veneer)
    *(.v4_bx)
    *(.iplt)
    . = ALIGN(4);
    _etext = .;
  } >RAM :Loader

  .rodata :
  {
    . = ALIGN(4);
    *(EXCLUDE_FILE(*Dev_Inf.c.obj) .rodata EXCLUDE_FILE(*Dev_Inf.c.obj) .rodata*)
    . = ALIGN(4);
  } >RAM :Loader

  .gnu.sgstubs :
  {
    . = ALIGN(32);
    *(.gnu.sgstubs*)
    . = ALIGN(32);
  } >RAM :Loader

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >RAM :Loader
  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >RAM :Loader

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM :Loader

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM :Loader

  .Dev_info 0 :
  {
    KEEP(*Dev_Inf.c.obj(.rodata .rodata*))
  } :SgInfo

  .ARM.attributes 0 : { *(.ARM.attributes) }
}

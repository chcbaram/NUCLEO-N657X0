/*
  stm32n6-fw — XIP (Execute In Place)

  외부 NOR(XSPI2 memory-mapped) 에서 그대로 실행한다. FSBL 이 XIP 를 켜고 0x7010_1000 으로 점프한다.
  .data 는 NOR 에 실린 초기값을 startup 이 AXISRAM1 로 복사한다 (LMA = NOR, VMA = RAM).

    0x7010_0000  4 KB   TAG (firm_tag_t)
    0x7010_1000  1 KB   벡터 테이블 (VTOR)
    0x7010_1400         firm_ver_t (.version)
                        코드, rodata, .data 초기값      ← 여기까지가 이미지 (_fw_flash_size)
    0x3400_0000  1 MB   .data, .bss, heap, stack (AXISRAM1)

  [미검증] 아직 보드에서 돌려 보지 않았다. 앱 SystemInit 이 XSPI2 를 리셋하지 않게 해야 한다 (docs/29).
*/

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);
_sstack = _estack - _Min_Stack_Size;

_Min_Heap_Size  = 0x200;
_Min_Stack_Size = 0x1000;

MEMORY
{
  FLASH  (rx)     : ORIGIN = 0x70101000,   LENGTH = 16M - 4K
  RAM    (xrw)    : ORIGIN = 0x34000000,   LENGTH = 1024K
}

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    _fw_flash_begin = .;
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  /* 벡터 1 KB 뒤 고정 위치. FSBL 이 이미지 시작 + 0x400 에서 읽는다 */
  .version _fw_flash_begin + 0x400 :
  {
    KEEP(*(.version))
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    *(.RamFunc)
    *(.RamFunc*)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  /* 모듈 디스크립터 표 (MODULE_DEF) */
  .module :
  {
    . = ALIGN(4);
    _smodule = .;
    KEEP (*(.module))
    KEEP (*(.module*))
    _emodule = .;
    . = ALIGN(4);
  } >FLASH

  .ARM.extab : { . = ALIGN(4); *(.ARM.extab* .gnu.linkonce.armextab.*) . = ALIGN(4); } >FLASH
  .ARM :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >FLASH

  .preinit_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .gnu.sgstubs :
  {
    . = ALIGN(32);
    *(.gnu.sgstubs*)
    . = ALIGN(32);
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  /* 이미지 끝 = .data 의 적재 끝. FSBL 이 이만큼 복사하고(LRUN) CRC 를 본다 */
  _fw_flash_end  = LOADADDR(.data) + SIZEOF(.data);
  _fw_flash_size = _fw_flash_end - _fw_flash_begin;

  /* DMA 와 공유하는 버퍼. bspInit() 에서 MPU 로 non-cacheable 지정한다 (32 바이트 정렬).
     초기값이 없으므로 이미지에 넣지 않는다 (NOLOAD). 쓰기 전에 드라이버가 채운다 */
  .noncacheable (NOLOAD) :
  {
    . = ALIGN(32);
    __snoncacheable = .;
    KEEP(*(.noncacheable))
    . = ALIGN(32);
    __enoncacheable = .;
  } >RAM

  .bss (NOLOAD) :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}

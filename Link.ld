ENTRY( _start )

__stack_size = 256;

PROVIDE( _stack_size = __stack_size );

MEMORY
{
	FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 16K
	RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 2K
}

SECTIONS
{
    .init :
    { 
      _sinit = .;
      . = ALIGN(4);
      KEEP(*(SORT_NONE(.init)))
      . = ALIGN(4);
      _einit = .;
    } >FLASH AT>FLASH

    .text :
    {
      . = ALIGN(4);
      *(.text)
      *(.text.*)
      *(.rodata)
      *(.rodata*)
      *(.gnu.linkonce.t.*)
      . = ALIGN(4);
    } >FLASH AT>FLASH 

    PROVIDE( _etext = . );

    .dalign :
    {
      . = ALIGN(4);
      PROVIDE(_data_vma = .);
    } >RAM AT>FLASH  

    .dlalign :
    {
      . = ALIGN(4); 
      _etext = .;
    } > FLASH

    .data : AT(_etext)
    {
      . = ALIGN(4);
      _sdata = .;
      *(.data .data.*)
      *(.sdata .sdata.*)
      . = ALIGN(8);
      PROVIDE( __global_pointer$ = . + 0x800 );
      . = ALIGN(4);
      PROVIDE( _edata = .);
    } >RAM 

    _sidata = LOADADDR(.data);

    .bss :
    {
      . = ALIGN(4);
      PROVIDE( _sbss = .);
      *(.sbss*)
      *(.bss*)
      *(COMMON*)
      . = ALIGN(4);
      PROVIDE( _ebss = .);
    } >RAM AT>FLASH

    PROVIDE( _end = _ebss);
	PROVIDE( end = . );

    .stack ORIGIN(RAM) + LENGTH(RAM) - __stack_size :
    {
        . = ALIGN(4);
        _susrstack = . ;
        . = . + __stack_size;
        PROVIDE( _estack = .);
        PROVIDE( _eusrstack = .);
    } >RAM 
}

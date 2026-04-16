#include <stdint.h>

extern uint32_t _sbss, _ebss, _sdata, _edata, _sidata, _estack;
void main(void);

/* Reset vector: set gp/sp, enter C init (lives in .init at flash base). */
__attribute__((naked, section(".init"))) void _start(void) {
  __asm volatile(".option push\n"
                 ".option norelax\n"
                 "la     gp, __global_pointer$\n"
                 ".option pop\n"
                 "la     sp, _estack\n"
                 "j      startup_c_init\n" ::
                     : "memory");
}

/* Copy initialized RAM from LMA, zero BSS, call main (fallback loop if main returns). */
void startup_c_init(void) {
  uint32_t *src = &_sidata;
  uint32_t *dst = &_sdata;

  while (dst < &_edata) {
    *dst++ = *src++;
  }

  dst = &_sbss;
  while (dst < &_ebss) {
    *dst++ = 0;
  }

  main();

  while (1) {
    __asm__ volatile("nop");
  }
}

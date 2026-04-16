#include "ch32v00x.h"
#include <stdint.h>

/* Internal 24 MHz RC */
#define HSI_OSC_HZ 24000000u

#ifndef TWI_ADDRESS
#define TWI_ADDRESS 0x29
#endif

static uint32_t flash_limit_bytes;

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 64
#endif

#ifndef TIMEOUT_MS
#define TIMEOUT_MS 1000
#endif

#define CMD_WAIT             0x00
#define CMD_READ_VERSION     0x01
#define CMD_ACCESS_MEMORY    0x02
#define CMD_ACCESS_CHIPINFO  (0x10 | CMD_ACCESS_MEMORY)
#define CMD_ACCESS_FLASH     (0x20 | CMD_ACCESS_MEMORY)
#define CMD_ACCESS_EEPROM    (0x30 | CMD_ACCESS_MEMORY)
#define CMD_WRITE_FLASH_PAGE (0x40 | CMD_ACCESS_MEMORY)

#define CMD_SWITCH_APPLICATION CMD_READ_VERSION
#define CMD_BOOT_APPLICATION   (0x20 | CMD_SWITCH_APPLICATION)

#define BOOTTYPE_APPLICATION 0x80

#define MEMTYPE_CHIPINFO 0x00
#define MEMTYPE_FLASH    0x01

#define FLASH_PHYS_BASE ((uint32_t)0x08000000)

#define FLASH_KEY1 ((uint32_t)0x45670123)
#define FLASH_KEY2 ((uint32_t)0xCDEF89AB)

#define FLASH_STATR_BOOT_MODE ((uint32_t)(1u << 14))
#define RCC_RMVF              ((uint32_t)(1u << 24))
#define PFIC_KEY3             ((uint32_t)0xBEEF0000u)
#define PFIC_RESETSYS         ((uint32_t)(1u << 7))

#define CHIP_ID_U32 (*(volatile uint32_t *)(uintptr_t)0x1FFFF7C4)

#define I2C_ACK_ENABLE       ((uint16_t)0x0400)
#define I2C_CTLR2_FREQ_MASK  ((uint16_t)0xFFC0)
#define I2C_CTLR1_CLEAR_MASK ((uint16_t)0xFBF5)
#define I2C_CTLR1_NOSTRETCH  ((uint16_t)0x0080)

static const uint8_t info[12] = "TWIBOOT v4.0";

static uint8_t chipinfo[12];

#ifdef TWIBOOT_INFINITE
static uint16_t boot_timeout;
#else
/* When non-zero, SysTick may jump to the app; any I2C command clears this. */
static uint16_t boot_timeout = 1u;
#endif

static uint8_t cmd = CMD_WAIT;
static uint8_t buf[FLASH_PAGE_SIZE];
static uint32_t addr;
static uint8_t pending_flash_write;
static uint8_t i2c_phase;
static uint16_t i2c_wr_bcnt;

#ifdef LED_PIN
#if defined(LED_PORT_A)
#define LED_GPIO    GPIOA
#define LED_RCC_BIT (1u << 2)
#elif defined(LED_PORT_C)
#define LED_GPIO    GPIOC
#define LED_RCC_BIT (1u << 4)
#elif defined(LED_PORT_D)
#define LED_GPIO    GPIOD
#define LED_RCC_BIT (1u << 5)
#endif
#endif

static void twi_i2c_init(uint8_t own_addr7);
static void i2c_slave_poll(void);
static void i2c_spin_until(uint32_t iterations);
static void autoboot_timer_poll(void);
static void systick_arm(uint32_t cmp, uint8_t wait_done);
static uint8_t TWI_data_write(uint16_t bcnt, uint8_t data);
static uint8_t TWI_data_read(uint16_t bcnt);
static void write_flash_page(void);
static void prog_64_aligned(uint32_t phys, const uint8_t *src);
static void flash_unlock_fast(void);
static void flash_erase_page_fast(uint32_t page_phys);
static void jump_application(void) __attribute__((noreturn));

/*
 * Bootloader runs only on cold reset. Typical CH32V003 silicon leaves RCC->CFGR0 with HPRE=/3
 * (e.g. 0x20) → 8 MHz HCLK from 24 MHz HSI; RM reset value may differ. Override with
 * -DBOOT_CPU_HZ=24000000u if your part really boots at HSI/1.
 */
#ifndef BOOT_CPU_HZ
#define BOOT_CPU_HZ (HSI_OSC_HZ / 3u)
#endif
#define K_SYSTICK    (BOOT_CPU_HZ / 8000u)
#define I2C_PCLK_MHZ ((uint16_t)(BOOT_CPU_HZ / 1000000u))
#define I2C_CCR_100K ((uint16_t)(BOOT_CPU_HZ / 200000u))

static uint16_t urem_u16(uint16_t n, uint16_t d) {
  while (n >= d)
    n -= d;
  return n;
}

/* Program on-chip SysTick compare (CMP); if wait_done, spin until SR bit0 then disable. */
static void systick_arm(uint32_t cmp, uint8_t wait_done) {
  SysTick->CTLR = 0;
  SysTick->SR = 0;
  SysTick->CMP = cmp;
  SysTick->CNT = 0;
  SysTick->CTLR = 1u;
  if (wait_done) {
    while ((SysTick->SR & 1u) == 0u) {
    }
    SysTick->CTLR = 0;
  }
}

/* Busy delay after peripheral clock enables (NOP loop, scaled by iterations). */
static void i2c_spin_until(uint32_t iterations) {
  volatile uint32_t i;
  for (i = 0; i < (iterations * 4u); i++)
    __asm__ volatile("nop");
}

/* Read one byte from the application image in flash (linear offset from FLASH_PHYS_BASE). */
static uint8_t flash_read_u8(uint32_t off) {
  return *(volatile uint8_t *)(FLASH_PHYS_BASE + off);
}

/* Build the 12-byte chipinfo blob for TWI; sets flash_limit_bytes from ESIG capacity. */
static void fill_chipinfo(void) {
  uint32_t id = CHIP_ID_U32;
  uint16_t flash_kb = ESIG->FLACAP;
  uint32_t flash_bytes = (uint32_t)flash_kb << 10;

  flash_limit_bytes = flash_bytes;

  chipinfo[0] = (uint8_t)((id >> 24) & 0xFF);
  chipinfo[1] = (uint8_t)((id >> 16) & 0xFF);
  chipinfo[2] = (uint8_t)((id >> 8) & 0xFF);
  chipinfo[3] = (uint8_t)(id & 0xFF);

  chipinfo[4] = (uint8_t)((flash_bytes >> 24) & 0xFF);
  chipinfo[5] = (uint8_t)((flash_bytes >> 16) & 0xFF);
  chipinfo[6] = (uint8_t)((flash_bytes >> 8) & 0xFF);
  chipinfo[7] = (uint8_t)(flash_bytes & 0xFF);

  chipinfo[8] = (uint8_t)(FLASH_PAGE_SIZE >> 8);
  chipinfo[9] = (uint8_t)(FLASH_PAGE_SIZE & 0xFF);
  chipinfo[10] = 0x00;
  chipinfo[11] = 0x00;
}

/* Unlock flash controller for erase/program (FPEC keys + fast mode keys). */
static void flash_unlock_fast(void) {
  FLASH->KEYR = FLASH_KEY1;
  FLASH->KEYR = FLASH_KEY2;
  FLASH->MODEKEYR = FLASH_KEY1;
  FLASH->MODEKEYR = FLASH_KEY2;
}

/* Erase one flash page at page_phys if it lies in the allowed application range. */
static void flash_erase_page_fast(uint32_t page_phys) {
  if (page_phys < FLASH_PHYS_BASE || page_phys >= (FLASH_PHYS_BASE + flash_limit_bytes))
    return;

  FLASH->CTLR |= FLASH_CTLR_PAGE_ER;
  FLASH->ADDR = page_phys;
  FLASH->CTLR |= FLASH_CTLR_STRT;
  while (FLASH->STATR & FLASH_STATR_BSY) {
  }
  FLASH->CTLR &= ~FLASH_CTLR_PAGE_ER;
}

/* Erase and reprogram the flash page containing addr (from TWI write protocol). */
static void write_flash_page(void) {
  uint32_t page0 = (uint32_t)addr & ~(uint32_t)(FLASH_PAGE_SIZE - 1u);
  uint32_t phys = FLASH_PHYS_BASE + page0;

  if (phys < FLASH_PHYS_BASE || phys >= (FLASH_PHYS_BASE + flash_limit_bytes))
    return;

  flash_erase_page_fast(phys);
  prog_64_aligned(phys, buf);
}

/* Program 64 bytes at phys (64-byte aligned) using the controller page buffer sequence. */
static void prog_64_aligned(uint32_t phys, const uint8_t *src) {
  uint32_t w[16];
  int i;

  phys &= ~(uint32_t)63u;

  for (i = 0; i < 16; i++) {
    w[i] = (uint32_t)src[i * 4] | ((uint32_t)src[i * 4 + 1] << 8) |
           ((uint32_t)src[i * 4 + 2] << 16) | ((uint32_t)src[i * 4 + 3] << 24);
  }

  /* One flash page: hold PAGE_PG, reset buffer, load 16 words with BUF_LOAD, STRT, drop PAGE_PG. */
  FLASH->CTLR |= FLASH_CTLR_PAGE_PG;

  FLASH->CTLR |= FLASH_CTLR_BUF_RST;
  while (FLASH->STATR & FLASH_STATR_BSY) {
  }

  for (i = 0; i < 16; i++) {
    *(volatile uint32_t *)(phys + (uint32_t)(4u * (uint32_t)i)) = w[i];
    FLASH->CTLR |= FLASH_CTLR_BUF_LOAD;
    while (FLASH->STATR & FLASH_STATR_BSY) {
    }
  }

  FLASH->ADDR = phys;
  FLASH->CTLR |= FLASH_CTLR_STRT;
  while (FLASH->STATR & FLASH_STATR_BSY) {
  }

  FLASH->CTLR &= ~FLASH_CTLR_PAGE_PG;
}

/* I2C write side of the TWI protocol: command state machine; returns ACK (1) or NACK (0). */
static uint8_t TWI_data_write(uint16_t bcnt, uint8_t data) {
  uint8_t ack = 0x01;

  switch (bcnt) {
  case 0:
    switch (data) {
    case CMD_SWITCH_APPLICATION:
    case CMD_ACCESS_MEMORY:
    case CMD_WAIT:
      boot_timeout = 0;
      cmd = data;
      break;
    default:
      cmd = CMD_WAIT;
      ack = 0x00;
      break;
    }
    break;

  case 1:
    switch (cmd) {
    case CMD_SWITCH_APPLICATION:
      if (data == BOOTTYPE_APPLICATION)
        cmd = CMD_BOOT_APPLICATION;
      ack = 0x00;
      break;
    case CMD_ACCESS_MEMORY:
      if (data == MEMTYPE_CHIPINFO) {
        addr = 0;
        cmd = CMD_ACCESS_CHIPINFO;
      } else if (data == MEMTYPE_FLASH) {
        addr = 0;
        cmd = CMD_ACCESS_FLASH;
      } else {
        ack = 0x00;
      }
      break;
    default:
      ack = 0x00;
      break;
    }
    break;

  case 2:
  case 3:
  case 4:
  case 5:
    addr = (addr << 8) | data;
    if (bcnt == 5 && cmd == CMD_ACCESS_FLASH && addr >= flash_limit_bytes) {
      cmd = CMD_WAIT;
      ack = 0x00;
    }
    break;

  default:
    if (cmd == CMD_ACCESS_FLASH) {
      uint16_t pos = (uint16_t)(bcnt - 6u);
      buf[pos] = data;
      if (pos >= (uint16_t)(FLASH_PAGE_SIZE - 1u)) {
        cmd = CMD_WRITE_FLASH_PAGE;
        ack = 0x00;
      }
    } else {
      ack = 0x00;
    }
    break;
  }

  return ack;
}

/* I2C read side: version string, chipinfo, or flash byte stream per current cmd. */
static uint8_t TWI_data_read(uint16_t bcnt) {
  switch (cmd) {
  case CMD_READ_VERSION:
    return info[urem_u16(bcnt, (uint16_t)sizeof(info))];
  case CMD_ACCESS_CHIPINFO:
    return chipinfo[urem_u16(bcnt, (uint16_t)sizeof(chipinfo))];
  case CMD_ACCESS_FLASH:
    if ((uint32_t)addr >= flash_limit_bytes)
      return 0xFF;
    return flash_read_u8(addr++);
  default:
    return 0xFF;
  }
}

/* I2C1 slave on PC1/PC2 at 100 kHz (PCLK = BOOT_CPU_HZ). */
static void twi_i2c_init(uint8_t own_addr7) {
  uint16_t freq_mhz = I2C_PCLK_MHZ;
  uint16_t ccr = I2C_CCR_100K;
  uint16_t tmp;

  if (freq_mhz < 2u)
    freq_mhz = 2u;
  if (freq_mhz > 63u)
    freq_mhz = 63u;

  RCC->APB2PCENR |= (1u << 0) | (1u << 4);
  RCC->APB1PCENR |= (1u << 21);
  i2c_spin_until(256u);

  AFIO->PCFR1 &= ~((uint32_t)(1u << 1));

  {
    uint32_t cfg = GPIOC->CFGLR;
    cfg &= ~((uint32_t)0x0FF0u);
    cfg |= ((uint32_t)0x0FF0u);
    GPIOC->CFGLR = cfg;
  }

  I2C1->CTLR1 &= ~(uint16_t)0x0001u;

  I2C1->CTLR2 = (uint16_t)((I2C1->CTLR2 & I2C_CTLR2_FREQ_MASK) | (freq_mhz & 0x3Fu));

  if (ccr < 4u)
    ccr = 4u;
  I2C1->CKCFGR = ccr;

  I2C1->CTLR1 |= (uint16_t)0x0001u;

  tmp = I2C1->CTLR1;
  tmp &= I2C_CTLR1_CLEAR_MASK;
  tmp |= I2C_ACK_ENABLE;
  I2C1->CTLR1 = tmp;

  I2C1->OADDR2 = 0u;
  I2C1->OADDR1 = (uint16_t)(0x4000u | ((uint16_t)own_addr7 << 1));

  I2C1->CTLR1 |= I2C_CTLR1_NOSTRETCH;
  I2C1->CTLR1 |= I2C_ACK_ENABLE;

  i2c_spin_until(256u);
}

/* Service I2C1 slave events: flags, ADDR/STOPF clears, RX/TX bytes via TWI_data_* . */
static void i2c_slave_poll(void) {
  uint16_t star1 = I2C1->STAR1;

  if (star1 & I2C_STAR1_ADDR) {
    /* ADDR: must read STAR1 then STAR2 to clear and see TX vs RX. */
    (void)I2C1->STAR1;
    uint16_t star2 = I2C1->STAR2;

    i2c_wr_bcnt = 0;
    I2C1->CTLR1 |= I2C_ACK_ENABLE;
    i2c_phase = (star2 & I2C_STAR2_TRA) ? 2u : 1u;
    return;
  }

  if (star1 & I2C_STAR1_STOPF) {
    /* STOPF: read STAR1, then set PE again to clear (slave). */
    (void)I2C1->STAR1;
    I2C1->CTLR1 |= (uint16_t)0x0001u;
    I2C1->CTLR1 |= I2C_CTLR1_NOSTRETCH | I2C_ACK_ENABLE;

    if (cmd == CMD_WRITE_FLASH_PAGE)
      pending_flash_write = 1;

    i2c_wr_bcnt = 0;
    i2c_phase = 0;
    return;
  }

  if (star1 & (I2C_STAR1_BERR | I2C_STAR1_ARLO)) {
    I2C1->STAR1 = (uint16_t)~(I2C_STAR1_BERR | I2C_STAR1_ARLO);
    return;
  }

  if (star1 & I2C_STAR1_AF) {
    I2C1->STAR1 = (uint16_t)~I2C_STAR1_AF;
    I2C1->CTLR1 |= I2C_ACK_ENABLE;
    i2c_wr_bcnt = 0;
    i2c_phase = 0;
    return;
  }

  if (star1 & I2C_STAR1_OVR) {
    (void)I2C1->STAR1;
    (void)I2C1->DATAR;
    return;
  }

  if (star1 & I2C_STAR1_BTF) {
    (void)I2C1->STAR1;
    (void)I2C1->DATAR;
    return;
  }

  if (i2c_phase == 1u && (star1 & I2C_STAR1_RXNE)) {
    uint8_t d = (uint8_t)I2C1->DATAR;
    if (TWI_data_write(i2c_wr_bcnt++, d) == 0x00)
      I2C1->CTLR1 &= ~I2C_ACK_ENABLE;
    return;
  }

  if (i2c_phase == 2u && (star1 & I2C_STAR1_TXE)) {
    I2C1->DATAR = TWI_data_read(i2c_wr_bcnt++);
    return;
  }
}

/* If autoboot is armed, watch SysTick; expiry requests jump to application. */
static void autoboot_timer_poll(void) {
  if (boot_timeout == 0u) {
    SysTick->CTLR = 0;
    return;
  }
  if (SysTick->SR & 1u) {
    cmd = CMD_BOOT_APPLICATION;
    SysTick->CTLR = 0;
    boot_timeout = 0;
  }
}

/* Tear down bootloader peripherals, exit boot mode, trigger processor reset into user flash. */
static void jump_application(void) {
#ifdef LED_PIN
  LED_GPIO->BSHR = (1u << LED_PIN);
#endif

  I2C1->CTLR1 &= ~(uint16_t)0x0001u;
  RCC->APB1PCENR &= ~(1u << 21);

  RCC->RSTSCKR |= RCC_RMVF;

  FLASH->KEYR = FLASH_KEY1;
  FLASH->KEYR = FLASH_KEY2;
  FLASH->BOOT_MODEKEYR = FLASH_KEY1;
  FLASH->BOOT_MODEKEYR = FLASH_KEY2;
  FLASH->STATR &= ~FLASH_STATR_BOOT_MODE;
  FLASH->CTLR |= FLASH_CTLR_LOCK;

  __asm volatile("" ::: "memory");

  PFIC->CFGR = PFIC_KEY3 | PFIC_RESETSYS;

  while (1)
    ;
}

/* Boot entry: fixed cold-reset HCLK, optional LED, power-on delay, I2C + flash unlock, autoboot
 * loop. */
void main(void) {
  uint32_t k = K_SYSTICK;
  if (k == 0u)
    k = 1u;

#ifdef LED_PIN
#ifdef LED_GPIO
  {
    uint32_t sh = (uint32_t)LED_PIN * 4u;
    RCC->APB2PCENR |= LED_RCC_BIT;
    LED_GPIO->CFGLR = (LED_GPIO->CFGLR & ~((uint32_t)0xFu << sh)) | ((uint32_t)0x3u << sh);
    LED_GPIO->BSHR = (1u << (LED_PIN + 16));
  }
#endif
#endif

  {
    uint32_t c = 200u * k;
    if (c < 100u)
      c = 100u;
    systick_arm(c, 1u);
  }

  cmd = 0;
  i2c_phase = 0;
  pending_flash_write = 0;

  fill_chipinfo();
  twi_i2c_init((uint8_t)TWI_ADDRESS);
  flash_unlock_fast();

#ifndef TWIBOOT_INFINITE
  {
    uint32_t c = TIMEOUT_MS * k;
    if (c < 1000u)
      c = 1000u;
    systick_arm(c, 0u);
  }
#endif

  while (cmd != CMD_BOOT_APPLICATION) {
    i2c_slave_poll();
    autoboot_timer_poll();

    if (pending_flash_write) {
      pending_flash_write = 0;
      write_flash_page();
      cmd = CMD_WAIT;
    }
  }

  jump_application();
}
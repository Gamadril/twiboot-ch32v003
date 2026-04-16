#ifndef CH32V00X_H
#define CH32V00X_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PERIPH_BASE           ((uint32_t)0x40000000)

#define APB1PERIPH_BASE       PERIPH_BASE
#define APB2PERIPH_BASE       (PERIPH_BASE + 0x10000)
#define AHBPERIPH_BASE        (PERIPH_BASE + 0x20000)

#define I2C1_BASE             (APB1PERIPH_BASE + 0x5400)
#define GPIOA_BASE            (APB2PERIPH_BASE + 0x0800)
#define GPIOC_BASE            (APB2PERIPH_BASE + 0x1000)
#define GPIOD_BASE            (APB2PERIPH_BASE + 0x1400)
#define RCC_BASE              (AHBPERIPH_BASE + 0x1000)
#define FLASH_R_BASE          (AHBPERIPH_BASE + 0x2000)
#define AFIO_BASE             (APB2PERIPH_BASE + 0x0000)
#define ESIG_BASE             ((uint32_t)0x1FFFF7E0)
#define PFIC_BASE             ((uint32_t)0xE000E000)
#define SYSTICK_BASE          ((uint32_t)0xE000F000)

typedef struct {
    volatile uint32_t CTLR;
    volatile uint32_t SR;
    volatile uint32_t CNT;
    uint32_t      RESERVED0;
    volatile uint32_t CMP;
    uint32_t      RESERVED1;
} SysTick_TypeDef;
typedef struct {
    volatile uint32_t CTLR;
    volatile uint32_t CFGR0;
    volatile uint32_t INTR;
    volatile uint32_t APB2PRSTR;
    volatile uint32_t APB1PRSTR;
    volatile uint32_t AHBPCENR;
    volatile uint32_t APB2PCENR;
    volatile uint32_t APB1PCENR;
    uint32_t      RESERVED0;
    volatile uint32_t RSTSCKR;
} RCC_TypeDef;

typedef struct {
    volatile uint32_t ACTLR;
    volatile uint32_t KEYR;
    volatile uint32_t OBKEYR;
    volatile uint32_t STATR;
    volatile uint32_t CTLR;
    volatile uint32_t ADDR;
    uint32_t      RESERVED0;
    volatile uint32_t OBR;
    volatile uint32_t WPR;
    volatile uint32_t MODEKEYR;
    volatile uint32_t BOOT_MODEKEYR;
} FLASH_TypeDef;

typedef struct {
    volatile uint32_t CFGLR;
    volatile uint32_t CFGHR;
    volatile const uint32_t INDR;
    volatile uint32_t OUTDR;
    volatile uint32_t BSHR;
    volatile uint32_t BCR;
    volatile uint32_t LCKR;
} GPIO_TypeDef;

typedef struct {
    volatile uint16_t CTLR1;
    uint16_t      RESERVED0;
    volatile uint16_t CTLR2;
    uint16_t      RESERVED1;
    volatile uint16_t OADDR1;
    uint16_t      RESERVED2;
    volatile uint16_t OADDR2;
    uint16_t      RESERVED3;
    volatile uint16_t DATAR;
    uint16_t      RESERVED4;
    volatile uint16_t STAR1;
    uint16_t      RESERVED5;
    volatile uint16_t STAR2;
    uint16_t      RESERVED6;
    volatile uint16_t CKCFGR;
    uint16_t      RESERVED7;
} I2C_TypeDef;

typedef struct {
    uint32_t      RESERVED0;
    volatile uint32_t PCFR1;
    volatile uint32_t EXTICR;
} AFIO_TypeDef;

typedef struct {
    volatile const uint16_t FLACAP;
    uint16_t     RES1;
    uint32_t     RES2;
    volatile const uint32_t UNIID1;
    volatile const uint32_t UNIID2;
    volatile const uint32_t UNIID3;
} ESIG_TypeDef;

/* Interrupt controller: CFGR must be at +0x48 for a valid software reset (key | bit7). */
typedef struct {
    volatile const uint32_t ISR[8];
    volatile const uint32_t IPR[8];
    volatile uint32_t ITHRESDR;
    volatile uint32_t RESERVED0;
    volatile uint32_t CFGR;
    volatile const uint32_t GISR;
    volatile uint8_t VTFIDR[4];
    uint8_t RESERVED1[12];
    volatile uint32_t VTFADDR[4];
    uint8_t RESERVED2[0x90];
    volatile uint32_t IENR[8];
    uint8_t RESERVED3[0x60];
    volatile uint32_t IRER[8];
    uint8_t RESERVED4[0x60];
    volatile uint32_t IPSR[8];
    uint8_t RESERVED5[0x60];
    volatile uint32_t IPRR[8];
    uint8_t RESERVED6[0x60];
    volatile uint32_t IACTR[8];
    uint8_t RESERVED7[0xe0];
    volatile uint8_t IPRIOR[256];
    uint8_t RESERVED8[0x810];
    volatile uint32_t SCTLR;
} PFIC_TypeDef;

#define RCC                   ((RCC_TypeDef *)RCC_BASE)
#define FLASH                 ((FLASH_TypeDef *)FLASH_R_BASE)
#define GPIOA                 ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOC                 ((GPIO_TypeDef *)GPIOC_BASE)
#define GPIOD                 ((GPIO_TypeDef *)GPIOD_BASE)
#define I2C1                  ((I2C_TypeDef *)I2C1_BASE)
#define AFIO                  ((AFIO_TypeDef *)AFIO_BASE)
#define PFIC                  ((PFIC_TypeDef *)PFIC_BASE)
#define SysTick               ((SysTick_TypeDef *)SYSTICK_BASE)
#define ESIG                  ((ESIG_TypeDef *)ESIG_BASE)

#define RCC_HPRE              ((uint32_t)0x000000F0)

#define FLASH_CTLR_STRT       ((uint32_t)0x00000040)
#define FLASH_CTLR_PAGE_PG    ((uint32_t)0x00010000)
#define FLASH_CTLR_PAGE_ER    ((uint32_t)0x00020000)
#define FLASH_CTLR_BUF_LOAD   ((uint32_t)0x00040000)
#define FLASH_CTLR_BUF_RST    ((uint32_t)0x00080000)
#define FLASH_CTLR_LOCK       ((uint32_t)0x00000080)
#define FLASH_STATR_BSY       ((uint32_t)0x00000001)

#define I2C_STAR1_ADDR        ((uint16_t)0x0002)
#define I2C_STAR1_BTF         ((uint16_t)0x0004)
#define I2C_STAR1_STOPF       ((uint16_t)0x0010)
#define I2C_STAR1_RXNE        ((uint16_t)0x0040)
#define I2C_STAR1_TXE         ((uint16_t)0x0080)
#define I2C_STAR1_BERR        ((uint16_t)0x0100)
#define I2C_STAR1_ARLO        ((uint16_t)0x0200)
#define I2C_STAR1_AF          ((uint16_t)0x0400)
#define I2C_STAR1_OVR         ((uint16_t)0x0800)
#define I2C_STAR2_TRA         ((uint16_t)0x0004)

#ifdef __cplusplus
}
#endif

#endif

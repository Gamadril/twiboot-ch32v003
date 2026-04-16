# twiboot-ch32v003
### I2C Bootloader for WCH CH32V003 (RISC-V)

This is a I2C bootloader for the CH32V003, designed to be **portably built** using standard system toolchains (e.g., `apt install gcc-riscv64-unknown-elf`). The protocol spec follows the original twiboot version for AVR with one exception: all addresses are 32-bit instead of 16-bit.  

## Key Features
- **Standalone build**: No WCH EVT/SDK required; minimal register map in [`ch32v00x.h`](ch32v00x.h)
- **System flash only**: The bootloader image fits entirely in the system flash window at `0x1FFFF000`.
- **Handover to app**: Clears boot mode in `FLASH->STATR` and requests reset via **PFIC** so the chip restarts into user code at `0x08000000`.

> [!IMPORTANT]
> **The user application MUST be linked at address `0x00000000`.**  
> The bootloader transparently maps protocol addresses (0-relative) to the physical flash base (`0x08000000`). Apps linked at physical addresses (e.g. `0x08000000`) will be **rejected with a protocol NAK**. Configure your linker with `FLASH ORIGIN = 0x00000000`.

## Build Requirements
- **Standard RISC-V Toolchain**: `riscv64-unknown-elf-gcc` (Version 12+ recommended).
- **Make**

## Project Structure
- `boot_twiboot.c`: Core bootloader logic (I2C Slave + Flash programming).
- `startup_bl.c`: Minimal RISC-V entry point with stack/GP initialization.
- `ch32v00x.h`: Bare-metal register and bit definitions.
- `Link.ld`: Linker script optimized for the 1920-byte System flash window.

## How to Build
To build the bootloader, you must specify the 7-bit I2C address:

```bash
# Basic build for address 0x27
make TWI_ADDRESS=0x27 clean all
```

The output files (`.bin`, `.hex`, `.elf`) are written to the **`build/`** directory.

### Build Options
You can customize the build by passing additional variables to `make`:

| Variable | Default | Description |
| :------- | :------ | :---------- |
| `TWI_ADDRESS` | (required) | The 7-bit I2C slave address (e.g. `0x27`). |
| `LED_PIN` | (unset) | Optional. Set to a pin like `PD0` or `PC4` to enable status LED (active-low). |
| `FLASH_PAGE_SIZE`| `64` | The flash page size of your MCU. `64` for CH32V003. |
| `TWIBOOT_INFINITE`| `0` | Set to `1` to disable the 1-second timeout. The bootloader will stay active on the I2C bus forever. |

Example for a debug build with LED on PC4 and no timeout:
```bash
make TWI_ADDRESS=0x27 LED_PIN=PC4 TWIBOOT_INFINITE=1 clean all
```

## How to Flash
The bootloader image must be programmed once into the **System Flash** area. The included `Makefile` provides targets using `wlink`:

```bash
# Program to System Flash and set boot mode
make TWI_ADDRESS=0x27 flash
```

## Boot mode selection (CH32V003)

Cold-start vector selection is **not** part of the bootloader binary alone: it comes from the **user option bytes** in the information block. On CH32V003 the **USER** byte includes **START_MODE** (bit 5): when set to **0**, the chip boots from the **System / BOOT flash** region (`0x1FFFF000`); when set to **1**, it boots from **Code flash** (`0x08000000`).

> [!NOTE]
> The interpretation of the START_MODE bit can vary between different flashing tools (some use `0` for System, some use `1`). The `wlink` tool used in the Makefile handles this via the `--system-boot` flag.

- **Use this bootloader on reset:** Option bytes must select **System flash**.
- **`wlink` command:** `wlink flash --system-boot system --address 0x1FFFF000 <bl.bin>` programs the image to the 1.9KB System Flash area and ensures the chip is configured for System Flash boot.

> [!CAUTION]
> You must specify `--address 0x1FFFF000`. If omitted, `wlink` will default to User Flash (`0x08000000`), even if `--system-boot system` is provided.

At runtime, the bootloader can trigger a soft-reset into the application by clearing the `BOOT_MODE` bit in `FLASH->STATR` and initiating a software reset.

**Important:** Default autoboot timeout is **1 second**. If no I2C communication is detected within this window, the loader jumps to the user application. Use `TWIBOOT_INFINITE=1` during development to keep the bootloader active for scanning.


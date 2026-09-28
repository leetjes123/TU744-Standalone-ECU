#ifndef LRE_STORAGE_LAYOUT_H
#define LRE_STORAGE_LAYOUT_H
/* Explicit profile: original development EEPROM or stock EEPROM + NOR tune. */
#ifndef STOCK_95080
#define STOCK_95080 0
#endif
#if STOCK_95080
#define EEPROM_BYTES 1024U
#define HISTORY_SLOTS 1U
#define HISTORY_BASE0 0U
#define HISTORY_BASE1 0U
#define FAULT_BASE0 800U
#define FAULT_BASE1 896U
/* These 64 KiB sectors exist in both top- and bottom-boot Am29F400B maps.
   The stock profile linker excludes the entire 0x50000..0x7FFFF region. */
#define FLASH_CAL0 0x50000UL
#define FLASH_CAL1 0x60000UL
u8 hal_cal_read(u8 slot, u16 at, u8 *bytes, u8 length);
u8 hal_cal_erase(u8 slot);
u8 hal_cal_program(u8 slot, u16 at, const u8 *bytes, u8 length);
#else
#define EEPROM_BYTES 8192U
#define HISTORY_SLOTS 2U
#define HISTORY_BASE0 3200U
#define HISTORY_BASE1 7296U
#define FAULT_BASE0 3104U
#define FAULT_BASE1 7200U
#endif
#endif

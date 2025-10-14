#include "HAL_Flash.h"
#include "variants.h"  // for MKS_TFT35_V1_0, FLASH_Unlock etc.
#include "my_misc.h"
#include "FlashStore.h"

/*
 * Sector  0 0x0800 0000 - 0x0800 3FFF  16KByte
 * Sector  1 0x0800 4000 - 0x0800 7FFF  16KByte
 * Sector  2 0x0800 8000 - 0x0800 BFFF  16KByte
 * Sector  3 0x0800 C000 - 0x0800 FFFF  16KByte
 * Sector  4 0x0801 0000 - 0x0801 FFFF  64KByte
 * Sector  5 0x0802 0000 - 0x0803 FFFF 128KByte ___256KByte
 * Sector  6 0x0804 0000 - 0x0805 FFFF 128KByte
 * Sector  7 0x0804 0000 - 0x0807 FFFF 128KByte ___512KByte
 * Sector  8 0x0804 0000 - 0x0809 FFFF 128KByte
 * Sector  9 0x0804 0000 - 0x080B FFFF 128KByte
 * Sector 10 0x0804 0000 - 0x080D FFFF 128KByte
 * Sector 11 0x080E 0000 - 0x080F FFFF 128KByte ___1MByte
 */

#if defined(MKS_TFT35_V1_0)  // MKS_TFT35_V1_0 bootloader uses sector 0, 1 and 2 (48KBytes)
  #define FLASH_SECTOR_0_ADDR  ((uint32_t)0x08000000)  // base @ of sector  0,  16 KBytes
  #define FLASH_SECTOR_1_ADDR  ((uint32_t)0x08004000)  // base @ of sector  1,  16 KBytes
  #define FLASH_SECTOR_2_ADDR  ((uint32_t)0x08008000)  // base @ of sector  2,  16 KBytes
  #define FLASH_SECTOR_3_ADDR  ((uint32_t)0x0800C000)  // base @ of sector  3,  16 KBytes
  #define FLASH_SECTOR_4_ADDR  ((uint32_t)0x08010000)  // base @ of sector  4,  64 KBytes
  #define FLASH_SECTOR_5_ADDR  ((uint32_t)0x08020000)  // base @ of sector  5, 128 KBytes
  #define FLASH_SECTOR_6_ADDR  ((uint32_t)0x08040000)  // base @ of sector  6, 128 KBytes
  #define FLASH_SECTOR_7_ADDR  ((uint32_t)0x08060000)  // base @ of sector  7, 128 KBytes
  #define FLASH_SECTOR_8_ADDR  ((uint32_t)0x08080000)  // base @ of sector  8, 128 KBytes
  #define FLASH_SECTOR_9_ADDR  ((uint32_t)0x080A0000)  // base @ of sector  9, 128 KBytes
  #define FLASH_SECTOR_10_ADDR ((uint32_t)0x080C0000)  // base @ of sector 10, 128 KBytes
  #define FLASH_SECTOR_11_ADDR ((uint32_t)0x080E0000)  // base @ of sector 11, 128 KBytes

  // Default is use I2C AT24C16 2KBytes EEPROM
  // Thanks to darkspr1te for the implementation
  #define SIGN_ADDRESS (0x08060000)    // reserve the last 128KB for user params or we damage the boot loader on this board
  #define FLASH_SECTOR FLASH_Sector_7  // points to an available sector (0x08060000 - 0x0807FFFF)
  #define FLASH_SECTOR_SIZE 0x4000     // 16KBytes flash sector
#else
  #define SIGN_ADDRESS (0x08004000)    // reserve the second sector (16KB) to save user parameters
  #define FLASH_SECTOR FLASH_Sector_1
  #define FLASH_SECTOR_SIZE 0x4000 // 16KB flash sector
#endif

#if defined(MKS_TFT35_V1_0)

// returns in which sector id (NOT 0-11!) a flash memory address is located
uint8_t HAL_FlashGetSector(uint32_t flash_address)
{ // Sector lookup table, 1 byte represents 16KB 
  if (flash_address < FLASH_BASE)
    return 0;
  
  static const uint8_t sector_data1[4] = {
    FLASH_Sector_0, FLASH_Sector_1, FLASH_Sector_2, FLASH_Sector_3 };   // Sector  0-3  16KB

  static const uint8_t sector_data2[8] = {
    FLASH_Sector_4, FLASH_Sector_5, FLASH_Sector_6,  FLASH_Sector_7,    // Sector    4  64KB
    FLASH_Sector_8, FLASH_Sector_9, FLASH_Sector_10, FLASH_Sector_11 }; // Sector 5-11 128KB

  if (flash_address < FLASH_SECTOR_4_ADDR)
    return sector_data1[((flash_address - FLASH_BASE) >> 14) % 4];

  return sector_data2[((flash_address - FLASH_BASE) >> 17) % 8];
}

#endif  // MKS_TFT35_V1_0

uint32_t Find_EmptyFlashSlot()
{
  for (uint32_t i = 0; i < (FLASH_SECTOR_SIZE / PARA_SIZE); i++)
  {
    if (*((volatile uint32_t *)(SIGN_ADDRESS + (i * PARA_SIZE) + 1)) == EMPTY_FLASH_WORD)
      return i; // found
  }

  return (FLASH_SECTOR_SIZE / PARA_SIZE); // nothing found, indicate impossible slot
}

void HAL_FlashRead(uint32_t * data, uint32_t len)
{
  uint32_t slot = Find_EmptyFlashSlot();
  if (slot) // read from slot before empty slot
    slot--;

  for (uint32_t i = 0; i < (len >> 2); i++)
  {
    data[i] = *((volatile uint32_t *)(SIGN_ADDRESS + (slot * PARA_SIZE) + (i << 2)));
  }
}

void HAL_FlashWrite(uint32_t * data, uint32_t len)
{
  // find an empty flash slot
  uint32_t slot = Find_EmptyFlashSlot();

  FLASH_Unlock();

  if (slot == (FLASH_SECTOR_SIZE / PARA_SIZE)) // no more empty slots, start over
  {
    #if defined(MKS_TFT35_V1_0)  // added for MKS_TFT35_V1_0 support
      FLASH_EraseSector(HAL_FlashGetSector(SIGN_ADDRESS), VoltageRange_3);
    #else
      FLASH_EraseSector(FLASH_SECTOR, VoltageRange_3);
    #endif
    slot = 0; // restart from the first slot
  }

  for (i = 0; i < len; i++)
  {
    FLASH_ProgramByte(SIGN_ADDRESS + (slot * PARA_SIZE) + i, data[i]);
  for (uint32_t i = 0; i < (len >> 2); i++)
  {
    FLASH_ProgramWord(SIGN_ADDRESS + (slot * PARA_SIZE) + (i << 2), data[i]);
  }

  FLASH_Lock();
}

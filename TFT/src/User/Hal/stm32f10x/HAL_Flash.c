#include "HAL_Flash.h"
#include "variants.h"  // for FLASH_Unlock etc.
#include "my_misc.h"
#include "FlashStore.h"

#define SIGN_ADDRESS (0x08040000 - 0x800)  // reserve the last page (2KB) to save user parameters
#define FLASH_SECTOR_SIZE 0x800 // 2KB flash sector

uint32_t Find_EmptyFlashSlot()
{
  for (uint32_t i = 0; i < (FLASH_SECTOR_SIZE / PARA_SIZE); i++)
  {
    if (*((volatile uint32_t *)(SIGN_ADDRESS + (i * PARA_SIZE) + 1)) == EMPTY_FLASH_WORD)
      return i; // found
  }

  return (FLASH_SECTOR_SIZE / PARA_SIZE); // nothing found, indicate impossible slot
}

void HAL_FlashRead(uint8_t * data, uint32_t len)
{
  uint32_t i = 0;

  uint32_t slot = Find_EmptyFlashSlot();
  if (slot) // read from slot before empty slot
    slot--;

  for (i = 0; i < len; i++)
  {
    data[i] = *((volatile uint8_t *)(SIGN_ADDRESS + (slot * PARA_SIZE) + i));
  }
}

void HAL_FlashWrite(uint8_t * data, uint32_t len)
{
  uint32_t i = 0;
 
  // find an empty flash slot
  uint32_t slot = Find_EmptyFlashSlot();

  FLASH_Unlock();

  if (slot == (FLASH_SECTOR_SIZE / PARA_SIZE)) // no more empty slots, start over
  {
    FLASH_ErasePage(SIGN_ADDRESS);
    slot = 0; // restart from the first slot
  }

  for (i = 0; i < len; i += 2)
  {
    uint16_t data16 = data[i] | (data[MIN(i + 1, len - 1)] << 8);  // stm32f10x needs to write at least 16 bits at a time

    FLASH_ProgramHalfWord(SIGN_ADDRESS + (slot * PARA_SIZE) + i, data16);
  }

  FLASH_Lock();
}

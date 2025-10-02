#include "FlashStore.h"
#include "Touch_Screen.h"
#include "Settings.h"
#include "HAL_Flash.h"
#include <string.h>

#ifdef I2C_EEPROM  // added I2C_EEPROM suppport for MKS_TFT35_V1_0
  #include "i2c_eeprom.h"
#endif

#define TSC_SIGN  0x20200512  // (YYYYMMDD) DO NOT MODIFY (Touch Screem Calibration sign)
#define PARA_SIGN 0x20240203  // (YYYYMMDD) if a new setting parameter is added, modify here and
                              // initialize the initial value in the "initSettings()" function
enum
{
  PARA_TSC_EXIST = (1 << 0),
  PARA_NOT_STORED = (1 << 1),
};

static uint8_t paraStatus = 0;

static void wordToByte(uint32_t word, uint8_t * bytes)
{
  uint8_t len = 4;
  uint8_t i = 0;

  for (i = 0; i < len; i++)
  {
    bytes[i] = (word >> 24) & 0xFF;
    word <<= 8;
  }
}

static uint32_t byteToWord(uint8_t * bytes, uint8_t len)
{
  uint32_t word = 0;
  uint8_t i = 0;

  for (i = 0; i < len; i++)
  {
    word <<= 8;
    word |= bytes[i];
  }

  return word;
}

void readStoredPara(void)
{
  uint32_t data[PARA_SIZE];
  uint32_t index = 0;
  uint32_t sign = 0;

  #ifdef I2C_EEPROM  // added I2C_EEPROM suppport for MKS_TFT35_V1_0
    EEPROM_FlashRead((uint8_t*)data, PARA_SIZE);
  #else
    HAL_FlashRead((uint8_t*)data, PARA_SIZE);
  #endif

  sign = data[index++];

  if (sign == TSC_SIGN)
  {
    paraStatus |= PARA_TSC_EXIST;  // if the touch screen calibration parameter already exists

    for (int i = 0; i < sizeof(TS_CalPara) / sizeof(TS_CalPara[0]); i++)
    {
      TS_CalPara[i] = data[index++];
    }
  }

  sign = data[index++];

  if (sign != PARA_SIGN)  // if the settings parameter is illegal, reset settings parameter
  {
    paraStatus |= PARA_NOT_STORED;
    initSettings();
  }
  else
  {
    memcpy(&infoSettings, &data[index], sizeof(SETTINGS));
    //if ((paraStatus & PARA_TSC_EXIST) == 0) infoSettings.rotated_ui = DISABLED;  // unecessarily rotates UI to Default?
  }
}

void storePara(void)
{
  uint32_t data[PARA_SIZE];
  uint32_t index = 0;

  memset(data, 0xFF, sizeof(data)); // initialise buffer to unwritten flash memory
  data[index++] = TSC_SIGN;

  for (int i = 0; i < sizeof(TS_CalPara) / sizeof(TS_CalPara[0]); i++)
  {
    data[index++] = TS_CalPara[i];
  }

  data[index++] = PARA_SIGN;

  memcpy(&data[index], &infoSettings, sizeof(SETTINGS));
  #ifdef I2C_EEPROM                      // added I2C_EEPROM suppport for MKS_TFT35_V1_0
    EEPROM_FlashWrite((uint8_t*)data, PARA_SIZE);  // store settings in I2C_EEPROM
  #else
    HAL_FlashWrite((uint8_t*)data, PARA_SIZE);
  #endif
}

bool readIsTSCExist(void)
{
  return ((paraStatus & PARA_TSC_EXIST) != 0);
}

bool readIsNotStored(void)
{
  return ((paraStatus & PARA_NOT_STORED) != 0);
}

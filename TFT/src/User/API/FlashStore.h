#ifndef _FLASH_STORE_H_
#define _FLASH_STORE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#define EMPTY_FLASH_WORD 0xFFFFFFFF

#define PARA_SIZE (128 * 2)  // max size of settings buffer to read/write

void readStoredPara(void);  // read settings parameter if exist, or reset settings parameter
void storePara(void);
bool readIsTSCExist(void);
bool readIsNotStored(void);

#ifdef __cplusplus
}
#endif

#endif

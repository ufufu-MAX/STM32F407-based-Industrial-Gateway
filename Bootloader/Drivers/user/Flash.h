#ifndef _FLASH_H__
#define _FLASH_H__

#include <stdint.h>

// image_size：准备写入内部 APP 区的 BIN 字节数。
uint8_t Flash_Erase_APP(uint32_t image_size);

// flash_address：内部 Flash 目标地址。
// data：待写入数据的地址。
// word_count：需要写入多少个 32 位 word，不是字节数。
uint8_t Flash_Write(uint32_t flash_address,
					const uint32_t *data,
					uint32_t word_count);

#endif

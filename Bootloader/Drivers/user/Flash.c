#include "main.h"
#include "Flash.h"

static uint32_t Flash_GetSector(uint32_t address)
{
	// 根据内部 Flash 地址，找出它属于 STM32F407 的哪个 Sector。
	if (address < 0x08004000U) return FLASH_SECTOR_0;
	if (address < 0x08008000U) return FLASH_SECTOR_1;
	if (address < 0x0800C000U) return FLASH_SECTOR_2;
	if (address < 0x08010000U) return FLASH_SECTOR_3;
	if (address < 0x08020000U) return FLASH_SECTOR_4;
	if (address < 0x08040000U) return FLASH_SECTOR_5;
	if (address < 0x08060000U) return FLASH_SECTOR_6;
	if (address < 0x08080000U) return FLASH_SECTOR_7;
	if (address < 0x080A0000U) return FLASH_SECTOR_8;
	if (address < 0x080C0000U) return FLASH_SECTOR_9;
	if (address < 0x080E0000U) return FLASH_SECTOR_10;
	return FLASH_SECTOR_11;
}

uint8_t Flash_Erase_APP(uint32_t image_size)
{
	FLASH_EraseInitTypeDef flash_config; // HAL 擦除函数需要的 Sector 配置
	uint32_t sector_error;               // 擦除失败时，HAL 在这里返回出错 Sector
	uint32_t last_address;               // BIN 写入内部 Flash 后占用的最后一个地址
	uint32_t last_sector;                // 最后一个地址所在的 Sector 编号
	uint8_t result = 0U;                 // 函数结果：0=失败，1=成功

	if ((image_size == 0U) || (image_size > 0x000F0000U))
	{
		return 0U;
	}

	last_address = 0x08010000U + image_size - 1U;
	last_sector = Flash_GetSector(last_address);

	flash_config.Banks = FLASH_BANK_1;
	flash_config.NbSectors = last_sector - FLASH_SECTOR_4 + 1U;
	flash_config.Sector = FLASH_SECTOR_4;
	flash_config.TypeErase = FLASH_TYPEERASE_SECTORS;
	flash_config.VoltageRange = FLASH_VOLTAGE_RANGE_3;

	HAL_FLASH_Unlock();

	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
						   FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
						   FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

	if (HAL_FLASHEx_Erase(&flash_config, &sector_error) == HAL_OK)
	{
		result = 1U;
	}

	HAL_FLASH_Lock();
	return result;
}

uint8_t Flash_Write(uint32_t flash_address,
					const uint32_t *data,
					uint32_t word_count)
{
	uint32_t index;       // 当前正在写第几个 32 位 word
	uint8_t result = 1U; // 函数结果：默认成功，遇到错误时改成 0

	if ((data == 0) ||
		((flash_address & 0x3U) != 0U) ||
		(flash_address < 0x08010000U) ||
		((flash_address + word_count * 4U) > 0x08100000U))
	{
		return 0U;
	}

	HAL_FLASH_Unlock();

	for (index = 0U; index < word_count; index++)
	{
		if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
							  flash_address,
							  data[index]) != HAL_OK)
		{
			result = 0U;
			break;
		}

		// 写后立即读回，发现错误就停止。
		if (*(__IO uint32_t *)flash_address != data[index])
		{
			result = 0U;
			break;
		}

		flash_address += 4U;
	}

	HAL_FLASH_Lock();
	return result;
}

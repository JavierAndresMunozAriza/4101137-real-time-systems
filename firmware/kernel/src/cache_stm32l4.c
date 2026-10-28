/*
 * Zephyr turns the L4's ART accelerator on for us in soc_early_init_hook().
 * This undoes it before the application starts, so the same binary can be
 * measured with and without the flash cache.
 */

#include <zephyr/init.h>
#include <stm32l4xx_ll_system.h>

static int lab_flash_cache_off(void)
{
	LL_FLASH_DisableInstCache();
	LL_FLASH_DisableDataCache();
	return 0;
}

SYS_INIT(lab_flash_cache_off, PRE_KERNEL_1, 0);

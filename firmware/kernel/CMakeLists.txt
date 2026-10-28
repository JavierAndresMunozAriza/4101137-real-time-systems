cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(soilsense_superloop)

target_sources(app PRIVATE src/main.c)
target_sources_ifdef(CONFIG_LAB_FLASH_CACHE_OFF app PRIVATE src/cache_stm32l4.c)

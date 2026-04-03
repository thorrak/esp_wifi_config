# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wifi_cfg_webui_customize)

# Create LittleFS partition image from www directory
littlefs_create_partition_image(storage www FLASH_IN_PROJECT)

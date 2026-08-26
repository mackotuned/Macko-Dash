cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(PROJECT_VER "1.1.8")

add_compile_options(-Wno-format)
project(mackodash)

spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)

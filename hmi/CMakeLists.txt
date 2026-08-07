# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# Exclude old freemodbus component which exists in older IDF versions
set(EXCLUDE_COMPONENTS freemodbus)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(bist_hmi)

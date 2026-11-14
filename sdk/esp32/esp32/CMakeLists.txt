cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Build only main and its explicit dependency graph. No LCD/camera/demo stack.
idf_build_set_property(MINIMAL_BUILD ON)
project(h1_wifi)

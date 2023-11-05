cmake_minimum_required(VERSION 3.1...3.27)
project(
  VMMTest
  VERSION 1.0
  LANGUAGES C CXX)

set (CMAKE_C_STANDARD 23)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fdiagnostics-color=always")

add_executable(vmmtest main.c vm/fault.c vm/resident.c vm/swapper.c vm/vad.c vm/tables.c vm/ws.c)
target_include_directories(vmmtest PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/kdk)

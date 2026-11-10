cmake_minimum_required(VERSION 3.16)
project(r00tshot VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(R00T_DEFS UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
  add_compile_options(/W3 /utf-8 /EHsc /permissive-)
else()
  add_compile_options(-Wall -Wno-unused-parameter)
endif()

add_compile_definitions(${R00T_DEFS})

set(R00T_LIBS gdiplus gdi32 user32 msimg32 shell32 shlwapi ole32 uuid advapi32)

add_executable(r00tshot WIN32
  src/main.cpp
  src/overlay.cpp
  src/capture.cpp
  src/util.cpp
  res/r00tshot.rc)

target_include_directories(r00tshot PRIVATE src res)
target_link_libraries(r00tshot PRIVATE ${R00T_LIBS})

add_executable(r00tshot-setup
  src/installer.cpp
  src/util.cpp)

target_include_directories(r00tshot-setup PRIVATE src)
target_link_libraries(r00tshot-setup PRIVATE ${R00T_LIBS})

set_target_properties(r00tshot r00tshot-setup PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin
  RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)

if(MINGW)
  target_link_options(r00tshot PRIVATE -municode -static)
  target_link_options(r00tshot-setup PRIVATE -static)
endif()

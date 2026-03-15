cmake_minimum_required(VERSION 3.16.0)
set(IDF_COMPONENT_MANAGER 1)
set(SECURE_BOOT_VERIFICATION_KEY_SOURCE
	"${CMAKE_SOURCE_DIR}/signature_verification_key.bin")
set(SECURE_BOOT_VERIFICATION_KEY_DEST
	"${CMAKE_BINARY_DIR}/signature_verification_key.bin")
if(EXISTS "${SECURE_BOOT_VERIFICATION_KEY_SOURCE}")
  file(COPY "${SECURE_BOOT_VERIFICATION_KEY_SOURCE}"
	   DESTINATION "${CMAKE_BINARY_DIR}")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "3.0.12")
project(SC_WiFi_toWM---Copy-SunnyDay_AntiG4th-main)

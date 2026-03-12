# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.cpp)

idf_component_register(
	SRCS ${app_sources}
	INCLUDE_DIRS
		${CMAKE_SOURCE_DIR}/src
		${CMAKE_SOURCE_DIR}/src/compat
		${CMAKE_SOURCE_DIR}/src/config
		${CMAKE_SOURCE_DIR}/src/control
		${CMAKE_SOURCE_DIR}/src/hardware
		${CMAKE_SOURCE_DIR}/src/network
		${CMAKE_SOURCE_DIR}/src/storage
		${CMAKE_SOURCE_DIR}/src/utils
	EMBED_TXTFILES
		${CMAKE_SOURCE_DIR}/certs/server_cert.pem
		${CMAKE_SOURCE_DIR}/certs/server_key.pem
	REQUIRES esp_http_server esp_https_ota mdns littlefs json
)

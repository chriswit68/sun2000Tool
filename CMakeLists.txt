cmake_minimum_required(VERSION 3.10)
project(sun2000Tool)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_path(LIBMODBUS_INCLUDE_DIR modbus/modbus.h
	PATHS /opt/homebrew/include /usr/local/include)
find_library(LIBMODBUS_LIBRARY modbus
	PATHS /opt/homebrew/lib /usr/local/lib)
if(NOT LIBMODBUS_INCLUDE_DIR OR NOT LIBMODBUS_LIBRARY)
	message(FATAL_ERROR "libmodbus wurde nicht gefunden. Installiere libmodbus und setze bei Bedarf LIBMODBUS_INCLUDE_DIR und LIBMODBUS_LIBRARY.")
endif()

add_library(modbus_scanner src/network_scanner.cpp)
target_include_directories(modbus_scanner PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${LIBMODBUS_INCLUDE_DIR})
target_link_libraries(modbus_scanner PUBLIC ${LIBMODBUS_LIBRARY})
target_compile_options(modbus_scanner PRIVATE -Wall -Wextra)

add_executable(${PROJECT_NAME}
	src/main.cpp
	src/modbus_client.cpp
	src/output.cpp
	src/sun2000_poller.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE modbus_scanner)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)

include(CTest)
if(BUILD_TESTING)
	add_executable(sun2000Tool-Tests tests/tests.cpp)
	target_link_libraries(sun2000Tool-Tests PRIVATE modbus_scanner)
	target_compile_options(sun2000Tool-Tests PRIVATE -Wall -Wextra)
	add_test(NAME sun2000Tool-Tests COMMAND sun2000Tool-Tests)

	add_executable(sun2000Tool-Output-Tests
		tests/output_tests.cpp
		src/output.cpp)
	target_include_directories(sun2000Tool-Output-Tests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src)
	target_compile_options(sun2000Tool-Output-Tests PRIVATE -Wall -Wextra)
	add_test(NAME sun2000Tool-Output-Tests COMMAND sun2000Tool-Output-Tests)
endif()

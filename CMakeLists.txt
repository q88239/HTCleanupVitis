cmake_minimum_required(VERSION 3.13)

project(open_htj2k LANGUAGES CXX DESCRIPTION "Open source implementation of 15444-1 and 15444-15")
# option(BUILD_SHARED_LIBS "Shared Libraries" ON)
enable_language(CXX)

#### Compiler settings
set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "${CMAKE_HOST_SYSTEM_PROCESSOR}")

if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
	if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^[xX]86_64$")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:AVX2 /EHsc /D \"_CRT_SECURE_NO_WARNINGS\"")
	else()
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /D \"_CRT_SECURE_NO_WARNINGS\"")
	endif()
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} /Od /DDEBUG /ZI")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} /Ox")
	set(CMAKE_CXX_FLAGS_RelWithDebInfo "${CMAKE_CXX_FLAGS} /O2 /ZI")
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU|Intel")
	if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^[xX]86_64$")
		set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-Wformat -fno-omit-frame-pointer")
	else()
		set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-Wall -Wformat -Wextra -Wconversion -Wsign-conversion -Wunused-parameter")
	endif()
endif()


if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -O0 -g")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O2")
	if (NOT MINGW)
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
	endif()
	set(CMAKE_CXX_FLAGS_RelWithDebInfo "${CMAKE_CXX_FLAGS} -O2 -g")
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -O0 -g")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O2")
	if (NOT MINGW)
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
	endif()
	set(CMAKE_CXX_FLAGS_RelWithDebInfo "${CMAKE_CXX_FLAGS} -O2 -g")
endif()

#add_executable(test_bench ht_cleanup_encode.cpp ht_cleanup_encode_tb.cpp)
add_executable(test_bench ht_cleanup_encode.cpp)

# This is a cmake script. Process it with the CMake gui or command line utility
# to produce makefiles / Visual Studio project files on Mac OS X and Windows.
#
# To configure the build options either use the CMake gui, or run the command
# line utility including the "-i" option.

cmake_minimum_required(VERSION 3.20)
project(mosquitto)
set (VERSION 2.0.20)

cmake_policy(SET CMP0042 NEW)
# 禁用 -Werror=undef
if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-undef -Wno-unused-function)
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/")

add_definitions (-DCMAKE -DVERSION=\"${VERSION}\")

if (WIN32)
	add_definitions("-D_CRT_SECURE_NO_WARNINGS")
	add_definitions("-D_CRT_NONSTDC_NO_DEPRECATE")
endif (WIN32)

if(APPLE)
        set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -undefined dynamic_lookup")
endif(APPLE)

include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_FLAGS_DEBUG "-Og -fno-omit-frame-pointer")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -DNDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -fno-omit-frame-pointer ${MARCH} -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 ${MARCH} -DNDEBUG")
if (CX_PLATFORM STREQUAL "darwin")
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -gdwarf")
	set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -gdwarf")
else()
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb3")
	set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -ggdb3")
endif()

set(CMAKE_CXX_FLAGS_DEBUG ${CMAKE_C_FLAGS_DEBUG})
set(CMAKE_CXX_FLAGS_MINSIZEREL ${CMAKE_C_FLAGS_MINSIZEREL})
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO ${CMAKE_C_FLAGS_RELWITHDEBINFO})
set(CMAKE_CXX_FLAGS_RELEASE ${CMAKE_C_FLAGS_RELEASE})

# 设置默认传输协议为 QUIC
set(WITH_TRANSPORT "QUIC" CACHE STRING "Choose transport protocol: QUIC or TCP")
set_property(CACHE WITH_TRANSPORT PROPERTY STRINGS "QUIC" "TCP")

# 定义全局选项
option(WITH_STATIC_LIBRARIES "Build static versions of the libmosquitto/pp libraries?" OFF)
option(WITH_PIC "Build the static library with PIC (Position Independent Code) enabled archives?" OFF)
option(WITH_BUNDLED_DEPS "Build with bundled dependencies?" ON)
option(WITH_THREADING "Include client library threading support?" ON)
option(WITH_DLT "Include DLT support?" OFF)
option(WITH_CJSON "Build with cJSON support (required for dynamic security plugin and useful for mosquitto_sub)?" ON)

# 定义TCP相关的选项
option(WITH_SRV "Include SRV lookup support?" OFF)
option(WITH_TLS "Include SSL/TLS support?" ON)
option(WITH_TLS_PSK "Include TLS-PSK support (requires WITH_TLS)?" ON)
option(WITH_EC "Include Elliptic Curve support (requires WITH_TLS)?" ON)
option(WITH_UNIX_SOCKETS "Include Unix Domain Socket support?" ON)
option(WITH_SOCKS "Include SOCKS5 support?" ON)

function(configure_quic)
	add_definitions("-DWITH_QUIC")

	if (WIN32)
		set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
		set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /NODEFAULTLIB:libucrt.lib /DEFAULTLIB:ucrt.lib")
		set(QUIC_TLS "schannel" CACHE STRING "TLS Library to use")
	else()
		set(QUIC_TLS "openssl" CACHE STRING "TLS Library to use")
	endif()

    set(QUIC_BUILD_SHARED ON CACHE BOOL "Builds MsQuic as a dynamic library" FORCE)
    if(WITH_STATIC_LIBRARIES)
        set(QUIC_BUILD_SHARED OFF CACHE BOOL "Builds MsQuic as a static library" FORCE)
    endif()

	set(QUIC_ENABLE_LOGGING ON CACHE BOOL "Enable MsQuic logging")

	add_subdirectory(third_party/msquic)
endfunction()

function(configure_tcp)
	add_definitions("-DWITH_TCP")

	if (WITH_TLS)
		find_package(OpenSSL REQUIRED)
		add_definitions("-DWITH_TLS")

		# mosquitto uses OpenSSL 1.1 API, so set OPENSSL_API_COMPAT accordingly:
		# https://www.openssl.org/docs/manmaster/man7/OPENSSL_API_COMPAT.html
		# TODO: migrate off ENGINE API (deprecated since OpenSSL 3.0), see:
		#       https://www.openssl.org/docs/manmaster/man7/migration_guide.html#Engines-and-METHOD-APIs
		add_definitions("-DOPENSSL_API_COMPAT=0x10100000L")

		if (WITH_TLS_PSK)
			add_definitions("-DWITH_TLS_PSK")
		endif (WITH_TLS_PSK)

		if (WITH_EC)
			add_definitions("-DWITH_EC")
		endif (WITH_EC)
	else (WITH_TLS)
		set (OPENSSL_INCLUDE_DIR "")
	endif (WITH_TLS)

	if (WITH_UNIX_SOCKETS AND NOT WIN32)
		add_definitions("-DWITH_UNIX_SOCKETS")
	endif (WITH_UNIX_SOCKETS AND NOT WIN32)

	if (WITH_SOCKS)
		add_definitions("-DWITH_SOCKS")
	endif (WITH_SOCKS)
endfunction()

# 根据 WITH_TRANSPORT 选择配置
if (WITH_TRANSPORT STREQUAL "QUIC")
    configure_quic()
elseif (WITH_TRANSPORT STREQUAL "TCP")
    configure_tcp()
else()
    message(FATAL_ERROR "Invalid value for WITH_TRANSPORT: ${WITH_TRANSPORT}. Choose 'QUIC' or 'TCP'.")
endif()

# 配置线程支持
if (WITH_THREADING)
	add_definitions("-DWITH_THREADING")
	if(WIN32)
		find_package(Pthreads4W REQUIRED)
	endif()
endif (WITH_THREADING)

message(STATUS "WITH_DLT = ${WITH_DLT}")

# 配置 DLT 支持
if (WITH_DLT)
	find_package(DLT REQUIRED)
	find_package(PkgConfig)
	pkg_check_modules(DLT "automotive-dlt >= 2.11")
	add_definitions("-DWITH_DLT")
endif (WITH_DLT)

# 配置 cJSON 支持
if (WITH_CJSON)
	# 添加外置cjson库
	if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
		add_subdirectory(third_party/cjson)
		set(CJSON_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party/cjson)
		set(CJSON_LIBRARY cjson)
		include_directories(${CJSON_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/third_party)
	endif()
    FIND_PACKAGE(cJSON)
    if (CJSON_FOUND)
	    message(STATUS ${CJSON_FOUND})
    else (CJSON_FOUND)
	    message(STATUS "Optional dependency cJSON not found. Some features will be disabled.")
    endif(CJSON_FOUND)
endif()

# ========================================
# Include projects
# ========================================

option(WITH_CLIENTS "Build clients?" OFF)
option(WITH_BROKER "Build broker?" OFF)
option(WITH_BENCHMARKS "Build benchmarks?" ON)
option(WITH_APPS "Build apps?" OFF)
option(WITH_PLUGINS "Build plugins?" OFF)
option(DOCUMENTATION "Build documentation?" OFF)

add_subdirectory(lib)
if (WITH_CLIENTS)
	add_subdirectory(client)
endif (WITH_CLIENTS)

if (WITH_BROKER)
	add_subdirectory(src)
endif (WITH_BROKER)

if (WITH_BENCHMARKS)
	add_subdirectory(benchmark_mqtt_quic)
endif (WITH_BENCHMARKS)

if (WITH_APPS)
	add_subdirectory(apps)
endif (WITH_APPS)

if (WITH_PLUGINS)
	add_subdirectory(plugins)
endif (WITH_PLUGINS)

if (DOCUMENTATION)
	add_subdirectory(man)
endif (DOCUMENTATION)

# ========================================
# Install config file
# ========================================

if (WITH_BROKER)
	install(FILES mosquitto.conf aclfile.example pskfile.example pwfile.example DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/mosquitto")
endif (WITH_BROKER)

# ========================================
# Install pkg-config files
# ========================================

configure_file(libmosquitto.pc.in libmosquitto.pc @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libmosquitto.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
configure_file(libmosquittopp.pc.in libmosquittopp.pc @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libmosquittopp.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

# ========================================
# Testing
# ========================================
enable_testing()

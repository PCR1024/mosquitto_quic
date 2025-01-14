# 设置目标系统名称和架构
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 指定交叉编译器路径
set(QL_TOOLCHAIN_DIR /opt/ql_crosstools/ql-ag550qcn-le20-gcc820-v1-toolchain/gcc/usr/bin/arm-oe-linux-gnueabi)  # 根据实际更改 
set(GNU_MACHINE ${QL_TOOLCHAIN_DIR}/arm-oe-linux-gnu)
set(FLOAT_ABI_SUFFIX eabi)
set(CMAKE_C_COMPILER ${GNU_MACHINE}${FLOAT_ABI_SUFFIX}-gcc)
set(CMAKE_CXX_COMPILER ${GNU_MACHINE}${FLOAT_ABI_SUFFIX}-g++)

# 设置 sysroot
set(QL_SDK_DIR /home/whiz/Projects/Shangyu/ql-ol-extsdk-ag551qcnabr03a18m8g_ocpu)  # 根据实际更改 
set(CMAKE_FIND_ROOT_PATH ${QL_SDK_DIR}/ql-sysroots)
set(CMAKE_SYSROOT ${QL_SDK_DIR}/ql-sysroots)

# 设置 openssldir
set(QL_OPENSSL_DIR /usr/lib/ssl)  # 根据实际更改 

# 设置编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv7-a -marm -mfpu=neon -mfloat-abi=hard -Werror -Wall -Wundef -finline-functions -finline-limit=64" CACHE STRING "C flags")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv7-a -marm -mfpu=neon -mfloat-abi=hard -Werror -Wall -Wundef -finline-functions -finline-limit=64" CACHE STRING "C++ flags")

# 设置头文件搜索路径
include_directories(
  ${CMAKE_SYSROOT}/include
  ${CMAKE_SYSROOT}/usr/include
  ${CMAKE_SYSROOT}/usr/include/ql_lib_utils
  ${CMAKE_SYSROOT}/usr/include/alsa-intf
  ${CMAKE_SYSROOT}/usr/include/qmi-framework
  ${CMAKE_SYSROOT}/usr/include/qlsyslog
  ${CMAKE_SYSROOT}/usr/include/ql-sdk
)


# 设置库文件搜索路径
link_directories(
  ${CMAKE_ROOTFS}/usr
  ${CMAKE_ROOTFS}/usr/lib
)

# 设置查找库和头文件的模式
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
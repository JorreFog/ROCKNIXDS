# Cross toolchain for the RG DS / RG DS Plus (RK3566: Cortex-A55), static so one binary runs on any
# firmware. Ubuntu/Debian: apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-a55")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-a55")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

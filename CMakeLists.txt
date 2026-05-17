# 指定CMake所需的最低版本号为3.16
cmake_minimum_required(VERSION 3.16)

# 定义项目名称
project(edgeFlowMirror)

# 设置项目路径为构建目录下的项目文件夹
set(PROJECT_PATH ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME})  # 修正：原${PROJECT}应为${PROJECT_NAME}
# 设置安装路径前缀为项目路径
set(CMAKE_INSTALL_PREFIX ${PROJECT_PATH})
# 设置CMake模块搜索路径为当前源代码目录下的modules文件夹
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/modules)

# Maya版本配置
# 如果定义了MAYA_VERSION变量
if(DEFINED MAYA_VERSION)
    # 设置Maya版本字符串为传入的MAYA_VERSION
    set(MAYA_VERSION_STRING ${MAYA_VERSION})
else()
    # 否则，将默认Maya版本设置为"2022"
    set(MAYA_VERSION_STRING "2022")
endif()

# AVX优化标志设置
# 如果是Windows系统
if (WIN32)
    # Windows平台使用AVX编译器标志
    set(COMPILE_FLAGS "/arch:AVX")
else()
    # 非Windows系统（Linux/macOS）使用AVX编译器标志
    set(COMPILE_FLAGS "-mavx")
endif()

# 查找Maya库
# 查找并加载Maya库，REQUIRED表示如果找不到将报错
find_package(Maya REQUIRED)
find_package(OpenGL REQUIRED)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 源文件列表定义（核心修改）
# 递归查找src目录下所有.cpp文件
file(GLOB_RECURSE SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"  # 可选：头文件是否加入，不影响编译但便于IDE识别
)

# 确保至少找到一个源文件，避免空编译
if(NOT SOURCE_FILES)
    message(FATAL_ERROR "未在src目录下找到任何.cpp或.h文件，请检查路径是否正确！")
endif()

# 创建共享库（动态链接库）
# 创建名为PROJECT_NAME的共享库，使用上述源文件列表
add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})
# 链接Maya库到当前项目
target_link_libraries(${PROJECT_NAME} PRIVATE Maya::Maya OpenGL::GL)
# 添加Maya库的头文件包含路径（Maya::Maya已包含，此行可省略）
target_include_directories(${PROJECT_NAME} PRIVATE ${MAYA_INCLUDE_DIR})
# 添加AVX优化编译选项
target_compile_options(${PROJECT_NAME} PRIVATE ${COMPILE_FLAGS})

# 注册为Maya插件
# 调用自定义的MAYA_PLUGIN函数，设置插件特有的编译属性
MAYA_PLUGIN(${PROJECT_NAME})

# 安装配置
# 将构建目标（插件）安装到plug-ins目录
install(TARGETS ${PROJECT_NAME} ${MAYA_TARGET_TYPE} DESTINATION plug-ins)
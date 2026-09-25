cmake_minimum_required(VERSION 3.21.1)

option(LINK_INSIGHT "Link Qt Insight Tracker library" ON)
option(BUILD_QDS_COMPONENTS "Build design studio components" ON)

project(KestrelApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_property(_kestrel_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT _kestrel_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "CMake build type" FORCE)
endif()

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /permissive- /Zc:__cplusplus")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml)
set(QML_IMPORT_PATH ${QT_QML_OUTPUT_DIRECTORY}
    CACHE STRING "Import paths for Qt Creator's code model" FORCE)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Concurrent Gui Network Widgets Qml Quick QuickTimeline ShaderTools)
qt_standard_project_setup()

option(KESTREL_BUILD_TESTS "Build Kestrel project-format tests" OFF)
if (KESTREL_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    add_executable(ProjectFlightsTest
        tests/ProjectFlightsTest.cpp
        src/project/ProjectLoader.h
        src/project/ProjectLoader.cpp)
    target_include_directories(ProjectFlightsTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(ProjectFlightsTest PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME ProjectFlightsTest COMMAND ProjectFlightsTest)
endif()

qt_add_executable(${CMAKE_PROJECT_NAME})
qt_add_resources(${CMAKE_PROJECT_NAME} "configuration"
    PREFIX "/"
    FILES qtquickcontrols2.conf)

include(qds)
if (BUILD_QDS_COMPONENTS)
    include(qmlcomponents OPTIONAL)
endif()
if (LINK_INSIGHT)
    include(insight OPTIONAL)
endif()

include(GNUInstallDirs)
install(TARGETS ${CMAKE_PROJECT_NAME}
    BUNDLE DESTINATION . COMPONENT KestrelRuntime
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT KestrelRuntime
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT KestrelRuntime)

qt_generate_deploy_qml_app_script(
    TARGET ${CMAKE_PROJECT_NAME}
    OUTPUT_SCRIPT kestrel_deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${kestrel_deploy_script} COMPONENT KestrelRuntime)

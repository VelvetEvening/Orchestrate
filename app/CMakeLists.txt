cmake_minimum_required(VERSION 3.21)

project(Orchestrate VERSION 1.1.2 LANGUAGES CXX)

configure_file(src/version.h.in generated/version.h @ONLY)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11.2 REQUIRED COMPONENTS Widgets Sql Network)

add_library(OrchestrateUpdate STATIC
    src/update/releaseinfo.cpp src/update/releaseinfo.h
    src/update/updatewidget.cpp src/update/updatewidget.h
    src/platform/instancelock.cpp src/platform/instancelock.h
)
target_link_libraries(OrchestrateUpdate PUBLIC Qt6::Widgets Qt6::Network)
target_include_directories(OrchestrateUpdate PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(OrchestrateAutoStart STATIC src/platform/autostart.cpp src/platform/autostart.h)
target_link_libraries(OrchestrateAutoStart PUBLIC Qt6::Widgets)
target_include_directories(OrchestrateAutoStart PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(OrchestrateShortcut STATIC src/platform/globalshortcut.cpp src/platform/globalshortcut.h)
target_link_libraries(OrchestrateShortcut PUBLIC Qt6::Widgets)
target_include_directories(OrchestrateShortcut PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if (WIN32)
    target_link_libraries(OrchestrateShortcut PRIVATE user32 kernel32)
endif()

add_library(OrchestrateText STATIC
    src/widgets/headingtext.cpp
    src/widgets/headingtext.h
    src/widgets/headingtextedit.cpp
    src/widgets/headingtextedit.h
)
target_link_libraries(OrchestrateText PUBLIC Qt6::Widgets)
target_include_directories(OrchestrateText PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

qt_add_executable(Orchestrate WIN32
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/data/appdatabase.cpp
    src/data/appdatabase.h
    src/pages/calendarpage.cpp
    src/pages/calendarpage.h
    src/pages/projectspage.cpp
    src/pages/projectspage.h
    src/pages/automationpage.cpp
    src/pages/automationpage.h
    src/pages/placeholderpage.cpp
    src/pages/placeholderpage.h
    resources/resources.qrc
)

if (WIN32)
    target_sources(Orchestrate PRIVATE resources/app.rc)
    # MinGW's resource compiler does not track included headers in Ninja.
    # Rebuild Explorer's version resource whenever the CMake version changes.
    set_property(SOURCE resources/app.rc APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h")
    # 标题栏配色通过 DWM 设置
    target_link_libraries(Orchestrate PRIVATE dwmapi)
endif()

target_link_libraries(Orchestrate PRIVATE Qt6::Widgets Qt6::Sql OrchestrateText OrchestrateShortcut OrchestrateAutoStart OrchestrateUpdate)

target_include_directories(Orchestrate PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pages
)

if (WIN32)
    set_target_properties(Orchestrate PROPERTIES
        WIN32_EXECUTABLE TRUE
        OUTPUT_NAME "Orchestrate"
        VERSION "${PROJECT_VERSION}"
    )
endif()

# 内置工具随程序一起放在 <程序目录>/tools/，Orchestrate 启动时自动同步。
# 只复制脚本和声明，工具运行时写在 tools/<工具>/state/ 里的状态不会被覆盖。
add_custom_target(SyncBuiltinTools
    COMMAND ${CMAKE_COMMAND}
            "-DTOOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tools"
            "-DTOOL_DESTINATION=$<TARGET_FILE_DIR:Orchestrate>/tools"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncBuiltinTools.cmake"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/updater" "$<TARGET_FILE_DIR:Orchestrate>/updater"
    COMMENT "Syncing built-in tools and updater without runtime state"
    VERBATIM
)
add_dependencies(Orchestrate SyncBuiltinTools)

install(TARGETS Orchestrate
    RUNTIME DESTINATION bin
)
install(DIRECTORY tools/
    DESTINATION bin/tools
    PATTERN "state" EXCLUDE
)
install(DIRECTORY updater/ DESTINATION bin/updater)

include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Pinned, offline source dependencies. See README.md for versions and local changes.
add_library(OrchestrateMicroTeX STATIC)
foreach(folder atom box core fonts utils res/builtin res/font res/parser res/reg res/sym)
    file(GLOB sources CONFIGURE_DEPENDS "microtex/src/${folder}/*.cpp")
    target_sources(OrchestrateMicroTeX PRIVATE ${sources})
endforeach()
target_sources(OrchestrateMicroTeX PRIVATE
    microtex/src/latex.cpp microtex/src/render.cpp microtex/src/platform/qt/graphic_qt.cpp
    tinyxml2/tinyxml2.cpp)
target_include_directories(OrchestrateMicroTeX PUBLIC microtex/src PRIVATE tinyxml2)
target_compile_definitions(OrchestrateMicroTeX PUBLIC BUILD_QT)
target_link_libraries(OrchestrateMicroTeX PUBLIC Qt6::Gui)
set_target_properties(OrchestrateMicroTeX PROPERTIES AUTOMOC OFF)

# All math fonts travel inside the worker executable, independent of cwd/network.
file(GLOB_RECURSE math_fonts "${CMAKE_CURRENT_SOURCE_DIR}/microtex/res/fonts/*.ttf")
qt_add_resources(OrchestrateMicroTeX math_fonts PREFIX "/orchestrate-math"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/microtex"
    FILES ${math_fonts})

install(FILES microtex/LICENSE DESTINATION bin/licenses/microtex)
install(DIRECTORY microtex/res/fonts/licences/ DESTINATION bin/licenses/microtex/fonts)
install(FILES tinyxml2/LICENSE.txt DESTINATION bin/licenses/tinyxml2)

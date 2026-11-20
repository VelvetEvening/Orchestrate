add_executable(BuiltinIdentityTest
    test_builtin_identity.cpp
)
target_include_directories(BuiltinIdentityTest PRIVATE ../src)
target_link_libraries(BuiltinIdentityTest PRIVATE Qt6::Core Qt6::Sql)
# The production database lives next to Orchestrate.exe; keep fixtures elsewhere.
set_target_properties(BuiltinIdentityTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/builtin-identity-test>"
)
add_test(NAME builtin_identity COMMAND BuiltinIdentityTest)

add_executable(DatabaseMigrationTest test_database_migration.cpp)
target_include_directories(DatabaseMigrationTest PRIVATE ../src)
target_link_libraries(DatabaseMigrationTest PRIVATE Qt6::Core Qt6::Sql)
set_target_properties(DatabaseMigrationTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/database-migration-test>"
)
add_test(NAME database_migration COMMAND DatabaseMigrationTest)
add_executable(DatabaseSchemaTest test_database_schema.cpp)
target_include_directories(DatabaseSchemaTest PRIVATE ../src)
target_link_libraries(DatabaseSchemaTest PRIVATE Qt6::Core Qt6::Sql)
set_target_properties(DatabaseSchemaTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/database-schema-test>")
add_test(NAME database_schema COMMAND DatabaseSchemaTest)

# Exercise the real window, manifest parser, SQLite and UI handlers, offscreen.
# Only the manifest is staged: this fixture must never contain shutdown scripts.
find_package(Qt6 6.11.2 REQUIRED COMPONENTS Test)
add_executable(ManifestParserTest test_manifest_parser.cpp)
target_link_libraries(ManifestParserTest PRIVATE OrchestrateAutomation Qt6::Test)
set_target_properties(ManifestParserTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/manifest-parser-test>")
add_test(NAME manifest_parser COMMAND ManifestParserTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/manifest-parser-test/results.txt,txt" -o -,txt)
add_executable(MarkdownEditTest test_markdown_edit.cpp)
target_link_libraries(MarkdownEditTest PRIVATE OrchestrateText Qt6::Test)
set_target_properties(MarkdownEditTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/markdown-edit-test>")
add_test(NAME markdown_edit COMMAND MarkdownEditTest -o "${CMAKE_CURRENT_BINARY_DIR}/markdown-edit-test/results.txt,txt")
set_tests_properties(markdown_edit PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
add_executable(MathPreviewTest test_math_preview.cpp)
target_link_libraries(MathPreviewTest PRIVATE OrchestrateText Qt6::Test)
set_target_properties(MathPreviewTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/math-preview-test>")
add_test(NAME math_preview COMMAND MathPreviewTest -o "${CMAKE_CURRENT_BINARY_DIR}/math-preview-test/results.txt,txt")
set_tests_properties(math_preview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
add_executable(ReleaseInfoTest test_release_info.cpp)
target_link_libraries(ReleaseInfoTest PRIVATE OrchestrateUpdate Qt6::Test)
set_target_properties(ReleaseInfoTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/release-info-test>")
add_test(NAME release_info COMMAND ReleaseInfoTest)
set_tests_properties(release_info PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_executable(UpdateCheckerTest test_update_checker.cpp)
target_link_libraries(UpdateCheckerTest PRIVATE OrchestrateUpdate Qt6::Test)
set_target_properties(UpdateCheckerTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/update-checker-test>")
add_test(NAME update_checker COMMAND UpdateCheckerTest)
set_tests_properties(update_checker PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
add_executable(AutoStartTest test_autostart.cpp)
target_link_libraries(AutoStartTest PRIVATE OrchestrateAutoStart Qt6::Test)
set_target_properties(AutoStartTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/autostart-test>")
add_test(NAME autostart COMMAND AutoStartTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/autostart-test/results.txt,txt" -o -,txt)
add_executable(AutomationContractTest test_automation_contract.cpp)
target_include_directories(AutomationContractTest PRIVATE ../src)
target_compile_definitions(AutomationContractTest PRIVATE ORCHESTRATE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(AutomationContractTest PRIVATE Qt6::Widgets Qt6::Sql Qt6::Test OrchestrateUi)
set_target_properties(AutomationContractTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/automation-contract-test>")
add_test(NAME automation_contract COMMAND AutomationContractTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/automation-contract-test/results.txt,txt" -o -,txt)
set_tests_properties(automation_contract PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
add_test(NAME tool_copy COMMAND ${CMAKE_COMMAND} "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/tool-copy-test"
    "-DSYNC_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/../cmake/SyncBuiltinTools.cmake" -P "${CMAKE_CURRENT_SOURCE_DIR}/test_tool_copy.cmake")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Git REQUIRED)
if(Python3_Interpreter_FOUND)
    add_test(NAME adapter_example COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_adapter_example.py")
endif()
add_executable(BuiltinAcceptanceTest test_builtin_acceptance.cpp)
target_include_directories(BuiltinAcceptanceTest PRIVATE ../src)
target_link_libraries(BuiltinAcceptanceTest PRIVATE Qt6::Widgets Qt6::Sql Qt6::Test OrchestrateText OrchestrateShortcut OrchestrateAutoStart OrchestrateUpdate OrchestrateClipboard)
if (WIN32)
    target_link_libraries(BuiltinAcceptanceTest PRIVATE dwmapi)
endif()
set_target_properties(BuiltinAcceptanceTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/builtin-acceptance-test>"
)
# configure_file tracks changes to the manifest even when no C++ file changed.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/scheduled-shutdown/orchestrate-tool.json"
    "${CMAKE_CURRENT_BINARY_DIR}/builtin-acceptance-test/tools/scheduled-shutdown/orchestrate-tool.json"
    COPYONLY
)
add_test(NAME builtin_acceptance COMMAND BuiltinAcceptanceTest)
set_tests_properties(builtin_acceptance PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 90
    RUN_SERIAL TRUE
)

add_executable(HeadingTextTest test_heading_text.cpp)
target_include_directories(HeadingTextTest PRIVATE ../src)
target_link_libraries(HeadingTextTest PRIVATE OrchestrateText OrchestrateShortcut OrchestrateAutoStart OrchestrateUpdate OrchestrateClipboard Qt6::Sql Qt6::Test)
if (WIN32)
    target_link_libraries(HeadingTextTest PRIVATE dwmapi)
endif()
set_target_properties(HeadingTextTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/heading-text-test>"
)
add_test(NAME heading_text COMMAND HeadingTextTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/heading-text-test/artifacts/results.txt,txt")
set_tests_properties(heading_text PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

add_executable(ClipboardTest test_clipboard.cpp)
target_include_directories(ClipboardTest PRIVATE ../src)
target_link_libraries(ClipboardTest PRIVATE OrchestrateClipboard OrchestrateText OrchestrateShortcut OrchestrateAutoStart OrchestrateUpdate Qt6::Test)
set_target_properties(ClipboardTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/clipboard-test>")
add_test(NAME clipboard COMMAND ClipboardTest -o "${CMAKE_CURRENT_BINARY_DIR}/clipboard-test/results.txt,txt" -o -,txt)
add_custom_target(SyncMathTestWorkers ALL DEPENDS OrchestrateMath)
foreach(math_test MarkdownEditTest MathPreviewTest HeadingTextTest BuiltinAcceptanceTest ClipboardTest)
    add_custom_command(TARGET SyncMathTestWorkers POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${math_test}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:OrchestrateMath>" "$<TARGET_FILE_DIR:${math_test}>/OrchestrateMath.exe")
    add_dependencies(${math_test} SyncMathTestWorkers)
endforeach()
set_tests_properties(clipboard PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
if (WIN32)
    target_link_libraries(ClipboardTest PRIVATE dwmapi)
endif()
if (WIN32)
    # Qt's bin directory also contains the MinGW runtime shipped with this Qt kit.
    target_link_libraries(MathPreviewTest PRIVATE psapi)
    string(REPLACE ";" "\;" test_path "$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
    add_executable(GlobalShortcutTest test_global_shortcut.cpp)
    target_link_libraries(GlobalShortcutTest PRIVATE OrchestrateApplication Qt6::Test user32)
    set_target_properties(GlobalShortcutTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/global-shortcut-test>")
    add_test(NAME global_shortcut COMMAND GlobalShortcutTest
        -o "${CMAKE_CURRENT_BINARY_DIR}/global-shortcut-test/results.txt,txt" -o -,txt)
    set_tests_properties(global_shortcut PROPERTIES ENVIRONMENT "PATH=${test_path}" TIMEOUT 30)
    add_executable(StartupVisibilityTest test_startup_visibility.cpp)
    target_link_libraries(StartupVisibilityTest PRIVATE Qt6::Core Qt6::Sql Qt6::Test user32)
    target_compile_definitions(StartupVisibilityTest PRIVATE ORCHESTRATE_APPLICATION="$<TARGET_FILE:Orchestrate>")
    set_target_properties(StartupVisibilityTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/startup-visibility-test>")
    add_test(NAME startup_visibility COMMAND StartupVisibilityTest
        -o "${CMAKE_CURRENT_BINARY_DIR}/startup-visibility-test/results.txt,txt" -o -,txt)
    set_tests_properties(startup_visibility PROPERTIES ENVIRONMENT "PATH=${test_path}" TIMEOUT 40 RUN_SERIAL TRUE)
    set_tests_properties(builtin_identity PROPERTIES ENVIRONMENT "PATH=${test_path}")
    set_property(TEST math_preview APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
    set_property(TEST markdown_edit APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
    set_tests_properties(database_migration PROPERTIES ENVIRONMENT "PATH=${test_path}")
    set_tests_properties(database_schema PROPERTIES ENVIRONMENT "PATH=${test_path}")
    set_tests_properties(manifest_parser PROPERTIES ENVIRONMENT "PATH=${test_path}")
    set_tests_properties(autostart PROPERTIES ENVIRONMENT "PATH=${test_path}")
    set_property(TEST release_info APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
    set_property(TEST update_checker APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
    target_link_libraries(UpdateCheckerTest PRIVATE psapi)
    set_property(TEST automation_contract APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
    set_property(TEST builtin_acceptance APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
    set_property(TEST heading_text APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
    set_property(TEST clipboard APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
    find_program(WINDOWS_POWERSHELL NAMES powershell.exe REQUIRED)
    if (WINDOWS_POWERSHELL)
        add_test(NAME update_backup COMMAND "${WINDOWS_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/Test-UpdateBackup.ps1")
        add_test(NAME portable_update COMMAND "${WINDOWS_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/Test-PortableUpdate.ps1")
        add_test(NAME update_tasks COMMAND "${WINDOWS_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/Test-UpdateTasks.ps1")
        add_test(NAME update_startup COMMAND "${WINDOWS_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/Test-UpdateStartup.ps1"
            -Application "$<TARGET_FILE:Orchestrate>")
        set_tests_properties(update_startup PROPERTIES ENVIRONMENT "PATH=${test_path};QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
        add_test(NAME shutdown_process_identity
            COMMAND "${WINDOWS_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/Test-ShutdownProcessIdentity.ps1")
        add_test(NAME shutdown_result_codes COMMAND "${WINDOWS_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/Test-ShutdownResultCodes.ps1")
        add_test(NAME shutdown_task_settings COMMAND "${WINDOWS_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/Test-ShutdownTaskSettings.ps1")
        add_test(NAME shutdown_disable
            COMMAND "${WINDOWS_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/Test-ShutdownDisable.ps1")
    endif()
endif()

foreach(git_consumer BuiltinAcceptanceTest HeadingTextTest ClipboardTest)
    target_link_libraries(${git_consumer} PRIVATE OrchestrateGit)
endforeach()

add_executable(GitHistoryTest test_git_history.cpp)
target_link_libraries(GitHistoryTest PRIVATE OrchestrateGit OrchestrateText Qt6::Sql Qt6::Test)
set_target_properties(GitHistoryTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/git-history-test>")
add_test(NAME git_history COMMAND GitHistoryTest -o "${CMAKE_CURRENT_BINARY_DIR}/git-history-test/results.txt,txt" -o -,txt)
set_tests_properties(git_history PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=${test_path}" TIMEOUT 120)

# v3.4.0 navigation uses the actual module pages and synthetic portable data.
add_executable(WorkspaceNavigationTest test_workspace_navigation.cpp)
target_include_directories(WorkspaceNavigationTest PRIVATE ../src)
target_link_libraries(WorkspaceNavigationTest PRIVATE OrchestrateText OrchestrateShortcut OrchestrateAutoStart OrchestrateUpdate OrchestrateClipboard OrchestrateGit Qt6::Sql Qt6::Test)
if(WIN32)
    target_link_libraries(WorkspaceNavigationTest PRIVATE dwmapi)
endif()
set_target_properties(WorkspaceNavigationTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/workspace-navigation-test>")
add_custom_command(TARGET WorkspaceNavigationTest POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:OrchestrateMath>" "$<TARGET_FILE_DIR:WorkspaceNavigationTest>/OrchestrateMath.exe")
add_test(NAME workspace_navigation COMMAND WorkspaceNavigationTest -o "${CMAKE_CURRENT_BINARY_DIR}/workspace-navigation-test/results.txt,txt" -o -,txt)
set_tests_properties(workspace_navigation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=${test_path}" TIMEOUT 90)

add_executable(ConfigurableToolsTest test_configurable_tools.cpp)
target_include_directories(ConfigurableToolsTest PRIVATE ../src)
target_link_libraries(ConfigurableToolsTest PRIVATE Qt6::Widgets Qt6::Sql Qt6::Test OrchestrateUi)
set_target_properties(ConfigurableToolsTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/configurable-tools-test>")
add_test(NAME configurable_tools COMMAND ConfigurableToolsTest -o "${CMAKE_CURRENT_BINARY_DIR}/configurable-tools-test/results.txt,txt" -o -,txt)
set_tests_properties(configurable_tools PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
if (WIN32)
    set_property(TEST configurable_tools APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
endif()

# Reuse the production targets; each test compiles only its own test source.
foreach(database_test BuiltinIdentityTest DatabaseMigrationTest DatabaseSchemaTest)
    target_link_libraries(${database_test} PRIVATE OrchestrateData)
endforeach()
foreach(application_test AutomationContractTest BuiltinAcceptanceTest HeadingTextTest
        ClipboardTest GitHistoryTest WorkspaceNavigationTest ConfigurableToolsTest)
    target_link_libraries(${application_test} PRIVATE OrchestrateApplication)
endforeach()

add_executable(SkinLibraryTest test_skin_library.cpp)
target_link_libraries(SkinLibraryTest PRIVATE OrchestrateUi Qt6::Test)
add_test(NAME skin_library COMMAND SkinLibraryTest)

add_executable(ThemeRenderingTest test_theme_rendering.cpp)
target_link_libraries(ThemeRenderingTest PRIVATE OrchestrateText Qt6::Test)
add_test(NAME theme_rendering COMMAND ThemeRenderingTest)

set_tests_properties(skin_library theme_rendering PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
if(WIN32)
    set_property(TEST skin_library theme_rendering APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
endif()

if(WIN32)
    add_executable(PageTransitionNativeTest test_page_transition.cpp)
    target_link_libraries(PageTransitionNativeTest PRIVATE OrchestrateUi Qt6::Test user32)
    set_target_properties(PageTransitionNativeTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/page-transition-native-test>")
    add_test(NAME page_transition_native COMMAND PageTransitionNativeTest
        -o "${CMAKE_CURRENT_BINARY_DIR}/page-transition-native-test/results.txt,txt" -o -,txt)
    set_tests_properties(page_transition_native PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows;PATH=${test_path}" TIMEOUT 30 RUN_SERIAL TRUE)
endif()

get_property(test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(test_target IN LISTS test_targets)
    get_target_property(test_type ${test_target} TYPE)
    if(test_type STREQUAL "EXECUTABLE")
        orchestrate_enable_warnings(${test_target})
    endif()
endforeach()

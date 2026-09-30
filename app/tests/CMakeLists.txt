add_executable(BuiltinIdentityTest
    test_builtin_identity.cpp
    ../src/data/appdatabase.cpp
    ../src/data/appdatabase.h
)
target_include_directories(BuiltinIdentityTest PRIVATE ../src)
target_link_libraries(BuiltinIdentityTest PRIVATE Qt6::Core Qt6::Sql)
# The production database lives next to Orchestrate.exe; keep fixtures elsewhere.
set_target_properties(BuiltinIdentityTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/builtin-identity-test>"
)
add_test(NAME builtin_identity COMMAND BuiltinIdentityTest)

add_executable(DatabaseMigrationTest test_database_migration.cpp ../src/data/appdatabase.cpp)
target_include_directories(DatabaseMigrationTest PRIVATE ../src)
target_link_libraries(DatabaseMigrationTest PRIVATE Qt6::Core Qt6::Sql)
set_target_properties(DatabaseMigrationTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/database-migration-test>"
)
add_test(NAME database_migration COMMAND DatabaseMigrationTest)
add_executable(DatabaseSchemaTest test_database_schema.cpp ../src/data/appdatabase.cpp)
target_include_directories(DatabaseSchemaTest PRIVATE ../src)
target_link_libraries(DatabaseSchemaTest PRIVATE Qt6::Core Qt6::Sql)
set_target_properties(DatabaseSchemaTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/database-schema-test>")
add_test(NAME database_schema COMMAND DatabaseSchemaTest)

# Exercise the real window, manifest parser, SQLite and UI handlers, offscreen.
# Only the manifest is staged: this fixture must never contain shutdown scripts.
find_package(Qt6 6.11.2 REQUIRED COMPONENTS Test)
add_executable(ReleaseInfoTest test_release_info.cpp)
target_link_libraries(ReleaseInfoTest PRIVATE OrchestrateUpdate Qt6::Test)
set_target_properties(ReleaseInfoTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/release-info-test>")
add_test(NAME release_info COMMAND ReleaseInfoTest)
set_tests_properties(release_info PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_executable(AutoStartTest test_autostart.cpp)
target_link_libraries(AutoStartTest PRIVATE OrchestrateAutoStart Qt6::Test)
set_target_properties(AutoStartTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/autostart-test>")
add_test(NAME autostart COMMAND AutoStartTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/autostart-test/results.txt,txt" -o -,txt)
add_executable(AutomationContractTest test_automation_contract.cpp ../src/pages/automationpage.cpp ../src/data/appdatabase.cpp)
target_include_directories(AutomationContractTest PRIVATE ../src)
target_compile_definitions(AutomationContractTest PRIVATE ORCHESTRATE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(AutomationContractTest PRIVATE Qt6::Widgets Qt6::Sql Qt6::Test)
set_target_properties(AutomationContractTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/automation-contract-test>")
add_test(NAME automation_contract COMMAND AutomationContractTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/automation-contract-test/results.txt,txt" -o -,txt)
set_tests_properties(automation_contract PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
add_test(NAME tool_copy COMMAND ${CMAKE_COMMAND} "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/tool-copy-test"
    "-DSYNC_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/../cmake/SyncBuiltinTools.cmake" -P "${CMAKE_CURRENT_SOURCE_DIR}/test_tool_copy.cmake")
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME adapter_example COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_adapter_example.py")
endif()
add_executable(BuiltinAcceptanceTest
    test_builtin_acceptance.cpp
    ../src/mainwindow.cpp
    ../src/mainwindow.h
    ../src/data/appdatabase.cpp
    ../src/data/appdatabase.h
    ../src/pages/automationpage.cpp
    ../src/pages/automationpage.h
    ../src/pages/calendarpage.cpp
    ../src/pages/calendarpage.h
    ../src/pages/projectspage.cpp
    ../src/pages/projectspage.h
    ../resources/resources.qrc
)
target_include_directories(BuiltinAcceptanceTest PRIVATE ../src)
target_link_libraries(BuiltinAcceptanceTest PRIVATE Qt6::Widgets Qt6::Sql Qt6::Test OrchestrateText OrchestrateShortcut OrchestrateAutoStart OrchestrateUpdate)
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

add_executable(HeadingTextTest
    test_heading_text.cpp
    ../src/mainwindow.cpp
    ../src/mainwindow.h
    ../src/data/appdatabase.cpp
    ../src/data/appdatabase.h
    ../src/pages/automationpage.cpp
    ../src/pages/automationpage.h
    ../src/pages/calendarpage.cpp
    ../src/pages/calendarpage.h
    ../src/pages/projectspage.cpp
    ../src/pages/projectspage.h
    ../resources/resources.qrc
)
target_include_directories(HeadingTextTest PRIVATE ../src)
target_link_libraries(HeadingTextTest PRIVATE OrchestrateText OrchestrateShortcut OrchestrateAutoStart OrchestrateUpdate Qt6::Sql Qt6::Test)
if (WIN32)
    target_link_libraries(HeadingTextTest PRIVATE dwmapi)
endif()
set_target_properties(HeadingTextTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/heading-text-test>"
)
add_test(NAME heading_text COMMAND HeadingTextTest
    -o "${CMAKE_CURRENT_BINARY_DIR}/heading-text-test/artifacts/results.txt,txt")
set_tests_properties(heading_text PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
if (WIN32)
    # Qt's bin directory also contains the MinGW runtime shipped with this Qt kit.
    string(REPLACE ";" "\;" test_path "$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
    set_tests_properties(builtin_identity PROPERTIES ENVIRONMENT "PATH=${test_path}")
    set_tests_properties(database_migration PROPERTIES ENVIRONMENT "PATH=${test_path}")
    set_tests_properties(database_schema PROPERTIES ENVIRONMENT "PATH=${test_path}")
    set_tests_properties(autostart PROPERTIES ENVIRONMENT "PATH=${test_path}")
    set_property(TEST release_info APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
    set_property(TEST automation_contract APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
    set_property(TEST builtin_acceptance APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
    set_property(TEST heading_text APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
    find_program(WINDOWS_POWERSHELL NAMES powershell.exe)
    if (WINDOWS_POWERSHELL)
        add_test(NAME portable_update COMMAND "${WINDOWS_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/Test-PortableUpdate.ps1")
        add_test(NAME shutdown_process_identity
            COMMAND "${WINDOWS_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/Test-ShutdownProcessIdentity.ps1")
        add_test(NAME shutdown_disable
            COMMAND "${WINDOWS_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/Test-ShutdownDisable.ps1")
    endif()
endif()

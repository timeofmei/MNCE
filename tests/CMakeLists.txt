find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Test)

add_executable(application_smoke_test application-smoke-test.cpp)
target_link_libraries(
    application_smoke_test
    PRIVATE
        mnce_project_options
        mnce_ui
        Qt6::Core
        Qt6::Sql
        Qt6::Test
        Qt6::Widgets
)
set_target_properties(
    application_smoke_test
    PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
        CXX_EXTENSIONS OFF
)

add_test(NAME application_smoke_test COMMAND application_smoke_test)

if(WIN32)
    get_target_property(qt_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(qt_bin_directory "${qt_qmake_executable}" DIRECTORY)
    set_tests_properties(
        application_smoke_test
        PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${qt_bin_directory}"
    )
endif()

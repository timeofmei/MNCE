find_package(Qt6 6.10.2 REQUIRED COMPONENTS Test)

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

add_executable(file_hash_service_test file-hash-service-test.cpp)
target_link_libraries(
    file_hash_service_test
    PRIVATE mnce_project_options mnce_services Qt6::Core Qt6::Test
)
set_target_properties(file_hash_service_test PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
add_test(NAME file_hash_service_test COMMAND file_hash_service_test)

add_executable(media_repository_test media-repository-test.cpp)
target_link_libraries(
    media_repository_test
    PRIVATE mnce_project_options mnce_persistence mnce_services Qt6::Core Qt6::Sql Qt6::Test Qt6::Widgets
)
set_target_properties(media_repository_test PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
add_test(NAME media_repository_test COMMAND media_repository_test)

add_executable(series_service_test series-service-test.cpp)
target_link_libraries(
    series_service_test
    PRIVATE mnce_project_options mnce_persistence mnce_services Qt6::Core Qt6::Sql Qt6::Test
)
set_target_properties(series_service_test PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
add_test(NAME series_service_test COMMAND series_service_test)

add_executable(media_library_ui_test media-library-ui-test.cpp)
target_link_libraries(
    media_library_ui_test
    PRIVATE mnce_project_options mnce_ui Qt6::Core Qt6::Sql Qt6::Test Qt6::Widgets
)
set_target_properties(media_library_ui_test PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
add_test(NAME media_library_ui_test COMMAND media_library_ui_test)

add_executable(playback_domain_test playback-domain-test.cpp)
target_link_libraries(
    playback_domain_test
    PRIVATE mnce_project_options mnce_domain Qt6::Core Qt6::Test
)
set_target_properties(playback_domain_test PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
add_test(NAME playback_domain_test COMMAND playback_domain_test)

add_executable(playback_persistence_test playback-persistence-test.cpp)
target_link_libraries(
    playback_persistence_test
    PRIVATE mnce_project_options mnce_persistence Qt6::Core Qt6::Sql Qt6::Test
)
set_target_properties(playback_persistence_test PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
add_test(NAME playback_persistence_test COMMAND playback_persistence_test)

add_executable(player_controller_test player-controller-test.cpp)
target_link_libraries(
    player_controller_test
    PRIVATE mnce_project_options mnce_services Qt6::Core Qt6::Test
)
set_target_properties(player_controller_test PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
add_test(NAME player_controller_test COMMAND player_controller_test)

add_executable(media_playback_ui_test media-playback-ui-test.cpp)
target_link_libraries(
    media_playback_ui_test
    PRIVATE mnce_project_options mnce_ui Qt6::Core Qt6::Sql Qt6::Test Qt6::Widgets
)
set_target_properties(media_playback_ui_test PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
add_test(NAME media_playback_ui_test COMMAND media_playback_ui_test)

add_executable(window_frame_test window-frame-test.cpp)
target_link_libraries(
    window_frame_test
    PRIVATE mnce_project_options mnce_ui Qt6::Core Qt6::Test Qt6::Widgets
)
set_target_properties(window_frame_test PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
add_test(NAME window_frame_test COMMAND window_frame_test)

if(WIN32)
    get_target_property(qt_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(qt_bin_directory "${qt_qmake_executable}" DIRECTORY)
    set_tests_properties(
        application_smoke_test file_hash_service_test media_repository_test series_service_test
        media_library_ui_test playback_domain_test playback_persistence_test player_controller_test
        media_playback_ui_test window_frame_test
        PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${qt_bin_directory}"
    )
endif()

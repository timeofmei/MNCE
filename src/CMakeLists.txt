add_library(mnce_domain STATIC
    domain/media-item.cpp
    domain/media-item.h
    domain/target-language.cpp
    domain/target-language.h
)
target_link_libraries(mnce_domain PUBLIC Qt6::Core PRIVATE mnce_project_options)
target_include_directories(mnce_domain PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(mnce_domain PROPERTIES CXX_EXTENSIONS OFF)

add_library(mnce_persistence STATIC
    persistence/media-repository.cpp
    persistence/media-repository.h
)
target_link_libraries(
    mnce_persistence
    PUBLIC mnce_domain Qt6::Core Qt6::Sql
    PRIVATE mnce_project_options
)
target_include_directories(mnce_persistence PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(mnce_persistence PROPERTIES CXX_EXTENSIONS OFF)

add_library(mnce_services STATIC
    services/file-hash-service.cpp
    services/file-hash-service.h
    services/media-import-service.cpp
    services/media-import-service.h
    services/media-refresh-service.cpp
    services/media-refresh-service.h
)
target_link_libraries(
    mnce_services
    PUBLIC mnce_domain mnce_persistence Qt6::Core
    PRIVATE mnce_project_options
)
target_include_directories(mnce_services PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(mnce_services PROPERTIES CXX_EXTENSIONS OFF)

add_library(mnce_ui STATIC
    ui/main-window.cpp
    ui/main-window.h
)
target_link_libraries(
    mnce_ui
    PUBLIC
        mnce_domain
        mnce_persistence
        mnce_services
        Qt6::Concurrent
        Qt6::Widgets
    PRIVATE
        mnce_project_options
)
target_include_directories(mnce_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(
    mnce_ui
    PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
        CXX_EXTENSIONS OFF
)

add_executable(mnce WIN32 main.cpp)
target_link_libraries(
    mnce
    PRIVATE
        mnce_project_options
        mnce_ui
        Qt6::Core
        Qt6::Multimedia
        Qt6::Network
        Qt6::Sql
        Qt6::Widgets
)
set_target_properties(
    mnce
    PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
        CXX_EXTENSIONS OFF
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.timeofmei.mnce"
)

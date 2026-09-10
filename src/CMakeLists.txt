add_library(mnce_ui STATIC
    ui/main-window.cpp
    ui/main-window.h
)
target_link_libraries(
    mnce_ui
    PUBLIC
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
        Qt6::Concurrent
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
)

cmake_minimum_required(VERSION 3.16)
project(FestivalVirtualSinger VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(wxWidgets REQUIRED COMPONENTS core base adv)
include(${wxWidgets_USE_FILE})
find_package(ALSA REQUIRED)
find_package(Threads REQUIRED)

add_executable(FestivalVirtualSinger
    main.cpp
    event_timing.cpp
    festival_bridge_linux.cpp
    piano_roll.cpp
    song_model.cpp
    tone_preview.cpp
    sample_pool.cpp
    waveform_editor.cpp
    slice_model.cpp
    sample_engine.cpp
    slicer_piano_roll.cpp
    auto_slicer.cpp
    offline_renderer.cpp
)

target_include_directories(FestivalVirtualSinger PRIVATE ${ALSA_INCLUDE_DIRS})
target_link_libraries(FestivalVirtualSinger PRIVATE
    ${wxWidgets_LIBRARIES}
    ${ALSA_LIBRARIES}
    Threads::Threads
)

target_compile_definitions(FestivalVirtualSinger PRIVATE __WXGTK__)

include(GNUInstallDirs)
install(TARGETS FestivalVirtualSinger RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

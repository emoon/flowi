add_subdirectory(shared_smoke)
add_subdirectory(app_smoke)
add_subdirectory(dock_demo)
add_subdirectory(two_windows)
add_subdirectory(widget_gallery)

if(FLOWI_GPU_BACKEND AND NOT APPLE)
    add_subdirectory(gpu_window)
endif()

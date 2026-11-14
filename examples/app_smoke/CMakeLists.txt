# Uses the fl_app runner directly from C, in a window.
add_executable(app_smoke main.c)
target_link_libraries(app_smoke PRIVATE flowi_objects)
apply_target_config(app_smoke)

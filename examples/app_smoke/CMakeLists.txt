################################################################################
# examples/app_smoke — direct-C usage of the fl_app runner (windowed).
################################################################################
add_executable(app_smoke main.c)
target_link_libraries(app_smoke PRIVATE flowi_objects)
apply_target_config(app_smoke)

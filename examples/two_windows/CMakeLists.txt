# Two OS windows on one fl_app. GPU only; run from the repo root.
add_executable(two_windows main.c)
target_link_libraries(two_windows PRIVATE flowi_objects)
apply_target_config(two_windows)

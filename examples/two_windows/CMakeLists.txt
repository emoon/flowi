################################################################################
# examples/two_windows — two real OS windows on one fl_app (multi-window). GPU-only; run from the repo root.
################################################################################
add_executable(two_windows main.c)
target_link_libraries(two_windows PRIVATE flowi_objects)
apply_target_config(two_windows)

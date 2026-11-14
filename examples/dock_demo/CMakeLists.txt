# Builds a first-run dock layout in code and drives the panel-author API from one workspace.
# GPU only; run from the repo root.
add_executable(dock_demo main.c)
target_link_libraries(dock_demo PRIVATE flowi_objects)
apply_target_config(dock_demo)

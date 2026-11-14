# Widget gallery on the fl_app runner. The renderer is chosen at runtime (--gpu, the default, or --software).
# Run from the repo root.
add_executable(widget_gallery main.c)
target_link_libraries(widget_gallery PRIVATE flowi_objects)
apply_target_config(widget_gallery)

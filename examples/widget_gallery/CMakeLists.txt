################################################################################
# examples/widget_gallery — fl_app runner gallery of the widgets. The renderer is chosen at runtime (--gpu default,
# --software); run from the repo root.
################################################################################
add_executable(widget_gallery main.c)
target_link_libraries(widget_gallery PRIVATE flowi_objects)
apply_target_config(widget_gallery)

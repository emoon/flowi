################################################################################
# examples/shared_smoke — links the shipped shared library (libflowi.so) and runs
# one headless frame end to end. This manual example links ONLY `flowi`
# (which propagates its public + runner include roots). No SDL, no GPU. Run manually from the build tree; it is
# deliberately not registered with CTest.
################################################################################
add_executable(shared_smoke main.c)
target_link_libraries(shared_smoke PRIVATE flowi)
apply_target_config(shared_smoke)

# Links only the shared library target `flowi` and runs one headless frame, with no SDL and no GPU.
# Run it by hand from the build tree; it is not registered with CTest.
add_executable(shared_smoke main.c)
target_link_libraries(shared_smoke PRIVATE flowi)
apply_target_config(shared_smoke)

# Target Configuration Helpers
#
# Warning flags on flowi's own targets (the vendored deps keep their own), and
# link-time completeness on shared libraries.

function(apply_target_config target_name)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE ${WARNING_FLAGS})
    endif()

    get_target_property(target_type ${target_name} TYPE)
    if(target_type STREQUAL "SHARED_LIBRARY")
        if(UNIX AND NOT APPLE)
            target_link_options(${target_name} PRIVATE -Wl,--no-undefined)
        elseif(APPLE)
            target_link_options(${target_name} PRIVATE -Wl,-undefined,error)
        endif()
    endif()
endfunction()

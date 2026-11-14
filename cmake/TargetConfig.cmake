# Applies the warning flags to flowi's own targets (vendored dependencies keep
# their own) and makes a shared library fail to link with undefined symbols.

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

add_library(uds STATIC
    src/uds_core.c
)

target_include_directories(uds PUBLIC
    include
)

target_compile_definitions(uds PRIVATE
    _POSIX_C_SOURCE=199309L
)

target_link_libraries(uds PUBLIC
    rte
)

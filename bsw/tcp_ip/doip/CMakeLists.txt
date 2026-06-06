add_library(doip STATIC
    src/doip.c
)

target_include_directories(doip PUBLIC
    include
)

target_link_libraries(doip PUBLIC
    socket_adapter
)

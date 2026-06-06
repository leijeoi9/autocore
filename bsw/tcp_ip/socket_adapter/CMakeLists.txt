add_library(socket_adapter STATIC
    src/socket_adapter.c
)

target_include_directories(socket_adapter PUBLIC
    include
)

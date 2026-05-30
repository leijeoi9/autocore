add_library(logger STATIC
    src/logger.c
)

target_include_directories(logger PUBLIC
    include
)

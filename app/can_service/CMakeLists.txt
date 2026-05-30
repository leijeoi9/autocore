add_executable(can_service
    src/main.c
)

target_link_libraries(can_service
    logger
)

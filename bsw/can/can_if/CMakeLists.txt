add_library(can_if STATIC
    src/can_router.c
    src/can_scheduler.c
    src/can_dispatcher.c
    src/can_filter.c
    src/can_signal.c
    src/can_db.c
    src/can_message.c
    src/can_message_db.c
    src/can_message_handler.c
)

target_include_directories(can_if PUBLIC
    include
)

target_link_libraries(can_if PUBLIC
    mcal_can
    rte
)

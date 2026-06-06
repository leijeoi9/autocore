add_library(can_driver STATIC
    src/can_driver.c
    src/can_signal.c
    src/can_db.c
    src/can_dispatcher.c
    src/can_scheduler.c
    src/can_filter.c
    src/can_router.c
    src/can_message.c
    src/can_message_db.c
    src/can_message_handler.c
)

target_include_directories(can_driver PUBLIC
    include
)

target_link_libraries(can_driver PUBLIC
    rte
)
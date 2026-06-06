add_library(rte STATIC
    src/rte_vehicle_data.c
    src/rte.c
)

target_include_directories(rte PUBLIC
    include
)

target_link_libraries(rte PUBLIC
    can_if
    mcal_can
)

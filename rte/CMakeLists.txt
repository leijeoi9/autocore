add_library(rte STATIC
    src/rte_vehicle_data.c
)

target_include_directories(rte PUBLIC
    include
)

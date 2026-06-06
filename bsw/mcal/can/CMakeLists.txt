add_library(mcal_can STATIC
    src/can_driver.c
)

target_include_directories(mcal_can PUBLIC
    include
)

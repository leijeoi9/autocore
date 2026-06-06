add_library(iso_tp STATIC
    src/iso_tp.c
)

target_include_directories(iso_tp PUBLIC
    include
)

target_link_libraries(iso_tp PUBLIC
    mcal_can
)

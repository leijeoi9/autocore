#include "can_signal.h"

can_signal_desc_t can_db[] = {
    {
        .can_id = 0x123,
        .start_bit = 0,
        .length = 16,
        .factor = 0.1,
        .offset = 0,
        .name = "engine_speed"
    },
    {
        .can_id = 0x123,
        .start_bit = 16,
        .length = 16,
        .factor = 0.1,
        .offset = 0,
        .name = "vehicle_speed"
    },
    {
    .can_id = 0x123,
    .start_bit = 32,
    .length = 16,
    .factor = 0.1,
    .offset = 0,
    .name = "engine_temp"
    }
};
const int can_db_size =
    sizeof(can_db) / sizeof(can_db[0]);
#include "can_message_db.h"
#include "can_message_handler.h"
#include <stddef.h>

can_message_desc_t can_message_db[] = {

    {
        .can_id = 0x100,
        .name = "ENGINE_STATUS",
        .handler = NULL
    },

    {
        .can_id = 0x200,
        .name = "VEHICLE_STATUS",
        .handler = NULL
    },

    {
        .can_id = 0x123,
        .name = "TEST_STATUS",
        .handler = test_message_handler
    }
};
const int can_message_db_size =
    sizeof(can_message_db) /
    sizeof(can_message_db[0]);

const can_message_desc_t *find_message_desc(int can_id)
{
    for (int i = 0; i < can_message_db_size; i++) {

        if (can_message_db[i].can_id == can_id)
            return &can_message_db[i];
    }

    return NULL;
}

const char *find_message_name(int can_id)
{
    const can_message_desc_t *desc;

    desc = find_message_desc(can_id);

    if (!desc)
        return "UNKNOWN";

    return desc->name;
}
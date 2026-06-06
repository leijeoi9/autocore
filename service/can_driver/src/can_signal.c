#include "can_signal.h"
#include "can_db.h"
#include <string.h>

static unsigned int extract_bits(const struct can_frame *frame,
                                 int start_bit,
                                 int length)
{
    unsigned int value = 0;

    for (int i = 0; i < length; i++) {
        int bit_index = start_bit + i;
        int byte_index = bit_index / 8;
        int bit_in_byte = bit_index % 8;

        if (frame->data[byte_index] & (1 << bit_in_byte)) {
            value |= (1 << i);
        }
    }

    return value;
}



void can_decode(const struct can_frame *frame, can_signal_t *sig)
{
    sig->engine_speed = 0;
    sig->vehicle_speed = 0;
    sig->engine_temp = 0;
    for (int i = 0; i < can_db_size; i++) {

        
        if (can_db[i].can_id == frame->can_id) {

            unsigned int raw =
                extract_bits(frame,
                              can_db[i].start_bit,
                              can_db[i].length);

            float value =
                (raw * can_db[i].factor) + can_db[i].offset;

            if (strcmp(can_db[i].name, "engine_speed") == 0)
                sig->engine_speed = value;
            else if (strcmp(can_db[i].name, "vehicle_speed") == 0)
                sig->vehicle_speed = value;
            else if (strcmp(can_db[i].name, "engine_temp") == 0){
                sig->engine_temp = value;}
            
        }
    }
}
#ifndef RTE_VEHICLE_DATA_H
#define RTE_VEHICLE_DATA_H

typedef struct {

    float engine_speed;

    float vehicle_speed;

    float engine_temp;

} rte_vehicle_data_t;

extern rte_vehicle_data_t g_vehicle_data;

#endif

#include "logger.h"

int main(void)
{
    log_info("CAN", "can service start");
    log_warn("CAN", "test warning");
    log_error("CAN", "test error");

    return 0;
}
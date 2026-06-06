#include <stdio.h>
#include "logger.h"

void log_info(const char *module, const char *msg)
{
    printf("[INFO][%s] %s\n", module, msg);
}

void log_warn(const char *module, const char *msg)
{
    printf("[WARN][%s] %s\n", module, msg);
}

void log_error(const char *module, const char *msg)
{
    printf("[ERROR][%s] %s\n", module, msg);
}
#ifndef LOGGER_H
#define LOGGER_H

void log_info(const char *module, const char *msg);
void log_warn(const char *module, const char *msg);
void log_error(const char *module, const char *msg);

#endif
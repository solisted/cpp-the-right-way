#ifndef SL_LOG_H
#define SL_LOG_H

#define SL_LOG_MAX_MESSAGE_LENGTH 1024
#define SL_LOG_MAX_DATE_LENGTH      64
#define SL_LOG_MAX_PID_LENGTH       11
#define SL_LOG_MAX_ARG_LENGTH       11
#define SL_LOG_MAX_IP_LENGTH        16
#define SL_LOG_MAX_PORT_LENGTH       6

#include <stdarg.h>
#include <sys/types.h>
#include <netinet/in.h>

#include "sl_arena.h"

typedef enum sl_log_level sl_log_level;

typedef struct sl_log sl_log;

enum sl_log_level {
    SL_LOG_DEBUG,
    SL_LOG_INFO,
    SL_LOG_ERROR,
    SL_LOG_MAX
};

struct sl_log {
    sl_log_level min_level;
    int log_fd;
    char pid[SL_LOG_MAX_PID_LENGTH];
    char ip_address[SL_LOG_MAX_IP_LENGTH];
    char ip_port[SL_LOG_MAX_PORT_LENGTH];
};

void sl_log_init(sl_log *log, sl_log_level min_level, int log_fd);
void sl_log_write(sl_log *log, sl_log_level level, char *message);
void sl_log_write_format(sl_arena *arena, sl_log *log, sl_log_level level, char *format, ...);
void sl_log_write_buffer(sl_log *log, sl_log_level level, char *message, size_t length);
void sl_log_set_pid(sl_log *log, pid_t pid);
void sl_log_set_ip_address_port(sl_log *log, struct sockaddr_in *address);

#endif

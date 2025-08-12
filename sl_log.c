#include "sl_log.h"

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>

#include <arpa/inet.h>

static char *sl_log_levels[SL_LOG_MAX] = {
    "[debug] ", "[info] ", "[error] "
};

static void sl_log_itoa(uint32_t value, char *buffer, size_t length)
{
    size_t value_length = 0;

    while (value > 0 && length-- > 0) {
        uint8_t digit = '0' + (value % 10);
        buffer[length] = digit;
        value /= 10;
        value_length ++;
    }

    for (int n = 0; n < value_length; n ++) {
        buffer[n] = buffer[n + length];
    }

    buffer[value_length] = 0;
}

static void sl_log_append(char *dest, size_t dest_size, char *src, size_t src_size, size_t *used_size)
{
    if (src_size > dest_size - *used_size) {
        src_size = dest_size - *used_size;
    }

    memcpy(dest + *used_size, src, src_size);
    *used_size += src_size;

    return;
}

void sl_log_init(sl_log *log, sl_log_level min_level, int log_fd)
{
    *log = (sl_log) {0};

    log->min_level = min_level;
    log->log_fd = log_fd;
}

void sl_log_write(sl_log *log, sl_log_level level, char *message, ...)
{
    if (level < log->min_level || level >= SL_LOG_MAX) {
        return;
    }

    char log_buffer[SL_LOG_MAX_MESSAGE_LENGTH] = {0};
    size_t log_buffer_size = 0;

    time_t timestamp = time(NULL);
    struct tm *local_time = localtime(&timestamp);
    char date_buffer[SL_LOG_MAX_DATE_LENGTH];
    size_t date_length = strftime(date_buffer, SL_LOG_MAX_DATE_LENGTH, "%D %T ", local_time);

    sl_log_append(log_buffer, SL_LOG_MAX_MESSAGE_LENGTH, date_buffer, date_length, &log_buffer_size);
    sl_log_append(log_buffer, SL_LOG_MAX_MESSAGE_LENGTH, sl_log_levels[level], strlen(sl_log_levels[level]), &log_buffer_size);

    if (log->pid[0] != 0) {
        sl_log_append(log_buffer, SL_LOG_MAX_MESSAGE_LENGTH, "#", 1, &log_buffer_size);
        sl_log_append(log_buffer, SL_LOG_MAX_MESSAGE_LENGTH, log->pid, strlen(log->pid), &log_buffer_size);
        sl_log_append(log_buffer, SL_LOG_MAX_MESSAGE_LENGTH, " ", 1, &log_buffer_size);
    }

    if (log->ip_address[0] != 0 && log->ip_port[0] != 0) {
        sl_log_append(log_buffer, SL_LOG_MAX_MESSAGE_LENGTH, log->ip_address, strlen(log->ip_address), &log_buffer_size);
        sl_log_append(log_buffer, SL_LOG_MAX_MESSAGE_LENGTH, ":", 1, &log_buffer_size);
        sl_log_append(log_buffer, SL_LOG_MAX_MESSAGE_LENGTH, log->ip_port, strlen(log->ip_port), &log_buffer_size);
        sl_log_append(log_buffer, SL_LOG_MAX_MESSAGE_LENGTH, " ", 1, &log_buffer_size);
    }

    va_list arguments;
    char *dollar_pointer = message;
    char argument[SL_LOG_MAX_ARG_LENGTH] = {0};

    for (dollar_pointer = message; *dollar_pointer != 0 && *dollar_pointer != '$'; dollar_pointer ++);

    if (*dollar_pointer == 0) {
        sl_log_append(log_buffer, SL_LOG_MAX_MESSAGE_LENGTH, message, strlen(message), &log_buffer_size);
    } else if (*dollar_pointer == '$') {
        va_start(arguments, message);
        sl_log_append(log_buffer, SL_LOG_MAX_MESSAGE_LENGTH, message, dollar_pointer - message, &log_buffer_size);
        sl_log_itoa(va_arg(arguments, uint32_t), argument, SL_LOG_MAX_ARG_LENGTH);
        sl_log_append(log_buffer, SL_LOG_MAX_MESSAGE_LENGTH, argument, strlen(argument), &log_buffer_size);
        sl_log_append(log_buffer, SL_LOG_MAX_MESSAGE_LENGTH, dollar_pointer + 1, strlen(dollar_pointer) - 1, &log_buffer_size);
        va_end(arguments);
    }

    if (errno > 0) {
        sl_log_append(log_buffer, SL_LOG_MAX_MESSAGE_LENGTH, ": ", 2, &log_buffer_size);
        char *error_message = strerror(errno);
        sl_log_append(log_buffer, SL_LOG_MAX_MESSAGE_LENGTH, error_message, strlen(error_message), &log_buffer_size);
        errno = 0;
    }

    sl_log_append(log_buffer, SL_LOG_MAX_MESSAGE_LENGTH, "\n", 1, &log_buffer_size);

    write(log->log_fd, log_buffer, log_buffer_size);
}

void sl_log_set_pid(sl_log *log, pid_t pid)
{
    sl_log_itoa(pid, log->pid, SL_LOG_MAX_PID_LENGTH);
}

void sl_log_set_ip_address_port(sl_log *log, struct sockaddr_in *address)
{
    memcpy(log->ip_address, inet_ntoa(address->sin_addr), SL_LOG_MAX_IP_LENGTH - 1);
    sl_log_itoa(ntohs(address->sin_port), log->ip_port, SL_LOG_MAX_PORT_LENGTH);
}

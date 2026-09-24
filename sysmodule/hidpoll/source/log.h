#pragma once

#define HP_LOG_PATH "sdmc:/config/hidpoll/log.txt"

void hp_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

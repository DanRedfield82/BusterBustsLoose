#pragma once
#include <stdbool.h>
#include <stdint.h>

void bg_start(const char* romPath);
bool bg_ready(void);
bool bg_failed(void);
void bg_wait(void);
const uint8_t* bg_state(int* size);
void bg_shutdown(void);

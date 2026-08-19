#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void warning_chime_init(void);
void warning_chime_process(uint32_t active_warning_mask);
void warning_chime_test(void);
void warning_chime_set_volume(int volume_percent);

#ifdef __cplusplus
}
#endif

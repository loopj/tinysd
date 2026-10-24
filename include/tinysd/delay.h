#pragma once

#include <stdint.h>

/**
 * Wait at least a given time. The application supplies this, and may spin or sleep.
 *
 * @param microseconds the time
 */
void tsd_delay_us(uint32_t microseconds);

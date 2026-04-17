#include "fake_hal.h"

/* adc_protocol.c currently depends only on HAL functions, which are provided
 * by fake_hal.c. This file keeps the dependency boundary explicit for future
 * protocol fakes without adding duplicate symbols today. */

/* Author: R.E. van Eekelen. Maintenance-only authenticated OTA. */
#pragma once
#include <stdbool.h>
void OtaUpdateTask(void *argument);
void OtaMarkValidTask(void *argument);
bool OtaUpdate_Active(void);

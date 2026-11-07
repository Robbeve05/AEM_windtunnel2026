/* Host-only calibration boundary; storage/CRC are covered separately. */
#include <stdbool.h>
bool FanCalibration_IsValid(void) { return false; }
float FanCalibration_GetBaseFrequency(float reference, float fallback)
{
    (void)reference;
    return fallback;
}

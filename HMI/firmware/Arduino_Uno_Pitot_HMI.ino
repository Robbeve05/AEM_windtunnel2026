#include <Wire.h>
#include <math.h>

const uint8_t ASPD_ADDRESS = 0x28;
const float AIR_DENSITY = 1.225f;
const float PSI_TO_PA = 6894.757f;
const float P_MAX_PSI = 1.0f;
const float P_MIN_PSI = -1.0f;
const int ZERO_SAMPLES = 200;
const int FILTER_SAMPLES = 10;
const float PRESSURE_DEADBAND = 5.0f;

float pressureOffsetPa = 0.0f;

bool readASPD(float &pressurePa, float &temperatureC)
{
    Wire.requestFrom(ASPD_ADDRESS, (uint8_t)4);
    if (Wire.available() != 4) return false;

    uint8_t data[4];
    for (int i = 0; i < 4; i++) data[i] = Wire.read();

    uint8_t status = (data[0] >> 6) & 0x03;
    if (status != 0) return false;

    uint16_t pressureRaw = ((uint16_t)(data[0] & 0x3F) << 8) | data[1];
    float pressurePsi = ((pressureRaw - (0.1f * 16383.0f))
        * (P_MAX_PSI - P_MIN_PSI) / (0.8f * 16383.0f)) + P_MIN_PSI;
    pressurePa = pressurePsi * PSI_TO_PA;

    uint16_t temperatureRaw = ((uint16_t)data[2] << 3) | (data[3] >> 5);
    temperatureC = (200.0f * temperatureRaw / 2047.0f) - 50.0f;
    return true;
}

void calibrateZero()
{
    float sum = 0.0f;
    int count = 0;
    for (int i = 0; i < ZERO_SAMPLES; i++) {
        float p, t;
        if (readASPD(p, t)) { sum += p; count++; }
        delay(10);
    }
    if (count > 0) pressureOffsetPa = sum / count;
}

bool getFilteredPressure(float &pressurePa, float &temperatureC)
{
    float pSum = 0.0f, tSum = 0.0f;
    int count = 0;
    for (int i = 0; i < FILTER_SAMPLES; i++) {
        float p, t;
        if (readASPD(p, t)) {
            pSum += p - pressureOffsetPa;
            tSum += t;
            count++;
        }
        delay(2);
    }
    if (count == 0) return false;
    pressurePa = pSum / count;
    temperatureC = tSum / count;
    return true;
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Wire.begin();
    Wire.setClock(100000);
    // IMPORTANT: keep the pitot tube out of the airflow during startup calibration.
    calibrateZero();
}

void loop()
{
    float pressurePa, temperatureC;
    if (getFilteredPressure(pressurePa, temperatureC)) {
        if (fabs(pressurePa) < PRESSURE_DEADBAND) pressurePa = 0.0f;
        float pressureForSpeed = fabs(pressurePa);
        float airspeedMs = pressureForSpeed > 0.0f
            ? sqrtf((2.0f * pressureForSpeed) / AIR_DENSITY)
            : 0.0f;
        float airspeedKmh = airspeedMs * 3.6f;

        // Machine-readable line for Raspberry Pi HMI.
        Serial.print("pressure_pa=");
        Serial.print(pressurePa, 2);
        Serial.print(",temperature_c=");
        Serial.print(temperatureC, 2);
        Serial.print(",airspeed_ms=");
        Serial.print(airspeedMs, 2);
        Serial.print(",airspeed_kmh=");
        Serial.println(airspeedKmh, 2);
    }
    delay(20);
}

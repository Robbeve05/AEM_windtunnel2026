from typing import Literal
from pydantic import BaseModel, Field, model_validator

from backend.fan_calibration import CalibrationConfig

# Independent validated ESP32 firmware/protocol ceilings; the JSON configuration
# may only LOWER these limits. Raising them needs coordinated firmware validation.
FIRMWARE_MAX_AIRSPEED_MS = 40.0
FIRMWARE_MAX_FREQUENCY_HZ = 50.0


class Tap(BaseModel):
    name: str = Field(pattern=r"^[A-Za-z][A-Za-z0-9_]{0,19}$")
    x_c: float = Field(ge=0, le=1, allow_inf_nan=False)
    surface: Literal['upper', 'lower'] = 'upper'


class Config(BaseModel):
    simulation: bool = True
    device_mode: Literal['esp32_tcp'] = 'esp32_tcp'
    esp32_host: str = '192.168.50.10'
    esp32_port: int = Field(default=5000, ge=1, le=65535)
    sample_rate_hz: float = Field(default=20, ge=1, le=100, allow_inf_nan=False)
    filter_cutoff_hz: float = Field(default=1.0, ge=0.1, le=10, allow_inf_nan=False)
    air_density: float = Field(default=1.225, gt=0, le=10, allow_inf_nan=False)
    max_airspeed_ms: float = Field(gt=0, le=FIRMWARE_MAX_AIRSPEED_MS, allow_inf_nan=False)
    min_manual_frequency_hz: float = Field(ge=0, lt=FIRMWARE_MAX_FREQUENCY_HZ, allow_inf_nan=False)
    max_manual_frequency_hz: float = Field(gt=0, le=FIRMWARE_MAX_FREQUENCY_HZ, allow_inf_nan=False)
    default_recording_rate_hz: int = Field(ge=1, le=100)
    flow_statistics_window_s: float = Field(default=5.0, ge=1, le=60, allow_inf_nan=False)
    calibration_defaults: CalibrationConfig
    data_directory: str = 'data/measurements'
    usb_mount_path: str = ''
    simulate_usb: bool = True
    flow_metrics_enabled: bool = True
    pressure_taps: list[Tap] = Field(default_factory=list, max_length=128)

    @model_validator(mode='after')
    def unique_taps(self):
        if not 0 <= self.min_manual_frequency_hz < self.max_manual_frequency_hz:
            raise ValueError('Configured manual frequency limits are invalid')
        if self.calibration_defaults.start_hz < self.min_manual_frequency_hz:
            raise ValueError('Calibration start frequency is below the configured manual minimum')
        if self.calibration_defaults.end_hz > self.max_manual_frequency_hz:
            raise ValueError('Calibration end frequency exceeds the configured manual maximum')
        if len({t.name for t in self.pressure_taps}) != len(self.pressure_taps):
            raise ValueError('Pressure tap names must be unique')
        return self


class ManualFrequency(BaseModel):
    hz: float = Field(ge=0, le=FIRMWARE_MAX_FREQUENCY_HZ, allow_inf_nan=False)

class ControlMode(BaseModel):
    mode: Literal['AUTO','MANUAL']

class Setpoint(BaseModel):
    airspeed_ms: float = Field(ge=0, allow_inf_nan=False)


class MeasurementRequest(BaseModel):
    name: str = Field(min_length=1, max_length=80, pattern=r'^[A-Za-z0-9][A-Za-z0-9 _.-]*$')
    recording_rate_hz: int = Field(default=25, ge=1, le=100)


class Recalculate(BaseModel):
    air_density: float = Field(default=1.225, gt=0, le=10, allow_inf_nan=False)


class FlowMetricsSetting(BaseModel):
    enabled: bool


class PressureFilterSetting(BaseModel):
    cutoff_hz: float = Field(ge=0.1, le=10, allow_inf_nan=False)

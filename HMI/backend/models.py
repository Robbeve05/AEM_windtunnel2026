from typing import Literal
from pydantic import BaseModel, Field, model_validator


class Tap(BaseModel):
    name: str = Field(pattern=r"^[A-Za-z][A-Za-z0-9_]{0,19}$")
    x_c: float = Field(ge=0, le=1, allow_inf_nan=False)
    surface: Literal['upper', 'lower'] = 'upper'


class Config(BaseModel):
    simulation: bool = True
    device_mode: Literal['arduino_usb', 'esp32_tcp'] = 'arduino_usb'
    esp32_host: str = '192.168.50.10'
    esp32_port: int = Field(default=5000, ge=1, le=65535)
    sample_rate_hz: float = Field(default=20, ge=1, le=100, allow_inf_nan=False)
    filter_cutoff_hz: float = Field(default=1.0, ge=0.1, le=10, allow_inf_nan=False)
    air_density: float = Field(default=1.225, gt=0, le=10, allow_inf_nan=False)
    max_airspeed_ms: float = Field(default=30, gt=0, le=100, allow_inf_nan=False)
    serial_port: str = 'auto'
    baud_rate: int = Field(default=115200, gt=0)
    data_directory: str = 'data/measurements'
    usb_mount_path: str = ''
    simulate_usb: bool = True
    flow_metrics_enabled: bool = True
    pressure_taps: list[Tap] = Field(default_factory=list, max_length=128)

    @model_validator(mode='after')
    def unique_taps(self):
        if len({t.name for t in self.pressure_taps}) != len(self.pressure_taps):
            raise ValueError('Pressure tap names must be unique')
        return self


class Setpoint(BaseModel):
    airspeed_ms: float = Field(ge=0, allow_inf_nan=False)


class MeasurementRequest(BaseModel):
    name: str = Field(min_length=1, max_length=80, pattern=r'^[A-Za-z0-9][A-Za-z0-9 _.-]*$')
    recording_rate_hz: int = Field(default=25, ge=1, le=100)


class Recalculate(BaseModel):
    air_density: float = Field(default=1.225, gt=0, le=10, allow_inf_nan=False)


class FlowMetricsSetting(BaseModel):
    enabled: bool


class ArduinoFilterSetting(BaseModel):
    cutoff_hz: float = Field(ge=0.1, le=10, allow_inf_nan=False)

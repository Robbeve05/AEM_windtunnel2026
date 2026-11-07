from typing import Literal
from pydantic import BaseModel, Field, model_validator


class PressureTap(BaseModel):
    tap_id: str = Field(pattern=r'^[A-Za-z][A-Za-z0-9_-]{0,31}$')
    surface: Literal['upper', 'lower']
    x_c: float = Field(ge=0, le=1, allow_inf_nan=False)
    z_c: float | None = Field(default=None, allow_inf_nan=False)
    channel: str = Field(min_length=1, max_length=64)
    enabled: bool = True
    physical_location: dict = Field(default_factory=dict)
    sensor_config: dict = Field(default_factory=dict)
    calibration: dict = Field(default_factory=dict)


def simulation_taps():
    """Explicit synthetic diamond-section geometry, not a measured real airfoil."""
    return [PressureTap(tap_id=f'{surface[0].upper()}{i}', surface=surface,
            x_c=x, z_c=(1 if surface == 'upper' else -1)*0.12*min(x, 1-x),
            channel=f'sim:{surface}:{i}', sensor_config={'adapter': 'simulation'},
            calibration={'status': 'synthetic fixture'})
            for surface in ('upper', 'lower') for i, x in enumerate((0, .08, .2, .4, .6, .8, 1))]


class ExperimentConfig(BaseModel):
    name: str = Field(min_length=1, max_length=80, pattern=r'^[A-Za-z0-9][A-Za-z0-9 _.-]*$')
    airfoil: str = Field(min_length=1, max_length=80)
    chord_mm: float = Field(gt=0, le=10000, allow_inf_nan=False)
    alpha_deg: float = Field(ge=-90, le=90, allow_inf_nan=False)
    averaging_s: float = Field(default=5, ge=.05, le=3600, allow_inf_nan=False)
    description: str = Field(default='', max_length=2000)
    source: Literal['physical', 'simulation']
    sample_rate_hz: float = Field(default=20, ge=1, le=100, allow_inf_nan=False)
    simulation_airspeed_ms: float = Field(default=15, gt=0, le=40, allow_inf_nan=False)
    simulation_noise_pa: float = Field(default=.1, ge=0, le=100, allow_inf_nan=False)
    simulation_seed: int = Field(default=42, ge=0, le=2147483647)
    taps: list[PressureTap] = Field(default_factory=list, max_length=128)

    @model_validator(mode='after')
    def validate_taps(self):
        if not self.taps and self.source == 'simulation':
            self.taps = simulation_taps()
        identifiers, positions, channels = set(), set(), set()
        for tap in self.taps:
            position = (tap.surface, tap.x_c)
            if tap.tap_id in identifiers or position in positions or tap.channel in channels:
                raise ValueError('Tap identifiers, channel assignments and positions per surface must be unique')
            identifiers.add(tap.tap_id);positions.add(position);channels.add(tap.channel)
        return self


class AnalysisSettings(BaseModel):
    minimum_q_pa: float = Field(default=1, ge=1, le=10000, allow_inf_nan=False)
    minimum_samples_per_tap: int = Field(default=5, ge=2, le=1000000)
    max_reference_age_s: float = Field(default=.1, gt=0, le=10, allow_inf_nan=False)
    moment_reference_x_c: float = Field(default=.25, ge=0, le=1, allow_inf_nan=False)


class CompareRequest(BaseModel):
    identifiers: list[str] = Field(min_length=1, max_length=12)

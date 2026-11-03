"""No-hardware regression tests for updating saved Pi configuration."""
import json
import unittest
from pathlib import Path
from migrate_config import migrate_config

ROOT=Path(__file__).resolve().parents[1]
DEFAULTS=json.loads((ROOT/'config/config.json').read_text(encoding='utf-8'))

class MigrationTests(unittest.TestCase):
    def test_legacy_file_receives_new_limits(self):
        legacy={k:v for k,v in DEFAULTS.items() if k not in (
            'min_manual_frequency_hz','max_manual_frequency_hz',
            'calibration_defaults','default_recording_rate_hz','flow_statistics_window_s'
        )}
        result=migrate_config(DEFAULTS,legacy)
        self.assertEqual(result['min_manual_frequency_hz'],10)
        self.assertEqual(result['calibration_defaults']['start_hz'],10)
        self.assertFalse(result['simulation'])
    def test_preserves_site_ip_and_lower_limits(self):
        values={**DEFAULTS,'esp32_host':'10.0.0.25','max_airspeed_ms':35}
        result=migrate_config(DEFAULTS,values)
        self.assertEqual(result['esp32_host'],'10.0.0.25')
        self.assertEqual(result['max_airspeed_ms'],35)
        self.assertEqual(migrate_config(DEFAULTS,result),result)
    def test_migrates_legacy_chart_maximum_without_raising_motor_limit(self):
        legacy = {k: v for k, v in DEFAULTS.items()
                  if k != "graph_max_airspeed_ms"}
        legacy["max_airspeed_ms"] = 50
        migrated = migrate_config(DEFAULTS, legacy)
        self.assertEqual(migrated["max_airspeed_ms"], 40)
        self.assertEqual(migrated["graph_max_airspeed_ms"], 50)
        self.assertEqual(migrate_config(DEFAULTS, migrated), migrated)

    def test_migrates_high_legacy_value_but_respects_explicit_chart_preference(self):
        legacy = {**DEFAULTS, "max_airspeed_ms": 60,
                  "graph_max_airspeed_ms": 70}
        migrated = migrate_config(DEFAULTS, legacy)
        self.assertEqual(migrated["max_airspeed_ms"], 40)
        self.assertEqual(migrated["graph_max_airspeed_ms"], 70)

    def test_rejects_unsafe_or_conflicting_limits(self):
        for delta in ({'max_manual_frequency_hz':60},
                      {'min_manual_frequency_hz':0},
                      {'max_airspeed_ms':200},
                      {'max_manual_frequency_hz':30}):
            with self.subTest(delta=delta),self.assertRaises(ValueError):
                migrate_config(DEFAULTS,{**DEFAULTS,**delta})

if __name__=='__main__': unittest.main()

import csv
import json
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path

from backend.fan_dynamics import (
    FanDynamicEstimator,
    FanDynamicIdentifier,
    FanDynamics,
    FanDynamicsModel,
    analyze_calibration_folder,
    estimate_frequency_response,
    identify,
    identify_fan_dynamics,
)


class FanModelTests(unittest.TestCase):
    def test_existing_model_fitting_apis(self):
        samples = [(20, 16), (0, 0), (10, 8)]
        fitted = FanDynamicIdentifier().fit(samples)
        self.assertIsInstance(fitted, FanDynamicsModel)
        self.assertAlmostEqual(fitted.gain_ms_per_hz, 0.8)
        self.assertAlmostEqual(fitted.airspeed_for(10), 8)
        self.assertIs(FanDynamics, FanDynamicsModel)
        self.assertIs(FanDynamicEstimator, FanDynamicIdentifier)
        self.assertAlmostEqual(estimate_frequency_response(samples).gain_ms_per_hz, 0.8)
        self.assertAlmostEqual(identify_fan_dynamics(samples).gain_ms_per_hz, 0.8)
        self.assertAlmostEqual(FanDynamicsModel().airspeed_for(100), 40)

    def test_model_rejects_invalid_values(self):
        with self.assertRaises(ValueError):
            FanDynamicsModel(gain_ms_per_hz=0)
        with self.assertRaises(ValueError):
            FanDynamicsModel().airspeed_for(-1)


class OfflineIdentificationTests(unittest.TestCase):
    def _fixture(self):
        points = [
            {"target_hz": "10", "mean_actual_hz": "10", "mean_pv": "8"},
            {"target_hz": "20", "mean_actual_hz": "20", "mean_pv": "16"},
        ]
        start = datetime(2026, 11, 7, tzinfo=timezone.utc)
        rows = []
        airspeed = 8.0
        for index in range(16):
            frequency_hz = 10 + index * (10 / 15)
            equilibrium = 8 + 0.8 * (frequency_hz - 10)
            airspeed = equilibrium + (airspeed - equilibrium) * (1 - 2.718281828 ** (-0.1 / 0.3))
            rows.append(
                {
                    "timestamp": (start + timedelta(seconds=index * 0.1)).isoformat(),
                    "valid": "true",
                    "actual_hz": str(frequency_hz),
                    "pv": str(airspeed),
                    "target_hz": "20",
                    "phase": "RAMPING",
                }
            )
        return rows, points

    def test_identify_from_csv_rows(self):
        rows, points = self._fixture()
        result = identify(rows, points)
        self.assertTrue(result["advisory_only"])
        self.assertEqual(len(result["transitions"]), 2)
        self.assertEqual(result["transitions"][0]["status"], "BASELINE_ONLY")
        transition = result["transitions"][1]
        self.assertAlmostEqual(transition["K_ms_per_hz"], 0.8)
        self.assertIsNotNone(transition["tau_s"])

    def test_analyze_calibration_folder_writes_json(self):
        rows, points = self._fixture()
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            for name, records in (
                ("raw_samples.csv", rows),
                ("characteristic.csv", points),
            ):
                with (folder / name).open("w", newline="", encoding="utf-8") as file:
                    writer = csv.DictWriter(file, fieldnames=records[0].keys())
                    writer.writeheader()
                    writer.writerows(records)

            result = analyze_calibration_folder(folder)
            saved = json.loads((folder / "dynamics.json").read_text(encoding="utf-8"))
            self.assertEqual(saved, result)
            self.assertTrue(saved["advisory_only"])


if __name__ == "__main__":
    unittest.main()

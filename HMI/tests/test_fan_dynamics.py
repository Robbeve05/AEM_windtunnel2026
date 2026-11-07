import csv
import json
import math
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
    simc_parameters,
    simulate_candidate,
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
            self.assertTrue((folder / "dynamic_transitions.csv").is_file())

    def test_measured_ramp_and_delay_are_identified_without_approving_tuning(self):
        start = datetime(2026, 11, 7, tzinfo=timezone.utc)
        points = [dict(target_hz=10, mean_actual_hz=10, mean_pv=8),
                  dict(target_hz=20, mean_actual_hz=20, mean_pv=16)]
        frequencies = [min(20, 10+i*0.5) for i in range(200)]
        rows = []
        measured = 8.0
        for i, frequency in enumerate(frequencies):
            if i:
                delayed_frequency = frequencies[i-5] if i >= 5 else 10
                equilibrium = 8+0.8*(delayed_frequency-10)
                measured = equilibrium+(measured-equilibrium)*math.exp(-0.1/0.8)
            rows.append(dict(timestamp=(start+timedelta(seconds=i*0.1)).isoformat(),
                             valid=True, actual_hz=frequency, pv=measured,
                             target_hz=20, phase="RAMPING"))
        transition = identify(rows, points)['transitions'][1]
        self.assertAlmostEqual(transition['tau_s'], 0.8, delta=0.15)
        self.assertAlmostEqual(transition['L_s'], 0.4, delta=0.16)
        self.assertGreater(transition['r2'], 0.99)
        self.assertFalse(transition['model_valid'])
        self.assertFalse(transition['hardware_commissioned'])
        self.assertIn('ti_s', transition['candidate_pi'])
        self.assertNotIn('ki', transition['candidate_pi'])
        incomplete = identify(rows[::10], points)['transitions'][1]
        self.assertIn('warnings', incomplete)

    def test_simc_units_validation_and_simulation(self):
        pi = simc_parameters(0.8, 2, 0.2, 0.8)
        self.assertAlmostEqual(pi['kp'], 2.5)
        self.assertEqual(pi['ti_s'], 2)
        self.assertEqual(pi['td_s'], 0)
        for args in ((0, 2, 0, 1), (1, 0, 0, 1), (1, 2, -1, 1),
                     (1, 2, 0, 0), (1, math.nan, 0, 1)):
            with self.assertRaises(ValueError):
                simc_parameters(*args)
        simulation = simulate_candidate(0.8, 2, 0.2, pi)
        self.assertTrue(simulation['advisory_only'])
        self.assertLess(abs(simulation['final_error_ms']), 0.02)
        self.assertTrue(all(0 <= row['actual_hz'] <= 50 for row in simulation['trace']))


if __name__ == "__main__":
    unittest.main()

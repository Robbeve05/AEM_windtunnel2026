import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from backend.measurements import save_json


class AtomicStorageTests(unittest.TestCase):
    def test_transient_windows_reader_lock_retries_atomically(self):
        with tempfile.TemporaryDirectory() as root:
            target = Path(root)/'experiment.json'
            target.write_text('{"old": true}')
            replace = Path.replace
            calls = []

            def locked_once(source, destination):
                calls.append(source)
                if len(calls) == 1:
                    self.assertEqual(json.loads(target.read_text()), {'old': True})
                    error = PermissionError('reader still holds metadata')
                    error.winerror = 32
                    raise error
                return replace(source, destination)

            with patch.object(Path, 'replace', locked_once), patch('backend.measurements.time.sleep'):
                save_json(target, {'new': True})
            self.assertEqual(json.loads(target.read_text()), {'new': True})
            self.assertEqual(len(calls), 2)
            self.assertEqual(list(Path(root).glob('*.tmp')), [])

    def test_persistent_denial_preserves_old_file_and_fails(self):
        with tempfile.TemporaryDirectory() as root:
            target = Path(root)/'experiment.json'
            target.write_text('{"old": true}')
            error = PermissionError('persistent denial')
            error.winerror = 5
            with patch.object(Path, 'replace', side_effect=error) as replace, \
                    patch('backend.measurements.time.sleep'):
                with self.assertRaises(PermissionError):
                    save_json(target, {'new': True})
            self.assertEqual(replace.call_count, 10)
            self.assertEqual(json.loads(target.read_text()), {'old': True})
            self.assertEqual(list(Path(root).glob('*.tmp')), [])

"""Compact firmware records, dictionary decoding and socket history integration."""
import asyncio
import unittest

from backend.fault_codes import DICTIONARY, parse_fault_record
from backend.esp32 import ESP32Client

FRAME = ('FAULT seq=42 t=269567 code=0302 detail=0002 prev=2 mode=1 tick=269568 '
         'age=4294967295 timeout=100 safety_seq=17 flags=1 reason=9')


class FaultCodesTests(unittest.TestCase):
    def test_future_trigger_exact_evidence_and_actual_mode(self):
        r = parse_fault_record(FRAME)
        self.assertEqual(r['code_text'], '0302')
        self.assertEqual(r['previous_state'], 'READY')
        self.assertEqual(r['operating_mode'], 'MANUAL')
        self.assertIn('INVALID (raw 4294967295)', r['triggering_condition'])
        self.assertIn('now=269567 tick=269568', r['triggering_condition'])
        self.assertIn('safety sequence=17 valid=True', r['triggering_condition'])

    def test_every_bit_combination_and_unknown_bits(self):
        items = [r for r in DICTIONARY.values() if r['kind'] == 'INTERLOCK']
        self.assertEqual(sum(item['mask'] for item in items), 127)
        for item in items:
            r = parse_fault_record(FRAME.replace('detail=0002', f"detail={item['mask']:04x}"))
            self.assertIn(item['description'], r['triggering_condition'])
        r = parse_fault_record(FRAME.replace('detail=0002', 'detail=0086'))
        self.assertIn('Digital output', r['triggering_condition'])
        self.assertIn('Unknown interlock bits 0x00000080', r['triggering_condition'])

    def test_malformed_records_are_not_displayed_as_evidence(self):
        for line in (FRAME+' seq=43', FRAME.replace('prev=2', 'prev=99'),
                     FRAME.replace('mode=1', 'mode=9'), FRAME.replace('t=269567', 't=-1'),
                     FRAME.replace('flags=1', 'flags=8'), FRAME.replace('age=4294967295', 'age=nan')):
            self.assertIsNone(parse_fault_record(line))


class SocketFaultTests(unittest.IsolatedAsyncioTestCase):
    async def test_history_survives_status_refresh_and_is_bounded(self):
        client = ESP32Client('127.0.0.1', 5020)
        client._reader = asyncio.StreamReader()
        for seq in range(1, 41):
            client._reader.feed_data((FRAME.replace('seq=42 ', f'seq={seq} ')+'\n').encode())
        client._reader.feed_data((FRAME.replace('seq=42 ', 'seq=40 ')+'\n').encode())
        client._reader.feed_data(b'STATUS state=FAULT fault=INTERLOCK mode=MANUAL\n')
        client._reader.feed_eof()
        with self.assertRaises(ConnectionError):
            await client._read(lambda _: None)
        self.assertEqual(len(client._fault_history), 32)
        self.assertEqual(client._latest['fault_history'][0]['seq'], 9)
        self.assertEqual(client._latest['fault_record']['seq'], 40)
        self.assertEqual(client._latest['fault_record']['operating_mode'], 'MANUAL')

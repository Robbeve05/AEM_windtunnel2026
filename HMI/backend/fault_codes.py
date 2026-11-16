"""Decode compact firmware evidence; registry data is generated from ESP32 sources."""
import json
from pathlib import Path

DICTIONARY = json.loads(Path(__file__).with_name('fault_codes.json').read_text(encoding='utf-8'))
STATE_NAMES = tuple(DICTIONARY.pop('_states'))


def parse_fault_record(line):
    if not line.startswith('FAULT ') or len(line) > 512:
        return None
    values = {}
    try:
        for token in line.split()[1:]:
            key, value = token.split('=', 1)
            if key in values:
                return None
            values[key] = int(value, 16 if key in ('code', 'detail') else 10)
        required = {'seq', 't', 'code', 'detail', 'prev', 'mode', 'tick', 'age', 'timeout', 'safety_seq', 'flags', 'reason'}
        if set(values) != required or any(value < 0 or value > 0xFFFFFFFF for value in values.values()):
            return None
        if values['prev'] >= len(STATE_NAMES) or values['mode'] not in (0, 1) or values['flags'] > 3:
            return None
        if values['code'] > 0xFFFF or values['timeout'] > 0xFFFF:
            return None
    except (ValueError, TypeError):
        return None
    entry = DICTIONARY.get(f"{values['code']:04X}", {})
    conditions = []
    known_mask = 0
    for item in DICTIONARY.values():
        if item['kind'] == 'INTERLOCK':
            known_mask |= item['mask']
            if values['detail'] & item['mask']:
                conditions.append(item['description'])
    if values['detail'] & ~known_mask:
        conditions.append(f"Unknown interlock bits 0x{values['detail'] & ~known_mask:08X}")
    age = str(values['age']) if values['flags'] & 2 else f"INVALID (raw {values['age']})"
    if values['detail'] & 2:
        conditions.append(f"now={values['t']} tick={values['tick']} age={age} timeout={values['timeout']} ms; "
                          f"safety sequence={values['safety_seq']} valid={bool(values['flags'] & 1)}")
    values.update(code_text=f"{values['code']:04X}",
                  description=entry.get('description', 'Unknown firmware fault code'),
                  triggering_condition='; '.join(conditions) or entry.get('description', 'Unknown'),
                  previous_state=STATE_NAMES[values['prev']],
                  operating_mode='MANUAL' if values['mode'] else 'AUTO')
    return values

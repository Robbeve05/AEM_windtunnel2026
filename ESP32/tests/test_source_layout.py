"""Check the agreed layout for every application C source and header."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SEPARATOR = '/' * 79
FUNCTION = re.compile(
    r'^[ \t]*(?:static\s+)?(?:inline\s+)?(?:const\s+)?[A-Za-z_]\w*'
    r'(?:\s+|\s*\*\s*)(?:IRAM_ATTR\s+)?(?:\*\s*)?'
    r'([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{', re.M)


def masked(source):
    return re.sub(
        r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|^[ \t]*\#[^\n]*(?:\\\n[^\n]*)*',
        lambda match: re.sub(r'[^\n]', ' ', match[0]), source, flags=re.S | re.M)


class SourceLayoutTests(unittest.TestCase):
    def test_all_application_files(self):
        files = sorted([*ROOT.glob('src/*.c'), *ROOT.glob('include/*.h')])
        self.assertTrue(files)
        for path in files:
            with self.subTest(path=path.name):
                source = path.read_text(encoding='utf-8')
                self.assertTrue(source.startswith(SEPARATOR+'\n// File information\n\n'))
                # Match the complete ControlTask-style metadata block, not only
                # the presence of a section label above a one-line comment.
                self.assertRegex(source,
                    r'\A'+re.escape(SEPARATOR)+r'\n// File information\n\n/\*\n'
                    r' \* '+re.escape(path.name)+r'\n \*\n'
                    r' \* \S[^\n]*\n(?: \*(?: [^\n]*)?\n)* \*/\n\n')
                information = re.search(r'/\*.*?\*/', source, re.S)[0]
                self.assertIn('\n * Author: R.E. van Eekelen\n', information)
                for line in source.splitlines():
                    if re.fullmatch(r'\s*/{20,}\s*', line):
                        self.assertEqual(line, SEPARATOR)
                code = masked(source)
                depth = 0
                levels = []
                for char in code:
                    levels.append(depth)
                    depth += (char == '{') - (char == '}')
                functions = [match for match in FUNCTION.finditer(code)
                             if levels[match.start()] == 0]
                for match in functions:
                    name = match[1]
                    self.assertRegex(source[:match.start()],
                                     re.escape(SEPARATOR)+r'\n// '+re.escape(name)+r'\(\)\n')
                    end, depth = match.end(), 1
                    while depth:
                        depth += (code[end] == '{') - (code[end] == '}')
                        end += 1
                    self.assertNotIn(SEPARATOR, source[match.end():end])
                    if path.suffix == '.c' and code[match.start():match.end()].lstrip().startswith('static '):
                        first_function = functions[0].start()
                        self.assertRegex(source[:first_function], r'\b'+re.escape(name)+r'\s*\([^;{}]*\);')
                ranks = {'File information': 0, 'Include guard': 1, 'System includes': 2,
                         'FreeRTOS includes': 3, 'Application includes': 4}
                ranks.update(({'Public definitions': 5, 'Public enumerations': 6,
                               'Public data structures': 7, 'Public declarations': 8,
                               'Function declarations': 9} if path.suffix == '.h' else
                              {'Local definitions': 5, 'File-local data structures': 6,
                               'File-local variables': 7, 'Function prototypes': 8,
                               'Initialization': 9, 'Module-specific functionality': 10}))
                sections = re.findall(re.escape(SEPARATOR)+r'\n// ([^\n]+)\n', source)
                order = [ranks[title] for title in sections if title in ranks]
                self.assertEqual(order, sorted(set(order)))
                if path.suffix == '.h':
                    self.assertIn('// Include guard\n\n#pragma once', source)
                section = None
                for line in source.splitlines():
                    if line.startswith('// ') and line[3:] in ranks:
                        section = line[3:]
                    if line.startswith('#include'):
                        include = re.search(r'[<"]([^>"]+)', line)[1]
                        expected = ('FreeRTOS includes' if include.startswith('freertos/') else
                                    'Application includes' if (ROOT/'include'/include).is_file() or
                                    include == 'OtaLocalConfig.h' else 'System includes')
                        self.assertEqual(section, expected, f'{path.name}: {line}')


if __name__ == '__main__':
    unittest.main()

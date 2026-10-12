"""Check descriptor/SDP contracts against the actual wire report sizes."""
import subprocess
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

BINARY = Path(__file__).resolve().parents[1] / 'hidclient'


def descriptor(mode):
    return subprocess.check_output([str(BINARY), '--mode', mode, '--print-descriptor'])


def report_bits(data):
    result = {}
    globals_ = {'id': 0, 'size': 0, 'count': 0}
    stack = []
    collections = 0
    offset = 0
    while offset < len(data):
        prefix = data[offset]
        offset += 1
        if prefix == 0xFE:
            raise AssertionError('Unexpected long item')
        size = (0, 1, 2, 4)[prefix & 3]
        kind, tag = (prefix >> 2) & 3, prefix >> 4
        value = int.from_bytes(data[offset:offset + size], 'little')
        if offset + size > len(data):
            raise AssertionError('Truncated descriptor')
        offset += size
        if kind == 1:
            if tag in (7, 8, 9):
                globals_[{7: 'size', 8: 'id', 9: 'count'}[tag]] = value
            elif tag == 10:
                stack.append(globals_.copy())
            elif tag == 11:
                globals_ = stack.pop()
        elif kind == 0:
            if tag == 10:
                collections += 1
            elif tag == 12:
                collections -= 1
            elif tag in (8, 9, 11):
                key = (globals_['id'], tag)
                result[key] = result.get(key, 0) + globals_['size'] * globals_['count']
    assert collections == 0 and not stack
    return result


class DescriptorTests(unittest.TestCase):
    def test_wire_sizes_match_descriptor(self):
        self.assertEqual(report_bits(descriptor('combo')),
                         {(1, 8): 64, (1, 9): 8, (2, 8): 40, (3, 8): 16})

    def test_modes_omit_other_device_collections(self):
        self.assertEqual(report_bits(descriptor('keyboard')),
                         {(1, 8): 64, (1, 9): 8, (3, 8): 16})
        self.assertEqual(report_bits(descriptor('mouse')), {(2, 8): 40})

    def test_sdp_contains_exact_descriptor_and_typed_attributes(self):
        for mode, subclass in [('keyboard', 0x40), ('mouse', 0x80), ('combo', 0xC0)]:
            xml = subprocess.check_output([str(BINARY), '--mode', mode, '--name', 'Key & "Mouse"', '--print-sdp'])
            record = ET.fromstring(xml)
            attrs = {int(node.attrib['id'], 16): node for node in record.findall('attribute')}
            self.assertEqual(attrs[0x100].find('text').attrib['value'], 'Key & "Mouse"')
            self.assertEqual(int(attrs[0x202].find('uint8').attrib['value'], 16), subclass)
            self.assertEqual(attrs[0x20E].find('boolean').attrib['value'], 'true')
            self.assertEqual(attrs[0x205].find('boolean').attrib['value'], 'false')
            text = attrs[0x206].find('.//text')
            self.assertEqual(text.attrib['encoding'], 'hex')
            self.assertEqual(bytes.fromhex(text.attrib['value']), descriptor(mode))
            primary = [int(n.attrib['value'], 16) for n in attrs[4].findall('.//uint16')]
            secondary = [int(n.attrib['value'], 16) for n in attrs[0xD].findall('.//uint16')]
            self.assertEqual(primary, [0x11])
            self.assertEqual(secondary, [0x13])

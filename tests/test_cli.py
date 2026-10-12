from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]

class CommandLineTests(unittest.TestCase):
    def test_no_implicit_all_devices(self):
        result = subprocess.run([str(ROOT / 'hidclient')], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Select --device', result.stderr)

    def test_invalid_event_numbers(self):
        for argument in ('-e', '-e-1', '-eabc', '-e64garbage', '-e99999999999999999999999'):
            with self.subTest(argument=argument):
                result = subprocess.run([str(ROOT / 'hidclient'), argument], capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b'Invalid arguments', result.stderr)

    def test_large_event_number_is_parsed(self):
        result = subprocess.run([str(ROOT / 'hidclient'), '-s', '-e65535'], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'/dev/input/event65535', result.stderr)
        self.assertNotIn(b'Invalid arguments', result.stderr)

    def test_launcher_requires_device(self):
        result = subprocess.run(['python3', str(ROOT / 'launch.py')], capture_output=True)
        self.assertEqual(result.returncode, 2)

    def test_launcher_rejects_missing_device(self):
        result = subprocess.run(['python3', str(ROOT / 'launch.py'), '--device',
                                 '/nonexistent/hidclient-test'], capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn(b'does not exist', result.stderr)

    def test_launcher_rejects_invalid_mac(self):
        result = subprocess.run(['python3', str(ROOT / 'launch.py'), '--device', '/dev/null',
                                 '--target', 'not-a-mac'], capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn(b'MAC address', result.stderr)

    def test_launcher_replaces_itself_with_configured_executable(self):
        import importlib.util
        from unittest.mock import patch
        spec = importlib.util.spec_from_file_location('hidclient_launcher', ROOT / 'launch.py')
        launcher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(launcher)
        arguments = ['launch.py', '--device', '/dev/null', '--target', 'AA:BB:CC:DD:EE:FF',
                     '--mode', 'keyboard', '--adapter', 'hci2', '--swap-ctrl-meta']
        with patch.object(launcher.sys, 'argv', arguments), patch.object(launcher.os, 'execv') as execute:
            self.assertEqual(launcher.main(), 0)
        executable, command = execute.call_args.args
        self.assertEqual(executable, str(ROOT / 'hidclient'))
        self.assertIn('--grab', command)
        self.assertIn('--swap-ctrl-meta', command)
        self.assertIn('--host', command)
        self.assertNotIn('--connect', command)
        self.assertEqual(command[command.index('--adapter') + 1], 'hci2')
        self.assertEqual(command[command.index('--mode') + 1], 'keyboard')

    def test_invalid_modes_and_shortcuts(self):
        for arguments in (['--mode', 'ps4'], ['--exit-key', 'madeup'],
                          ['--exit-key', 'F12', '--disconnect-key', 'F12'],
                          ['--adapter', 'hci/../../'], ['--host', 'not-a-mac']):
            with self.subTest(arguments=arguments):
                result = subprocess.run([str(ROOT / 'hidclient'), *arguments], capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b'Invalid arguments', result.stderr)

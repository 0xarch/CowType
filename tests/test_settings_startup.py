# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise application lifetime, not just widget construction, without a desktop."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


class StartupTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('gtk4-broadwayd'), 'GTK Broadway is unavailable')
    def test_preferences_stay_mapped_and_exit_cleanly(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temporary:
            env = dict(os.environ, XDG_RUNTIME_DIR=temporary,
                XDG_CONFIG_HOME=temporary + '/config', XDG_DATA_HOME=temporary + '/data',
                XDG_STATE_HOME=temporary + '/state', GDK_BACKEND='broadway', BROADWAY_DISPLAY=':72')
            memory = Path(temporary) / 'data/cowtype/learning-v1.tsv'
            memory.parent.mkdir(parents=True)
            memory.write_text('# cowtype-learning-v1\n# reset-token\t\nce shi\t测试记忆\t8\t100\n')
            server = subprocess.Popen(['gtk4-broadwayd', '--port=0', ':72'], env=env,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                for _ in range(100):
                    if list(Path(temporary).glob('*.socket')): break
                    if server.poll() is not None: self.fail('Broadway exited before startup')
                    time.sleep(0.05)
                installed = os.environ.get('COWTYPE_TEST_SETTINGS_EXECUTABLE')
                command = [installed] if installed else [sys.executable, str(root / 'src-settings/app/launch.py')]
                result = subprocess.run([*command, '--smoke-test'], env=env,
                    capture_output=True, text=True, timeout=15)
                log = Path(temporary + '/state/cowtype/settings.log')
                self.assertEqual(result.returncode, 0, log.read_text() if log.exists() else result.stderr)
                self.assertIn('remain mapped after startup', result.stdout)
                self.assertIn('Memory editor opened with synthetic record', result.stdout)
                self.assertIn('Custom dictionary autosave and deletion passed', result.stdout)
                self.assertEqual(log.stat().st_mode & 0o777, 0o600)
                self.assertIn('Settings starting', log.read_text())
            finally:
                server.terminate()
                server.wait(timeout=5)

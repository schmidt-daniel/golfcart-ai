"""Launch-file tests for golfcart_bringup.

Verifies the launch files are present and parseable. Also ensures pytest collects
at least one test so `colcon test` does not fail with "no tests collected".
"""

import os


def _launch_dir():
    return os.path.join(os.path.dirname(__file__), '..', 'launch')


def test_launch_files_present():
    launch_dir = _launch_dir()
    expected = [
        'joystick_control.launch.py',
        'keyboard_control.launch.py',
        'web_teleop.launch.py',
        'core.launch.py',
        'web_server.launch.py',
    ]
    for f in expected:
        assert os.path.isfile(os.path.join(launch_dir, f)), f'Missing {f}'


def test_launch_files_parse():
    """Each launch file must be valid Python (parseable)."""
    import ast
    launch_dir = _launch_dir()
    for f in os.listdir(launch_dir):
        if f.endswith('.launch.py'):
            with open(os.path.join(launch_dir, f)) as fh:
                ast.parse(fh.read(), filename=f)
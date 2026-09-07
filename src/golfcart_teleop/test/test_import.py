"""Import tests for golfcart_teleop.

Verifies the teleop node modules are importable (catches syntax/import errors
at test time). Also ensures pytest collects at least one test so `colcon test`
does not fail with "no tests collected".
"""

import os


def test_import_teleop_modules():
    import golfcart_teleop.arduino_joystick_node  # noqa: F401
    import golfcart_teleop.hmi_node  # noqa: F401
    import golfcart_teleop.joystick_node  # noqa: F401
    import golfcart_teleop.keyboard_teleop_node  # noqa: F401
    import golfcart_teleop.web_teleop_server  # noqa: F401


def test_web_files_present():
    """The web/ directory must contain the teleop and summon pages."""
    pkg_dir = os.path.join(os.path.dirname(__file__), '..')
    web_dir = os.path.join(pkg_dir, 'web')
    assert os.path.isfile(os.path.join(web_dir, 'index.html'))
    assert os.path.isfile(os.path.join(web_dir, 'summon.html'))

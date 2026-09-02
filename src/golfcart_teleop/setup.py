from setuptools import setup

package_name = 'golfcart_teleop'

setup(
    name=package_name,
    version='0.1.0',
    packages=[package_name],
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        ('share/' + package_name + '/web', ['web/index.html']),
    ],
    install_requires=['setuptools', 'pyserial'],
    zip_safe=True,
    maintainer='golfcart',
    maintainer_email='dev@example.com',
    description='Joystick and keyboard teleop nodes.',
    license='Apache-2.0',
    entry_points={
        'console_scripts': [
            'joystick_node = golfcart_teleop.joystick_node:main',
            'arduino_joystick_node = golfcart_teleop.arduino_joystick_node:main',
            'keyboard_teleop_node = golfcart_teleop.keyboard_teleop_node:main',
            'web_teleop_server = golfcart_teleop.web_teleop_server:main',
        ],
    },
)

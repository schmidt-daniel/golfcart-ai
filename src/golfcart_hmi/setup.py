from setuptools import setup

package_name = 'golfcart_hmi'

setup(
    name=package_name,
    version='0.1.0',
    packages=[package_name],
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
    ],
    install_requires=['setuptools', 'pyserial', 'Pillow'],
    extras_require={'test': ['pytest']},
    zip_safe=True,
    maintainer='golfcart',
    maintainer_email='dev@example.com',
    description='Handle-unit HMI: ESP32 firmware (LVGL) + Pi-side serial gateway.',
    license='Apache-2.0',
    entry_points={
        'console_scripts': [
            'handle_gateway = golfcart_hmi.handle_gateway:main',
        ],
    },
)
from setuptools import find_packages, setup

setup(
    name='chaos_sensors', version='0.1.0', packages=find_packages(),
    data_files=[('share/ament_index/resource_index/packages', ['resource/chaos_sensors']),
                ('share/chaos_sensors', ['package.xml'])],
    install_requires=['setuptools'], zip_safe=True,
    maintainer='Chaos maintainers', maintainer_email='maintainer@example.com',
    description='Mock sensor publishers and asset-scoped sensor health diagnostics.',
    license='Proprietary',
    entry_points={'console_scripts': [
        'mock_sensor = chaos_sensors.mock:main',
        'sensor_monitor = chaos_sensors.monitor:main',
    ]},
)

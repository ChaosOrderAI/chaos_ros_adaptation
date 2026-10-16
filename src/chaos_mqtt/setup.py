from setuptools import setup

setup(
    name='chaos_mqtt', version='0.1.0', packages=['chaos_mqtt'],
    data_files=[('share/ament_index/resource_index/packages', ['resource/chaos_mqtt']),
                ('share/chaos_mqtt', ['package.xml'])],
    install_requires=['setuptools'], zip_safe=True,
    maintainer='Chaos maintainers', maintainer_email='maintainers@example.com',
    description='MQTT command facade for the Chaos ROS 2 runtime', license='Proprietary',
    entry_points={'console_scripts': ['gateway = chaos_mqtt.gateway:main']},
)

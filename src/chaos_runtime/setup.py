from setuptools import find_packages, setup

setup(
    name='chaos_runtime', version='0.1.0', packages=find_packages(),
    data_files=[('share/ament_index/resource_index/packages', ['resource/chaos_runtime']),
                ('share/chaos_runtime', ['package.xml'])],
    install_requires=['setuptools'], zip_safe=True,
    maintainer='Chaos maintainers', maintainer_email='maintainer@example.com',
    description='Asset-scoped robot runtime and adapter plugins.', license='Proprietary',
    entry_points={
        'console_scripts': ['robot_runtime = chaos_runtime.node:main',
                            'go2_driver = chaos_runtime.go2_driver:main'],
        'chaos.robot_adapters': [
            'mock = chaos_runtime.adapters.mock:MockAdapter',
            'lingao_ros2 = chaos_runtime.adapters.lingao:LingaoAdapter',
            'unitree_go2 = chaos_runtime.adapters.go2:Go2Adapter',
            'dji_psdk_legacy = chaos_runtime.adapters.dji_legacy:DjiLegacyAdapter',
        ],
    },
)

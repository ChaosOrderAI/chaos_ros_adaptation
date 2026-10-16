from glob import glob
from setuptools import find_packages, setup

setup(
    name='chaos_bringup', version='0.1.0', packages=find_packages(),
    data_files=[('share/ament_index/resource_index/packages', ['resource/chaos_bringup']),
                ('share/chaos_bringup', ['package.xml']),
                ('share/chaos_bringup/launch', glob('launch/*.launch.py'))],
    install_requires=['setuptools'], zip_safe=True,
    maintainer='Chaos maintainers', maintainer_email='maintainers@example.com',
    description='Per-host launch for configured robot assets', license='Proprietary',
)

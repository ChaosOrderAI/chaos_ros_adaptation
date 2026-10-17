from glob import glob
from setuptools import find_packages, setup

setup(
    name='chaos_data', version='0.1.0', packages=find_packages(),
    data_files=[('share/ament_index/resource_index/packages', ['resource/chaos_data']),
                ('share/chaos_data', ['package.xml']),
                ('share/chaos_data/config', glob('config/*.yaml'))],
    install_requires=['setuptools', 'PyYAML', 'numpy'], zip_safe=True,
    maintainer='Chaos maintainers', maintainer_email='maintainer@example.com',
    description='Native rosbag recording and offline episode export with clock quality and provenance.',
    license='Proprietary',
    entry_points={'console_scripts': ['collector = chaos_data.recorder:main',
                                     'audit = chaos_data.audit:main',
                                     'data_audit = chaos_data.audit:main',
                                     'data_export = chaos_data.dataset:main']},
)

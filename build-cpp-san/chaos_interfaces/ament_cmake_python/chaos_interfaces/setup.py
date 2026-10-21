from setuptools import find_packages
from setuptools import setup

setup(
    name='chaos_interfaces',
    version='0.1.0',
    packages=find_packages(
        include=('chaos_interfaces', 'chaos_interfaces.*')),
)

from setuptools import find_packages
from setuptools import setup

setup(
    name='chaos_data',
    version='0.1.0',
    packages=find_packages(
        include=('chaos_data', 'chaos_data.*')),
)

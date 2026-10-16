from setuptools import find_packages, setup

setup(
    name='chaos_sdk', version='0.1.0', packages=find_packages(),
    data_files=[('share/ament_index/resource_index/packages', ['resource/chaos_sdk']),
                ('share/chaos_sdk', ['package.xml']),
                ('share/chaos_sdk/examples', ['examples/mock_client.py'])],
    install_requires=['setuptools'], zip_safe=True,
    maintainer='Chaos maintainers', maintainer_email='maintainer@example.com',
    description='Async Python client for asset capability contracts.', license='Proprietary',
)

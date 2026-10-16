from pathlib import Path
from setuptools import find_packages, setup

root = Path(__file__).parent
data_files = [
    ('share/ament_index/resource_index/packages', ['resource/chaos_assets']),
    ('share/chaos_assets', ['package.xml']),
]
for directory in ('config', 'models'):
    for path in sorted(p for p in (root / directory).rglob('*') if p.is_file()):
        relative = path.relative_to(root)
        data_files.append((str(Path('share/chaos_assets') / relative.parent), [str(relative)]))

setup(
    name='chaos_assets', version='0.1.0', packages=find_packages(),
    data_files=data_files, install_requires=['setuptools', 'PyYAML'], zip_safe=True,
    maintainer='Chaos maintainers', maintainer_email='maintainers@example.com',
    description='Validated, versioned multi-robot assets', license='Proprietary',
    entry_points={'console_scripts': ['asset_config = chaos_assets.cli:main']},
)

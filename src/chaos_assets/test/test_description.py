from copy import deepcopy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET

import pytest
import yaml

import ament_index_python.packages

from chaos_assets import ConfigurationError, load_inventory
from chaos_assets import description as descriptions


ROOT = Path(__file__).resolve().parents[1]
SIMPLE = '''<robot name="test_robot">
  <link name="base_link"/><link name="imu_link"/>
  <joint name="imu_mount" type="fixed">
    <parent link="base_link"/><child link="imu_link"/>
    <origin xyz="0 0 0.2" rpy="0 0 0"/>
  </joint>
</robot>'''


def write_inventory(tmp_path, *, description=None, model_description=None, xml=SIMPLE, mutate=None):
    model_directory = tmp_path / 'models'
    model_directory.mkdir(exist_ok=True)
    model = yaml.safe_load((ROOT / 'models/lingao_vehicle/v1/model.yaml').read_text())
    if model_description is not None:
        model['description'] = model_description
    (model_directory / 'model.yaml').write_text(yaml.safe_dump(model))
    (tmp_path / 'robot.urdf').write_text(xml)
    data = {
        'schema_version': 1, 'hosts': {'dev': {'ros_domain_id': 87}},
        'assets': [{'id': 'car', 'host': 'dev', 'model_file': 'models/model.yaml'}],
    }
    if description is not None:
        data['assets'][0]['description'] = description
    if mutate:
        mutate(data)
    path = tmp_path / 'inventory.yaml'
    path.write_text(yaml.safe_dump(data))
    return path


def sensor(frame='car/imu_link', **extra):
    return {'kind': 'imu', 'frame_id': frame, 'driver': {'mode': 'mock'},
            'streams': {'data': {'type': 'sensor_msgs/msg/Imu', 'topic': 'data'}}, **extra}


def test_existing_assets_have_no_description():
    assert all(asset['description'] is None for asset in load_inventory(ROOT / 'config/demo.yaml').assets)


def test_urdf_defaults_freeze_names_structure_and_digest(tmp_path):
    path = write_inventory(tmp_path, description={'file': 'robot.urdf'})
    asset = load_inventory(path).assets[0]
    description = asset['description']
    assert description['file'] == str(tmp_path / 'robot.urdf')
    assert description['format'] == 'urdf'
    assert description['frame_prefix'] == 'car/'
    assert description['root'] == 'car/base_link'
    assert description['links'] == ['car/base_link', 'car/imu_link']
    assert description['joints'] == [{'name': 'imu_mount', 'type': 'fixed',
                                      'parent': 'car/base_link', 'child': 'car/imu_link'}]
    assert description['publish_frequency'] == 30.0
    assert description['joint_states_topic'] == '/robots/car/joint_states'
    assert description['use_sim_time'] is False
    assert description['arguments'] == {}
    assert description['sha256'] == hashlib.sha256(description['xml'].encode()).hexdigest()
    assert description['source_sha256'] == hashlib.sha256(SIMPLE.encode()).hexdigest()
    assert ET.fromstring(description['xml']).find('link').get('name') == 'base_link'
    (tmp_path / 'robot.urdf').write_text(SIMPLE.replace('0 0 0.2', '0 0 0.3'))
    assert '0 0 0.2' in description['xml']
    assert load_inventory(path).assets[0]['config_digest'] != asset['config_digest']


def test_description_merges_model_and_instance_with_declaring_file_paths(tmp_path):
    (tmp_path / 'models').mkdir()
    (tmp_path / 'models/model.urdf').write_text(SIMPLE)
    path = write_inventory(tmp_path, model_description={
        'file': 'model.urdf', 'publish_frequency': 40, 'use_sim_time': True,
    }, description={'joint_states_topic': '/hardware/joints'})
    result = load_inventory(path).assets[0]['description']
    assert result['file'] == str(tmp_path / 'models/model.urdf')
    assert result['publish_frequency'] == 40
    assert result['use_sim_time'] is True
    assert result['joint_states_topic'] == '/hardware/joints'
    data = yaml.safe_load(path.read_text())
    data['assets'][0]['description']['file'] = 'robot.urdf'
    path.write_text(yaml.safe_dump(data))
    assert load_inventory(path).assets[0]['description']['file'] == str(tmp_path / 'robot.urdf')


def test_description_can_disable_unavailable_inherited_file(tmp_path):
    result = load_inventory(write_inventory(tmp_path, model_description={'file': 'not-installed.urdf'},
                                            description={'enabled': False})).assets[0]['description']
    assert result == {'enabled': False}


def test_replacing_model_file_reinfers_format_and_replaces_arguments(tmp_path):
    result = load_inventory(write_inventory(tmp_path,
        model_description={'file': 'original.xacro', 'format': 'xacro', 'arguments': {'size': 1}},
        description={'file': 'robot.urdf'})).assets[0]['description']
    assert result['format'] == 'urdf'
    assert result['arguments'] == {}


def test_fifo_description_source_is_rejected_without_blocking(tmp_path):
    os.mkfifo(tmp_path / 'pipe.urdf')
    with pytest.raises(ConfigurationError, match='regular file'):
        load_inventory(write_inventory(tmp_path, description={'file': 'pipe.urdf'}))


@pytest.mark.parametrize('value,match', [
    ({'typo': True}, 'unknown fields'),
    ({'enabled': 'false'}, 'boolean'),
    ({'file': 123}, 'string'),
    ({'format': 'sdf'}, 'format'),
    ({'format': []}, 'format'),
    ({'arguments': []}, 'mapping'),
    ({'arguments': {'bad-name': 1}}, 'arguments name'),
    ({'arguments': {'width': []}}, 'finite scalar'),
    ({'arguments': {'width': float('nan')}}, 'finite scalar'),
    ({'arguments': {'width': 10 ** 400}}, 'finite scalar|64-bit range'),
    ({'frame_prefix': 'car'}, 'frame_prefix'),
    ({'frame_prefix': '/car/'}, 'frame_prefix'),
    ({'frame_prefix': 'car//'}, 'frame_prefix'),
    ({'frame_prefix': None}, 'frame_prefix'),
    ({'publish_frequency': 0}, 'publish_frequency'),
    ({'publish_frequency': True}, 'publish_frequency'),
    ({'publish_frequency': float('inf')}, 'publish_frequency'),
    ({'joint_states_topic': '../joints'}, 'joint_states_topic'),
    ({'use_sim_time': 'true'}, 'boolean'),
])
def test_disabled_description_still_checks_types_and_unknown_fields(tmp_path, value, match):
    with pytest.raises(ConfigurationError, match=match):
        load_inventory(write_inventory(tmp_path, description={'enabled': False, **value}))


@pytest.mark.parametrize('value', [None, False, [], 'robot.urdf'])
def test_description_must_be_mapping(tmp_path, value):
    with pytest.raises(ConfigurationError, match='mapping'):
        load_inventory(write_inventory(tmp_path, mutate=lambda data: data['assets'][0].update(description=value)))


def test_enabled_description_requires_existing_file_and_valid_format(tmp_path):
    with pytest.raises(ConfigurationError, match='file'):
        load_inventory(write_inventory(tmp_path, description={}))
    with pytest.raises(ConfigurationError, match='cannot read description'):
        load_inventory(write_inventory(tmp_path, description={'file': 'missing.urdf'}))
    with pytest.raises(ConfigurationError, match='cannot infer format'):
        load_inventory(write_inventory(tmp_path, description={'file': 'robot.xml'}))
    with pytest.raises(ConfigurationError, match='only Xacro'):
        load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf', 'arguments': {'width': 1}}))


def test_description_accepts_installed_package_resources(tmp_path):
    # The package index is the only source of a package resource, so the test
    # uses a resource of an installed package. The native engine reads the
    # index of the sourced ROS installation.
    share = Path(ament_index_python.packages.get_package_share_directory('chaos_assets'))
    resource = share / 'models/demo_ugv/v1/urdf/robot.urdf'
    result = load_inventory(
        write_inventory(tmp_path, description={'file': 'package://chaos_assets/models/demo_ugv/v1/urdf/robot.urdf'}))
    # The engine reports the real path of the resource, so a symlinked
    # installation and its source tree name the same file.
    assert result.assets[0]['description']['file'] == os.path.realpath(resource)


def test_custom_empty_prefix_and_relative_joint_state_topic(tmp_path):
    result = load_inventory(write_inventory(tmp_path, description={
        'file': 'robot.urdf', 'frame_prefix': '', 'joint_states_topic': 'actuators/joints',
    }, mutate=lambda data: data['assets'][0].update(frames={'base': 'base_link'}, namespace='/mobile/car')))
    assert result.assets[0]['description']['root'] == 'base_link'
    assert result.assets[0]['description']['joint_states_topic'] == '/mobile/car/actuators/joints'


def test_root_must_match_robot_base(tmp_path):
    with pytest.raises(ConfigurationError, match='must equal asset.frames.base'):
        load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf'}, xml=SIMPLE.replace('base_link', 'root')))


@pytest.mark.parametrize('xml,match', [
    ('<not_robot/>', 'root must be robot'),
    ('<robot>', 'invalid URDF XML'),
    ('<!DOCTYPE robot><robot name="r"><link name="base_link"/></robot>', 'DOCTYPE'),
    ('<!DOCTYPE robot [<!ENTITY e "x">]><robot name="r"><link name="base_link"/></robot>', 'DOCTYPE'),
    ('<robot name="r"><link name="base_link"/><link name="base_link"/></robot>', 'duplicate URDF link'),
    ('<robot name="r"><link name="base_link"/><link name="detached"/></robot>', 'single rooted tree'),
    (SIMPLE.replace('name="imu_link"', 'name="bad link"'), 'link frame'),
    (SIMPLE.replace('type="fixed"', 'type="floating"'), 'unsupported type'),
    (SIMPLE.replace('type="fixed"', 'type="planar"'), 'unsupported type'),
    (SIMPLE.replace('type="fixed"', 'type="revolute"'), 'invalid URDF'),
    (SIMPLE.replace('<parent link="base_link"/>', '<parent link="missing"/>'), 'unknown link'),
    (SIMPLE.replace('<parent link="base_link"/>', ''), 'requires parent and child'),
    (SIMPLE.replace('xyz="0 0 0.2"', 'xyz="not-number 0 0"'), 'invalid URDF'),
])
def test_rejects_invalid_urdf_before_launch(tmp_path, xml, match):
    with pytest.raises(ConfigurationError, match=match):
        load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf'}, xml=xml))


@pytest.mark.parametrize('namespace', ['http://www.ros.org/wiki/xacro', 'http://ros.org/wiki/xacro'])
@pytest.mark.parametrize('content', ['<xacro:include filename="mount.xacro"/>',
                                   '<link name="base_link" xacro:custom="value"/>'])
def test_unexpanded_xacro_cannot_silently_be_loaded_as_urdf(tmp_path, namespace, content):
    xml = f'<robot name="unexpanded" xmlns:xacro="{namespace}"><link name="base_link"/>{content}</robot>'
    with pytest.raises(ConfigurationError, match='unexpanded Xacro.*format: xacro'):
        load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf'}, xml=xml))


def test_unused_xacro_namespace_is_allowed_in_expanded_urdf(tmp_path):
    xml = SIMPLE.replace('<robot ', '<robot xmlns:xacro="http://www.ros.org/wiki/xacro" ')
    assert load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf'}, xml=xml))


def test_duplicate_joint_child_and_cycle_are_rejected_before_native_parser(tmp_path):
    duplicate = SIMPLE.replace('</robot>', '''<joint name="duplicate" type="fixed">
      <parent link="base_link"/><child link="imu_link"/></joint></robot>''')
    with pytest.raises(ConfigurationError, match='duplicate URDF joint child'):
        load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf'}, xml=duplicate))
    cycle = SIMPLE.replace('</robot>', '''<joint name="cycle" type="fixed">
      <parent link="imu_link"/><child link="base_link"/></joint></robot>''')
    with pytest.raises(ConfigurationError, match='URDF transform cycle'):
        load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf'}, xml=cycle))


def test_source_size_and_process_limits(tmp_path):
    path = write_inventory(tmp_path, description={'file': 'robot.urdf'})
    (tmp_path / 'robot.urdf').write_bytes(b' ' * (1024 * 1024 + 1))
    with pytest.raises(ConfigurationError, match='exceeds 1 MiB'):
        load_inventory(path)
    # The output limit and the deadline of the native process runner are
    # covered by the native suite: test_process.cpp, "EnforcesTheOutputLimit"
    # and "EnforcesTheDeadline".


def mesh_xml(resource):
    return SIMPLE.replace('<link name="base_link"/>', f'''<link name="base_link"><visual>
      <geometry><mesh filename="{resource}"/></geometry></visual></link>''')


@pytest.mark.parametrize('kind', ['relative', 'absolute', 'file', 'package'])
def test_mesh_resources_are_checked_and_portable_paths_preserved(tmp_path, kind):
    mesh = tmp_path / 'shape.stl'
    mesh.write_text('solid test\nendsolid test\n')
    if kind == 'package':
        # A resource of an installed package keeps its portable form.
        resource = 'package://chaos_assets/models/demo_ugv/v1/urdf/robot.urdf'
        expected = resource
    else:
        resource = {'relative': 'shape.stl', 'absolute': str(mesh), 'file': mesh.as_uri()}[kind]
        expected = mesh.as_uri()
    result = load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf'}, xml=mesh_xml(resource)))
    assert ET.fromstring(result.assets[0]['description']['xml']).find('.//mesh').get('filename') == expected


@pytest.mark.parametrize('resource,match', [
    ('missing.stl', 'resource does not exist'),
    ('https://example.com/mesh.stl', 'remote resource'),
    ('file://host/tmp/mesh.stl', 'without host'),
    ('file:relative.stl', 'absolute local'),
    ('package://fixture/../mesh.stl', 'invalid package'),
    ('mesh.stl?download=true', 'queries/fragments'),
])
def test_mesh_rejects_unavailable_or_remote_resources(tmp_path, resource, match):
    with pytest.raises(ConfigurationError, match=match):
        load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf'}, xml=mesh_xml(resource)))


def test_texture_resources_are_checked(tmp_path):
    xml = SIMPLE.replace('</robot>', '<material name="paint"><texture filename="missing.png"/></material></robot>')
    with pytest.raises(ConfigurationError, match='resource does not exist'):
        load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf'}, xml=xml))


@pytest.mark.parametrize('reserved', ['odom', 'map'])
def test_urdf_cannot_occupy_localization_frame(tmp_path, reserved):
    with pytest.raises(ConfigurationError, match='occupies reserved'):
        load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf'},
                                       xml=SIMPLE.replace('imu_link', reserved)))


def test_same_urdf_on_two_robots_uses_independent_prefixes(tmp_path):
    def add_robot(data):
        second = deepcopy(data['assets'][0])
        second['id'] = 'other'
        data['assets'].append(second)
    assets = load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf'}, mutate=add_robot)).assets
    assert assets[0]['description']['root'] == 'car/base_link'
    assert assets[1]['description']['root'] == 'other/base_link'
    assert not set(assets[0]['description']['links']) & set(assets[1]['description']['links'])


def test_two_urdfs_cannot_publish_same_child_frame_in_same_domain(tmp_path):
    (tmp_path / 'other.urdf').write_text(SIMPLE.replace('base_link', 'other/base_link')
                                      .replace('imu_link', 'car/imu_link'))

    def add_robot(data):
        data['hosts']['other'] = {'ros_domain_id': 87}
        data['assets'].append({'id': 'other', 'host': 'other', 'model_file': 'models/model.yaml',
                               'description': {'file': 'other.urdf', 'frame_prefix': ''}})

    path = write_inventory(tmp_path, description={'file': 'robot.urdf'}, mutate=add_robot)
    with pytest.raises(ConfigurationError, match='duplicate URDF frame'):
        load_inventory(path)
    data = yaml.safe_load(path.read_text())
    data['hosts']['other']['ros_domain_id'] = 88
    path.write_text(yaml.safe_dump(data))
    assert len(load_inventory(path).assets) == 2


@pytest.mark.parametrize('reserved', ['base', 'odom', 'map'])
def test_urdf_cannot_claim_another_robots_reserved_frames(tmp_path, reserved):
    def add_robot(data):
        data['assets'].append({'id': 'other', 'host': 'dev', 'model_file': 'models/model.yaml',
                               'frames': {reserved: 'car/imu_link'}})
    with pytest.raises(ConfigurationError, match='occupies reserved'):
        load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf'}, mutate=add_robot))


def test_sensor_can_reference_own_urdf_mount_but_must_not_publish_it_twice(tmp_path):
    def add_sensor(data):
        data['assets'][0]['sensors'] = {'imu': sensor()}
    path = write_inventory(tmp_path, description={'file': 'robot.urdf'}, mutate=add_sensor)
    assert load_inventory(path).assets[0]['sensors']['imu']['frame_id'] == 'car/imu_link'
    data = yaml.safe_load(path.read_text())
    data['assets'][0]['sensors']['imu']['transform'] = {'xyz': [0, 0, 0.2], 'rpy': [0, 0, 0]}
    path.write_text(yaml.safe_dump(data))
    with pytest.raises(ConfigurationError, match='duplicate transform publisher'):
        load_inventory(path)


def test_sensor_can_extend_urdf_tree_with_measured_mount(tmp_path):
    def add_sensor(data):
        data['assets'][0]['sensors'] = {'extra': sensor('car/extra_imu', transform={
            'parent': 'car/imu_link', 'xyz': [0, 0, 0.2], 'rpy': [0, 0, 0],
        })}
    assert load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf'}, mutate=add_sensor))


@pytest.mark.parametrize('claim', ['sensor', 'stream'])
def test_other_robot_sensor_cannot_claim_urdf_frame(tmp_path, claim):
    def add_robot(data):
        other_sensor = sensor('other/imu')
        if claim == 'sensor':
            other_sensor['frame_id'] = 'car/imu_link'
        else:
            other_sensor['streams']['data']['frame_id'] = 'car/imu_link'
        data['assets'].append({'id': 'other', 'host': 'dev', 'model_file': 'models/model.yaml',
                               'sensors': {'imu': other_sensor}})
    with pytest.raises(ConfigurationError, match='belongs to another asset URDF'):
        load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf'}, mutate=add_robot))


def test_different_domains_and_disabled_assets_do_not_reserve_urdf_frames(tmp_path):
    def add_robot(data):
        data['hosts']['other'] = {'ros_domain_id': 88}
        data['assets'].append({'id': 'other', 'host': 'other', 'model_file': 'models/model.yaml',
                               'sensors': {'imu': sensor()}})
    path = write_inventory(tmp_path, description={'file': 'robot.urdf'}, mutate=add_robot)
    assert load_inventory(path)
    data = yaml.safe_load(path.read_text())
    data['hosts']['other']['ros_domain_id'] = 87
    data['assets'][0]['enabled'] = False
    path.write_text(yaml.safe_dump(data))
    assert load_inventory(path)


def test_combined_graph_cycle_is_checked():
    # Direct graph unit test: normal inventory validation additionally protects
    # base_link from sensor ownership before this cross-source graph check.
    asset = {'id': 'car', 'enabled': True, 'ros_domain_id': 87, 'frames': {'base': 'car/base'},
             'description': {'enabled': True, 'links': ['car/base', 'car/imu'],
                             'joints': [{'parent': 'car/base', 'child': 'car/imu'}]},
             'sensors': {'extra': {'id': 'extra', 'enabled': True, 'frame_id': 'car/base',
                                    'streams': {}, 'transform': {'parent': 'car/imu'}}}}
    with pytest.raises(ConfigurationError, match='URDF/sensor transform cycle'):
        descriptions.validate_description_graph([asset])


def moving_xml(mimic_a='', mimic_b='', mimic_c=''):
    parts = ['<robot name="mimics"><link name="base_link"/>']
    for name, mimic in [('a', mimic_a), ('b', mimic_b), ('c', mimic_c)]:
        parts.append(f'''<link name="{name}_link"/><joint name="{name}" type="continuous">
          <parent link="base_link"/><child link="{name}_link"/>{mimic}</joint>''')
    return ''.join(parts) + '</robot>'


@pytest.mark.parametrize('xml,match', [
    (moving_xml('<mimic joint="missing"/>'), 'existing movable'),
    (moving_xml('<mimic joint="b"/>', '<mimic joint="a"/>'), 'mimic joint cycle'),
    (moving_xml('<mimic joint="b"/>', '<mimic joint="c"/>'), 'chained mimic'),
])
def test_invalid_mimic_relations_are_rejected(tmp_path, xml, match):
    with pytest.raises(ConfigurationError, match=match):
        load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf'}, xml=xml))


def test_direct_mimic_relation_is_supported(tmp_path):
    assert load_inventory(write_inventory(tmp_path, description={'file': 'robot.urdf'},
                                          xml=moving_xml('<mimic joint="b" multiplier="-1"/>')))


def test_xacro_missing_dependency_is_actionable(tmp_path):
    # The bridge reports the missing dependency, so a deployment without the
    # xacro package gets an actionable message instead of a traceback. The
    # bridge is started with an empty module path, so the case does not
    # depend on the xacro package of the machine.
    bridge = Path(__file__).resolve().parents[1] / 'tools' / 'xacro_expand.py'
    request = tmp_path / 'request.json'
    request.write_text(json.dumps({'file': str(tmp_path / 'robot.xacro'),
                                   'source': SIMPLE, 'arguments': {}}))
    environment = dict(os.environ)
    environment['PYTHONPATH'] = str(tmp_path / 'empty')
    environment['PYTHONNOUSERSITE'] = '1'
    completed = subprocess.run([sys.executable, str(bridge), str(request)],
                               capture_output=True, text=True, env=environment)
    assert completed.returncode != 0
    assert 'install ros-jazzy-xacro' in completed.stderr


@pytest.mark.skipif(importlib.util.find_spec('xacro') is None, reason='xacro is an optional local test dependency')
def test_real_xacro_arguments_include_and_snapshot(tmp_path):
    (tmp_path / 'mount.xacro').write_text('''<robot xmlns:xacro="http://www.ros.org/wiki/xacro">
      <xacro:macro name="mount" params="height">
        <link name="imu_link"/><joint name="imu_mount" type="fixed">
          <parent link="base_link"/><child link="imu_link"/><origin xyz="0 0 ${height}"/>
        </joint>
      </xacro:macro></robot>''')
    (tmp_path / 'robot.xacro').write_text('''<robot name="xacro_test" xmlns:xacro="http://www.ros.org/wiki/xacro">
      <xacro:arg name="height" default="0.2"/><xacro:include filename="mount.xacro"/>
      <link name="base_link"/><xacro:mount height="$(arg height)"/>
    </robot>''')
    path = write_inventory(tmp_path, description={'file': 'robot.xacro', 'arguments': {'height': 0.5}})
    asset = load_inventory(path).assets[0]
    assert ET.fromstring(asset['description']['xml']).find('.//origin').get('xyz') == '0 0 0.5'
    (tmp_path / 'mount.xacro').write_text((tmp_path / 'mount.xacro').read_text().replace('0 0 ${height}', '0 ${height} 0'))
    assert asset['config_digest'] != load_inventory(path).assets[0]['config_digest']


@pytest.mark.skipif(importlib.util.find_spec('xacro') is None, reason='xacro is an optional local test dependency')
def test_xacro_include_rejects_entity_declarations(tmp_path):
    (tmp_path / 'mount.xacro').write_text('<!DOCTYPE robot><robot/>')
    (tmp_path / 'robot.xacro').write_text('''<robot name="xacro_test" xmlns:xacro="http://www.ros.org/wiki/xacro">
      <xacro:include filename="mount.xacro"/><link name="base_link"/></robot>''')
    with pytest.raises(ConfigurationError, match='DOCTYPE'):
        load_inventory(write_inventory(tmp_path, description={'file': 'robot.xacro'}))


@pytest.mark.skipif(importlib.util.find_spec('xacro') is None, reason='xacro is an optional local test dependency')
def test_xacro_missing_optional_include_keeps_relative_include_stack(tmp_path):
    (tmp_path / 'mount.xacro').write_text('<robot><link name="base_link"/></robot>')
    (tmp_path / 'robot.xacro').write_text('''<robot name="xacro_test" xmlns:xacro="http://www.ros.org/wiki/xacro">
      <xacro:include filename="missing.xacro" optional="true"/>
      <xacro:include filename="mount.xacro"/></robot>''')
    asset = load_inventory(write_inventory(tmp_path, description={'file': 'robot.xacro'})).assets[0]
    assert asset['description']['links'] == ['car/base_link']


@pytest.mark.skipif(importlib.util.find_spec('xacro') is None, reason='xacro is an optional local test dependency')
def test_xacro_fifo_include_is_rejected(tmp_path):
    os.mkfifo(tmp_path / 'pipe.xacro')
    (tmp_path / 'robot.xacro').write_text('''<robot name="xacro_test" xmlns:xacro="http://www.ros.org/wiki/xacro">
      <xacro:include filename="pipe.xacro"/><link name="base_link"/></robot>''')
    with pytest.raises(ConfigurationError, match='regular file'):
        load_inventory(write_inventory(tmp_path, description={'file': 'robot.xacro'}))


@pytest.mark.skipif(importlib.util.find_spec('xacro') is None, reason='xacro is an optional local test dependency')
def test_xacro_oversized_include_is_rejected(tmp_path):
    # An include is bounded, so a huge file never reaches the memory of the
    # launcher.
    (tmp_path / 'huge.xacro').write_bytes(b' ' * (1024 * 1024 + 1))
    (tmp_path / 'robot.xacro').write_text('''<robot name="xacro_test" xmlns:xacro="http://www.ros.org/wiki/xacro">
      <xacro:include filename="huge.xacro"/><link name="base_link"/></robot>''')
    with pytest.raises(ConfigurationError, match='exceeds 1 MiB'):
        load_inventory(write_inventory(tmp_path, description={'file': 'robot.xacro'}))

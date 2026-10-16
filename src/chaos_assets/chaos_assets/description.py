"""Freeze trusted local URDF/Xacro descriptions before any ROS process starts.

Xacro is executable local configuration, not a sandbox for untrusted input.
Expansion and native URDF validation run in bounded child processes; no shell
command is assembled from configuration. Model coordinates remain untouched.
"""

from copy import deepcopy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import selectors
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time
from urllib.parse import unquote, urlsplit
import xml.etree.ElementTree as ET

from .inventory import ConfigurationError, _merge, _record, _resolve_path, _text
from .sensors import _FRAME, _topic


_FIELDS = {'enabled', 'file', 'format', 'arguments', 'frame_prefix',
           'publish_frequency', 'joint_states_topic', 'use_sim_time'}
_ARGUMENT = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')
_SOURCE_LIMIT = 1024 * 1024
_XML_LIMIT = 4 * 1024 * 1024
_PROCESS_TIMEOUT = 10.0
_XACRO_NAMESPACES = ('{http://www.ros.org/wiki/xacro}', '{http://ros.org/wiki/xacro}')
_XACRO_PROGRAM = '''import json, os, re, stat, sys
try:
    import xacro
except ImportError:
    sys.stderr.write("Xacro requires the xacro package; install ros-jazzy-xacro and source the ROS environment\\n")
    sys.exit(2)
try:
    request = json.load(sys.stdin)
    original_parse = xacro.parse
    def checked_parse(inp, filename=None):
        if inp is None:
            try:
                descriptor = os.open(filename, os.O_RDONLY | os.O_NONBLOCK)
            except OSError as error:
                xacro.filestack.pop()
                raise xacro.XacroException(error.strerror + ": " + error.filename, exc=error)
            try:
                if not stat.S_ISREG(os.fstat(descriptor).st_mode):
                    raise ValueError("Xacro include must be a regular file")
                with os.fdopen(descriptor, "rb", closefd=False) as stream:
                    inp = stream.read(1024 * 1024 + 1)
            finally:
                os.close(descriptor)
            if len(inp) > 1024 * 1024:
                raise ValueError("Xacro include exceeds 1 MiB")
            inp = inp.decode("utf-8")
        if isinstance(inp, str) and re.search(r"<!\\s*(?:DOCTYPE|ENTITY)\\b", inp, re.IGNORECASE):
            raise ValueError("DOCTYPE and ENTITY declarations are not allowed")
        return original_parse(inp, filename)
    xacro.parse = checked_parse
    xacro.init_stacks(request["file"])
    document = xacro.parse(request["source"], request["file"])
    xacro.process_doc(document, mappings=request["arguments"])
    sys.stdout.write(document.toxml())
except Exception as error:
    sys.stderr.write(str(error) + "\\n")
    sys.exit(1)
'''


def _finite(value):
    try:
        return type(value) in (int, float) and math.isfinite(value)
    except OverflowError:
        return False


def _declaration(value, directory, where):
    result = deepcopy(_record(value, where, _FIELDS))
    for key in ('enabled', 'use_sim_time'):
        if key in result and type(result[key]) is not bool:
            raise ConfigurationError(f'{where}.{key}: expected boolean')
    if 'file' in result:
        result['file'] = str(_resolve_path(result['file'], directory, f'{where}.file'))
    if 'format' in result and result['format'] not in ('urdf', 'xacro'):
        raise ConfigurationError(f'{where}.format: expected urdf or xacro')
    if 'arguments' in result:
        for name, value in _record(result['arguments'], f'{where}.arguments').items():
            _text(name, f'{where}.arguments name', _ARGUMENT)
            if not (type(value) in (str, bool) or _finite(value)):
                raise ConfigurationError(f'{where}.arguments.{name}: expected a finite scalar')
    if 'frame_prefix' in result:
        prefix = result['frame_prefix']
        if not isinstance(prefix, str) or (prefix and (
                not prefix.endswith('/') or not _FRAME.fullmatch(prefix[:-1]))):
            raise ConfigurationError(f'{where}.frame_prefix: expected empty string or a frame path ending in /')
    if 'publish_frequency' in result:
        value = result['publish_frequency']
        if not _finite(value) or not 0.1 <= value <= 1000:
            raise ConfigurationError(f'{where}.publish_frequency: expected finite number in [0.1, 1000]')
    if 'joint_states_topic' in result:
        # Validate now; resolve relative names against the final asset namespace.
        _topic(result['joint_states_topic'], '/unused', f'{where}.joint_states_topic')
    return result


def _read_source(path, where):
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
        try:
            if not stat.S_ISREG(os.fstat(descriptor).st_mode):
                raise ConfigurationError(f'{where}: description source must be a regular file')
            with os.fdopen(descriptor, 'rb', closefd=False) as stream:
                source = stream.read(_SOURCE_LIMIT + 1)
        finally:
            os.close(descriptor)
        if len(source) > _SOURCE_LIMIT:
            raise ConfigurationError(f'{where}: description source exceeds 1 MiB')
        return source.decode('utf-8')
    except (OSError, UnicodeError) as error:
        raise ConfigurationError(f'{where}: cannot read description {path}: {error}') from error


def _reject_entities(xml, where):
    if re.search(r'<!\s*(?:DOCTYPE|ENTITY)\b', xml, re.IGNORECASE):
        raise ConfigurationError(f'{where}: DOCTYPE and ENTITY declarations are not allowed')


def _run_bounded(command, where, input_bytes=b'', limit=_XML_LIMIT):
    """Bound both stdout and stderr while draining them to avoid pipe deadlocks."""
    with tempfile.TemporaryFile() as source:
        source.write(input_bytes)
        source.seek(0)
        try:
            process = subprocess.Popen(command, stdin=source, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, start_new_session=True)
        except OSError as error:
            raise ConfigurationError(f'{where}: failed to execute {command[0]}: {error}') from error
        buffers = {'stdout': bytearray(), 'stderr': bytearray()}
        deadline = time.monotonic() + _PROCESS_TIMEOUT
        completed = False
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ, 'stdout')
                selector.register(process.stderr, selectors.EVENT_READ, 'stderr')
                total = 0
                while selector.get_map():
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise ConfigurationError(f'{where}: validation/expansion timed out after {_PROCESS_TIMEOUT:g}s')
                    for key, _ in selector.select(remaining):
                        chunk = os.read(key.fileobj.fileno(), 65536)
                        if not chunk:
                            selector.unregister(key.fileobj)
                            continue
                        total += len(chunk)
                        if total > limit:
                            raise ConfigurationError(f'{where}: validation/expansion output exceeds {limit} bytes')
                        buffers[key.data].extend(chunk)
                try:
                    code = process.wait(timeout=max(0.001, deadline - time.monotonic()))
                except subprocess.TimeoutExpired as error:
                    raise ConfigurationError(f'{where}: validation/expansion timed out') from error
            output = bytes(buffers['stdout']).decode('utf-8')
            if code:
                detail = bytes(buffers['stderr'] or buffers['stdout']).decode('utf-8', errors='replace')[:2048]
                raise ConfigurationError(f'{where}: validation/expansion failed: {detail.strip()}')
            completed = True
            return output
        except UnicodeError as error:
            raise ConfigurationError(f'{where}: description output is not UTF-8') from error
        finally:
            # Also terminate inherited pipe holders if a trusted Xacro include
            # accidentally spawns a child which outlives the expansion timeout.
            if not completed:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            process.wait()
            process.stdout.close()
            process.stderr.close()


def _resource(value, directory, where):
    _text(value, where)
    try:
        uri = urlsplit(value)
    except ValueError as error:
        raise ConfigurationError(f'{where}: invalid resource URI') from error
    if uri.query or uri.fragment:
        raise ConfigurationError(f'{where}: resource queries/fragments are not supported')
    if uri.scheme == 'package':
        path = _resolve_path(value, directory, where)
        normalized = value
    elif uri.scheme == 'file':
        if uri.netloc or not uri.path.startswith('/'):
            raise ConfigurationError(f'{where}: file resource must be an absolute local file URI without host')
        path = Path(unquote(uri.path)).resolve()
        normalized = path.as_uri()
    elif uri.scheme or uri.netloc:
        raise ConfigurationError(f'{where}: remote resource URI is not supported')
    else:
        path = _resolve_path(value, directory, where)
        normalized = path.as_uri()
    if not path.is_file():
        raise ConfigurationError(f'{where}: mesh/texture resource does not exist: {path}')
    return normalized


def _parse(xml, directory, where):
    _reject_entities(xml, where)
    try:
        root = ET.fromstring(xml)
    except (ET.ParseError, ValueError, RecursionError) as error:
        raise ConfigurationError(f'{where}: invalid URDF XML: {error}') from error
    if root.tag != 'robot':
        raise ConfigurationError(f'{where}: URDF root must be robot')
    for element in root.iter():
        if any(name.startswith(_XACRO_NAMESPACES) for name in (element.tag, *element.attrib)):
            raise ConfigurationError(f'{where}: unexpanded Xacro element or attribute; declare format: xacro')
        if element.tag in ('mesh', 'texture'):
            if 'filename' not in element.attrib:
                raise ConfigurationError(f'{where}: {element.tag} requires filename')
            element.set('filename', _resource(element.attrib['filename'], directory, where))
    try:
        normalized = ET.tostring(root, encoding='unicode')
    except RecursionError as error:
        raise ConfigurationError(f'{where}: URDF XML nesting is too deep') from error
    if len(normalized.encode('utf-8')) > _XML_LIMIT:
        raise ConfigurationError(f'{where}: expanded URDF exceeds 4 MiB')
    return root, normalized


def _native_validate(xml, where):
    executable = shutil.which('check_urdf')
    if executable is None:
        raise ConfigurationError(f'{where}: check_urdf is unavailable; install ros-jazzy-urdfdom and source the ROS environment')
    with tempfile.TemporaryDirectory(prefix='chaos-urdf-') as directory:
        path = Path(directory) / 'robot.urdf'
        path.write_text(xml, encoding='utf-8')
        _run_bounded([executable, str(path)], where)


def _structure(root, prefix, where):
    links, link_names = [], set()
    for link in root.findall('link'):
        frame = prefix + _text(link.get('name'), f'{where}.link.name')
        _text(frame, f'{where}.link frame', _FRAME)
        if frame in link_names:
            raise ConfigurationError(f'{where}: duplicate URDF link {frame}')
        links.append(frame)
        link_names.add(frame)
    if not links:
        raise ConfigurationError(f'{where}: URDF needs at least one link')
    joints, joint_names, parents = [], set(), {}
    for joint in root.findall('joint'):
        name = _text(joint.get('name'), f'{where}.joint.name')
        if name in joint_names:
            raise ConfigurationError(f'{where}: duplicate URDF joint {name}')
        joint_names.add(name)
        if joint.get('type') not in ('fixed', 'revolute', 'continuous', 'prismatic'):
            raise ConfigurationError(f'{where}: joint {name} has unsupported type {joint.get("type")!r}; supported types are fixed, revolute, continuous and prismatic')
        parent, child = joint.find('parent'), joint.find('child')
        if parent is None or child is None:
            raise ConfigurationError(f'{where}: joint {name} requires parent and child')
        parent = prefix + _text(parent.get('link'), f'{where}.joint.parent')
        child = prefix + _text(child.get('link'), f'{where}.joint.child')
        if parent not in link_names or child not in link_names:
            raise ConfigurationError(f'{where}: joint {name} references an unknown link')
        if child in parents:
            raise ConfigurationError(f'{where}: duplicate URDF joint child {child}')
        parents[child] = parent
        joints.append({'name': name, 'type': joint.get('type'), 'parent': parent, 'child': child})
    _acyclic(parents, f'{where}: URDF transform cycle')
    roots = set(links) - set(parents)
    if len(roots) != 1:
        raise ConfigurationError(f'{where}: URDF must form a single rooted tree')
    joint_types = {joint['name']: joint['type'] for joint in joints}
    mimics = {}
    for joint in root.findall('joint'):
        mimic = joint.find('mimic')
        if mimic is None:
            continue
        name, target = joint.get('name'), mimic.get('joint')
        if target not in joint_types or joint_types[target] == 'fixed' or joint_types[name] == 'fixed':
            raise ConfigurationError(f'{where}: mimic joint {name} must reference an existing movable joint')
        mimics[name] = target
    _acyclic(mimics, f'{where}: mimic joint cycle')
    # RSP evaluates mimic values in one ordered pass. A chain is therefore
    # order-dependent; accept direct mimic relations only.
    if any(target in mimics for target in mimics.values()):
        raise ConfigurationError(f'{where}: chained mimic joints are not supported; use a direct movable joint reference')
    return links, joints, roots.pop()


def _acyclic(parents, message):
    complete = set()
    for child in parents:
        frame, seen = child, set()
        while frame in parents and frame not in complete:
            if frame in seen:
                raise ConfigurationError(f'{message}: {frame}')
            seen.add(frame)
            frame = parents[frame]
        complete.update(seen)


def resolve_description(model_description, instance_description, model_directory, inventory_directory, asset):
    if model_description is None and instance_description is None:
        return None
    where = f'{asset["id"]}.description'
    model = _declaration({} if model_description is None else model_description, model_directory, f'{where} (model)')
    instance = _declaration({} if instance_description is None else instance_description, inventory_directory, where)
    if 'file' in instance and instance['file'] != model.get('file'):
        # Parameters and an explicit format belong to their source template.
        # Selecting another template must not inherit incompatible Xacro args.
        model.pop('arguments', None)
        if 'format' not in instance:
            model.pop('format', None)
    description = _merge(model, instance)
    if not description.get('enabled', True):
        return {'enabled': False}
    path = Path(_text(description.get('file'), f'{where}.file'))
    format_name = description.get('format', path.suffix.lstrip('.'))
    if format_name not in ('urdf', 'xacro'):
        raise ConfigurationError(f'{where}.format: cannot infer format; expected urdf or xacro')
    arguments = description.get('arguments', {})
    if format_name == 'urdf' and arguments:
        raise ConfigurationError(f'{where}.arguments: only Xacro accepts arguments')
    source = _read_source(path, where)
    _reject_entities(source, where)
    xml = source
    if format_name == 'xacro':
        mappings = {name: str(value).lower() if type(value) is bool else str(value)
                    for name, value in arguments.items()}
        xml = _run_bounded([sys.executable, '-c', _XACRO_PROGRAM], where,
                           json.dumps({'file': str(path), 'source': source, 'arguments': mappings}).encode('utf-8'))
    root_element, xml = _parse(xml, path.parent, where)
    # Check the tree before urdfdom: malformed cyclic graphs can trigger a
    # native recursive traversal in some urdfdom versions.
    prefix = description.get('frame_prefix', asset['id'] + '/')
    links, joints, root = _structure(root_element, prefix, where)
    _native_validate(xml, where)
    if root != asset['frames']['base']:
        raise ConfigurationError(f'{where}: root frame {root!r} must equal asset.frames.base {asset["frames"]["base"]!r}')
    return {
        'enabled': True, 'file': str(path), 'format': format_name,
        'arguments': deepcopy(arguments), 'frame_prefix': prefix,
        'publish_frequency': float(description.get('publish_frequency', 30.0)),
        'joint_states_topic': _topic(description.get('joint_states_topic', 'joint_states'),
                                     asset['namespace'], f'{where}.joint_states_topic'),
        'use_sim_time': description.get('use_sim_time', False),
        'xml': xml, 'root': root, 'links': links, 'joints': joints,
        'sha256': hashlib.sha256(xml.encode('utf-8')).hexdigest(),
        'source_sha256': hashlib.sha256(source.encode('utf-8')).hexdigest(),
    }


def validate_description_graph(assets):
    """Combine RSP joints and sensor mounting transforms within each DDS domain."""
    active = [asset for asset in assets if asset['enabled']]
    reserved, links, transforms = {}, {}, {}
    for asset in active:
        domain = asset['ros_domain_id']
        for name, frame in asset['frames'].items():
            reserved.setdefault((domain, frame), []).append((asset['id'], name))
    for asset in active:
        description = asset.get('description')
        if not description or not description['enabled']:
            continue
        domain, owner = asset['ros_domain_id'], asset['id']
        for frame in description['links']:
            key = (domain, frame)
            for other_owner, kind in reserved.get(key, []):
                if (other_owner, kind) != (owner, 'base'):
                    raise ConfigurationError(f'{owner}.description: URDF link {frame} occupies reserved {other_owner}.frames.{kind}')
            if key in links:
                raise ConfigurationError(f'duplicate URDF frame in ROS domain {domain}: {frame} ({links[key]}, {owner})')
            links[key] = owner
        for joint in description['joints']:
            transforms[(domain, joint['child'])] = joint['parent']
    for asset in active:
        domain, owner = asset['ros_domain_id'], asset['id']
        for sensor in asset['sensors'].values():
            if not sensor['enabled']:
                continue
            frames = {sensor['frame_id']} | {stream['frame_id'] for stream in sensor['streams'].values()}
            for frame in frames:
                other_owner = links.get((domain, frame))
                if other_owner is not None and other_owner != owner:
                    raise ConfigurationError(f'{owner}.sensors.{sensor["id"]}: frame {frame} belongs to another asset URDF ({other_owner})')
            if 'transform' in sensor:
                child = (domain, sensor['frame_id'])
                if child in transforms:
                    raise ConfigurationError(f'duplicate transform publisher in ROS domain {domain}: {child[1]} is already published by URDF')
                transforms[child] = sensor['transform']['parent']
    _acyclic({key: (key[0], value) for key, value in transforms.items()}, 'URDF/sensor transform cycle')

"""Launch the optional skill/agent layer for one resolved asset.

Unlike sensors or drivers, the skill layer is not derived from the asset
contract: it is an opt-in add-on selected at launch time. The vocabulary file
is read here (before preflight) so a malformed document fails fast with the
asset's own error reporting rather than at process start.
"""

import json
from pathlib import Path

from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue

from .nodes import node_action


def _read_vocabulary(path):
    """Load the versioned skill vocabulary JSON for the asset."""
    return json.loads(Path(path).read_text(encoding='utf-8'))


def skill_processes(asset, environment, *, enable_agent=False,
                    vocabulary_file=None, initial_hub=None,
                    llama_endpoint=None, task=None, iteration_hz=None):
    """Return (label, process) pairs for the skill layer of one asset.

    With ``vocabulary_file`` unset the layer is disabled and the function
    returns an empty list. With ``enable_agent`` unset only the skill server
    runs; the agent node is added when an LLM endpoint is supplied.
    """
    if not vocabulary_file:
        return []
    vocab = _read_vocabulary(vocabulary_file)
    namespace = asset['namespace']
    asset_id = asset['id']
    vocab_json = json.dumps(vocab)

    common = {
        'asset_id': asset_id,
        'vocabulary_json': ParameterValue(vocab_json, value_type=str),
    }
    if initial_hub:
        common['initial_hub'] = initial_hub

    specs = [
        (
            f'SkillServer {asset_id}',
            {
                'package': 'chaos_skill',
                'executable': 'chaos_skill_server',
                'name': 'chaos_skill_server',
                'namespace': namespace,
                'parameters': [dict(common)],
                'arguments': [],
                'remappings': [],
            },
        ),
    ]

    if enable_agent and llama_endpoint:
        agent_params = {
            'asset_id': asset_id,
            # The agent lives in the same namespace as the asset; skill_ns is
            # only for when it is placed elsewhere, so default to empty.
            'skill_ns': '',
            'vocabulary_json': ParameterValue(vocab_json, value_type=str),
            'llama_endpoint': llama_endpoint,
        }
        if task:
            agent_params['task'] = task
        if iteration_hz is not None:
            agent_params['iteration_hz'] = float(iteration_hz)
        specs.append(
            (
                f'Agent {asset_id}',
                {
                    'package': 'chaos_agent',
                    'executable': 'chaos_agent_node',
                    'name': 'chaos_agent',
                    'namespace': namespace,
                    'parameters': [agent_params],
                    'arguments': [],
                    'remappings': [],
                },
            ))

    return [(label, node_action(spec, environment)) for label, spec in specs]

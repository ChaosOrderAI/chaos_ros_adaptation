"""Small capability checks for the upstream Humble and Jazzy Python bindings.

Humble's SequentialReader owns its storage through a C++ destructor; it has
neither close() nor set_read_order(). Do not infer file order from that API.
"""


def close_bag(handle):
    """Close explicitly when exposed; callers must also release the handle."""
    close = getattr(handle, 'close', None)
    if close is not None:
        close()


def request_read_order(reader, rosbag2_py, order):
    """Return whether the binding and storage accepted an explicit read order."""
    if not all((hasattr(reader, 'set_read_order'), hasattr(rosbag2_py, 'ReadOrder'),
                hasattr(rosbag2_py, 'ReadOrderSortBy'))):
        return False
    try:
        return bool(reader.set_read_order(rosbag2_py.ReadOrder(
            getattr(rosbag2_py.ReadOrderSortBy, order), False)))
    except RuntimeError:
        return False


def topic_metadata(rosbag2_py, *, name, type_name, topic_id=0):
    """Construct write metadata with Humble's constructor or Jazzy's added id."""
    values = {'name': name, 'type': type_name, 'serialization_format': 'cdr'}
    if hasattr(rosbag2_py.TopicMetadata, 'id'):
        values['id'] = topic_id
    return rosbag2_py.TopicMetadata(**values)

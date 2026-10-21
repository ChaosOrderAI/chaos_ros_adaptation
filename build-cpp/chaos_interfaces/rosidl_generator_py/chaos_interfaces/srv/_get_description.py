# generated from rosidl_generator_py/resource/_idl.py.em
# with input from chaos_interfaces:srv/GetDescription.idl
# generated code does not contain a copyright notice

# This is being done at the module level and not on the instance level to avoid looking
# for the same variable multiple times on each instance. This variable is not supposed to
# change during runtime so it makes sense to only look for it once.
from os import getenv

ros_python_check_fields = getenv('ROS_PYTHON_CHECK_FIELDS', default='')


# Import statements for member types

import rosidl_parser.definition  # noqa: E402, I100


class Metaclass_GetDescription_Request(type):
    """Metaclass of message 'GetDescription_Request'."""

    _CREATE_ROS_MESSAGE = None
    _CONVERT_FROM_PY = None
    _CONVERT_TO_PY = None
    _DESTROY_ROS_MESSAGE = None
    _TYPE_SUPPORT = None

    __constants = {
    }

    @classmethod
    def __import_type_support__(cls):
        try:
            from rosidl_generator_py import import_type_support
            module = import_type_support('chaos_interfaces')
        except ImportError:
            import logging
            import traceback
            logger = logging.getLogger(
                'chaos_interfaces.srv.GetDescription_Request')
            logger.debug(
                'Failed to import needed modules for type support:\n' +
                traceback.format_exc())
        else:
            cls._CREATE_ROS_MESSAGE = module.create_ros_message_msg__srv__get_description__request
            cls._CONVERT_FROM_PY = module.convert_from_py_msg__srv__get_description__request
            cls._CONVERT_TO_PY = module.convert_to_py_msg__srv__get_description__request
            cls._TYPE_SUPPORT = module.type_support_msg__srv__get_description__request
            cls._DESTROY_ROS_MESSAGE = module.destroy_ros_message_msg__srv__get_description__request

    @classmethod
    def __prepare__(cls, name, bases, **kwargs):
        # list constant names here so that they appear in the help text of
        # the message class under "Data and other attributes defined here:"
        # as well as populate each message instance
        return {
        }


class GetDescription_Request(metaclass=Metaclass_GetDescription_Request):
    """Message class 'GetDescription_Request'."""

    __slots__ = [
        '_check_fields',
    ]

    _fields_and_field_types = {
    }

    # This attribute is used to store an rosidl_parser.definition variable
    # related to the data type of each of the components the message.
    SLOT_TYPES = (
    )

    def __init__(self, **kwargs):
        if 'check_fields' in kwargs:
            self._check_fields = kwargs['check_fields']
        else:
            self._check_fields = ros_python_check_fields == '1'
        if self._check_fields:
            assert all('_' + key in self.__slots__ for key in kwargs.keys()), \
                'Invalid arguments passed to constructor: %s' % \
                ', '.join(sorted(k for k in kwargs.keys() if '_' + k not in self.__slots__))

    def __repr__(self):
        typename = self.__class__.__module__.split('.')
        typename.pop()
        typename.append(self.__class__.__name__)
        args = []
        for s, t in zip(self.get_fields_and_field_types().keys(), self.SLOT_TYPES):
            field = getattr(self, s)
            fieldstr = repr(field)
            # We use Python array type for fields that can be directly stored
            # in them, and "normal" sequences for everything else.  If it is
            # a type that we store in an array, strip off the 'array' portion.
            if (
                isinstance(t, rosidl_parser.definition.AbstractSequence) and
                isinstance(t.value_type, rosidl_parser.definition.BasicType) and
                t.value_type.typename in ['float', 'double', 'int8', 'uint8', 'int16', 'uint16', 'int32', 'uint32', 'int64', 'uint64']
            ):
                if len(field) == 0:
                    fieldstr = '[]'
                else:
                    if self._check_fields:
                        assert fieldstr.startswith('array(')
                    prefix = "array('X', "
                    suffix = ')'
                    fieldstr = fieldstr[len(prefix):-len(suffix)]
            args.append(s + '=' + fieldstr)
        return '%s(%s)' % ('.'.join(typename), ', '.join(args))

    def __eq__(self, other):
        if not isinstance(other, self.__class__):
            return False
        return True

    @classmethod
    def get_fields_and_field_types(cls):
        from copy import copy
        return copy(cls._fields_and_field_types)


# Import statements for member types

import builtins  # noqa: E402, I100

import math  # noqa: E402, I100

# already imported above
# import rosidl_parser.definition


class Metaclass_GetDescription_Response(type):
    """Metaclass of message 'GetDescription_Response'."""

    _CREATE_ROS_MESSAGE = None
    _CONVERT_FROM_PY = None
    _CONVERT_TO_PY = None
    _DESTROY_ROS_MESSAGE = None
    _TYPE_SUPPORT = None

    __constants = {
    }

    @classmethod
    def __import_type_support__(cls):
        try:
            from rosidl_generator_py import import_type_support
            module = import_type_support('chaos_interfaces')
        except ImportError:
            import logging
            import traceback
            logger = logging.getLogger(
                'chaos_interfaces.srv.GetDescription_Response')
            logger.debug(
                'Failed to import needed modules for type support:\n' +
                traceback.format_exc())
        else:
            cls._CREATE_ROS_MESSAGE = module.create_ros_message_msg__srv__get_description__response
            cls._CONVERT_FROM_PY = module.convert_from_py_msg__srv__get_description__response
            cls._CONVERT_TO_PY = module.convert_to_py_msg__srv__get_description__response
            cls._TYPE_SUPPORT = module.type_support_msg__srv__get_description__response
            cls._DESTROY_ROS_MESSAGE = module.destroy_ros_message_msg__srv__get_description__response

    @classmethod
    def __prepare__(cls, name, bases, **kwargs):
        # list constant names here so that they appear in the help text of
        # the message class under "Data and other attributes defined here:"
        # as well as populate each message instance
        return {
        }


class GetDescription_Response(metaclass=Metaclass_GetDescription_Response):
    """Message class 'GetDescription_Response'."""

    __slots__ = [
        '_asset_id',
        '_model',
        '_model_version',
        '_capabilities',
        '_base_frame',
        '_odom_frame',
        '_map_frame',
        '_linear_limit_mps',
        '_angular_limit_rps',
        '_command_timeout_s',
        '_lease_max_s',
        '_check_fields',
    ]

    _fields_and_field_types = {
        'asset_id': 'string',
        'model': 'string',
        'model_version': 'string',
        'capabilities': 'sequence<string>',
        'base_frame': 'string',
        'odom_frame': 'string',
        'map_frame': 'string',
        'linear_limit_mps': 'double',
        'angular_limit_rps': 'double',
        'command_timeout_s': 'double',
        'lease_max_s': 'double',
    }

    # This attribute is used to store an rosidl_parser.definition variable
    # related to the data type of each of the components the message.
    SLOT_TYPES = (
        rosidl_parser.definition.UnboundedString(),  # noqa: E501
        rosidl_parser.definition.UnboundedString(),  # noqa: E501
        rosidl_parser.definition.UnboundedString(),  # noqa: E501
        rosidl_parser.definition.UnboundedSequence(rosidl_parser.definition.UnboundedString()),  # noqa: E501
        rosidl_parser.definition.UnboundedString(),  # noqa: E501
        rosidl_parser.definition.UnboundedString(),  # noqa: E501
        rosidl_parser.definition.UnboundedString(),  # noqa: E501
        rosidl_parser.definition.BasicType('double'),  # noqa: E501
        rosidl_parser.definition.BasicType('double'),  # noqa: E501
        rosidl_parser.definition.BasicType('double'),  # noqa: E501
        rosidl_parser.definition.BasicType('double'),  # noqa: E501
    )

    def __init__(self, **kwargs):
        if 'check_fields' in kwargs:
            self._check_fields = kwargs['check_fields']
        else:
            self._check_fields = ros_python_check_fields == '1'
        if self._check_fields:
            assert all('_' + key in self.__slots__ for key in kwargs.keys()), \
                'Invalid arguments passed to constructor: %s' % \
                ', '.join(sorted(k for k in kwargs.keys() if '_' + k not in self.__slots__))
        self.asset_id = kwargs.get('asset_id', str())
        self.model = kwargs.get('model', str())
        self.model_version = kwargs.get('model_version', str())
        self.capabilities = kwargs.get('capabilities', [])
        self.base_frame = kwargs.get('base_frame', str())
        self.odom_frame = kwargs.get('odom_frame', str())
        self.map_frame = kwargs.get('map_frame', str())
        self.linear_limit_mps = kwargs.get('linear_limit_mps', float())
        self.angular_limit_rps = kwargs.get('angular_limit_rps', float())
        self.command_timeout_s = kwargs.get('command_timeout_s', float())
        self.lease_max_s = kwargs.get('lease_max_s', float())

    def __repr__(self):
        typename = self.__class__.__module__.split('.')
        typename.pop()
        typename.append(self.__class__.__name__)
        args = []
        for s, t in zip(self.get_fields_and_field_types().keys(), self.SLOT_TYPES):
            field = getattr(self, s)
            fieldstr = repr(field)
            # We use Python array type for fields that can be directly stored
            # in them, and "normal" sequences for everything else.  If it is
            # a type that we store in an array, strip off the 'array' portion.
            if (
                isinstance(t, rosidl_parser.definition.AbstractSequence) and
                isinstance(t.value_type, rosidl_parser.definition.BasicType) and
                t.value_type.typename in ['float', 'double', 'int8', 'uint8', 'int16', 'uint16', 'int32', 'uint32', 'int64', 'uint64']
            ):
                if len(field) == 0:
                    fieldstr = '[]'
                else:
                    if self._check_fields:
                        assert fieldstr.startswith('array(')
                    prefix = "array('X', "
                    suffix = ')'
                    fieldstr = fieldstr[len(prefix):-len(suffix)]
            args.append(s + '=' + fieldstr)
        return '%s(%s)' % ('.'.join(typename), ', '.join(args))

    def __eq__(self, other):
        if not isinstance(other, self.__class__):
            return False
        if self.asset_id != other.asset_id:
            return False
        if self.model != other.model:
            return False
        if self.model_version != other.model_version:
            return False
        if self.capabilities != other.capabilities:
            return False
        if self.base_frame != other.base_frame:
            return False
        if self.odom_frame != other.odom_frame:
            return False
        if self.map_frame != other.map_frame:
            return False
        if self.linear_limit_mps != other.linear_limit_mps:
            return False
        if self.angular_limit_rps != other.angular_limit_rps:
            return False
        if self.command_timeout_s != other.command_timeout_s:
            return False
        if self.lease_max_s != other.lease_max_s:
            return False
        return True

    @classmethod
    def get_fields_and_field_types(cls):
        from copy import copy
        return copy(cls._fields_and_field_types)

    @builtins.property
    def asset_id(self):
        """Message field 'asset_id'."""
        return self._asset_id

    @asset_id.setter
    def asset_id(self, value):
        if self._check_fields:
            assert \
                isinstance(value, str), \
                "The 'asset_id' field must be of type 'str'"
        self._asset_id = value

    @builtins.property
    def model(self):
        """Message field 'model'."""
        return self._model

    @model.setter
    def model(self, value):
        if self._check_fields:
            assert \
                isinstance(value, str), \
                "The 'model' field must be of type 'str'"
        self._model = value

    @builtins.property
    def model_version(self):
        """Message field 'model_version'."""
        return self._model_version

    @model_version.setter
    def model_version(self, value):
        if self._check_fields:
            assert \
                isinstance(value, str), \
                "The 'model_version' field must be of type 'str'"
        self._model_version = value

    @builtins.property
    def capabilities(self):
        """Message field 'capabilities'."""
        return self._capabilities

    @capabilities.setter
    def capabilities(self, value):
        if self._check_fields:
            from collections.abc import Sequence
            from collections.abc import Set
            from collections import UserList
            from collections import UserString
            assert \
                ((isinstance(value, Sequence) or
                  isinstance(value, Set) or
                  isinstance(value, UserList)) and
                 not isinstance(value, str) and
                 not isinstance(value, UserString) and
                 all(isinstance(v, str) for v in value) and
                 True), \
                "The 'capabilities' field must be a set or sequence and each value of type 'str'"
        self._capabilities = value

    @builtins.property
    def base_frame(self):
        """Message field 'base_frame'."""
        return self._base_frame

    @base_frame.setter
    def base_frame(self, value):
        if self._check_fields:
            assert \
                isinstance(value, str), \
                "The 'base_frame' field must be of type 'str'"
        self._base_frame = value

    @builtins.property
    def odom_frame(self):
        """Message field 'odom_frame'."""
        return self._odom_frame

    @odom_frame.setter
    def odom_frame(self, value):
        if self._check_fields:
            assert \
                isinstance(value, str), \
                "The 'odom_frame' field must be of type 'str'"
        self._odom_frame = value

    @builtins.property
    def map_frame(self):
        """Message field 'map_frame'."""
        return self._map_frame

    @map_frame.setter
    def map_frame(self, value):
        if self._check_fields:
            assert \
                isinstance(value, str), \
                "The 'map_frame' field must be of type 'str'"
        self._map_frame = value

    @builtins.property
    def linear_limit_mps(self):
        """Message field 'linear_limit_mps'."""
        return self._linear_limit_mps

    @linear_limit_mps.setter
    def linear_limit_mps(self, value):
        if self._check_fields:
            assert \
                isinstance(value, float), \
                "The 'linear_limit_mps' field must be of type 'float'"
            assert not (value < -1.7976931348623157e+308 or value > 1.7976931348623157e+308) or math.isinf(value), \
                "The 'linear_limit_mps' field must be a double in [-1.7976931348623157e+308, 1.7976931348623157e+308]"
        self._linear_limit_mps = value

    @builtins.property
    def angular_limit_rps(self):
        """Message field 'angular_limit_rps'."""
        return self._angular_limit_rps

    @angular_limit_rps.setter
    def angular_limit_rps(self, value):
        if self._check_fields:
            assert \
                isinstance(value, float), \
                "The 'angular_limit_rps' field must be of type 'float'"
            assert not (value < -1.7976931348623157e+308 or value > 1.7976931348623157e+308) or math.isinf(value), \
                "The 'angular_limit_rps' field must be a double in [-1.7976931348623157e+308, 1.7976931348623157e+308]"
        self._angular_limit_rps = value

    @builtins.property
    def command_timeout_s(self):
        """Message field 'command_timeout_s'."""
        return self._command_timeout_s

    @command_timeout_s.setter
    def command_timeout_s(self, value):
        if self._check_fields:
            assert \
                isinstance(value, float), \
                "The 'command_timeout_s' field must be of type 'float'"
            assert not (value < -1.7976931348623157e+308 or value > 1.7976931348623157e+308) or math.isinf(value), \
                "The 'command_timeout_s' field must be a double in [-1.7976931348623157e+308, 1.7976931348623157e+308]"
        self._command_timeout_s = value

    @builtins.property
    def lease_max_s(self):
        """Message field 'lease_max_s'."""
        return self._lease_max_s

    @lease_max_s.setter
    def lease_max_s(self, value):
        if self._check_fields:
            assert \
                isinstance(value, float), \
                "The 'lease_max_s' field must be of type 'float'"
            assert not (value < -1.7976931348623157e+308 or value > 1.7976931348623157e+308) or math.isinf(value), \
                "The 'lease_max_s' field must be a double in [-1.7976931348623157e+308, 1.7976931348623157e+308]"
        self._lease_max_s = value


# Import statements for member types

# already imported above
# import builtins

# already imported above
# import rosidl_parser.definition


class Metaclass_GetDescription_Event(type):
    """Metaclass of message 'GetDescription_Event'."""

    _CREATE_ROS_MESSAGE = None
    _CONVERT_FROM_PY = None
    _CONVERT_TO_PY = None
    _DESTROY_ROS_MESSAGE = None
    _TYPE_SUPPORT = None

    __constants = {
    }

    @classmethod
    def __import_type_support__(cls):
        try:
            from rosidl_generator_py import import_type_support
            module = import_type_support('chaos_interfaces')
        except ImportError:
            import logging
            import traceback
            logger = logging.getLogger(
                'chaos_interfaces.srv.GetDescription_Event')
            logger.debug(
                'Failed to import needed modules for type support:\n' +
                traceback.format_exc())
        else:
            cls._CREATE_ROS_MESSAGE = module.create_ros_message_msg__srv__get_description__event
            cls._CONVERT_FROM_PY = module.convert_from_py_msg__srv__get_description__event
            cls._CONVERT_TO_PY = module.convert_to_py_msg__srv__get_description__event
            cls._TYPE_SUPPORT = module.type_support_msg__srv__get_description__event
            cls._DESTROY_ROS_MESSAGE = module.destroy_ros_message_msg__srv__get_description__event

            from service_msgs.msg import ServiceEventInfo
            if ServiceEventInfo.__class__._TYPE_SUPPORT is None:
                ServiceEventInfo.__class__.__import_type_support__()

    @classmethod
    def __prepare__(cls, name, bases, **kwargs):
        # list constant names here so that they appear in the help text of
        # the message class under "Data and other attributes defined here:"
        # as well as populate each message instance
        return {
        }


class GetDescription_Event(metaclass=Metaclass_GetDescription_Event):
    """Message class 'GetDescription_Event'."""

    __slots__ = [
        '_info',
        '_request',
        '_response',
        '_check_fields',
    ]

    _fields_and_field_types = {
        'info': 'service_msgs/ServiceEventInfo',
        'request': 'sequence<chaos_interfaces/GetDescription_Request, 1>',
        'response': 'sequence<chaos_interfaces/GetDescription_Response, 1>',
    }

    # This attribute is used to store an rosidl_parser.definition variable
    # related to the data type of each of the components the message.
    SLOT_TYPES = (
        rosidl_parser.definition.NamespacedType(['service_msgs', 'msg'], 'ServiceEventInfo'),  # noqa: E501
        rosidl_parser.definition.BoundedSequence(rosidl_parser.definition.NamespacedType(['chaos_interfaces', 'srv'], 'GetDescription_Request'), 1),  # noqa: E501
        rosidl_parser.definition.BoundedSequence(rosidl_parser.definition.NamespacedType(['chaos_interfaces', 'srv'], 'GetDescription_Response'), 1),  # noqa: E501
    )

    def __init__(self, **kwargs):
        if 'check_fields' in kwargs:
            self._check_fields = kwargs['check_fields']
        else:
            self._check_fields = ros_python_check_fields == '1'
        if self._check_fields:
            assert all('_' + key in self.__slots__ for key in kwargs.keys()), \
                'Invalid arguments passed to constructor: %s' % \
                ', '.join(sorted(k for k in kwargs.keys() if '_' + k not in self.__slots__))
        from service_msgs.msg import ServiceEventInfo
        self.info = kwargs.get('info', ServiceEventInfo())
        self.request = kwargs.get('request', [])
        self.response = kwargs.get('response', [])

    def __repr__(self):
        typename = self.__class__.__module__.split('.')
        typename.pop()
        typename.append(self.__class__.__name__)
        args = []
        for s, t in zip(self.get_fields_and_field_types().keys(), self.SLOT_TYPES):
            field = getattr(self, s)
            fieldstr = repr(field)
            # We use Python array type for fields that can be directly stored
            # in them, and "normal" sequences for everything else.  If it is
            # a type that we store in an array, strip off the 'array' portion.
            if (
                isinstance(t, rosidl_parser.definition.AbstractSequence) and
                isinstance(t.value_type, rosidl_parser.definition.BasicType) and
                t.value_type.typename in ['float', 'double', 'int8', 'uint8', 'int16', 'uint16', 'int32', 'uint32', 'int64', 'uint64']
            ):
                if len(field) == 0:
                    fieldstr = '[]'
                else:
                    if self._check_fields:
                        assert fieldstr.startswith('array(')
                    prefix = "array('X', "
                    suffix = ')'
                    fieldstr = fieldstr[len(prefix):-len(suffix)]
            args.append(s + '=' + fieldstr)
        return '%s(%s)' % ('.'.join(typename), ', '.join(args))

    def __eq__(self, other):
        if not isinstance(other, self.__class__):
            return False
        if self.info != other.info:
            return False
        if self.request != other.request:
            return False
        if self.response != other.response:
            return False
        return True

    @classmethod
    def get_fields_and_field_types(cls):
        from copy import copy
        return copy(cls._fields_and_field_types)

    @builtins.property
    def info(self):
        """Message field 'info'."""
        return self._info

    @info.setter
    def info(self, value):
        if self._check_fields:
            from service_msgs.msg import ServiceEventInfo
            assert \
                isinstance(value, ServiceEventInfo), \
                "The 'info' field must be a sub message of type 'ServiceEventInfo'"
        self._info = value

    @builtins.property
    def request(self):
        """Message field 'request'."""
        return self._request

    @request.setter
    def request(self, value):
        if self._check_fields:
            from chaos_interfaces.srv import GetDescription_Request
            from collections.abc import Sequence
            from collections.abc import Set
            from collections import UserList
            from collections import UserString
            assert \
                ((isinstance(value, Sequence) or
                  isinstance(value, Set) or
                  isinstance(value, UserList)) and
                 not isinstance(value, str) and
                 not isinstance(value, UserString) and
                 len(value) <= 1 and
                 all(isinstance(v, GetDescription_Request) for v in value) and
                 True), \
                "The 'request' field must be a set or sequence with length <= 1 and each value of type 'GetDescription_Request'"
        self._request = value

    @builtins.property
    def response(self):
        """Message field 'response'."""
        return self._response

    @response.setter
    def response(self, value):
        if self._check_fields:
            from chaos_interfaces.srv import GetDescription_Response
            from collections.abc import Sequence
            from collections.abc import Set
            from collections import UserList
            from collections import UserString
            assert \
                ((isinstance(value, Sequence) or
                  isinstance(value, Set) or
                  isinstance(value, UserList)) and
                 not isinstance(value, str) and
                 not isinstance(value, UserString) and
                 len(value) <= 1 and
                 all(isinstance(v, GetDescription_Response) for v in value) and
                 True), \
                "The 'response' field must be a set or sequence with length <= 1 and each value of type 'GetDescription_Response'"
        self._response = value


class Metaclass_GetDescription(type):
    """Metaclass of service 'GetDescription'."""

    _TYPE_SUPPORT = None

    @classmethod
    def __import_type_support__(cls):
        try:
            from rosidl_generator_py import import_type_support
            module = import_type_support('chaos_interfaces')
        except ImportError:
            import logging
            import traceback
            logger = logging.getLogger(
                'chaos_interfaces.srv.GetDescription')
            logger.debug(
                'Failed to import needed modules for type support:\n' +
                traceback.format_exc())
        else:
            cls._TYPE_SUPPORT = module.type_support_srv__srv__get_description

            from chaos_interfaces.srv import _get_description
            if _get_description.Metaclass_GetDescription_Request._TYPE_SUPPORT is None:
                _get_description.Metaclass_GetDescription_Request.__import_type_support__()
            if _get_description.Metaclass_GetDescription_Response._TYPE_SUPPORT is None:
                _get_description.Metaclass_GetDescription_Response.__import_type_support__()
            if _get_description.Metaclass_GetDescription_Event._TYPE_SUPPORT is None:
                _get_description.Metaclass_GetDescription_Event.__import_type_support__()


class GetDescription(metaclass=Metaclass_GetDescription):
    from chaos_interfaces.srv._get_description import GetDescription_Request as Request
    from chaos_interfaces.srv._get_description import GetDescription_Response as Response
    from chaos_interfaces.srv._get_description import GetDescription_Event as Event

    def __init__(self):
        raise NotImplementedError('Service classes can not be instantiated')

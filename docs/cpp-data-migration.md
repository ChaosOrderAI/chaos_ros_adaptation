# The native data module

This document describes the C++17 implementation of `chaos_data`: what it
does, how the parts fit together and which guarantees the module gives.

The module records a ROS 2 session, audits the source timestamps of a
recording and exports explicitly labelled episodes. Every part of it is
native: the package is a plain `ament_cmake` package and the Python
implementation that it replaced has been removed. The behaviour of the export
is compared against the frozen Python baseline with an independent
differential harness, so the migration is verified artefact by artefact
instead of being assumed.

## The entry points

The ament_cmake build installs five executables into `lib/chaos_data`:

| Executable | Task |
| --- | --- |
| `collector` | the production collector node |
| `chaos_data_recorder_worker` | the recorder, an isolated worker process |
| `audit`, `data_audit` | the source timestamp audit of a bag |
| `data_export` | the export of labelled episodes |

The names are the console script names of the original Python package, so
the callers of the module do not change.

## The libraries

| Library | Responsibility |
| --- | --- |
| `chaos_data_timing` | clock quality and the monotonic admission checks |
| `chaos_data_session` | the durable session metadata of a recording |
| `chaos_data_supervisor` | the bounded supervision of the worker process |
| `chaos_data_controller` | the clock gated collection supervisor |
| `chaos_data_ros_message` | the ROS message reading layer |
| `chaos_data_audit` | the read only source timestamp audit |
| `chaos_data_file_io` | the exclusive file output and the path rules |
| `chaos_data_export` | the profile, the provenance, the export pipeline |

The export library is split into the profile loader, the source provenance,
the typed bag reader with the control label stream, the causal resampler, the
SHA-256 helper and the lossless image sink. The provenance, the reader and
the control stream are internal modules: their headers are not installed, so
the public interface of the package stays small.

The YAML of the profile and the YAML of the bag metadata are parsed by the
shared configuration engine of the workspace, so the type of a scalar is
the one of that engine: a plain scalar resolves with the YAML 1.1 / PyYAML
implicit typing (a boolean, a null, a decimal, hexadecimal, octal, binary or
sexagesimal integer, a float), a quoted scalar always stays a string, a
duplicate mapping key is refused, the nesting depth is bounded and an integer
has to fit into a signed 64 bit integer. The export does not carry a second
YAML parser.

Every library is installed with an export set, so a consumer can link it:

```cmake
find_package(chaos_data REQUIRED)
target_link_libraries(my_target PRIVATE chaos_data::chaos_data_export)
```

That single `find_package` is enough: every package that an exported target
links PUBLIC is part of the exported dependencies of the package, so the
imported targets of the consumers exist without a second `find_package`. That
covers the packages of the public headers (`nlohmann_json`, `Boost`,
`sensor_msgs`, `rclcpp`) and the public link libraries of the export
(`chaos_assets`, `yaml-cpp`, `OpenSSL`, `chaos_interfaces`, `rosbag2`). The
exact list is in `ament_export_dependencies` of the `CMakeLists.txt`.

## The message reading layer

`chaos_data_ros_message` never expands a message into a JSON number array:
an image or a point cloud would allocate one JSON value per byte. It uses
the deserialization that the middleware already provides.

* The known types are decoded with `rclcpp::Serialization<T>`, so the payload
  keeps its own byte vector and no copy of the pixels is made.
* Every other type is decoded with the introspection metadata of the type
  and the type support of the active RMW. The type support is the
  dispatcher of `rosidl_typesupport_cpp`, so the type is resolved for the
  middleware of the process and no middleware specific name is hard coded.
* Only the fields that the caller names are read. A path is a sequence of
  member names; a numeric component selects one element of an array or a
  sequence. The bound of the element is the size that the introspection
  reports for the container, so an index outside the container is refused
  instead of reading memory that does not belong to the message.
* A sequence of booleans is a `std::vector<bool>`, whose elements are proxy
  references. They are read with the copy function of the introspection and
  never through the address of an element.

The ownership is explicit: the type support libraries are held by shared
pointers for as long as the metadata is used, the message object is created
by the initializer of the introspection and destroyed by its finalizer, also
when the deserialization failed in between. A payload is bounded at 64 MiB
before it is copied, and the type cache is bounded at 256 types.

## The audit

`audit` reads an existing bag and never writes into it. The report keeps the
field names and the semantics of the original implementation, including the
categories of a source stamp (`valid`, `missing`, `invalid`, `zero`,
`multi_stamp_excluded`) and the counters per topic.

* A topic beyond the topic limit is counted and dropped: it gets no
  statistics and no source timestamp is claimed for it, but it still has a
  line in the index.
* A message that cannot be decoded stays visible as a decode error. A source
  timestamp is never invented.
* The difference between the reception time and the source time is
  accumulated in a wide integer, because the difference of two clocks over
  many epochs exceeds the range of a 64 bit integer.
* The index and the report are created with exclusive create semantics. An
  existing file is never replaced, and a path that points into the bag is
  refused even when it is written with a symbolic link or with `..`.
* Every write, flush and close is checked, so a write error is an error and
  not a silently truncated file.

## The export

`data_export` reads a YAML profile that describes the features, the action
labels and the labelled episodes, and writes the Chaos portable intermediate
format.

### The profile

The profile is validated strictly: a mapping key has to be a string and it
has to be unique, a boolean is never read as a number, an unknown field is an
error, a number has to be finite, and a field path has to name an explicit
member or a numeric index. Nothing is evaluated.

The frame rate is kept as an exact rational number of unbounded integers, so
the grid of a step is computed with integer arithmetic. A double would
accumulate a rounding error over the steps of a long episode, and a frame
rate like `1.0e-300` has a denominator that does not fit into 64 bits. The
fraction is the one of the shortest decimal representation that reads back
as the same double, which is the number the profile was written with.

The age limit of a feature is the product of its milliseconds and a million,
rounded to the nearest nanosecond with a half going to the even neighbour,
because that is the rounding of the original implementation and a different
rounding would move the threshold of a feature by one nanosecond.

### The source of an episode

The session manifest, the clock journal and the metadata of the bag are
validated before anything is read. The episode has to lie inside one closed
segment without a suspect interval, the clock evidence has to be
synchronized in one epoch, and the journal has to belong to the session and
has to end with the session end event.

`allow_unverified_time` is restricted to mock assets, and it makes the
export report `synthetic: true` and `time_verified: false`.

The files of the source are hashed with SHA-256 and their size is recorded.
After the export the hashes are computed again: a source that changed while
it was read is an error.

### The causal rules

An observation is used at a grid step only when its source stamp and its
receipt time are both at or before that step. An observation from before the
episode is never carried into it, and an observation without a valid source
stamp clears the previous one, because a state without a time cannot refresh
a previous observation. A reversed source stamp is an error.

A control action is an offline label: it becomes effective at the source
time of the event, even when the event was received late, so a late
delivered stop still ends the action at the moment it was issued. Only an
issued velocity command is a label; a rejected command is an audit event
and never a new command. The sequence of the trace has to be continuous, the
monotonic time may not run backwards, and a trace that is not closed at the
end of the episode has no valid action.

The control events are written to the journal in a first pass and are read
back with a cursor while the grid is walked, so the memory of the export
does not grow with the number of control events of an episode. The reader
asks the storage for the selected topics, so a bag with many other topics
does not pay for their payloads.

### The images

The pixels are the original bytes of the message. The row padding of the
ROS image is removed, the byte order of the message is kept, and no pixel is
converted, resized or normalised. The array is written in the NumPy format
version 1.0 by the export itself; a consumer reads it with
`numpy.load(..., allow_pickle=False)`.

### The publication

The export is built in a private staging directory next to the output and is
published with a single `renameat2(RENAME_NOREPLACE)`, so the output
appears atomically and an output that was created in the meantime is not
replaced. A failure removes only the staging directory of this process.

## The collector and the worker

The collector is a ROS node that reads the asset configuration from the
`asset_json` parameter, publishes the status and the diagnostics of the
collection and drives the session. The recorder runs in a separate process
that the supervisor starts and bounds: the supervisor owns the control
channel, sends the stop command with an absolute deadline and takes the
process down when the deadline passes.

The worker sets the parent death signal, so it cannot outlive its
supervisor, and it owns the flush: the flush is synchronous, and the
deadline of the supervisor is the bound of a wedged flush.

## The build

Build in a fresh prefix, starting from a terminal that only sources ROS.
Colcon builds the declared core, interface and asset dependencies first:

```bash
source /opt/ros/jazzy/setup.bash
colcon --log-base log build --base-paths src \
  --packages-up-to chaos_data \
  --build-base build --install-base install \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/local_setup.bash
ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST ROS_DOMAIN_ID=198 \
  colcon test --base-paths src --packages-select chaos_data \
  --build-base build --install-base install
colcon test-result --test-result-base build --verbose
```

## The verification

The tests are registered with `ament_add_gtest`; the package has no Python
test of a Python implementation any more, because that implementation is
gone. Two native suites guard the module in the repository:

* `test/test_export_cpp.cpp` checks the profile loader, the exact frame
  rate, the age limit rounding, the YAML edge cases and the refusals, the
  bytes of the lossless image sink, and the cursor of the control journal.
  The cursor is the part of the export that reads while the episode is
  written, so a read error has to end the export instead of ending the
  action silently: the rest of the episode would be labelled with a command
  that was never stopped.
* `test/test_export_cli.py` drives the native `data_export` executable as a
  black box. It writes a real bag in both storages (SQLite and MCAP) with
  real `PoseStamped`, `Image` and `ControlTrace` messages, a real session
  directory and a real export profile, and then checks the exit code of the
  executable and the files it published. The cases are a valid episode that
  has to publish the vectors and the images, a gap in the control sequence,
  a trace of another asset, an episode whose action is never closed, an
  image with a malformed payload, a manifest without clock evidence, a bag
  metadata that names a file outside of the session, and a destination that
  already exists. A rejected case has to exit with a failure, to publish
  nothing and to leave the recording unchanged, and the digests of the
  source prove that last point.

The fixture contract of the Python test is the one of the frozen baseline,
because that is the contract the native exporter has to keep: a session
directory with a manifest, a clock journal and one bag segment, a profile
that names the features, the action and the episode interval, and a record
list of (topic, message, receipt time) triples ordered by the receipt time.
Only the builders of that contract are shared. The messages are real ROS
messages, the bag is written by rosbag2, and the expectations are the
documented behaviour of the native exporter; no Python implementation of the
export is part of this package and none is imported.

The numeric and the YAML edges of the differential harnesses below are
guarded inside the package this way, so a regression is caught by the
ordinary test run and not only by the external harness.

The export is verified in addition by three independent differential
harnesses that are not part of the package. The first one builds a real
SQLite and MCAP bag for nine variants (normal, sparse images, big endian
float, a dynamic array field, an invalid new stamp, a 64 bit trace sequence, a
decimal frame rate and the two rejected inputs), exports every variant with
the frozen Python baseline and with the native executable, and compares the
complete file list, every JSON and JSONL value and every NumPy array byte for
byte.

The second one widens the differential to eighty cases: it adds the causal
delay, the action time to live, the episode boundary, the validity of the
robot state and more image encodings, and it also feeds malformed traces,
manifests, journals and metadata to the export, so a rejection has to be the
same rejection.

The third one covers the numeric and the YAML edges: a frame rate of `1.0e-7`
and of `1.0e-300`, a frame rate with more digits than a double can hold, a
frame rate next to the limit of 240, a frame rate in scientific notation, a
quoted frame rate, a frame rate with an explicit string tag and a hexadecimal
frame rate. The source recording may not change while the export runs.

The public smoke scripts drive the installed executables: the audit is
called as the `audit` binary and the exporter as the `data_export` binary, so
a smoke test proves the behaviour of the shipped command line and not the
behaviour of a library.

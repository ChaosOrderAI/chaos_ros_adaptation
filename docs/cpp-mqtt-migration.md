# C++ MQTT migration: native gateway and transport

This document describes the native (C++) implementation of the MQTT bridge in
`chaos_mqtt` and how it replaces the former Python gateway. It is the
reference for the wire contract, the resource ownership rules and the
verification that guards both.

The document is self-contained: it does not depend on a particular workspace
layout, install prefix or developer machine.

## Scope

| Component | Language | Owner |
|---|---|---|
| `chaos_mqtt` gateway node | C++17 (`src/gateway_node.cpp`) | this package |
| Wire protocol, correlation cache | C++17 (`src/protocol.cpp`) | this package |
| Bounded action observation | C++17 (`include/chaos_mqtt/action_observer.hpp`) | this package |
| Adapter configuration generator | C++17 (`src/adapter_config.cpp`) | this package |
| MQTT <-> ROS transport | Rust (`transport/`) | third party, vendored unmodified |
| Python bindings | pybind11 thin wrapper (`src/py_bindings.cpp`) | this package |

The Rust transport is vendored third-party source. Its provenance, manifest
and `Cargo.lock` are kept under `transport/` and are not modified by this
migration.

## Wire contract (unchanged)

The native gateway implements the same contract as the former Python gateway.

* **Command envelope.** `{"bridge": {...}, "payload": {...}}`. `bridge` carries
  `source` (always `mqtt`), `request_id` and `timestamp_ms`; `payload` carries
  `command`, `ttl_ms` and the command specific fields.
* **Deadline.** The command deadline is `timestamp_ms + ttl_ms`, clamped by
  `valid_for_s` where the command defines one. It is an absolute Unix UTC
  instant and is never recomputed from the arrival time, so transport delay
  cannot extend the validity of a command. The deadline is converted into a
  ROS `builtin_interfaces/Time` for the runtime; the conversion rounds down
  and rejects values outside the ROS Time range.
* **Clock skew.** A command whose sender clock deviates by more than
  `maximum_clock_skew_ms` (default 5000 ms) is rejected as `EXPIRED`.
* **Reply envelope.** `{"source": "mqtt", "request_id", "command", "response"}`.
  `response` carries `ok`, `code`, `message`, `phase` and `terminal`.
* **Service replies.** The response body nests the runtime payload under
  `response.data`. `success`, `code` and `message` are lifted out of the
  payload into the envelope, exactly like the original implementation: the
  runtime payload fields stay inside `data`.
* **Task events.** `mqtt/task_events` publishes the lifecycle of a command:
  `accepted`, `feedback` and `result`. The terminal event is the only event
  with `terminal: true`.
* **Result semantics.** `execution_state` is `known` only when the action
  produced a real result payload. A goal that timed out, was rejected, was
  aborted or whose result was lost keeps `execution_state: unknown`: the
  gateway lost the observation, it did **not** stop the robot. `ok` is `true`
  only for a known, successful result. `ros_goal_status` carries the
  `action_msgs/GoalStatus` value of the terminal status.
* **Replay.** A repeated `request_id` returns the cached terminal reply (and
  the cached terminal event) without re-executing the command. The cache is
  bounded (`max_pending`, `max_completed`).
* **Rejection.** Unknown commands, unknown payload fields, unsupported asset
  capabilities and malformed envelopes are rejected with a correlated reply.
  The gateway never logs tokens or payloads.

## Bounded action observation

The most important structural difference to the former implementation is the
handling of action goals.

### Why the shared client was replaced

The former gateway used one `rclcpp_action` client per action type and
recreated that client after a number of timed-out observations. Recreating the
client dropped the callbacks of *all* goals of that type, including goals that
were still valid, and it left the pending service requests of the abandoned
goals inside the replacement client.

`rclcpp_action` offers no API to remove a single pending goal request or a
single pending result request. The only way to drop that middleware-side
bookkeeping is to destroy the client that owns it.

### The design

* Every in-flight goal owns a **dedicated** action client
  (`ActionObserver` in `include/chaos_mqtt/action_observer.hpp`).
* Two goals never share a client, so retiring one observation can never
  remove the callbacks of another goal.
* The ROS callbacks capture the observer **weakly**
  (`std::enable_shared_from_this::weak_from_this()`). The client therefore
  never keeps the observer, and through it the gateway node, alive.
* A client whose goal finished normally (rejected or result received) has no
  pending service request left and is recycled through a small bounded idle
  pool. A client of an abandoned observation is **destroyed**, because its
  `SendGoal`/`GetResult` request may still be pending.
* Destroying an observer stops the callbacks of its goal
  (`Client::stop_callbacks`) and then releases the client. The node
  destructor stops every observation this way, so no callback outlives the
  gateway.
* The number of simultaneously observed goals is bounded by the
  `action_slot_capacity` parameter (default 48, range 1..256). A command that
  exceeds the capacity is answered with `BUSY`; the observation state never
  grows beyond the bound.

### Release ordering (no use-after-free)

Destroying an action client from inside one of its own callbacks would free
the callback's owner while the callback is still on the stack. The
implementation therefore never releases a client from a client callback:

* the result callback only records the terminal state and marks the
  observation as finished;
* the gateway's expire timer (100 ms) performs the actual release, outside of
  every client callback;
* the node destructor releases the remaining observations after the executor
  was stopped.

### Deferred dispatch

A freshly created action client may not see its server yet. The gateway does
not block the executor and does not fail the command in that case: the goal
is remembered by the observation, the dispatch is deferred, and the expire
timer sends it as soon as the client is ready. The correlation deadline bounds
the wait, and a request that already timed out is never sent to the robot.

### Correlation rules

Every action callback first checks that the correlation record still exists:

* a late acknowledgment of a retired goal is dropped, the goal keeps running
  on the robot but the gateway no longer reports it;
* a late result of a retired goal never publishes a second terminal state and
  never changes the replay cache;
* the `goal_id` map entry is removed on every terminal path, so the map cannot
  grow without bound.

## Threading and locking

* The node is designed for a `MutuallyExclusive` executor: every ROS callback
  of the node runs on the same thread.
* The correlation records are additionally guarded by a mutex, so the code
  stays correct if the executor is ever reconfigured. No pointer into the
  correlation map is used outside of the lock.
* Publishers are called **outside** of the lock. Publishing under the lock
  could re-enter the node through an intra-process subscriber and deadlock.
* The expire timer runs on the executor thread and only releases clients that
  no callback owns.

## Parameters

| Parameter | Default | Meaning |
|---|---|---|
| `asset_json` | `{}` | Asset identity, capabilities, frames and limits |
| `maximum_clock_skew_ms` | 5000 | Accepted sender clock deviation |
| `request_timeout_s` | 5.0 | Deadline of a service command |
| `action_timeout_s` | 300.0 | Deadline of an accepted action goal |
| `action_slot_capacity` | 48 | Maximum number of simultaneously observed goals |

## Verification

### Native tests

| Test | Scope |
|---|---|
| `test_protocol` | Wire protocol, correlation cache, replay, depth guard |
| `test_adapter_config` | Adapter configuration generator |
| `test_gateway_unit` | Deadline conversion |
| `test_gateway_integration` | Live DDS round trips against a mock runtime |
| `test_action_observation` | Ownership rules of the action observation layer |

The integration test owns a mock runtime with a **separate** executor, so a
blocking server callback never delays the gateway's goal acknowledgment. The
mock provides genuinely silent endpoints:

* the lease service uses the deferred response callback of `rclcpp` and never
  answers the `hang` owner;
* the action server retains the goal handles of the silent goals, so those
  goals stay `EXECUTING` instead of being aborted with an empty default
  result;
* goals that publish feedback wait for a barrier that the test releases after
  the acceptance was observed, so the gateway has mapped the goal UUID before
  the feedback arrives.

The integration test covers the six normal behaviours (describe, lease,
velocity, stop, action lifecycle, telemetry) plus the failure modes:

* more than 32 silent goals while a healthy goal keeps receiving feedback and
  finally succeeds - the regression that the shared client caused;
* a late result after the timeout produces no further event and no changed
  replay cache;
* the observation capacity is bounded and the excess is answered with `BUSY`;
* destroying the gateway with a pending goal is clean;
* silent service requests and silent goals are reclaimed, and the gateway
  still serves normal traffic afterwards.

Bursts are published in small batches and every batch is confirmed before the
next one is sent, so the test proves that all commands really entered the
gateway instead of being truncated by the queue depth of the public QoS
profile.

### Ownership regression tests

`test_action_observation` drives the observation layer directly and proves the
rules that the pool counters cannot show:

* a retired observation of a goal that was accepted but never answered really
  releases its dedicated client. The test keeps a weak handle on the client,
  quiesces the executor and asserts that the handle is expired after the
  release - the pending result request of the abandoned goal disappears with
  the client. A local strong reference of the observation must not keep the
  client alive either.
* several deferred dispatches that expire before their action server appears
  are never sent, not even when the server shows up afterwards. The test
  counts the goals the server receives and requires zero.

### Smoke tests

* `scripts/mqtt_smoke_test.py` starts the native runtime and the native
  gateway, drives them over the real DDS topics and always terminates the
  processes it started.
* `scripts/transport_smoke_test.py` starts the Rust transport against a local
  broker on a random loopback port with a temporary mTLS certificate pair and
  exercises the MQTT side of the bridge.

### Sanitizers

The package can be built with `CHAOS_MQTT_SANITIZE=ON`. The flags are attached
to the libraries (`chaos_mqtt_core` and `chaos_mqtt_gateway`) as public usage
requirements, so the production code is instrumented and every consumer
(test binary, pybind module, node) inherits them. AddressSanitizer,
UndefinedBehaviorSanitizer and LeakSanitizer therefore cover the action
observation layer itself, not only the test entry point.

The sanitizer build is a separate build directory:

```bash
colcon build --packages-select chaos_mqtt \
  --cmake-args -DCHAOS_MQTT_SANITIZE=ON -DMQTT_ADAPTER_OFFLINE=ON
```

Two notes for a sanitizer run:

* the python binding test is skipped, because a sanitized extension cannot be
  loaded into an unsanitized interpreter unless the sanitizer runtime is
  preloaded;
* the cargo target directory is a cache variable. Pointing it at an existing
  cargo directory reuses the compiled Rust artifacts and avoids a rebuild of
  the vendored transport:

```bash
colcon build --packages-select chaos_mqtt \
  --cmake-args -DCHAOS_MQTT_SANITIZE=ON \
  -DMQTT_ADAPTER_CARGO_TARGET_DIR=<existing-cargo-dir>
```

The sanitizer run is the evidence that the action observation releases the
dedicated clients and that no pending observation leaks.

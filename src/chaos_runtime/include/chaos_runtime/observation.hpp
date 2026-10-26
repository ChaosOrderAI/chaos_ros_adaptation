// ROS-free runtime observation service.
//
// The service owns one process-lifetime generation of four joinable workers
// (trace, results, state, log). A producer publishes typed views through a
// bounded endpoint; the service copies the payload, queues it per producer
// and delivers it to the registered sinks outside every internal lock.
//
// OWNERSHIP CONTRACT
//   * ObservationService::process() is the single process-lifetime owner of
//     the active generation. Its manager is intentionally leaked, so no
//     static destructor ever takes part in the teardown of a live pool.
//   * A generation owns its four worker threads. A successful joinFor()
//     leaves them non joinable, so the ordinary generation destruction
//     reclaims them: no thread handle is detached and no generation leaks.
//   * A generation is replaced only after joinFor() reported success; an
//     unjoined or stopping generation is never swapped for a fresh pool.
//   * The coordinator/node keeps only an endpoint. The endpoint holds
//     metadata, per-producer counters and a WEAK service reference. It never
//     owns a sink, a publisher, a callback capture or a worker.
//   * A user callable is owned by one stable per channel holder that is
//     allocated before the registration is published. Afterwards only the
//     shared ownership handle travels: the registry and the workers never
//     copy a callable under a lock.
//   * Sinks, callback captures and queued payloads are owned by the service
//     and are destroyed by the workers, outside the internal locks.
//
// CLOSE AND JOIN CONTRACT
//   * close() flips the closed flag of the producer metadata and wakes the
//     workers. It takes no queue lock, never drains a queue, never destroys
//     a sink bundle and never waits for a callback. An admission that
//     committed before the close may still drain; an admission that starts
//     after close() returned is rejected.
//   * requestStop() raises the global stop gate and wakes the workers. It
//     takes no queue lock and destroys no user resource on the calling
//     thread.
//   * joinFor() owns the join of the four threads: concurrent callers wait
//     for the single join owner, every caller uses its own absolute
//     steady-clock deadline and a timeout leaves the threads joinable.
//     Joined is published only after all four threads finished their
//     resource cleanup and all joins completed.
//   * A worker thread is only finished when it really exited. The live
//     worker counters are a diagnostic: a user thread local destructor may
//     still be blocked after the worker body returned. Every worker owns an
//     exit ready shared state that is satisfied at the actual thread exit,
//     after the thread local cleanup, and the join owner waits for those
//     states before it joins a thread. A join therefore never waits for a
//     blocked user destructor.
//
// RETIREMENT CONTRACT
//   * Every channel retires its own registrations. The channel worker
//     drains its channel, releases its own sink holder and the callback
//     captures it owns outside every lock, and only then sets its own
//     retirement bit. A blocked destructor therefore occupies that channel
//     worker only.
//   * A registration slot is reusable only when all four retirement bits are
//     set, no work is pending and no resource is in flight.
//
// BOUNDS (compile-time, not configurable)
//   * 16 registered/retiring producer slots per service.
//   * Per producer: trace FIFO 256, results FIFO 128, log FIFO 64 and one
//     pending state slot.
//   * 4096 bytes of owned string payload per event (every string of the
//     event counts, including AdapterSnapshot detail/pose_frame/twist_frame
//     and the log level/text).
//   * 4 MiB aggregate queued/in-flight payload bytes for the whole service.
//     The fixed container/object overhead (deques, sink bundles, worker
//     stacks) is separate from this payload bound.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "chaos_runtime/observation_types.hpp"
#include "chaos_runtime/trace.hpp"

namespace chaos::runtime {

namespace detail {
struct ObservationImpl;
struct ProducerState;
}  // namespace detail

// The four observation channels. One dedicated worker serves each channel.
enum class ObservationChannel : std::uint8_t {
  trace = 0,
  results = 1,
  state = 2,
  log = 3
};

inline constexpr std::size_t kObservationChannelCount = 4U;

// Outcome of one admission attempt.
//
//   admitted             the payload was queued for delivery
//   coalesced            the payload was queued and replaced a pending
//                        state value (the previous value was dropped)
//   full                 the channel, the producer or the aggregate payload
//                        bound rejected the payload
//   contended            the internal queue lock was busy; the payload was
//                        dropped instead of waiting
//   closed               the producer was closed (or retired)
//   oversize             the owned string payload exceeded the per event
//                        bound
//   construction_failed  the payload copy or the queue insertion threw
enum class Admission : std::uint8_t {
  admitted,
  coalesced,
  full,
  contended,
  closed,
  oversize,
  construction_failed
};

// Compile-time bounds. Fixed by contract, never a configuration surface.
namespace observation_limits {

inline constexpr std::size_t kProducerSlots = 16U;
inline constexpr std::size_t kTraceQueueCapacity = 256U;
inline constexpr std::size_t kResultsQueueCapacity = 128U;
inline constexpr std::size_t kLogQueueCapacity = 64U;
inline constexpr std::size_t kStatePendingSlots = 1U;
inline constexpr std::size_t kMaxEventStringBytes = 4096U;
inline constexpr std::uint64_t kMaxAggregatePayloadBytes = 4ULL * 1024ULL * 1024ULL;
inline constexpr std::size_t kWorkerCount = kObservationChannelCount;

}  // namespace observation_limits

// The sink bundle transferred into the service at registration time.
//
// A missing callback is a disabled channel: the observation is admitted and
// counted as delivered (a consumed no-op) without invoking anything. The
// accounting therefore keeps attempted == admitted == delivered exact at
// quiescence even for a producer that registered only some of the sinks.
//
// The bundle is moved into the service owned per channel holders before the
// registration is published. A callable that cannot be copied fails the
// registration itself; a failed copy is never reported as a delivered
// no-op.
struct ObservationSinks {
  std::shared_ptr<TraceSink> trace;
  std::function<void(const CommandResultView &)> results;
  std::function<void(const StateView &)> state;
  std::function<void(const std::string &, const std::string &)> log;
};

// Per channel counters. Every value is a plain snapshot of an atomic
// counter; concurrent snapshots are not transactional.
struct ChannelStats {
  std::uint64_t attempted = 0;
  std::uint64_t admitted = 0;
  std::uint64_t delivered = 0;
  std::uint64_t dropped_full = 0;
  std::uint64_t dropped_contended = 0;
  std::uint64_t dropped_closed = 0;
  std::uint64_t dropped_oversize = 0;
  std::uint64_t dropped_construction = 0;
  std::uint64_t sink_exceptions = 0;
  std::uint64_t coalesced = 0;
};

// Per producer snapshot.
struct ObservationStats {
  std::array<ChannelStats, kObservationChannelCount> channels{};
  std::uint64_t queued_items = 0;
  std::uint64_t queued_bytes = 0;
  std::uint64_t in_flight_items = 0;
  std::uint64_t in_flight_bytes = 0;
  std::uint64_t payload_high_water_bytes = 0;
  // Highest trace sequence handed to the endpoint (assigned by the
  // coordinator before the attempt) and the highest delivered sequence.
  // A sequence gap between two delivered events reveals a lost event; a
  // trailing gap is only visible through the drop counters.
  std::uint64_t last_assigned_trace_sequence = 0;
  std::uint64_t last_delivered_trace_sequence = 0;
  bool closed = false;
};

// Service wide snapshot.
struct ObservationServiceStats {
  std::array<ChannelStats, kObservationChannelCount> channels{};
  std::uint64_t registered_producers = 0;
  std::uint64_t retiring_producers = 0;
  std::uint64_t slots_used = 0;
  std::uint64_t slots_total = observation_limits::kProducerSlots;
  std::uint64_t queued_items = 0;
  std::uint64_t queued_bytes = 0;
  std::uint64_t in_flight_items = 0;
  std::uint64_t in_flight_bytes = 0;
  std::uint64_t payload_high_water_bytes = 0;
  // Busy flags cover the sink invocation and the release of the owned sink
  // holder and its callback captures; the timestamp is the steady clock
  // reading of the current or last such step (0 when idle). A stalled
  // worker is visible as a growing busy duration.
  std::array<bool, kObservationChannelCount> worker_busy{};
  std::array<std::int64_t, kObservationChannelCount> worker_busy_since_ns{};
  bool stop_requested = false;
  bool joined = false;
};

// The producer facing handle.
//
// The endpoint holds a weak service reference and per producer counters. It
// owns no sink and no worker: closing or destroying it never runs a sink
// destructor and never destroys a callback capture.
//
// The registration is identified by its slot and its registration id. Every
// admission validates both, so a retired or reused slot is rejected instead
// of delivering into a foreign registration.
class ObservationEndpoint {
public:
  ObservationEndpoint(const ObservationEndpoint &) = delete;
  ObservationEndpoint & operator=(const ObservationEndpoint &) = delete;
  ~ObservationEndpoint();

  // Admission attempts. The view is borrowed for the duration of the call
  // only: the endpoint copies the payload and never retains the caller's
  // reference. The methods never block on a worker, a sink or a queue.
  Admission trace(const TraceEvent & event) noexcept;
  Admission result(const CommandResultView & result) noexcept;
  Admission state(const StateView & view) noexcept;
  Admission log(std::string_view level, std::string_view text) noexcept;

  // Records an observation that could not even be constructed. The assigned
  // trace sequence (when known) is recorded so that the loss stays visible
  // as a sequence gap.
  void recordConstructionFailure(ObservationChannel channel,
                                 std::uint64_t trace_sequence = 0) noexcept;

  // Closes this producer's admission atomically and wakes the service. It
  // never drains, joins, waits for a callback or destroys the sink bundle,
  // and it takes no queue lock: the closed flag lives in the producer
  // metadata and is flipped atomically. The service retires the
  // registration asynchronously. Idempotent and safe during a concurrent
  // enqueue: work accepted before the close may still drain, work accepted
  // after it is forbidden.
  void close() noexcept;

  // Snapshot of this producer. Readable without the service control locks
  // and without any sink callback.
  ObservationStats stats() const noexcept;

private:
  friend class ObservationService;
  ObservationEndpoint(std::weak_ptr<detail::ObservationImpl> impl,
                      std::size_t slot,
                      std::uint64_t registration_id,
                      std::shared_ptr<detail::ProducerState> producer);

  std::weak_ptr<detail::ObservationImpl> impl_;
  std::size_t slot_ = 0;
  std::uint64_t registration_id_ = 0;
  std::shared_ptr<detail::ProducerState> producer_;
};

// The observation service.
class ObservationService {
public:
  // Returns the active generation of this process. The same service is
  // returned while it is running or stopping. A new generation is created
  // only after the previous one reported a successful joinFor().
  //
  // The implementation and the service wrapper are allocated before the
  // first worker is started and the generation is published under the
  // manager lock, so a thread start failure never publishes a generation
  // and concurrent callers observe exactly one generation.
  static std::shared_ptr<ObservationService> process();

  // Transfers the sink bundle into the service owned registration storage
  // and returns the endpoint of the new producer.
  //
  // The endpoint ownership is allocated before the registration is
  // published: a failed allocation rolls the reservation back, so no open
  // slot and no callback bundle survive a failed registration.
  //
  // Throws std::runtime_error when no registration slot is available or
  // when the generation is stopping or joining: a registration failure is
  // an explicit startup failure, never a partially registered fallback.
  std::shared_ptr<ObservationEndpoint> registerProducer(ObservationSinks sinks);

  // Closes every producer admission and asks the four workers to finish the
  // already admitted work and exit. It raises the global stop gate, takes
  // no queue lock and never executes a sink or destroys a callback capture
  // on the calling thread. Idempotent.
  void requestStop() noexcept;

  // Waits against a steady clock deadline for the workers to finish the
  // queued work, the callback invocations, the retirement destruction and
  // the thread local cleanup of every worker. Only after the real thread
  // exit of every worker is known are the threads joined: a user thread
  // local destructor may still be blocked after a worker body returned.
  //
  // Exactly one caller owns the join: the other callers wait for that
  // owner, each with its own absolute deadline. A false return leaves the
  // threads joinable and every needed resource owned: nothing is detached
  // and no replacement generation is created. A later call can succeed.
  bool joinFor(std::chrono::milliseconds timeout);

  // Service wide snapshot; readable without the control locks.
  ObservationServiceStats stats() const noexcept;

  // True once joinFor() reported success.
  bool joined() const noexcept;

  // Destruction requires a previously successful joinFor(). The destructor
  // never hides an unbounded join, never detaches a worker and never forces
  // the process to exit. A generation that was never joined still owns
  // joinable workers, so destroying it violates the owner lifetime
  // precondition and the standard thread ownership contract applies.
  ~ObservationService();

  ObservationService(const ObservationService &) = delete;
  ObservationService & operator=(const ObservationService &) = delete;

private:
  explicit ObservationService(std::shared_ptr<detail::ObservationImpl> impl);

  std::shared_ptr<detail::ObservationImpl> impl_;
};

}  // namespace chaos::runtime

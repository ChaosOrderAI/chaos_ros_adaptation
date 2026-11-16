// ROS-free runtime observation service.
//
// Four joinable workers (trace, results, state, log) drain bounded per
// producer queues. Every rule that keeps the service bounded lives here:
//
//   * admission uses try_lock, so a producer never waits for a worker, a
//     sink, a registration or queue space;
//   * a user callable lives in a stable per channel holder that is
//     allocated before the registration is published. Only the shared
//     ownership handle is copied or moved afterwards: no user callable is
//     ever copied or destroyed while an internal lock is held;
//   * workers move the payload and the owned resource handle out of the
//     internal locks before invoking anything;
//   * a blocked channel worker holds no lock, so the other channels keep
//     running;
//   * every channel retires its own registrations: the sink holder and the
//     callback captures it owns are released outside every internal lock,
//     and only then the channel sets its own retirement bit. A slot becomes
//     reusable only when all four bits are set, no work is pending and no
//     resource is in flight;
//   * the in flight accounting is decremented after the payload and the
//     owned resource handle were really released, so a slot is never reused
//     while a destructor is still blocked;
//   * a worker is only finished when its thread really exited. The live
//     worker counters are a diagnostic: a user thread local destructor may
//     still be blocked after the worker body returned. Every worker owns an
//     exit ready shared state that is satisfied at the actual thread exit,
//     after the thread local cleanup, and the join owner waits for those
//     states before it joins a thread.
//
// The service is owned by the process manager (ObservationService::process),
// which is intentionally leaked: no static destructor can take part in the
// teardown of a live worker pool. A generation owns its four worker threads
// and reclaims them through an ordinary successful join.

#include "chaos_runtime/observation.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace chaos::runtime {
namespace detail {

constexpr std::size_t kTraceChannelIndex = 0U;
constexpr std::size_t kResultsChannelIndex = 1U;
constexpr std::size_t kStateChannelIndex = 2U;
constexpr std::size_t kLogChannelIndex = 3U;
constexpr std::size_t kChannelCount = kObservationChannelCount;
constexpr std::size_t kSlotCount = observation_limits::kProducerSlots;

// The close flag of a producer and the global stop gate are flipped without
// the queue lock, so a notification can be lost between the evaluation of a
// worker predicate and the wait that follows. The workers therefore re-check
// their predicate on a bounded interval: the delay is bounded and no worker
// spins.
constexpr std::chrono::milliseconds kWorkerPollInterval{5};

// The join ownership of the four worker threads.
enum class JoinState : std::uint8_t {
  idle,
  joining,
  joined
};

std::size_t channelIndex(ObservationChannel channel) {
  switch (channel) {
    case ObservationChannel::trace:
      return kTraceChannelIndex;
    case ObservationChannel::results:
      return kResultsChannelIndex;
    case ObservationChannel::state:
      return kStateChannelIndex;
    case ObservationChannel::log:
      return kLogChannelIndex;
  }
  return kTraceChannelIndex;
}

std::int64_t steadyNowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
           std::chrono::steady_clock::now().time_since_epoch())
    .count();
}

// Overflow safe addition: an oversized value is rejected before any copy.
std::uint64_t saturatedAdd(std::uint64_t left, std::uint64_t right) {
  const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max();
  if (left > limit - right) {
    return limit;
  }
  return left + right;
}

std::uint64_t addBytes(std::uint64_t total, std::size_t value) {
  return saturatedAdd(total, static_cast<std::uint64_t>(value));
}

std::uint64_t traceEventBytes(const TraceEvent & event) {
  std::uint64_t total = 0;
  total = addBytes(total, event.frame.size());
  total = addBytes(total, event.asset_id.size());
  total = addBytes(total, event.request_id.size());
  total = addBytes(total, event.command.size());
  total = addBytes(total, event.stage.size());
  total = addBytes(total, event.code.size());
  return total;
}

std::uint64_t commandResultBytes(const CommandResultView & result) {
  std::uint64_t total = 0;
  total = addBytes(total, result.request_id.size());
  total = addBytes(total, result.code.size());
  total = addBytes(total, result.message.size());
  return total;
}

std::uint64_t stateViewBytes(const StateView & view) {
  std::uint64_t total = 0;
  total = addBytes(total, view.asset_id.size());
  total = addBytes(total, view.model.size());
  total = addBytes(total, view.operational_state.size());
  total = addBytes(total, view.detail.size());
  total = addBytes(total, view.snapshot.detail.size());
  total = addBytes(total, view.snapshot.pose_frame.size());
  total = addBytes(total, view.snapshot.twist_frame.size());
  return total;
}

std::uint64_t logPayloadBytes(std::size_t level, std::size_t text) {
  return saturatedAdd(static_cast<std::uint64_t>(level),
                      static_cast<std::uint64_t>(text));
}

// Fixed size numeric counters. Every counter is an atomic, so a snapshot
// never needs the control mutex and never runs a sink callback.
struct Counters {
  std::atomic<std::uint64_t> attempted{0};
  std::atomic<std::uint64_t> admitted{0};
  std::atomic<std::uint64_t> delivered{0};
  std::atomic<std::uint64_t> dropped_full{0};
  std::atomic<std::uint64_t> dropped_contended{0};
  std::atomic<std::uint64_t> dropped_closed{0};
  std::atomic<std::uint64_t> dropped_oversize{0};
  std::atomic<std::uint64_t> dropped_construction{0};
  std::atomic<std::uint64_t> sink_exceptions{0};
  std::atomic<std::uint64_t> coalesced{0};
};

enum class Outcome {
  admitted,
  coalesced,
  full,
  contended,
  closed,
  oversize,
  construction
};

void bump(std::atomic<std::uint64_t> & counter) {
  counter.fetch_add(1U, std::memory_order_relaxed);
}

void applyOutcome(Counters & counters, Outcome outcome) {
  bump(counters.attempted);
  switch (outcome) {
    case Outcome::admitted:
      bump(counters.admitted);
      break;
    case Outcome::coalesced:
      bump(counters.admitted);
      bump(counters.coalesced);
      break;
    case Outcome::full:
      bump(counters.dropped_full);
      break;
    case Outcome::contended:
      bump(counters.dropped_contended);
      break;
    case Outcome::closed:
      bump(counters.dropped_closed);
      break;
    case Outcome::oversize:
      bump(counters.dropped_oversize);
      break;
    case Outcome::construction:
      bump(counters.dropped_construction);
      break;
  }
}

ChannelStats toStats(const Counters & counters) {
  ChannelStats stats;
  stats.attempted = counters.attempted.load(std::memory_order_relaxed);
  stats.admitted = counters.admitted.load(std::memory_order_relaxed);
  stats.delivered = counters.delivered.load(std::memory_order_relaxed);
  stats.dropped_full = counters.dropped_full.load(std::memory_order_relaxed);
  stats.dropped_contended =
    counters.dropped_contended.load(std::memory_order_relaxed);
  stats.dropped_closed = counters.dropped_closed.load(std::memory_order_relaxed);
  stats.dropped_oversize =
    counters.dropped_oversize.load(std::memory_order_relaxed);
  stats.dropped_construction =
    counters.dropped_construction.load(std::memory_order_relaxed);
  stats.sink_exceptions = counters.sink_exceptions.load(std::memory_order_relaxed);
  stats.coalesced = counters.coalesced.load(std::memory_order_relaxed);
  return stats;
}

// Per producer counters and metadata. The endpoint keeps a strong reference
// to this state, so the producer snapshot survives the retirement of the
// registration.
//
// The state owns metadata only: no sink, no callback capture and no worker.
// The closed flag is the linearization point of the close: it is flipped
// without the queue lock and it is re-read under the queue lock by every
// admission.
struct ProducerState {
  std::array<Counters, kChannelCount> channels;
  std::atomic<std::uint64_t> queued_items{0};
  std::atomic<std::uint64_t> queued_bytes{0};
  std::atomic<std::uint64_t> in_flight_items{0};
  std::atomic<std::uint64_t> in_flight_bytes{0};
  std::atomic<std::uint64_t> payload_high_water_bytes{0};
  std::atomic<std::uint64_t> last_assigned_trace_sequence{0};
  std::atomic<std::uint64_t> last_delivered_trace_sequence{0};
  std::atomic<bool> closed{false};
};

// The highest trace sequence the endpoint saw. The coordinator assigns the
// sequence before the attempt, so a lost event still moves the watermark.
void updateLastAssigned(const std::shared_ptr<ProducerState> & producer,
                        std::uint64_t sequence) {
  std::uint64_t current =
    producer->last_assigned_trace_sequence.load(std::memory_order_relaxed);
  while (sequence > current &&
         !producer->last_assigned_trace_sequence.compare_exchange_weak(
           current, sequence, std::memory_order_relaxed,
           std::memory_order_relaxed)) {
  }
}

void updateLastDelivered(const std::shared_ptr<ProducerState> & producer,
                          std::uint64_t sequence) {
  std::uint64_t current =
    producer->last_delivered_trace_sequence.load(std::memory_order_relaxed);
  while (sequence > current &&
         !producer->last_delivered_trace_sequence.compare_exchange_weak(
           current, sequence, std::memory_order_relaxed,
           std::memory_order_relaxed)) {
  }
}

// --- owned channel sinks ----------------------------------------------------
//
// Every user callable is owned by one of these holders. A holder is
// allocated before the registration is published, so the callable is moved
// into it outside every internal lock. From then on only the shared
// ownership handle travels: the registry, the workers and the work items
// copy a shared_ptr, never the callable.
struct ChannelSink {
  virtual ~ChannelSink() = default;
};

struct TraceChannelSink : ChannelSink {
  std::shared_ptr<TraceSink> sink;
};

struct ResultChannelSink : ChannelSink {
  std::function<void(const CommandResultView &)> callable;
};

struct StateChannelSink : ChannelSink {
  std::function<void(const StateView &)> callable;
};

struct LogChannelSink : ChannelSink {
  std::function<void(const std::string &, const std::string &)> callable;
};

// A registration in flight. The slot is reserved: the registration is not
// published yet and no admission can reach it.
struct Registration {
  std::size_t slot = 0;
  std::uint64_t registration_id = 0;
  std::shared_ptr<ProducerState> producer;
  std::array<std::shared_ptr<ChannelSink>, kChannelCount> holders{};
};

struct ObservationImpl : public std::enable_shared_from_this<ObservationImpl> {
  struct LogPayload {
    std::string level;
    std::string text;
  };

  template <typename T>
  struct Sized {
    T value{};
    std::uint64_t bytes = 0;
  };

  struct Slot {
    // A registration is reserved while the caller builds its endpoint and
    // occupied once the registration is published.
    bool reserved = false;
    bool occupied = false;
    bool counted_retiring = false;
    std::uint64_t registration_id = 0;
    std::shared_ptr<ProducerState> producer;
    // The owned sink of every channel. Each channel worker owns the
    // retirement of its own holder.
    std::array<std::shared_ptr<ChannelSink>, kChannelCount> channel_sinks{};
    std::deque<Sized<TraceEvent>> trace_queue;
    std::deque<Sized<CommandResultView>> results_queue;
    std::deque<Sized<LogPayload>> log_queue;
    std::optional<Sized<StateView>> pending_state;
    // One retirement bit per channel. A slot is reusable only when all four
    // bits are set.
    std::array<bool, kChannelCount> channel_retired{};
    std::array<bool, kChannelCount> channel_retiring{};
  };

  // A work item owns the payload and a shared ownership handle of the
  // channel sink. It never owns a user callable directly.
  struct TraceWork {
    std::size_t slot = 0;
    std::uint64_t bytes = 0;
    Sized<TraceEvent> payload;
    std::shared_ptr<ProducerState> producer;
    std::shared_ptr<ChannelSink> sink;
  };

  struct ResultWork {
    std::size_t slot = 0;
    std::uint64_t bytes = 0;
    Sized<CommandResultView> payload;
    std::shared_ptr<ProducerState> producer;
    std::shared_ptr<ChannelSink> sink;
  };

  struct StateWork {
    std::size_t slot = 0;
    std::uint64_t bytes = 0;
    Sized<StateView> payload;
    std::shared_ptr<ProducerState> producer;
    std::shared_ptr<ChannelSink> sink;
  };

  struct LogWork {
    std::size_t slot = 0;
    std::uint64_t bytes = 0;
    Sized<LogPayload> payload;
    std::shared_ptr<ProducerState> producer;
    std::shared_ptr<ChannelSink> sink;
  };

  // Per channel behaviour. pop() moves the payload out of the queue and
  // captureSink() copies the shared ownership handle of the channel sink.
  // The delivery happens outside every lock.
  struct TraceChannelTraits {
    using Payload = TraceEvent;
    using Work = TraceWork;
    static constexpr std::size_t kIndex = kTraceChannelIndex;

    static std::size_t capacity() {
      return observation_limits::kTraceQueueCapacity;
    }
    static std::size_t queued(const Slot & slot) {
      return slot.trace_queue.size();
    }
    static void push(Slot & slot, Sized<Payload> && payload) {
      slot.trace_queue.push_back(std::move(payload));
    }
    static bool pop(Slot & slot, Work & work) {
      if (slot.trace_queue.empty()) {
        return false;
      }
      work.payload = std::move(slot.trace_queue.front());
      slot.trace_queue.pop_front();
      return true;
    }
    static void captureSink(Slot & slot, Work & work) {
      work.sink = slot.channel_sinks[kIndex];
    }
    static void deliver(const Work & work) {
      const TraceChannelSink * holder =
        static_cast<const TraceChannelSink *>(work.sink.get());
      if (holder->sink) {
        holder->sink->publish(work.payload.value);
      }
    }
    // Only the trace channel carries an assigned sequence.
    static std::uint64_t deliveredSequence(const Work & work) {
      return work.payload.value.sequence;
    }
  };

  struct ResultChannelTraits {
    using Payload = CommandResultView;
    using Work = ResultWork;
    static constexpr std::size_t kIndex = kResultsChannelIndex;

    static std::size_t capacity() {
      return observation_limits::kResultsQueueCapacity;
    }
    static std::size_t queued(const Slot & slot) {
      return slot.results_queue.size();
    }
    static void push(Slot & slot, Sized<Payload> && payload) {
      slot.results_queue.push_back(std::move(payload));
    }
    static bool pop(Slot & slot, Work & work) {
      if (slot.results_queue.empty()) {
        return false;
      }
      work.payload = std::move(slot.results_queue.front());
      slot.results_queue.pop_front();
      return true;
    }
    static void captureSink(Slot & slot, Work & work) {
      work.sink = slot.channel_sinks[kIndex];
    }
    static void deliver(const Work & work) {
      const ResultChannelSink * holder =
        static_cast<const ResultChannelSink *>(work.sink.get());
      if (holder->callable) {
        holder->callable(work.payload.value);
      }
    }
    static std::uint64_t deliveredSequence(const Work &) {
      return 0U;
    }
  };

  struct StateChannelTraits {
    using Payload = StateView;
    using Work = StateWork;
    static constexpr std::size_t kIndex = kStateChannelIndex;

    static std::size_t capacity() {
      return observation_limits::kStatePendingSlots;
    }
    static std::size_t queued(const Slot & slot) {
      return slot.pending_state.has_value() ? 1U : 0U;
    }
    static void push(Slot & slot, Sized<Payload> && payload) {
      slot.pending_state = std::move(payload);
    }
    static bool pop(Slot & slot, Work & work) {
      if (!slot.pending_state.has_value()) {
        return false;
      }
      work.payload = std::move(*slot.pending_state);
      slot.pending_state.reset();
      return true;
    }
    static void captureSink(Slot & slot, Work & work) {
      work.sink = slot.channel_sinks[kIndex];
    }
    static void deliver(const Work & work) {
      const StateChannelSink * holder =
        static_cast<const StateChannelSink *>(work.sink.get());
      if (holder->callable) {
        holder->callable(work.payload.value);
      }
    }
    static std::uint64_t deliveredSequence(const Work &) {
      return 0U;
    }
  };

  struct LogChannelTraits {
    using Payload = LogPayload;
    using Work = LogWork;
    static constexpr std::size_t kIndex = kLogChannelIndex;

    static std::size_t capacity() {
      return observation_limits::kLogQueueCapacity;
    }
    static std::size_t queued(const Slot & slot) {
      return slot.log_queue.size();
    }
    static void push(Slot & slot, Sized<Payload> && payload) {
      slot.log_queue.push_back(std::move(payload));
    }
    static bool pop(Slot & slot, Work & work) {
      if (slot.log_queue.empty()) {
        return false;
      }
      work.payload = std::move(slot.log_queue.front());
      slot.log_queue.pop_front();
      return true;
    }
    static void captureSink(Slot & slot, Work & work) {
      work.sink = slot.channel_sinks[kIndex];
    }
    static void deliver(const Work & work) {
      const LogChannelSink * holder =
        static_cast<const LogChannelSink *>(work.sink.get());
      if (holder->callable) {
        holder->callable(work.payload.value.level, work.payload.value.text);
      }
    }
    static std::uint64_t deliveredSequence(const Work &) {
      return 0U;
    }
  };

  ObservationImpl() = default;

  ~ObservationImpl() {
    // A generation owns its four worker threads. A successful joinFor()
    // leaves them non joinable, so the ordinary destruction reclaims them.
    // Destroying a generation whose workers were never joined violates the
    // documented owner lifetime precondition; the standard thread ownership
    // contract applies and the workers are never detached here.
  }

  // --- startup -------------------------------------------------------------

  // The implementation and the service wrapper exist before the first
  // thread is started, so a partial start failure never publishes a
  // generation.
  void start() {
    // The exit ready shared state of every worker is prepared before the
    // first thread starts: a worker that fails to start never leaves a
    // dangling completion state behind, and the join owner always finds a
    // waiter side for every worker it has to observe.
    for (std::size_t index = 0; index < kChannelCount; ++index) {
      worker_exit[index].ready = worker_exit[index].promise.get_future().share();
    }
    live_workers.store(static_cast<int>(kChannelCount),
                       std::memory_order_release);
    std::size_t started = 0;
    try {
      workers[kTraceChannelIndex] = std::thread(
        [this] { this->template workerLoop<TraceChannelTraits>(kTraceChannelIndex); });
      ++started;
      workers[kResultsChannelIndex] = std::thread(
        [this] { this->template workerLoop<ResultChannelTraits>(kResultsChannelIndex); });
      ++started;
      workers[kStateChannelIndex] = std::thread(
        [this] { this->template workerLoop<StateChannelTraits>(kStateChannelIndex); });
      ++started;
      workers[kLogChannelIndex] = std::thread(
        [this] { this->template workerLoop<LogChannelTraits>(kLogChannelIndex); });
      ++started;
    } catch (...) {
      // A thread start failed: the workers that were already created are
      // idle. The stop gate is raised and the started workers are joined
      // before the construction fails: no orphan pool is left behind. Only
      // the actually started threads are handled: a thread that was never
      // created has no handle and no running body, and the rollback of the
      // registration storage stays untouched.
      requestStop();
      for (std::size_t index = 0; index < started; ++index) {
        if (workers[index].joinable()) {
          workers[index].join();
        }
      }
      live_workers.store(0, std::memory_order_release);
      started_workers = started;
      throw std::runtime_error(
        "chaos_runtime: failed to start the observation workers");
    }
    started_workers = started;
  }

  // --- registration --------------------------------------------------------

  // Reserves a registration slot and prepares the owned channel sinks. The
  // holders are allocated before the registry is touched, so no user
  // callable is copied or destroyed under the registry lock.
  Registration reserveProducer(ObservationSinks sinks) {
    Registration reservation;
    std::shared_ptr<TraceChannelSink> trace_holder =
      std::make_shared<TraceChannelSink>();
    trace_holder->sink = std::move(sinks.trace);
    reservation.holders[kTraceChannelIndex] = trace_holder;
    std::shared_ptr<ResultChannelSink> result_holder =
      std::make_shared<ResultChannelSink>();
    result_holder->callable = std::move(sinks.results);
    reservation.holders[kResultsChannelIndex] = result_holder;
    std::shared_ptr<StateChannelSink> state_holder =
      std::make_shared<StateChannelSink>();
    state_holder->callable = std::move(sinks.state);
    reservation.holders[kStateChannelIndex] = state_holder;
    std::shared_ptr<LogChannelSink> log_holder =
      std::make_shared<LogChannelSink>();
    log_holder->callable = std::move(sinks.log);
    reservation.holders[kLogChannelIndex] = log_holder;
    reservation.producer = std::make_shared<ProducerState>();
    {
      std::lock_guard<std::mutex> guard(mutex);
      // The registration gate: a stopped or joining generation never
      // acquires a new live sink.
      if (stop_requested.load(std::memory_order_acquire) ||
          join_state.load(std::memory_order_acquire) != JoinState::idle) {
        throw std::runtime_error(
          "chaos_runtime: the observation service is stopping");
      }
      std::size_t index = kSlotCount;
      for (std::size_t candidate = 0; candidate < kSlotCount; ++candidate) {
        if (!slots[candidate].occupied && !slots[candidate].reserved) {
          index = candidate;
          break;
        }
      }
      if (index == kSlotCount) {
        throw std::runtime_error(
          "chaos_runtime: no observation producer slot is available");
      }
      Slot & slot = slots[index];
      slot.reserved = true;
      slot.registration_id =
        next_registration_id.fetch_add(1U, std::memory_order_relaxed);
      slot.producer = reservation.producer;
      reservation.slot = index;
      reservation.registration_id = slot.registration_id;
    }
    return reservation;
  }

  // Publishes a reserved registration. The check is done under the registry
  // lock, so a stop that races this commit is either seen here or is seen
  // by the workers, which then close and retire the registration.
  bool commitReservation(Registration & reservation) {
    {
      std::lock_guard<std::mutex> guard(mutex);
      Slot & slot = slots[reservation.slot];
      if (!slot.reserved ||
          slot.registration_id != reservation.registration_id) {
        return false;
      }
      if (stop_requested.load(std::memory_order_acquire) ||
          join_state.load(std::memory_order_acquire) != JoinState::idle) {
        return false;
      }
      slot.channel_sinks = std::move(reservation.holders);
      slot.channel_retired = {};
      slot.channel_retiring = {};
      slot.occupied = true;
      slot.reserved = false;
      slot.producer->closed.store(false, std::memory_order_relaxed);
      registered_producers.fetch_add(1U, std::memory_order_relaxed);
    }
    cv.notify_all();
    return true;
  }

  // Releases a reserved registration that was never published. The owned
  // holders are moved out under the registry lock and destroyed outside of
  // it, so no user callable destructor runs under the lock.
  void rollbackReservation(Registration & reservation) {
    std::array<std::shared_ptr<ChannelSink>, kChannelCount> released;
    {
      std::lock_guard<std::mutex> guard(mutex);
      Slot & slot = slots[reservation.slot];
      if (slot.reserved &&
          slot.registration_id == reservation.registration_id) {
        slot.reserved = false;
        slot.registration_id = 0;
        slot.producer.reset();
      }
      released = std::move(reservation.holders);
    }
    for (std::shared_ptr<ChannelSink> & holder : released) {
      holder.reset();
    }
  }

  // --- admission -----------------------------------------------------------

  // The common admission path of the FIFO channels. The metadata gate and
  // the global stop gate are checked before the work and again while the
  // slot and the registration id are validated under the try lock.
  template <typename Traits>
  Admission admitFifo(std::size_t slot_index, std::uint64_t registration_id,
                       const std::shared_ptr<ProducerState> & producer,
                       Sized<typename Traits::Payload> && payload,
                       std::uint64_t bytes) noexcept {
    constexpr std::size_t channel = Traits::kIndex;
    try {
      if (producer->closed.load(std::memory_order_acquire) ||
          stop_requested.load(std::memory_order_acquire)) {
        recordOutcome(producer, channel, Outcome::closed);
        return Admission::closed;
      }
      std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
      if (!lock.owns_lock()) {
        recordOutcome(producer, channel, Outcome::contended);
        return Admission::contended;
      }
      Slot & slot = slots[slot_index];
      if (!slot.occupied || slot.registration_id != registration_id ||
          producer->closed.load(std::memory_order_acquire) ||
          stop_requested.load(std::memory_order_acquire)) {
        recordOutcome(producer, channel, Outcome::closed);
        return Admission::closed;
      }
      if (Traits::queued(slot) >= Traits::capacity()) {
        recordOutcome(producer, channel, Outcome::full);
        return Admission::full;
      }
      if (!reservePayload(bytes)) {
        recordOutcome(producer, channel, Outcome::full);
        return Admission::full;
      }
      bool stored = false;
      try {
        Traits::push(slot, std::move(payload));
        stored = true;
      } catch (...) {
        releasePayload(bytes);
      }
      if (!stored) {
        recordOutcome(producer, channel, Outcome::construction);
        return Admission::construction_failed;
      }
      producer->queued_items.fetch_add(1U, std::memory_order_relaxed);
      producer->queued_bytes.fetch_add(bytes, std::memory_order_relaxed);
      queued_items.fetch_add(1U, std::memory_order_relaxed);
      queued_bytes.fetch_add(bytes, std::memory_order_relaxed);
      updateProducerHighWater(producer);
      recordOutcome(producer, channel, Outcome::admitted);
      lock.unlock();
      cv.notify_all();
      return Admission::admitted;
    } catch (...) {
      recordOutcome(producer, channel, Outcome::construction);
      return Admission::construction_failed;
    }
  }

  Admission admitTrace(std::size_t slot_index, std::uint64_t registration_id,
                       const std::shared_ptr<ProducerState> & producer,
                       const TraceEvent & event) noexcept {
    try {
      // The coordinator assigned the sequence before the attempt, so a lost
      // event still shows up as a sequence gap.
      updateLastAssigned(producer, event.sequence);
      const std::uint64_t bytes = traceEventBytes(event);
      if (bytes > observation_limits::kMaxEventStringBytes) {
        recordOutcome(producer, kTraceChannelIndex, Outcome::oversize);
        return Admission::oversize;
      }
      Sized<TraceEvent> payload;
      payload.value = event;
      payload.bytes = bytes;
      return admitFifo<TraceChannelTraits>(slot_index, registration_id,
                                           producer, std::move(payload), bytes);
    } catch (...) {
      recordOutcome(producer, kTraceChannelIndex, Outcome::construction);
      return Admission::construction_failed;
    }
  }

  Admission admitResult(std::size_t slot_index, std::uint64_t registration_id,
                        const std::shared_ptr<ProducerState> & producer,
                        const CommandResultView & result) noexcept {
    try {
      const std::uint64_t bytes = commandResultBytes(result);
      if (bytes > observation_limits::kMaxEventStringBytes) {
        recordOutcome(producer, kResultsChannelIndex, Outcome::oversize);
        return Admission::oversize;
      }
      Sized<CommandResultView> payload;
      payload.value = result;
      payload.bytes = bytes;
      return admitFifo<ResultChannelTraits>(slot_index, registration_id,
                                            producer, std::move(payload), bytes);
    } catch (...) {
      recordOutcome(producer, kResultsChannelIndex, Outcome::construction);
      return Admission::construction_failed;
    }
  }

  Admission admitLog(std::size_t slot_index, std::uint64_t registration_id,
                     const std::shared_ptr<ProducerState> & producer,
                     std::string_view level, std::string_view text) noexcept {
    try {
      const std::uint64_t bytes = logPayloadBytes(level.size(), text.size());
      if (bytes > observation_limits::kMaxEventStringBytes) {
        recordOutcome(producer, kLogChannelIndex, Outcome::oversize);
        return Admission::oversize;
      }
      Sized<LogPayload> payload;
      if (!level.empty()) {
        payload.value.level.assign(level.data(), level.size());
      }
      if (!text.empty()) {
        payload.value.text.assign(text.data(), text.size());
      }
      payload.bytes = bytes;
      return admitFifo<LogChannelTraits>(slot_index, registration_id, producer,
                                         std::move(payload), bytes);
    } catch (...) {
      recordOutcome(producer, kLogChannelIndex, Outcome::construction);
      return Admission::construction_failed;
    }
  }

  Admission admitState(std::size_t slot_index, std::uint64_t registration_id,
                       const std::shared_ptr<ProducerState> & producer,
                       const StateView & view) noexcept {
    try {
      const std::uint64_t bytes = stateViewBytes(view);
      if (bytes > observation_limits::kMaxEventStringBytes) {
        recordOutcome(producer, kStateChannelIndex, Outcome::oversize);
        return Admission::oversize;
      }
      Sized<StateView> payload;
      payload.value = view;
      payload.bytes = bytes;
      if (producer->closed.load(std::memory_order_acquire) ||
          stop_requested.load(std::memory_order_acquire)) {
        recordOutcome(producer, kStateChannelIndex, Outcome::closed);
        return Admission::closed;
      }
      std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
      if (!lock.owns_lock()) {
        recordOutcome(producer, kStateChannelIndex, Outcome::contended);
        return Admission::contended;
      }
      Slot & slot = slots[slot_index];
      if (!slot.occupied || slot.registration_id != registration_id ||
          producer->closed.load(std::memory_order_acquire) ||
          stop_requested.load(std::memory_order_acquire)) {
        recordOutcome(producer, kStateChannelIndex, Outcome::closed);
        return Admission::closed;
      }
      bool coalesced = false;
      std::uint64_t replaced_bytes = 0;
      if (slot.pending_state.has_value()) {
        // The pending value is replaced; the previous value counts as
        // coalesced and its bytes are released atomically with the
        // reservation of the replacement.
        replaced_bytes = slot.pending_state->bytes;
        if (!replacePayload(replaced_bytes, bytes)) {
          recordOutcome(producer, kStateChannelIndex, Outcome::full);
          return Admission::full;
        }
        coalesced = true;
      } else {
        if (!reservePayload(bytes)) {
          recordOutcome(producer, kStateChannelIndex, Outcome::full);
          return Admission::full;
        }
      }
      bool stored = false;
      try {
        slot.pending_state = std::move(payload);
        stored = true;
      } catch (...) {
        if (coalesced) {
          replacePayload(bytes, replaced_bytes);
        } else {
          releasePayload(bytes);
        }
      }
      if (!stored) {
        recordOutcome(producer, kStateChannelIndex, Outcome::construction);
        return Admission::construction_failed;
      }
      if (coalesced) {
        producer->queued_bytes.fetch_sub(replaced_bytes,
                                         std::memory_order_relaxed);
        producer->queued_bytes.fetch_add(bytes, std::memory_order_relaxed);
        queued_bytes.fetch_sub(replaced_bytes, std::memory_order_relaxed);
        queued_bytes.fetch_add(bytes, std::memory_order_relaxed);
        recordOutcome(producer, kStateChannelIndex, Outcome::coalesced);
      } else {
        producer->queued_items.fetch_add(1U, std::memory_order_relaxed);
        producer->queued_bytes.fetch_add(bytes, std::memory_order_relaxed);
        queued_items.fetch_add(1U, std::memory_order_relaxed);
        queued_bytes.fetch_add(bytes, std::memory_order_relaxed);
        recordOutcome(producer, kStateChannelIndex, Outcome::admitted);
      }
      updateProducerHighWater(producer);
      lock.unlock();
      cv.notify_all();
      return coalesced ? Admission::coalesced : Admission::admitted;
    } catch (...) {
      recordOutcome(producer, kStateChannelIndex, Outcome::construction);
      return Admission::construction_failed;
    }
  }

  void recordConstruction(const std::shared_ptr<ProducerState> & producer,
                          std::size_t channel) {
    recordOutcome(producer, channel, Outcome::construction);
  }

  // --- lifecycle -----------------------------------------------------------

  // Wakes the workers without taking the queue lock.
  void wakeWorkers() noexcept {
    cv.notify_all();
  }

  // Raises the global stop gate. No queue lock is taken and no user
  // resource is destroyed on the calling thread.
  void requestStop() noexcept {
    stop_requested.store(true, std::memory_order_release);
    cv.notify_all();
  }

  // Waits for the workers to finish and joins them. Exactly one caller owns
  // the join at a time: the others wait for the owner, bounded by their own
  // deadline. A timeout leaves the threads joinable and every needed
  // resource owned.
  bool joinFor(std::chrono::milliseconds timeout) {
    if (joined.load(std::memory_order_acquire)) {
      return true;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::timed_mutex> gate(join_mutex, std::defer_lock);
    if (!gate.try_lock_until(deadline)) {
      return false;
    }
    while (true) {
      const JoinState state = join_state.load(std::memory_order_acquire);
      if (state == JoinState::joined) {
        return true;
      }
      if (state == JoinState::idle) {
        join_state.store(JoinState::joining, std::memory_order_release);
        break;
      }
      // Another caller owns the join: wait for it, but only until this
      // caller owns deadline.
      if (!join_cv.wait_until(gate, deadline, [this] {
            return join_state.load(std::memory_order_acquire) !=
                   JoinState::joining;
          })) {
        return false;
      }
    }
    gate.unlock();
    if (!waitForWorkerExit(deadline)) {
      // The deadline expired before every worker really exited: the threads
      // stay joinable and every needed resource stays owned. Nothing is
      // detached, no live resource is released and no replacement
      // generation may be created. The exit ready states are kept, so a
      // later call can finish the join once the blocked thread local
      // cleanup is released.
      {
        std::lock_guard<std::timed_mutex> guard(join_mutex);
        join_state.store(JoinState::idle, std::memory_order_release);
      }
      join_cv.notify_all();
      return false;
    }
    // The real exit of every worker is known: the threads can be joined.
    // Only the join owner touches the thread objects.
    for (std::size_t index = 0; index < kChannelCount; ++index) {
      if (workers[index].joinable()) {
        workers[index].join();
      }
    }
    {
      std::lock_guard<std::timed_mutex> guard(join_mutex);
      join_state.store(JoinState::joined, std::memory_order_release);
      joined.store(true, std::memory_order_release);
    }
    join_cv.notify_all();
    return true;
  }

  ObservationServiceStats stats() const noexcept {
    ObservationServiceStats snapshot;
    for (std::size_t index = 0; index < kChannelCount; ++index) {
      snapshot.channels[index] = toStats(channels[index]);
    }
    snapshot.registered_producers =
      registered_producers.load(std::memory_order_relaxed);
    snapshot.retiring_producers =
      retiring_producers.load(std::memory_order_relaxed);
    snapshot.slots_used = snapshot.registered_producers;
    snapshot.slots_total = kSlotCount;
    snapshot.queued_items = queued_items.load(std::memory_order_relaxed);
    snapshot.queued_bytes = queued_bytes.load(std::memory_order_relaxed);
    snapshot.in_flight_items = in_flight_items.load(std::memory_order_relaxed);
    snapshot.in_flight_bytes = in_flight_bytes.load(std::memory_order_relaxed);
    snapshot.payload_high_water_bytes =
      payload_high_water.load(std::memory_order_relaxed);
    for (std::size_t index = 0; index < kChannelCount; ++index) {
      snapshot.worker_busy[index] =
        worker_busy[index].load(std::memory_order_relaxed);
      snapshot.worker_busy_since_ns[index] =
        worker_busy_since_ns[index].load(std::memory_order_relaxed);
    }
    snapshot.stop_requested = stop_requested.load(std::memory_order_relaxed);
    snapshot.joined = joined.load(std::memory_order_relaxed);
    return snapshot;
  }

  // --- internals -----------------------------------------------------------

  void recordOutcome(const std::shared_ptr<ProducerState> & producer,
                     std::size_t channel, Outcome outcome) {
    applyOutcome(producer->channels[channel], outcome);
    applyOutcome(channels[channel], outcome);
  }

  void updateProducerHighWater(const std::shared_ptr<ProducerState> & producer) {
    const std::uint64_t current = producer->queued_bytes.load(
                                    std::memory_order_relaxed) +
                                  producer->in_flight_bytes.load(
                                    std::memory_order_relaxed);
    std::uint64_t high =
      producer->payload_high_water_bytes.load(std::memory_order_relaxed);
    while (current > high &&
           !producer->payload_high_water_bytes.compare_exchange_weak(
             high, current, std::memory_order_relaxed,
             std::memory_order_relaxed)) {
    }
  }

  bool reservePayload(std::uint64_t bytes) {
    if (bytes > observation_limits::kMaxAggregatePayloadBytes) {
      return false;
    }
    std::uint64_t current = payload_bytes.load(std::memory_order_relaxed);
    while (true) {
      if (current >
          observation_limits::kMaxAggregatePayloadBytes - bytes) {
        return false;
      }
      if (payload_bytes.compare_exchange_weak(
            current, current + bytes, std::memory_order_acq_rel,
            std::memory_order_relaxed)) {
        updateHighWater(current + bytes);
        return true;
      }
    }
  }

  bool replacePayload(std::uint64_t old_bytes, std::uint64_t new_bytes) {
    std::uint64_t current = payload_bytes.load(std::memory_order_relaxed);
    while (true) {
      if (current < old_bytes) {
        return false;
      }
      const std::uint64_t next = current - old_bytes;
      if (new_bytes >
          observation_limits::kMaxAggregatePayloadBytes - next) {
        return false;
      }
      if (payload_bytes.compare_exchange_weak(
            current, next + new_bytes, std::memory_order_acq_rel,
            std::memory_order_relaxed)) {
        updateHighWater(next + new_bytes);
        return true;
      }
    }
  }

  void releasePayload(std::uint64_t bytes) {
    payload_bytes.fetch_sub(bytes, std::memory_order_acq_rel);
  }

  void updateHighWater(std::uint64_t value) {
    std::uint64_t high = payload_high_water.load(std::memory_order_relaxed);
    while (value > high &&
           !payload_high_water.compare_exchange_weak(
             high, value, std::memory_order_relaxed,
             std::memory_order_relaxed)) {
    }
  }

  bool channelEmptyLocked(const Slot & slot, std::size_t channel) const {
    switch (channel) {
      case kTraceChannelIndex:
        return slot.trace_queue.empty();
      case kResultsChannelIndex:
        return slot.results_queue.empty();
      case kStateChannelIndex:
        return !slot.pending_state.has_value();
      case kLogChannelIndex:
        return slot.log_queue.empty();
      default:
        return true;
    }
  }

  bool hasWorkLocked(std::size_t channel) const {
    for (const Slot & slot : slots) {
      if (!slot.occupied) {
        continue;
      }
      if (!channelEmptyLocked(slot, channel)) {
        return true;
      }
    }
    return false;
  }

  bool hasRetirableLocked(std::size_t channel) const {
    for (const Slot & slot : slots) {
      if (!slot.occupied) {
        continue;
      }
      if (!slot.producer->closed.load(std::memory_order_acquire)) {
        continue;
      }
      // A channel that is already retiring or already retired its own sink
      // is skipped: the holder was moved out of the slot, so selecting the
      // slot again would retire a null holder in an endless loop.
      if (slot.channel_retiring[channel] || slot.channel_retired[channel]) {
        continue;
      }
      if (!channelEmptyLocked(slot, channel)) {
        continue;
      }
      return true;
    }
    return false;
  }

  bool anyOccupiedLocked() const {
    for (const Slot & slot : slots) {
      if (slot.occupied) {
        return true;
      }
    }
    return false;
  }

  // Counts a registration as retiring exactly once.
  void markRetiringLocked(Slot & slot) {
    if (slot.counted_retiring) {
      return;
    }
    slot.counted_retiring = true;
    retiring_producers.fetch_add(1U, std::memory_order_relaxed);
  }

  // Closes every producer of a stopping generation. Only the metadata flag
  // is touched; no user resource is destroyed here.
  void closeAllLocked() {
    for (Slot & slot : slots) {
      if (!slot.occupied) {
        continue;
      }
      if (!slot.producer->closed.load(std::memory_order_relaxed)) {
        slot.producer->closed.store(true, std::memory_order_relaxed);
      }
      markRetiringLocked(slot);
    }
  }

  // Selects one registration that this channel can retire. The owned sink
  // holder is moved out of the slot: the caller destroys it outside every
  // internal lock.
  bool selectRetirableLocked(std::size_t channel, std::size_t & index,
                             std::shared_ptr<ChannelSink> & retired_sink) {
    for (std::size_t candidate = 0; candidate < kSlotCount; ++candidate) {
      Slot & slot = slots[candidate];
      if (!slot.occupied) {
        continue;
      }
      if (!slot.producer->closed.load(std::memory_order_acquire)) {
        continue;
      }
      // A channel that is already retiring or already retired its own sink
      // is skipped: the holder was moved out of the slot, so selecting the
      // slot again would retire a null holder in an endless loop.
      if (slot.channel_retiring[channel] || slot.channel_retired[channel]) {
        continue;
      }
      if (!channelEmptyLocked(slot, channel)) {
        continue;
      }
      markRetiringLocked(slot);
      slot.channel_retiring[channel] = true;
      retired_sink = std::move(slot.channel_sinks[channel]);
      index = candidate;
      return true;
    }
    return false;
  }

  // A slot becomes reusable only when every channel retired its own sink,
  // no work is pending and no resource is in flight.
  void reclaimSlotLocked(std::size_t index) {
    Slot & slot = slots[index];
    if (!slot.occupied) {
      return;
    }
    for (std::size_t channel = 0; channel < kChannelCount; ++channel) {
      if (!slot.channel_retired[channel] || slot.channel_retiring[channel]) {
        return;
      }
      if (!channelEmptyLocked(slot, channel)) {
        return;
      }
    }
    if (slot.producer->in_flight_items.load(std::memory_order_acquire) !=
        0U) {
      return;
    }
    slot.occupied = false;
    slot.reserved = false;
    slot.registration_id = 0;
    slot.counted_retiring = false;
    slot.channel_retired = {};
    slot.channel_retiring = {};
    slot.producer.reset();
    registered_producers.fetch_sub(1U, std::memory_order_relaxed);
    retiring_producers.fetch_sub(1U, std::memory_order_relaxed);
  }

  // Reclaims every registration whose four channels are retired and whose
  // resources are really gone.
  void reclaimReadyLocked() {
    for (std::size_t index = 0; index < kSlotCount; ++index) {
      reclaimSlotLocked(index);
    }
  }

  // Waits until every started worker really exited.
  //
  // The live worker counter is a diagnostic only: it is decremented at the
  // end of the worker body, while a user thread local destructor of that
  // worker may still be blocked. The exit ready states are satisfied at the
  // actual thread exit, after the thread local cleanup, so they are the
  // only safe proof that a join cannot block. The wait uses the caller's
  // absolute deadline: a timeout returns false and leaves every thread
  // joinable.
  bool waitForWorkerExit(std::chrono::steady_clock::time_point deadline) {
    for (std::size_t index = 0; index < started_workers; ++index) {
      if (worker_exit[index].ready.wait_until(deadline) !=
          std::future_status::ready) {
        return false;
      }
    }
    return true;
  }

  template <typename Traits>
  void workerLoop(std::size_t channel_index) {
    // The worker keeps the implementation alive for the whole run.
    const std::shared_ptr<ObservationImpl> self = shared_from_this();
    typename Traits::Work work;
    std::size_t cursor = 0;
    while (true) {
      bool have_work = false;
      std::uint64_t bytes = 0;
      std::uint64_t sequence = 0;
      std::shared_ptr<ProducerState> producer;
      {
        std::unique_lock<std::mutex> lock(mutex);
        for (std::size_t step = 0; step < kSlotCount; ++step) {
          const std::size_t index = (cursor + step) % kSlotCount;
          Slot & slot = slots[index];
          if (!slot.occupied) {
            continue;
          }
          if (!Traits::pop(slot, work)) {
            continue;
          }
          work.slot = index;
          work.producer = slot.producer;
          // Only the shared ownership handle is copied here: the user
          // callable itself is never copied under the registry lock.
          Traits::captureSink(slot, work);
          cursor = (index + 1U) % kSlotCount;
          have_work = true;
          bytes = work.payload.bytes;
          sequence = Traits::deliveredSequence(work);
          producer = slot.producer;
          slot.producer->queued_items.fetch_sub(1U, std::memory_order_relaxed);
          slot.producer->queued_bytes.fetch_sub(bytes,
                                                std::memory_order_relaxed);
          queued_items.fetch_sub(1U, std::memory_order_relaxed);
          queued_bytes.fetch_sub(bytes, std::memory_order_relaxed);
          slot.producer->in_flight_items.fetch_add(1U,
                                                   std::memory_order_relaxed);
          slot.producer->in_flight_bytes.fetch_add(bytes,
                                                   std::memory_order_relaxed);
          in_flight_items.fetch_add(1U, std::memory_order_relaxed);
          in_flight_bytes.fetch_add(bytes, std::memory_order_relaxed);
          break;
        }
        if (!have_work) {
          std::size_t retire_index = kSlotCount;
          std::shared_ptr<ChannelSink> retired_sink;
          if (selectRetirableLocked(channel_index, retire_index,
                                    retired_sink)) {
            lock.unlock();
            // The owned sink holder and every callback capture it owns are
            // released here, outside every internal lock. A blocked
            // destructor occupies this channel worker only.
            worker_busy[channel_index].store(true, std::memory_order_relaxed);
            worker_busy_since_ns[channel_index].store(steadyNowNs(),
                                                      std::memory_order_relaxed);
            retired_sink.reset();
            worker_busy[channel_index].store(false, std::memory_order_relaxed);
            worker_busy_since_ns[channel_index].store(0,
                                                      std::memory_order_relaxed);
            {
              std::lock_guard<std::mutex> guard(mutex);
              Slot & slot = slots[retire_index];
              slot.channel_retiring[channel_index] = false;
              slot.channel_retired[channel_index] = true;
              reclaimSlotLocked(retire_index);
            }
            cv.notify_all();
            continue;
          }
          if (stop_requested.load(std::memory_order_acquire)) {
            // A stopping generation closes every producer; the channels
            // then retire their own registrations.
            closeAllLocked();
          }
          if (stop_requested.load(std::memory_order_acquire) &&
              !anyOccupiedLocked()) {
            // Every registration was retired by its four channels: this
            // worker completed its own retirement duties.
            break;
          }
          // The stop gate alone must not wake this worker: another channel
          // may still be blocked in a user destructor, so the producer slot
          // is not reclaimed yet and this channel has nothing to do. The
          // worker waits for its own work, for an eligible retirement or
          // for the moment when every slot is gone. The bounded poll keeps
          // the worker responsive when an atomic gate notification is lost.
          cv.wait_for(lock, kWorkerPollInterval, [this, channel_index] {
            return hasWorkLocked(channel_index) ||
                   hasRetirableLocked(channel_index) ||
                   (stop_requested.load(std::memory_order_acquire) &&
                    !anyOccupiedLocked());
          });
          continue;
        }
      }
      // The payload and the owned resource handle belong to this worker
      // now: no internal lock is held while the sink runs.
      bool threw = false;
      worker_busy[channel_index].store(true, std::memory_order_relaxed);
      worker_busy_since_ns[channel_index].store(steadyNowNs(),
                                                std::memory_order_relaxed);
      try {
        Traits::deliver(work);
      } catch (...) {
        // A failing sink is isolated: it is counted, never retried and
        // never reported through the log channel.
        threw = true;
      }
      // The payload and the owned resource handle are released here,
      // outside every internal lock and before the in flight accounting is
      // decremented: the resource is really gone when the slot becomes
      // reusable again.
      work = typename Traits::Work{};
      producer->in_flight_items.fetch_sub(1U, std::memory_order_relaxed);
      producer->in_flight_bytes.fetch_sub(bytes, std::memory_order_relaxed);
      in_flight_items.fetch_sub(1U, std::memory_order_relaxed);
      in_flight_bytes.fetch_sub(bytes, std::memory_order_relaxed);
      releasePayload(bytes);
      if (producer->in_flight_items.load(std::memory_order_relaxed) == 0U) {
        // The last resource of this producer is gone: a registration whose
        // four channels already retired becomes reusable now.
        std::lock_guard<std::mutex> guard(mutex);
        reclaimReadyLocked();
      }
      if (threw) {
        bump(producer->channels[channel_index].sink_exceptions);
        bump(channels[channel_index].sink_exceptions);
      } else {
        bump(producer->channels[channel_index].delivered);
        bump(channels[channel_index].delivered);
        updateLastDelivered(producer, sequence);
      }
      worker_busy[channel_index].store(false, std::memory_order_relaxed);
      worker_busy_since_ns[channel_index].store(0, std::memory_order_relaxed);
      cv.notify_all();
    }
    {
      std::lock_guard<std::mutex> guard(mutex);
      live_workers.fetch_sub(1, std::memory_order_release);
    }
    cv.notify_all();
    // The exit ready state is satisfied at the actual thread exit, that is
    // after the thread local cleanup of this worker ran. A user thread
    // local destructor may still be blocked there, so the end of this body
    // is not the end of the thread: an ordinary set_value here would be too
    // early and would let the join owner block inside an unbounded join.
    worker_exit[channel_index].promise.set_value_at_thread_exit();
  }

  // The queue lock is a plain mutex with a plain condition variable. A
  // condition_variable_any takes an internal mutex while the caller still
  // holds the queue lock, so the woken workers form a convoy on it and the
  // try_lock of the producers fails for the whole convoy.
  mutable std::mutex mutex;
  std::condition_variable cv;
  // The join ownership of the worker threads. It is independent of the
  // queue lock: a join never holds the queue lock while it waits.
  std::timed_mutex join_mutex;
  std::condition_variable_any join_cv;
  std::atomic<JoinState> join_state{JoinState::idle};
  std::array<Slot, kSlotCount> slots;
  std::array<Counters, kChannelCount> channels;
  std::array<std::atomic<bool>, kChannelCount> worker_busy{};
  std::array<std::atomic<std::int64_t>, kChannelCount> worker_busy_since_ns{};
  std::atomic<std::uint64_t> queued_items{0};
  std::atomic<std::uint64_t> queued_bytes{0};
  std::atomic<std::uint64_t> in_flight_items{0};
  std::atomic<std::uint64_t> in_flight_bytes{0};
  std::atomic<std::uint64_t> payload_bytes{0};
  std::atomic<std::uint64_t> payload_high_water{0};
  std::atomic<std::uint64_t> registered_producers{0};
  std::atomic<std::uint64_t> retiring_producers{0};
  std::atomic<std::uint64_t> next_registration_id{1};
  // A diagnostic only: the counter is decremented at the end of a worker
  // body, while the thread local cleanup of that worker may still run. It
  // is never used as a proof that a thread exited.
  std::atomic<int> live_workers{0};
  std::atomic<bool> stop_requested{false};
  std::atomic<bool> joined{false};
  // The exit ready shared state of every worker.
  //
  // The promise is prepared before the first worker starts and is satisfied
  // at the actual thread exit, after the thread local cleanup of the
  // worker. It is owned by the generation, so it outlives the thread and
  // can never break the shared state before the worker really exited. The
  // waiter side is shared: every join caller observes the same state and
  // no caller ever resets it.
  struct WorkerExit {
    std::promise<void> promise;
    std::shared_future<void> ready;
  };
  std::array<WorkerExit, kChannelCount> worker_exit;
  // The number of workers that were actually started. Only the started
  // workers have a running body and an exit ready state to wait for.
  std::size_t started_workers = 0;
  // The generation owns its four worker threads. A successful joinFor()
  // leaves them non joinable and the ordinary generation destruction
  // reclaims them.
  std::array<std::thread, kChannelCount> workers;
};

}  // namespace detail

// --- ObservationEndpoint ---------------------------------------------------

ObservationEndpoint::ObservationEndpoint(
  std::weak_ptr<detail::ObservationImpl> impl, std::size_t slot,
  std::uint64_t registration_id,
  std::shared_ptr<detail::ProducerState> producer)
    : impl_(std::move(impl)),
      slot_(slot),
      registration_id_(registration_id),
      producer_(std::move(producer)) {}

ObservationEndpoint::~ObservationEndpoint() {
  // Destruction performs the bounded close only: no drain, no join, no wait
  // for a callback and no sink destructor.
  close();
}

Admission ObservationEndpoint::trace(const TraceEvent & event) noexcept {
  const std::shared_ptr<detail::ObservationImpl> impl = impl_.lock();
  if (!impl) {
    detail::applyOutcome(producer_->channels[detail::kTraceChannelIndex],
                         detail::Outcome::closed);
    return Admission::closed;
  }
  return impl->admitTrace(slot_, registration_id_, producer_, event);
}

Admission ObservationEndpoint::result(
  const CommandResultView & result_view) noexcept {
  const std::shared_ptr<detail::ObservationImpl> impl = impl_.lock();
  if (!impl) {
    detail::applyOutcome(producer_->channels[detail::kResultsChannelIndex],
                         detail::Outcome::closed);
    return Admission::closed;
  }
  return impl->admitResult(slot_, registration_id_, producer_, result_view);
}

Admission ObservationEndpoint::state(const StateView & view) noexcept {
  const std::shared_ptr<detail::ObservationImpl> impl = impl_.lock();
  if (!impl) {
    detail::applyOutcome(producer_->channels[detail::kStateChannelIndex],
                         detail::Outcome::closed);
    return Admission::closed;
  }
  return impl->admitState(slot_, registration_id_, producer_, view);
}

Admission ObservationEndpoint::log(std::string_view level,
                                   std::string_view text) noexcept {
  const std::shared_ptr<detail::ObservationImpl> impl = impl_.lock();
  if (!impl) {
    detail::applyOutcome(producer_->channels[detail::kLogChannelIndex],
                         detail::Outcome::closed);
    return Admission::closed;
  }
  return impl->admitLog(slot_, registration_id_, producer_, level, text);
}

void ObservationEndpoint::recordConstructionFailure(
  ObservationChannel channel, std::uint64_t trace_sequence) noexcept {
  const std::size_t index = detail::channelIndex(channel);
  if (index == detail::kTraceChannelIndex && trace_sequence > 0U) {
    detail::updateLastAssigned(producer_, trace_sequence);
  }
  const std::shared_ptr<detail::ObservationImpl> impl = impl_.lock();
  if (impl) {
    impl->recordConstruction(producer_, index);
    return;
  }
  detail::applyOutcome(producer_->channels[index],
                       detail::Outcome::construction);
}

void ObservationEndpoint::close() noexcept {
  // The close is a metadata operation: the producer flag is flipped
  // atomically and the workers are woken. No queue lock is taken, no queue
  // or bundle is cleared and no user resource is destroyed here.
  //
  // Linearization: an admission that committed before this exchange may
  // still drain; an admission that starts after close() returned observes
  // the flag and is rejected.
  producer_->closed.exchange(true, std::memory_order_acq_rel);
  const std::shared_ptr<detail::ObservationImpl> impl = impl_.lock();
  if (impl) {
    impl->wakeWorkers();
  }
}

ObservationStats ObservationEndpoint::stats() const noexcept {
  ObservationStats snapshot;
  for (std::size_t index = 0; index < kObservationChannelCount; ++index) {
    snapshot.channels[index] = detail::toStats(producer_->channels[index]);
  }
  snapshot.queued_items = producer_->queued_items.load(std::memory_order_relaxed);
  snapshot.queued_bytes = producer_->queued_bytes.load(std::memory_order_relaxed);
  snapshot.in_flight_items =
    producer_->in_flight_items.load(std::memory_order_relaxed);
  snapshot.in_flight_bytes =
    producer_->in_flight_bytes.load(std::memory_order_relaxed);
  snapshot.payload_high_water_bytes =
    producer_->payload_high_water_bytes.load(std::memory_order_relaxed);
  snapshot.last_assigned_trace_sequence =
    producer_->last_assigned_trace_sequence.load(std::memory_order_relaxed);
  snapshot.last_delivered_trace_sequence =
    producer_->last_delivered_trace_sequence.load(std::memory_order_relaxed);
  snapshot.closed = producer_->closed.load(std::memory_order_relaxed);
  return snapshot;
}

// --- ObservationService ----------------------------------------------------

namespace {

// The process manager owns exactly one service generation. It is allocated
// on the heap and never destroyed, so no static destructor can take part in
// the teardown of a live worker pool.
struct ProcessManager {
  std::timed_mutex mutex;
  std::shared_ptr<ObservationService> active;
};

ProcessManager * processManager() {
  static ProcessManager * manager = new ProcessManager();
  return manager;
}

}  // namespace

ObservationService::ObservationService(
  std::shared_ptr<detail::ObservationImpl> impl)
    : impl_(std::move(impl)) {}

ObservationService::~ObservationService() {
  // Destruction requires a previously successful joinFor(). The destructor
  // never hides an unbounded join, never detaches a worker and never forces
  // the process to exit.
}

std::shared_ptr<ObservationService> ObservationService::process() {
  ProcessManager * manager = processManager();
  std::shared_ptr<ObservationService> retired;
  std::shared_ptr<ObservationService> active;
  {
    std::lock_guard<std::timed_mutex> guard(manager->mutex);
    if (manager->active && !manager->active->joined()) {
      // The running or stopping generation is reused: an unjoined pool is
      // never replaced with a fresh one.
      return manager->active;
    }
    retired = std::move(manager->active);
    // The implementation, the service wrapper and every ownership needed for
    // the publication exist before the first thread is started. A thread
    // start failure therefore never publishes a generation.
    std::shared_ptr<detail::ObservationImpl> impl =
      std::make_shared<detail::ObservationImpl>();
    std::shared_ptr<ObservationService> service(
      new ObservationService(impl));
    impl->start();
    active = std::move(service);
    manager->active = active;
  }
  // The previous generation is released outside the manager lock, so a sink
  // destructor never runs under the registry mutex.
  return active;
}

std::shared_ptr<ObservationEndpoint> ObservationService::registerProducer(
  ObservationSinks sinks) {
  // The registration is reserved first, then the endpoint ownership is
  // allocated, and only a successful allocation publishes the registration.
  // A failed allocation rolls the reservation back: no unreachable open slot
  // and no callback bundle survive a failed registration.
  detail::Registration reservation = impl_->reserveProducer(std::move(sinks));
  std::shared_ptr<ObservationEndpoint> endpoint;
  try {
    endpoint = std::shared_ptr<ObservationEndpoint>(new ObservationEndpoint(
      impl_, reservation.slot, reservation.registration_id,
      reservation.producer));
  } catch (...) {
    impl_->rollbackReservation(reservation);
    throw;
  }
  if (!impl_->commitReservation(reservation)) {
    impl_->rollbackReservation(reservation);
    throw std::runtime_error(
      "chaos_runtime: the observation service is stopping");
  }
  return endpoint;
}

void ObservationService::requestStop() noexcept {
  impl_->requestStop();
}

bool ObservationService::joinFor(std::chrono::milliseconds timeout) {
  return impl_->joinFor(timeout);
}

ObservationServiceStats ObservationService::stats() const noexcept {
  return impl_->stats();
}

bool ObservationService::joined() const noexcept {
  return impl_->joined.load(std::memory_order_acquire);
}

}  // namespace chaos::runtime

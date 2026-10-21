// Child-side descriptor mapping for the recorder control channel.
//
// The supervisor passes its Unix socket to the spawned worker on a fixed
// descriptor number. Two details are easy to get wrong and are therefore
// isolated here so they can be unit tested without spawning a process:
//
//  1. posix_spawn does *not* consume the parent's descriptor. The parent must
//     close its copy of the child end after a successful spawn, otherwise the
//     worker never observes EOF and the descriptor leaks.
//  2. A dup2 onto the fixed descriptor must not be a self-dup. dup2(fd, fd)
//     keeps FD_CLOEXEC set, so the channel would be closed by the exec and
//     the worker would start without a control channel.
#pragma once

namespace chaos::data {

// Fixed descriptor number of the control channel inside the spawned worker.
inline constexpr int kControlFd = 3;

// Scratch descriptor used only while remapping a colliding descriptor.
inline constexpr int kControlScratchFd = 4;

// Describes how the child side of the control socket is installed on
// kControlFd inside the spawned process.
struct ChildFdMapping {
  // Descriptor that must be dup2'ed onto kControlFd in the child.
  int source = -1;
  // Scratch descriptor that must be closed in the child afterwards; 0 when
  // no scratch descriptor is required.
  int scratch = 0;
};

// Plans the mapping for a child-side descriptor. When the descriptor already
// equals kControlFd it is parked on kControlScratchFd first so that the
// final dup2 always has distinct operands.
ChildFdMapping planChildFdMapping(int child_fd);

}  // namespace chaos::data

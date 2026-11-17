/**
 * @file src/platform/linux/steam_gate.h
 * @brief Holds each start of Steam's launcher until Ares has decided what it may find.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <sys/types.h>

namespace platf::steam_gate {
  /**
   * A fanotify permission mark on a directory: every exec of `file` in it waits until `decide(pid)` returns, run on a
   * thread of its own, so a slow decision never holds another start. Other files of the directory pass at once.
   * Closing the gate lets everything still held go.
   */
  class gate_t {
  public:
    virtual ~gate_t() = default;
  };

  /// nullptr where it cannot be had: no CAP_SYS_ADMIN, no fanotify permission events, or no such directory.
  std::unique_ptr<gate_t> start(const std::string &dir, const std::string &file, std::function<void(pid_t)> decide);
}  // namespace platf::steam_gate

/**
 * @file src/platform/linux/gamescope_stream.h
 * @brief Frames and input for a Gamescope running `--backend hermes`, with no virtual display.
 */
#pragma once

#include "src/platform/common.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

namespace video {
  struct config_t;
}

namespace platf::gamescope_stream {

  /**
   * One Gamescope's socket. It serves one connection, so capture and input share
   * it: capture connects, and input follows whichever connection is current.
   */
  class session_t {
  public:
    /** Make fd the connection in use; it stays the caller's to close. */
    void attach(int fd);
    /** Forget fd, unless a newer connection has taken over. */
    void detach(int fd);
    /** Send one message. False when nothing is connected or the send failed. */
    bool send(const void *message, std::size_t size);

  private:
    std::mutex mutex;
    int fd {-1};
  };

  /** The session of a socket path, created on first use. */
  std::shared_ptr<session_t> session_for(const std::string &socket);

  /** Drop what is kept for a socket, once its Gamescope is gone. */
  void forget_session(const std::string &socket);

  /** Capture from config.gamescope_stream_socket; null when it cannot connect. */
  std::shared_ptr<display_t> display(mem_type_e memory, const video::config_t &config);

}  // namespace platf::gamescope_stream

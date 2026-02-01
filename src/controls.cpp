module controls;

using namespace std::chrono_literals;

void Controls::write_pause(bool enable) {
  {
    auto l = std::lock_guard{ pause_m };
    model_paused = enable;
  }
  pause_cv.notify_one();
}

void Controls::toggle_pause() {
  {
    auto l = std::lock_guard{ pause_m };
    model_paused ^= true;
  }
  pause_cv.notify_one();
}

void Controls::reset() {
  write_pause(true);
  onReset();
}

void Controls::go_next() {
  next_frame = true;
  write_pause(false);
}

void Controls::handle_next() {
  if (not next_frame) return;
  next_frame = false;
  write_pause(true);
}

void Controls::wait_unpause(std::stop_token stop) {
  auto l = std::unique_lock{ pause_m };
  pause_cv.wait(l, [&paused = model_paused, &stop]{ return stop.stop_requested() or not paused; });
}

void Controls::rate_limit(Controls::clock::time_point last_time) {
  if (not ratelimit_enabled or tickrate == 0 or next_frame) {
    return;
  }
  const auto tickperiod = std::chrono::duration_cast<clock::duration>(1s / tickrate);
  const auto elapsed = clock::now() - last_time;
  const auto missing = tickperiod - std::min(elapsed, tickperiod);
  std::this_thread::sleep_for(missing);
}

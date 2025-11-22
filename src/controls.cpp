module controls;

using namespace std::chrono_literals;

void Controls::toggle_pause() {
  {
    auto l = std::lock_guard{ pause_m };
    model_paused ^= true;
  }
  pause_cv.notify_one();
}

void Controls::go_next() {
  next_frame = true;
  {
    auto l = std::lock_guard{ pause_m };
    model_paused = false;
  }
  pause_cv.notify_one();
}

void Controls::reset() {
  {
    auto l = std::lock_guard{ pause_m };
    model_paused = true;
  }
  pause_cv.notify_one();

  onReset();
}

void Controls::wait_unpause() {
  if (next_frame) {
    auto l = std::lock_guard{ pause_m };
    model_paused = true;
    next_frame = false;
  }
  {
    auto l = std::unique_lock{ pause_m };
    pause_cv.wait(l, [&paused = model_paused]{ return not paused; });
  }
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

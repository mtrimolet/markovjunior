#include <unistd.h>

import std;
import stormkit.log;
import stormkit.core;
import tui.consoleapp;
import gui.windowapp;

namespace stk = stormkit;

constexpr auto run_consoleapp(std::span<const std::string_view> args) noexcept -> int {
  auto app = ConsoleApp{};
  return app(args);
}

constexpr auto run_windowapp(std::span<const std::string_view> args) noexcept -> int {
  auto app = WindowApp{};
  return app(args);
}

auto main(const int argc, const char** argv) -> int {
  // [tmpfix] remove when xmake target properly handles workdir on macos
  // chdir("/Users/mtrimolet/Desktop/mtrimolet/markovjunior");

  stk::setup_signal_handler();
  stk::set_current_thread_name("MainThread");

  auto args = std::vector<std::string_view> {};
  for (auto i = 0u; i < static_cast<std::size_t>(argc); ++i) args.emplace_back(argv[i]);

  auto&& gui = std::ranges::find(args, "--gui") != std::ranges::end(args);
  
  auto _ = stk::log::Logger::create_logger_instance<stk::log::FileLogger>(".");

  if (gui) return run_windowapp(args);
  else     return run_consoleapp(args);
}

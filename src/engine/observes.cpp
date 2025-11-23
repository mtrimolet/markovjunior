module engine.observes;

import geometry;
import engine.match;

namespace stdr = std::ranges;
namespace stdv = std::views;

auto Observe::future(std::vector<Change<char>>& changes, Future& future, const Grid<char>& grid, const Observes& observes) noexcept -> void {
  auto values = std::unordered_set<char>{};

  future = {
    std::from_range,
    mdiota(grid.area()) | stdv::transform([&](auto u) noexcept {
      const auto value = grid[u];
      if (observes.contains(value)) {
        values.insert(value);
        const auto& obs = observes.at(value);
        if (obs.from) changes.emplace_back(u, *obs.from);
        return obs.to;
      }
      else {
        return std::unordered_set{ value };
      }
    }),
    grid.extents
  };

  if (const auto expected = observes | stdv::keys | stdr::to<std::unordered_set>();
                 expected != values
  ) {
    future = {};
  }
}

auto Observe::backward_potentials(Potentials& potentials, const Future& future, std::span<const RewriteRule> rules) noexcept -> void {
  propagate(
    stdv::zip(mdiota(future.area()), future)
      | stdv::transform([&extents = future.extents, &potentials](const auto& p) noexcept {
          auto [u, f] = p;
          return f
            | stdv::transform([&extents, u, &potentials](auto c) noexcept {
                if (not potentials.contains(c)) {
                  potentials.emplace(c, Potential{ extents, std::numeric_limits<double>::quiet_NaN() });
                }
                potentials.at(c)[u] = 0.0;
                return std::tuple{ u, c };
            });
      })
      | stdv::join,
    [&extents = future.extents, &potentials, &rules](auto&& front) noexcept {
      auto [u, c] = front;
      auto p = potentials.at(c)[u];
      return stdv::iota(0u, stdr::size(rules))
        | stdv::transform([&rules, u](auto r) noexcept {
            return Match{ rules, u, r };
        })
        | stdv::filter([&potentials, p](const auto& m) noexcept { return m.backward_match(potentials, p); })
        | stdv::transform([&potentials, p](const auto& m) noexcept { return m.backward_changes(potentials, p + 1); })
        // | stdv::filter(std::bind_back(&Match::backward_match, potentials, p))
        // | stdv::transform(std::bind_back(&Match::backward_changes, potentials, p + 1))
        | stdv::join
        | stdv::transform([&extents, &potentials](auto&& ch) noexcept {
            auto [c, p] = ch.value;
            if (not potentials.contains(c)) {
              potentials.emplace(c, Potential{ extents, std::numeric_limits<double>::quiet_NaN() });
            }
            potentials.at(c)[ch.u] = p;
            return std::tuple{ ch.u, c };
        });
    }
  );
}

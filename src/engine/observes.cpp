module engine.observes;

import log;
import geometry;
import engine.match;

namespace stdr = std::ranges;
namespace stdv = std::views;

auto Observe::goal_reached(const Grid<char>& grid, const Future& future) noexcept -> bool {
  return stdr::all_of(stdv::zip(grid, future), [](const auto& gf) static {
    const auto& [g, f] = gf;
    return stdr::contains(f, g);
  });
}

auto Observe::future(std::vector<Change<char>>& changes, std::optional<Future>& future, const Grid<char>& grid, const Observes& observes) noexcept -> void {
  auto values = charset{};
  auto okeys = observes | stdv::keys | stdr::to<charset>();

  future = {
    std::from_range,
    mdiota(grid.area()) | stdv::transform([&](auto u) noexcept {
      const auto value = grid[u];
      if (stdr::contains(okeys, value)) {
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

  if (okeys != values) {
    future = std::nullopt;
  }
}

auto Observe::backward_potentials(Potentials& potentials, const Future& future, std::span<const RewriteRule> rules) noexcept -> void {
  for (auto c : stdv::keys(potentials)) {
    stdr::fill(potentials.at(c).values, std::numeric_limits<double>::quiet_NaN());
  }
  const auto update = [&extents = future.extents, &potentials](const auto& cu, auto p) noexcept {
    const auto& [c, u] = cu;
    if (not potentials.contains(c)) {
      potentials.emplace(c, Potential{ extents, std::numeric_limits<double>::quiet_NaN() });
    }
    potentials.at(c)[u] = p;
    return std::tuple{ c, u, p };
  };
  propagate(
    stdv::zip(future, mdiota(future.area()))
      | stdv::transform([](const auto& zero) static noexcept {
          const auto& [f, u] = zero;
          return f | stdv::transform([u](auto c) noexcept {
            return std::tuple{ c, u };
          });
      })
      | stdv::join
      | stdv::transform(std::bind_back(update, 0.0)),
    [&potentials, &rules, &update](auto&& front) noexcept {
      auto [c, u, p] = front;
      return stdv::zip(rules, stdv::iota(0u))
        | stdv::transform([p_area = potentials.at(c).area(), c, u](const auto& v) noexcept {
            const auto& [rule, r] = v;
            return rule.get_oshifts(c)
                 | stdv::transform(std::bind_front(std::minus<Area3::Offset>{}, u))
                 | stdv::filter([p_area, r_area = rule.input.area()](auto u) noexcept {
                     auto ru_area = r_area + u;
                     return p_area.meet(ru_area) == ru_area;
                 })
                 | stdv::transform([r](auto u) noexcept {
                     return std::tuple{ u, r };
                 });

        })
        | stdv::join
        | stdv::transform([rules](auto&& ur) noexcept {
            return Match{ rules, std::get<0>(ur), std::get<1>(ur) };
        })
        | stdv::filter(std::bind_back(&Match::backward_match, std::cref(potentials), p))
        | stdv::transform(std::bind_back(&Match::backward_changes, std::cref(potentials)))
        | stdv::join
        | stdv::transform([](auto&& ch) static noexcept {
            return std::tuple{ ch.value, ch.u };
        })
        | stdv::transform(std::bind_back(update, p + 1.0));
    }
  );
}

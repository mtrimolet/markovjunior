module engine.match;

import log;

namespace stdr = std::ranges;
namespace stdv = std::views;

auto Match::scan(
  const Grid<char>& grid,
  std::span<const RewriteRule> rules,
  std::span<const Change<char>> history
) noexcept -> std::vector<Match> {
  if (not stdr::empty(history)) {
    return {
      std::from_range,
      stdv::zip(rules, stdv::iota(0u))
        | stdv::transform([&grid, &history](const auto& v) noexcept {
            const auto& [rule, r] = v;
            return history
              | stdv::transform(&Change<char>::u)
              // TODO group changes according to rule size
              // currently this is highly redundant on adjacent changes (which happens a lot..)
              | stdv::transform([&grid, &rule](auto u) noexcept {
                  return rule.get_ishifts(grid[u])
                    | stdv::transform(std::bind_front(std::minus<Area3::Offset>{}, u))
                    | stdv::filter([g_area = grid.area(), r_area = rule.input.area()](auto u) noexcept {
                        auto ru_area = r_area + u;
                        return g_area.meet(ru_area) == ru_area;
                    });
              })
              | stdv::join
              | stdr::to<std::unordered_set>()
              | stdv::transform([r](auto u) noexcept {
                  return std::tuple{ u, r };
              });
        })
        | stdv::join
        | stdv::transform([rules](auto&& ur) noexcept {
            return Match{ rules, std::get<0>(ur), std::get<1>(ur) };
        })
        | stdv::filter(std::bind_back(&Match::match, std::cref(grid)))
    };
  }

  return {
    std::from_range,
    stdv::zip(rules, stdv::iota(0u))
      | stdv::transform([&grid](const auto& v) noexcept {
          const auto& [rule, r] = v;
          const auto g_area = grid.area();
          return mdiota(g_area)
            | stdv::filter([r_area = rule.output.area(), g_area](auto u) noexcept {
                return glm::all(
                     glm::equal(u, g_area.shiftmax())
                  or glm::equal(u % static_cast<Area3::Offset>(r_area.size), r_area.shiftmax())
                );
            })
            | stdv::transform([&grid, &rule](auto u) noexcept {
                return rule.get_ishifts(grid[u])
                  | stdv::transform(std::bind_front(std::minus<Area3::Offset>{}, u))
                  | stdv::filter([g_area = grid.area(), r_area = rule.input.area()](auto u) noexcept {
                      auto ru_area = r_area + u;
                      return g_area.meet(ru_area) == ru_area;
                  });
            })
            | stdv::join
            | stdv::transform([r](auto u) noexcept {
                return std::tuple{ u, r };
            });
      })
      | stdv::join
      | stdv::transform([rules](auto&& ur) noexcept {
          return Match{ rules, std::get<0>(ur), std::get<1>(ur) };
      })
      | stdv::filter(std::bind_back(&Match::match, std::cref(grid)))
  };
}

auto Match::match(const Grid<char>& grid) const noexcept -> bool {
  // return stdr::mismatch(
  //   rules[r].input, mdiota(area()),
  //   [](const auto& i, char c) static noexcept {
  //     return not i or i->contains(c);
  //   },
  //   {}, [&grid](auto u) { return grid[u]; }
  // )
  //   .in1 == stdr::end(rules[r].input);
  return stdr::all_of(
    stdv::zip(mdiota(area()), rules[r].input),
    [&grid](const auto& input) noexcept {
      auto [u, i] = input;
      return not i
          or i->contains(grid[u]);
    }
  );
}

auto Match::conflict(const Match& other) const noexcept -> bool {
  return stdr::any_of(
    mdiota(area().meet(other.area())),
    [&a = *this, &b = other](auto u) noexcept {
      return a.rules[a.r].output.at(u - a.u)
         and b.rules[b.r].output.at(u - b.u);
    }
  );
}

auto Match::changes(const Grid<char>& grid) const noexcept -> std::vector<Change<char>> {
  return stdv::zip(mdiota(area()), rules[r].output)
    | stdv::filter([&grid](const auto& output) noexcept {
        auto [u, o] = output;
        return  o
           and *o != grid[u];
    })
    | stdv::transform([](auto&& output) static noexcept {
        return Change{std::get<0>(output), *std::get<1>(output)};
    })
    | stdr::to<std::vector>();
}

auto Match::delta(const Grid<char>& grid, const Potentials& potentials) const noexcept -> double {
  return stdr::fold_left(
    stdv::zip(mdiota(area()), rules[r].output)
      | stdv::filter([&grid](auto&& _o) noexcept {
          auto [u, o] = _o;
          return  o
             and *o != grid[u];
      })
      | stdv::transform([&grid, &potentials] (const auto& _o) noexcept {
          auto [u, o] = _o;

          auto new_value = *o;
          auto old_value = grid[u];

          auto new_p = potentials.contains(new_value) ? potentials.at(new_value)[u] : 0.0;
          auto old_p = potentials.contains(old_value) ? potentials.at(old_value)[u] : 0.0;

          if (not is_normal(old_p))
            old_p = -1.0;

          return new_p - old_p;
      }),
    0, std::plus{}
  );
}

auto Match::backward_match(const Potentials& potentials, double p) const noexcept -> bool {
  return stdr::all_of(
    stdv::zip(mdiota(area()), rules[r].output)
      | stdv::filter([](const auto& output) static noexcept {
          return std::get<1>(output) != std::nullopt;
      }),
    [p](auto current) noexcept {
      return is_normal(current)
         and current <= p;
    },
    [&potentials](const auto& output) noexcept {
      auto [u, o] = output;
      return potentials.contains(*o) ? potentials.at(*o)[u]
             : std::numeric_limits<double>::quiet_NaN();
    }
  );
}

auto Match::forward_match(const Potentials& potentials, double p) const noexcept -> bool {
  return stdr::all_of(
    stdv::zip(mdiota(area()), rules[r].input)
      | stdv::filter([](const auto& input) static noexcept {
          return std::get<1>(input) != std::nullopt;
      }),
    [p](auto current) noexcept {
      return is_normal(current)
         and current <= p;
    },
    [&potentials](const auto& input) noexcept {
      auto [u, i] = input;
      auto im = stdr::max(*i, {}, [&potentials, u] (auto i) {
        return potentials.contains(i) ? potentials.at(i)[u]
          : std::numeric_limits<double>::quiet_NaN();
      });
      return potentials.contains(im) ? potentials.at(im)[u]
        : std::numeric_limits<double>::quiet_NaN();
    }
  );
}

auto Match::backward_changes(const Potentials& potentials) const noexcept
-> std::vector<Change<char>> {
  return stdv::zip(mdiota(area()), rules[r].input)
    | stdv::filter([&potentials](const auto& input) noexcept {
        auto [u, i] = input;
        return i and stdr::any_of(*i, [&potentials, u](auto i) noexcept {
          return not potentials.contains(i)
             or not is_normal(potentials.at(i)[u]);
        });
    })
    | stdv::transform([&potentials](auto&& input) noexcept {
        auto [u, i] = input;
        auto im = *i | stdv::filter([&potentials, u] (auto i) noexcept {
          return not potentials.contains(i)
             or not is_normal(potentials.at(i)[u]);
        }) | stdr::to<std::vector>();
        return Change{ u, im[0] };
    })
    | stdr::to<std::vector>();
}

auto Match::forward_changes(const Potentials& potentials, double p) const noexcept
-> std::vector<Change<std::tuple<char, double>>> {
  return stdv::zip(mdiota(area()), rules[r].output)
    | stdv::filter([&potentials](const auto& output) noexcept {
        auto [u, o] = output;
        return o
           and is_normal(potentials.at(*o)[u]);
    })
    | stdv::transform([p](auto&& output) noexcept {
        return Change{ std::get<0>(output), std::tuple{ *std::get<1>(output), p }};
    })
    | stdr::to<std::vector>();
}

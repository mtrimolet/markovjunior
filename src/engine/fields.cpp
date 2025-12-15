module engine.fields;

import stormkit.core;
import geometry;

// namespace stk  = stormkit;
namespace stdr = std::ranges;
namespace stdv = std::views;

auto Field::potential(const Grid<char>& grid, Potential& potential) const noexcept -> void {
  propagate(
    mdiota(potential.area())
      | stdv::filter([this, &grid](auto u) noexcept {
          return zero.contains(grid[u]);
      })
      | stdv::transform([&potential](auto u) noexcept {
          potential[u] = 0.0;
          return std::tuple{ u, 0.0 };
      }),
    [this, &grid, &potential](auto&& front) noexcept {
      static constexpr auto neigh_size = 3ul * Area3::Size{ 1, 1, 1 };
      static constexpr auto neigh_shift = -static_cast<Area3::Offset>(neigh_size) / 2l;
      static constexpr auto neigh = Area3{ neigh_shift, neigh_size };

      auto [u, p] = front;
      auto new_us = (neigh + u).meet(potential.area());
      auto new_p  = inversed ? p - 1.0 : p + 1.0;
      return mdiota(new_us)
        | stdv::filter([this, &grid, &potential](auto n) noexcept {
            return not is_normal(potential[n])
               and substrate.contains(grid[n]);
        })
        | stdv::transform([&potential, new_p](auto n) noexcept {
            potential[n] = new_p;
            return std::tuple{ n, new_p };
        });
    }
  );
}

auto Field::potentials(const Fields& fields, const Grid<char>& grid, Potentials& potentials) noexcept -> void {
  for (auto& [c, f] : fields) {
    if (potentials.contains(c) and not f.recompute) {
      continue;
    }

    if (potentials.contains(c)) {
      stdr::fill(potentials.at(c).values, std::numeric_limits<double>::quiet_NaN());
      // stdr::fill(potentials.at(c), std::numeric_limits<double>::quiet_NaN());
    }
    else {
      potentials.emplace(c, Potential{ grid.extents, std::numeric_limits<double>::quiet_NaN() });
    }

    f.potential(grid, potentials.at(c));

    if (stdr::none_of(potentials.at(c), is_normal)) {
      potentials.erase(potentials.find(c));
      break;
    }
  }
}

auto Field::essential_missing(const Fields& fields, const Potentials& potentials) noexcept -> bool {
  return stdr::any_of(fields, [&potentials = potentials](const auto& p) noexcept {
      const auto& [c, f] = p;
      return f.essential and not potentials.contains(c);
    }
  );
}

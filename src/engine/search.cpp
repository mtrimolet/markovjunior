module engine.search;

import geometry;

import engine.match;

namespace stk  = stormkit;
namespace stdr = std::ranges;
namespace stdv = std::views;

template <>
struct std::hash<Grid<char>::Extents> {
  constexpr auto operator()(Grid<char>::Extents t) const noexcept -> std::size_t {
    auto h = std::hash<Grid<char>::Extents::index_type>{};
    return h(t.extent(0))
         ^ h(t.extent(1))
         ^ h(t.extent(2));
  }
};

template <typename T>
struct std::hash<std::vector<T>> {
  constexpr auto operator()(const std::vector<T>& t) const noexcept -> std::size_t {
    auto s = std::size_t{ 0 };
    auto h = std::hash<char>{};
    for (auto c : t)
      s ^= h(c);
    return s;
  }
};

template <>
struct std::hash<Grid<char>> {
  constexpr auto operator()(const Grid<char>& grid) const noexcept -> std::size_t {
    return std::hash<decltype(grid.extents)>{}(grid.extents)
         ^ std::hash<decltype(grid.values)>{}(grid.values);
  }
};

auto Search::trajectory(
  Trajectory& traj,
  const Future& future,
  const Grid<char>& grid,
  std::span<const RewriteRule> rules,
  bool all, stk::u32 limit, double depthCoefficient
) -> void {
  // traj = {};
  auto candidates = std::vector<Candidate>{};

  Potentials backward, forward;

  Observe::backward_potentials(backward, future, rules);
  Search::forward_potentials(forward, grid, rules);
  
  candidates.emplace_back(
    grid, -1, 0,
    Search::backward_delta(backward, grid),
    Search::forward_delta(forward, future) 
  );

  if (candidates[0].backward < 0.0 or candidates[0].forward < 0.0) {
    // traj = {};
    return;
  }
  if (candidates[0].backward == 0.0) {
    // traj = {};
    return;
  }

  auto visited = stdv::zip(
    candidates | stdv::transform(&Candidate::state),
    stdv::iota(std::size_t{ 0u })
  )
    | stdr::to<std::unordered_map>();

  for (
    auto q = stdv::zip(
      candidates | stdv::transform(std::bind_back(&Candidate::weight, depthCoefficient)),
      stdv::iota(0u)
    )
      | stdr::to<std::priority_queue>([](const auto& a, const auto& b){
          return std::get<0>(a) - std::get<0>(b);
      });
    not stdr::empty(q) and (limit == 0 or stdr::size(candidates) < limit);
    q.pop()
  ) {
    auto [score, parentIndex]  = q.top();
    const auto& parent = candidates[parentIndex];

    for (auto childState : parent.children(rules, all)) {
      if (visited.contains(childState)) {
        auto childIndex = visited.at(childState);

        auto& child = candidates[childIndex];
        if (child.depth <= parent.depth + 1) {
          continue;
        }
        
        child.depth = parent.depth + 1;
        child.parentIndex = parentIndex;

        if (child.backward < 0.0 or child.forward < 0.0) {
          continue;
        }

        q.emplace(child.weight(depthCoefficient), childIndex);
      }
      else {
        auto backward_estimate = Search::backward_delta(backward, childState);
        Search::forward_potentials(forward, childState, rules);
        auto forward_estimate  = Search::forward_delta(forward, future);
        if (backward_estimate < 0.0 or forward_estimate < 0.0) {
          continue;
        }

        auto childIndex = stdr::size(candidates);
        visited.emplace(childState, childIndex);
        candidates.emplace_back(
          childState, parentIndex, parent.depth + 1,
          backward_estimate,
          forward_estimate
        );

        auto& child = candidates[childIndex];

        if (child.forward == 0.0) {
          q = {}; // end the propagation
          break;
        }

        // if (limit == 0 and backward_estimate + forward_estimate <= record) {
        //   record = backward_estimate + forward_estimate;
        // }

        q.emplace(child.weight(depthCoefficient), childIndex);
      }
    }
  }

  if (stdr::empty(candidates)
   or stdr::prev(stdr::cend(candidates))->forward != 0.0
  ) {
    // traj = {};
    return;
  }

  // TODO use child.depth to resize traj
  for (auto candidate = stdr::prev(stdr::cend(candidates));
       candidate->parentIndex >= 0;
       candidate = stdr::next(stdr::cbegin(candidates), candidate->parentIndex)
  ) {
    traj.emplace(stdr::begin(traj), candidate->state);
  }
}

auto Search::forward_potentials(Potentials& potentials, const Grid<char>& grid, std::span<const RewriteRule> rules) noexcept -> void {
  propagate(
    stdv::zip(mdiota(grid.area()), grid)
      | stdv::transform([&potentials](const auto& p) noexcept {
          auto [u, c] = p;
          potentials.at(c)[u] = 0.0;
          return std::tuple{ u, c };
      }),
    [&potentials, &rules](auto&& front) noexcept {
      auto [u, c] = front;
      auto p = potentials.at(c)[u];
      return stdv::iota(0u, stdr::size(rules))
        | stdv::transform([&rules, u](auto r) noexcept {
            return Match{ rules, u, r };
        })
        | stdv::filter(std::bind_back(&Match::forward_match, potentials, p))
        | stdv::transform(std::bind_back(&Match::forward_changes, potentials, p + 1))
        | stdv::join
        | stdv::transform([&potentials](auto&& ch) noexcept {
            auto [c, p] = ch.value;
            potentials.at(c)[ch.u] = p;
            return std::tuple{ ch.u, c };
        });
    }
  );
}

auto Search::backward_delta(const Potentials& potentials, const Grid<char>& grid) noexcept -> double {
  auto vals = stdv::zip(mdiota(grid.area()), grid)
    | stdv::transform([&potentials] (const auto& locus) noexcept {
        auto [u, value] = locus;
        return potentials.contains(value) ? potentials.at(value)[u]
          : 0.0;
    });
  
  return std::reduce(
    // std::execution::par,
    stdr::begin(vals),
    stdr::end(vals)
  );
}

auto Search::forward_delta(const Potentials& potentials, const Future& future) noexcept -> double {
  auto vals = stdv::zip(mdiota(future.area()), future)
    | stdv::transform([&potentials] (const auto& locus) noexcept {
        auto [u, value] = locus;
        if (stdr::empty(value)) {
          return std::numeric_limits<double>::quiet_NaN();
        }

        auto candidates = potentials
          | stdv::transform([u] (const auto& pot){
              const auto& [c, potential] = pot;
              return potential[u];
          })
          | stdv::filter(is_normal);
        return stdr::empty(candidates) ? std::numeric_limits<double>::quiet_NaN()
          : stdr::min(candidates);
    });
  
  return std::reduce(
    // std::execution::par,
    stdr::begin(vals),
    stdr::end(vals)
  );
}

auto Candidate::weight(double depthCoefficient) const -> double {
  return depthCoefficient < 0.0 ? 1000.0 - static_cast<double>(depth)
    : forward + backward + 2.0 * depthCoefficient * static_cast<double>(depth);
}

// TODO maybe avoid duplication of rulenode logic ?
auto Candidate::children(std::span<const RewriteRule> rules, bool all) const -> std::vector<Grid<char>> {
  auto result = std::vector<Grid<char>>{};

  auto matches = Match::scan(state, rules);

  if (all) {
    // all :
    //   non overlaping matches induce a common substate when applied simultaneously
    //   overlaping matches induce a combinatoric of substates when applied concurrently
    //     cartesian product of the overlaping rules grouped by joined overlapping area
    // mock:
    auto common_substate = state;
    stdr::for_each(
      matches
        | stdv::transform(std::bind_back(&Match::changes, common_substate))
        | stdv::join,
      [&common_substate](auto&& c) {
        common_substate[c.u] = c.value;
    });
    // real :
    // fill hitgrid (Grid<u32>)
    // recursively enumerate while decrementing hitgrid :
    //   find u : location of hitgrid with highest nonzero value
    //   if none, we're done with this recursion :
    //       apply current sequence and push result
    result.push_back(common_substate);
    //   for each match m hitting u :
    //     recurse enumeration with :
    //       hitgrid decremented on m.area
    //       matches without those intersecting m
    //       current sequence appended with m
    //   
  }
  else {
    // one :
    //   each match gives an induced state when applied individually
    result.append_range(
      matches
        | stdv::transform([&state = state](auto&& m) {
          auto newstate = state;
          stdr::for_each(
            m.changes(newstate),
            [&newstate](auto&& c) {
              newstate[c.u] = c.value;
          });
          return newstate;
        })
    );
  }

  return result;
}


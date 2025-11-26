module engine.rulenode;

import sort;
import geometry;

import log;

namespace stk  = stormkit;
namespace stdr = std::ranges;
namespace stdv = std::views;

RuleNode::RuleNode(RuleNode::Mode _mode, std::vector<RewriteRule>&& _rules, RewriteRule::Unions&& _unions) noexcept 
: mode{_mode}, rules{std::move(_rules)}, unions{std::move(_unions)}
{}

RuleNode::RuleNode(RuleNode::Mode _mode, std::vector<RewriteRule>&& _rules, RewriteRule::Unions&& _unions, Fields&& _fields, double _temperature) noexcept 
: mode{_mode}, rules{std::move(_rules)}, unions{std::move(_unions)},
  inference{Inference::DISTANCE}, temperature{_temperature}, fields{std::move(_fields)}
{}

RuleNode::RuleNode(RuleNode::Mode _mode, std::vector<RewriteRule>&& _rules, RewriteRule::Unions&& _unions, Observes&& _observes, double _temperature) noexcept 
: mode{_mode}, rules{std::move(_rules)}, unions{std::move(_unions)},
  inference{Inference::OBSERVE}, temperature{_temperature}, observes{std::move(_observes)}
{}

RuleNode::RuleNode(RuleNode::Mode _mode, std::vector<RewriteRule>&& _rules, RewriteRule::Unions&& _unions, Observes&& _observes, stk::cpp::UInt _limit, double _depthCoefficient) noexcept 
: mode{_mode}, rules{std::move(_rules)}, unions{std::move(_unions)},
  inference{Inference::SEARCH}, limit{_limit}, depthCoefficient{_depthCoefficient}, observes{std::move(_observes)}
{}

auto RuleNode::operator()(const TracedGrid<char>& grid, std::vector<Change<char>>& changes) noexcept -> void {
  if (not predict(grid, changes)) return;
  scan(grid);
  infer(grid);
  select();
  apply(grid, changes);
}

template <typename ...T>
struct std::hash<std::tuple<T...>> {
  constexpr auto operator()(std::tuple<T...> t) const noexcept -> std::size_t {
    return std::apply([](T ...args) static noexcept {
      return (std::hash<T>{}(args) ^ ...);
    }, t);
  }
};

auto RuleNode::scan(const TracedGrid<char>& grid) noexcept -> void {
  auto now = stdr::cend(grid.history);
  auto since = prev
    .transform(std::bind_front(stdr::next, stdr::cbegin(grid.history)))
    .value_or(now);

  matches.erase(
    stdr::begin(stdr::remove_if(
      matches, std::not_fn(std::bind_back(&Match::match, std::cref(grid)))
    )),
    stdr::end(matches)
  );

  auto history = stdr::subrange(since, now) | stdr::to<std::vector>();

  matches.append_range(Match::scan(grid, rules, history));

  active = stdr::begin(matches);
}

auto RuleNode::apply(const TracedGrid<char>& grid, std::vector<Change<char>>& changes) -> void {
  if (active != stdr::end(matches))
    prev = stdr::size(grid.history);

  changes.append_range(
    stdr::subrange(active, stdr::cend(matches))
      | stdv::transform(std::bind_back(&Match::changes, std::cref(grid)))
      | stdv::join
  );

  matches.erase(active, stdr::end(matches));
}

auto RuleNode::predict(const Grid<char>& grid, std::vector<Change<char>>& changes) noexcept -> bool {
  switch (inference) {
    case Inference::RANDOM:
      return true;

    case Inference::DISTANCE:
      Field::potentials(fields, grid, potentials);
      if (Field::essential_missing(fields, potentials)) {
        return false;
      }

      return true;

    case Inference::OBSERVE:
      if (not stdr::empty(future)) {
        return true;
      }

      Observe::future(changes, future, grid, observes);
      if (stdr::empty(future)) {
        return false;
      }

      Observe::backward_potentials(potentials, future, rules);

      return true;

    case Inference::SEARCH:
      if (not stdr::empty(future)) {
        return true;
      }

      Observe::future(changes, future, grid, observes);
      if (stdr::empty(future)) {
        return false;
      }

      auto TRIES = limit < 0 ? 1 : 20;
      for (auto k = 0; k < TRIES && stdr::empty(trajectory); k++)
        Search::trajectory(trajectory, future, grid, rules, mode == Mode::ALL, limit, depthCoefficient);

      if (stdr::empty(trajectory))
        ilog("SEARCH RETURNED NULL");

      return true;
  }
}

auto RuleNode::select() noexcept -> void {
  switch (mode) {
    case Mode::ONE:
      if (auto picked = pick(active, stdr::end(matches));
               picked != stdr::end(matches)
      ) {
        active = stdr::prev(stdr::end(matches));
        std::iter_swap(picked, active);
      }
      else {
        active = stdr::end(matches);
      }
      break;

    case Mode::ALL:
      for (auto selection = stdr::end(matches);
                selection != active;
      ) {
        if (auto picked = pick(active, selection);
                 picked != selection
        ) {
          auto conflict = stdr::any_of(
            selection, stdr::end(matches),
            std::bind_back(&Match::conflict, *picked)
          );
          std::iter_swap(
            picked,
            conflict ? active++ : --selection
          );
        }
        else {
          active = selection;
        }
      }
      break;

    case Mode::PRL:
      active = stdr::begin(stdr::partition(
        active, stdr::end(matches),
        std::not_fn([this](const auto& match) noexcept {
          return rules[match.r].draw(rng);
        })
      ));
      break;
  }
}

auto RuleNode::pick(MatchIterator begin, MatchIterator end) noexcept -> MatchIterator {
  auto weights =
    stdr::subrange(begin, end)
      | stdv::transform(&Match::w);

  if (stdr::fold_left(weights, 0.0, std::plus{}) == 0.0) {
    return end;
  }

  auto picker = std::discrete_distribution{ stdr::cbegin(weights), stdr::cend(weights) };

  return stdr::next(begin, picker(rng));
}

auto RuleNode::infer(const Grid<char>& grid) noexcept -> void {
  stdr::for_each(
    active, stdr::end(matches),
    [&potentials = potentials, &grid](auto& m) noexcept {
      m.w = m.delta(grid, potentials);
  });

  active = stdr::begin(stdr::partition(
    active, stdr::end(matches),
    std::not_fn(is_normal),
    &Match::w
  ));

  stdr::for_each(
    active, stdr::end(matches),
    [temperature = temperature > 0.0 ? temperature : 1.0/* ,
    first_w = active->w */]
    (auto& m) noexcept {
      /** Boltzmann/Softmax distribution */
      // m.w = std::exp(-(first_w - m.w) / temperature);
      m.w = std::exp(-m.w / temperature);
    }
  );
}

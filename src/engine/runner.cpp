module engine.runner;

import log;

namespace stdr = std::ranges;

auto RuleRunner::operator()(TracedGrid<char>& grid) noexcept -> std::generator<bool> {
  if (steps > 0 and step >= steps) co_return;

  auto changes = std::vector<Change<char>>{};
  rulenode(grid, changes);
  if (stdr::empty(changes)) co_return;

  stdr::for_each(changes, std::bind_front(&TracedGrid<char>::apply, &grid));
  step++;
  co_yield true;
}

// TODO this is problematically not extensible, but the other options I think of are about inheritance and I would prefer to avoid that
auto reset(NodeRunner& n) noexcept -> void {
  if (auto p = n.target<RuleRunner>(); p != nullptr) {
    p->step = 0;
    p->rulenode.reset();
    return;
  }

  if (auto p = n.target<TreeRunner>(); p != nullptr) {
    stdr::for_each(p->nodes, reset);
    return;
  }

  std::unreachable();
}

auto current(const NodeRunner& n) noexcept -> const RuleNode* {
  if (auto p = n.target<RuleRunner>(); p != nullptr) {
    return &p->rulenode;
  }

  if (auto p = n.target<TreeRunner>(); p != nullptr) {
    if (p->current_node == stdr::end(p->nodes))
      return nullptr;
    return current(*p->current_node);
  }

  std::unreachable();
}

auto TreeRunner::operator()(TracedGrid<char>& grid) noexcept -> std::generator<bool> {
  for (current_node  = stdr::begin(nodes);
       current_node != stdr::end(nodes);
  ) {
    auto found = false;
    for (auto s : (*current_node)(grid)) {
      found = true;
      co_yield s;
    }

    if (not found) current_node++;

    else if (mode == Mode::MARKOV) current_node = stdr::begin(nodes);
  }

  stdr::for_each(nodes, reset);
}

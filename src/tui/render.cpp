module tui.render;

import log;
import geometry;

namespace stk  = stormkit;
namespace stdr = std::ranges;
namespace stdv = std::views;

using namespace ftxui;

namespace render {

Element canvasFromImage(const Image& img) noexcept {
  return canvas(img.dimx(), img.dimy(), std::bind_back(&Canvas::DrawImage, 0, 0, img));
}

// TODO canvas don't get sized properly
Element canvasFromImage(Image&& img) noexcept {
  return canvas(img.dimx(), img.dimy(), std::bind_back(&Canvas::DrawImage, 0, 0, std::move(img)));
}

Decorator window_wrap(std::string title) {
  return [title](Element inner) {
    return window(text(title), inner);
  };
}

Element grid(const ::TracedGrid<char>& g, const Palette& palette) noexcept {
  auto texture = Image{
    static_cast<int>(g.extents.extent(2)) * 2,
    static_cast<int>(g.extents.extent(1))
  };
  stdr::for_each(
    stdv::zip(mdiota(g.area()), g),
    [&](auto u_char) noexcept {
      auto [u, character] = u_char;
      
      auto b = palette.contains(character) ? palette.at(character)
                                           : Color::Default;
      auto& pixel0 = texture.PixelAt(u.x * 2, u.y);
      pixel0.character        = ' ';
      pixel0.background_color = b;
      auto& pixel1 = texture.PixelAt(u.x * 2 + 1, u.y);
      pixel1.character        = ' ';
      pixel1.background_color = b;
    }
  );
  auto w = texture.dimx(), h = texture.dimy();
  return canvasFromImage(std::move(texture))
    | size(WIDTH, EQUAL, w)
    | size(HEIGHT, EQUAL, h);
}

Element rule(const ::RewriteRule& rule, const Palette& palette) noexcept {
  auto input = Image{
    static_cast<int>(rule.input.extents.extent(2)) * 2,
    static_cast<int>(rule.input.extents.extent(1))
  };
  auto output = Image{
    static_cast<int>(rule.output.extents.extent(2)) * 2,
    static_cast<int>(rule.output.extents.extent(1))
  };

  stdr::for_each(
    stdv::zip(mdiota(rule.input.area()), rule.input, rule.output),
    [&input, &output, &palette](auto uio) noexcept {
      auto [u, i, o] = uio;

      auto ib = i and palette.contains(*i->begin()) ? palette.at(*i->begin())
                                                    : Color{ Color::Default };

      auto& ip0 = input.PixelAt(u.x * 2, u.y);
      ip0.character        = not i                         ? '>'
                           : palette.contains(*i->begin()) ? ' '
                                                           : '?';
      ip0.background_color = ib;
      auto& ip1 = input.PixelAt(u.x * 2 + 1, u.y);
      ip1.character        = not i                         ? '<'
                           : palette.contains(*i->begin()) ? ' '
                                                           : '?';
      ip1.background_color = ib;

      auto ob = o and palette.contains(*o) ? palette.at(*o)
                                           : Color{ Color::Default };
      auto& op0 = output.PixelAt(u.x * 2, u.y);
      op0.character        = not o                ? '>'
                           : palette.contains(*o) ? ' '
                                                  : '?';
      op0.background_color = ob;
      auto& op1 = output.PixelAt(u.x * 2 + 1, u.y);
      op1.character        = not o                ? '<'
                           : palette.contains(*o) ? ' '
                                                  : *o;
      op1.background_color = ob;
    }
  );

  auto w = input.dimx(), h = input.dimy();
  return hbox({
    canvasFromImage(std::move(input))
      | size(WIDTH, EQUAL, w)
      | size(HEIGHT, EQUAL, h)
      | border,
    text("→") | vcenter,
    canvasFromImage(std::move(output))
      | size(WIDTH, EQUAL, w)
      | size(HEIGHT, EQUAL, h)
      | border,
  });
}

Element potential_grid(const ::Potential& g) noexcept {
  auto texture = Image{
    static_cast<int>(g.extents.extent(2)) * 2,
    static_cast<int>(g.extents.extent(1))
  };
  auto [min_g, max_g] = stdr::fold_left(
    g, std::tuple{ 0.0, 0.0 },
    [](auto&& a, auto p) static noexcept {
      return std::tuple{
        std::min(std::get<0>(a), p),
        std::max(std::get<1>(a), p)
      };
    }
  );

  auto normalize = [&min_g, &max_g](double t) noexcept {
    // if (not is_normal(t)) return 0.0;

    // go to [-1, 1]
    t /= t > 0.0 ?  max_g
       : t < 0.0 ? -min_g
                 : 1.0;  

    return t;
  };

  stdr::for_each(
    stdv::zip(mdiota(g.area()), g),
    [&](auto u_val) noexcept {
      auto [u, value] = u_val;
      auto normalized = normalize(value);

      auto b = not is_normal(normalized) ? Color{ Color::White }
             : Color::Interpolate(
                 normalized + (normalized < 0.0 ? 1.0 : 0.0),
                 normalized < 0.0 ? Color{ Color::Blue  } : Color{ Color::Black },
                 normalized < 0.0 ? Color{ Color::Black } : Color{ Color::Red   }
             );

      auto& pixel0 = texture.PixelAt(u.x * 2, u.y);
      pixel0.character        = ' ';
      pixel0.background_color = b;

      auto& pixel1 = texture.PixelAt(u.x * 2 + 1, u.y);
      pixel1.character        = ' ';
      pixel1.background_color = b;
    }
  );
  auto w = texture.dimx(), h = texture.dimy();
  return canvasFromImage(std::move(texture))
    | size(WIDTH, EQUAL, w)
    | size(HEIGHT, EQUAL, h);
}

Element potential(char c, const Potential& pot, const Palette& palette) noexcept {
  auto col = palette.contains(c) ? palette.at(c) : Color::Default;
  return window(
    text(std::string{ c }) | ftxui::color(col) | inverted,
    potential_grid(pot)
  );
}

Element ruleRunner(const RuleRunner& node, const Palette& palette) noexcept {
  auto tag = node.rulenode.mode == RuleNode::Mode::ONE ? "one"
           : node.rulenode.mode == RuleNode::Mode::ALL ? "all"
                                                       : "prl";

  auto steps = node.steps != 0 ? std::format("{}", node.steps)
                               : std::string{ "∞" };

  auto elements = Elements{};
  for(
    auto irule = stdr::cbegin(node.rulenode.rules);
    irule != stdr::cend(node.rulenode.rules);
  ) {
    auto next_rule = stdr::find_if(
      irule + 1, stdr::cend(node.rulenode.rules),
      std::not_fn(&RewriteRule::is_copy)
    );
    elements.push_back(hbox({
      rule(*irule, palette),
      text(std::format("x{}", stdr::distance(irule, next_rule))),
    }));
    irule = next_rule;
  }

  return vbox({
    text(std::format("{} ({}/{})", tag, node.step, steps)),
    hbox({ separator(), vbox(elements) })
  });
}

Element treeRunner(const TreeRunner& node, const Palette& palette, bool selected) noexcept {
  auto tag = node.mode == TreeRunner::Mode::SEQUENCE ? "sequence"
                                                     : "markov";

  auto elements = stdv::zip(node.nodes, stdv::iota(stk::ioffset{ 0 }))
    | stdv::transform([&palette, &selected, current_index = node.current_index()](const auto& ni) noexcept {
        const auto& [n, i] = ni;
        return nodeRunner(n, palette, selected and current_index == i);
      })
    | stdr::to<Elements>();

  auto element = vbox({ text(tag), hbox({ separator(), vbox(elements) }) });
  // if (selected) element |= focus;
  return element;
}

Element nodeRunner(const NodeRunner& node, const Palette& palette, bool selected) noexcept {
  Element e;
  if (const auto& t = std::get_if<TreeRunner>(&node); t != nullptr) {
    e = treeRunner(*t, palette, selected);
  }
  else if (const auto& r = std::get_if<RuleRunner>(&node); r != nullptr) {
    e = ruleRunner(*r, palette);
  }
  else {
    e = text("<unknown_node_runner>");
  }
  if (selected) e |= focus;
  return e;
}

Element symbols(std::string_view values, const Palette& palette) noexcept {
  auto texture = Image{ 8 * 2, 1 + static_cast<int>(stdr::size(values)) / 8 };
  stdr::for_each(
    stdv::zip(values, mdiota(Area3{ {}, { 1, texture.dimy(), texture.dimx() / 2 } })),
    [&](auto&& cu) noexcept {
      auto [character, u] = cu;
      auto& pixel0 = texture.PixelAt(u.x * 2, u.y);
      pixel0.character        = character;
      pixel0.background_color = palette.at(character);
      auto& pixel1 = texture.PixelAt(u.x * 2 + 1, u.y);
      pixel1.character        = " ";
      pixel1.background_color = palette.at(character);
    }
  );

  auto w = texture.dimx(), h = texture.dimy();
  return canvasFromImage(std::move(texture))
      | size(WIDTH, EQUAL, w)
      | size(HEIGHT, EQUAL, h);
}

Element model(const ::Model& model, const Palette& palette) noexcept {
  return vbox({
    window(
      text("symbols"),
      symbols(model.symbols, palette)
    ),
    window(
      text(model.halted ? "program (H)" : "program"),
      nodeRunner(model.program, palette)
        | vscroll_indicator | frame
    ),
  });
}

Component ControlsView(Controls& controls) {
  return Container::Vertical({
    Container::Horizontal({
      Button("play/pause", std::bind_front(&Controls::toggle_pause, &controls)),
      Button("reset", std::bind_front(&Controls::reset, &controls)),
      Button("next", std::bind_front(&Controls::go_next, &controls)),
    }),
    Slider<decltype(controls.tickrate)>({
      .value = &controls.tickrate,
      .direction = Direction::Right,
      .on_change = nullptr,
    })
      | Renderer(border),
    Container::Horizontal({
      Renderer([&tickrate = controls.tickrate]{
        return text(std::format("{} tick/s ", tickrate));
      }),
      Checkbox({
        .label = "tickrate",
        .checked = &controls.ratelimit_enabled,
        .transform = nullptr,
      })
        | Renderer(vcenter),
    }),
  });
}

template <typename T>
struct GridScroll {
  T x;
  T y;
};

Component WorldAndPotentials(const TracedGrid<char>& grid, const Model& model, const render::Palette& palette) {
  struct Impl : ComponentBase {
    const Model& model;
    const RuleNode* node = nullptr;

    std::vector<std::string> tabnames = {};
    int tabselect = 0;
    Component tabtoggle;
    Component tabview;
    GridScroll<int> grid_scroll = { 0, 0 };

    Impl(const TracedGrid<char>& grid, const Model& _model, const render::Palette& palette)
    : model{ _model },
      tabnames{ { "World" } },
      tabtoggle{ Toggle(&tabnames, &tabselect) },
      tabview{
        Container::Tab({ Renderer([&grid, &palette]{
          return render::grid(grid, palette);
        }) }, &tabselect)
      }
    {
      Add(Container::Vertical({
        tabtoggle,
        tabview | Renderer([&grid_scroll = grid_scroll](Element e){
          return e
            | focusPosition(grid_scroll.x, grid_scroll.y)
            | vscroll_indicator | hscroll_indicator | frame
            | border | center | flex_grow;
        }),
      }));

      RefreshPotentials();
    }

    void RefreshPotentials() {
      auto r = current(model.program);

      if ((node == nullptr and r == nullptr)
       or (node == r and stdr::equal(
            stdv::keys(r->potentials)
              | stdr::to<std::set>(),
            tabnames | stdv::drop(1)
              | stdv::transform([](const auto& n) { return n[0]; })
              | stdr::to<std::set>()
          ))
      ) {
        return;
      }

      // ilog("refreshing {} -> {}", tabnames | stdv::drop(1), r ? stdv::keys(r->potentials) | stdr::to<std::vector>() : std::vector<char>{ });

      tabnames = { tabnames[0] };
      while (tabview->ChildCount() > 1) {
        tabview->ChildAt(1)->Detach();
      }

      if (r) {
        for (const auto& [sym, p] : r->potentials) {
          tabnames.push_back(std::format("{}", sym));
          tabview->Add(Renderer([&p]{
            return potential_grid(p);
          }));
        }
      }

      tabselect = node != r ? 0
        : std::min<int>(tabselect, stdr::size(tabnames) - 1);
      // ilog("new select {}", tabselect);

      node = r;
    }

    void OnAnimation(animation::Params& params) {
      RefreshPotentials();
      ComponentBase::OnAnimation(params);
    }
  };
  return Make<Impl>(grid, model, palette);
}

Component MainView(const TracedGrid<char>& grid, const Model& model, Controls& controls, const Palette& palette) {
  return Container::Horizontal({
    Container::Vertical({
      Renderer([]{
        return text("<Model Name>")
          | hcenter | border | xflex_grow;
      }),
      Renderer([&model, &palette]{
        return symbols(model.symbols, palette)
          | window_wrap("symbols");
      }),
      Renderer([&model, &palette]{
        return nodeRunner(model.program, palette)
          | vscroll_indicator
          | yframe
          | window_wrap("program");
      }),
      ControlsView(controls)
        | Renderer(window_wrap("controls") | size(HEIGHT, EQUAL, 9)),
    }),
    Renderer([]{ return separator(); }),
    WorldAndPotentials(grid, model, palette)
      | Renderer(flex_grow),
  })
    | Renderer(flex_grow);
}

}

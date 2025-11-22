module parser;

import log;

import symmetry;

namespace stk  = stormkit;
namespace stdr = std::ranges;
namespace stdv = std::views;

using namespace std::literals;

namespace parser {

inline constexpr auto is_tag(std::string_view tag) noexcept -> decltype(auto) {
  return [tag](const pugi::xml_node& c) noexcept {
    return c.name() == tag;
  };
}

auto Model(const pugi::xml_document& xmodel) noexcept -> ::Model {
  const auto& xnode = xmodel.first_child();

  stk::ensures(
    xnode.attribute("values"),
    std::format("missing '{}' attribute in '{}' node [:{}]",
                "values", "[root]", xnode.offset_debug())
  );
  auto symbols = std::string_view{ xnode.attribute("values").as_string() };
  stk::ensures(
    not stdr::empty(symbols),
    std::format("empty '{}' attribute in '{}' node [:{}]",
                "values", "[root]", xnode.offset_debug())
  );
  // ensure no duplicate
  auto origin = xnode.attribute("origin").as_bool(false);

  auto unions = RewriteRule::Unions{};
  unions.emplace(RewriteRule::IGNORED_SYMBOL, symbols | stdr::to<std::set>());
  unions.insert_range(symbols | stdv::transform([](auto c) static noexcept { 
    return std::tuple{ c, std::set{ c } };
  }));

  auto program = NodeRunner(xnode, unions);
  if (const auto p = program.target<TreeRunner>(); p == nullptr)
    program = TreeRunner{ TreeRunner::Mode::MARKOV, { std::move(program) } };

  return ::Model{
    // title,
    std::string{ symbols }, std::move(unions),
    std::move(origin),
    std::move(program)
  };
}

auto Union(const pugi::xml_node& xnode) noexcept -> decltype(auto) {
  stk::ensures(
    xnode.attribute("symbol"),
    std::format("missing '{}' attribute in '{}' node [:{}]",
                "symbol", "union", xnode.offset_debug())
  );
  auto symbol_str = std::string_view{ xnode.attribute("symbol").as_string() };
  stk::ensures(
    not stdr::empty(symbol_str),
    std::format("empty '{}' attribute in '{}' node [:{}]",
                "symbol", "union", xnode.offset_debug())
  );
  stk::ensures(
    stdr::size(symbol_str) == 1,
    std::format("only one character allowed for '{}' attribute of '{}' node [:{}]",
                "symbol", "union", xnode.offset_debug())
  );

  stk::ensures(
    xnode.attribute("values"),
    std::format("missing '{}' attribute in '{}' node [:{}]",
                "values", "union", xnode.offset_debug())
  );
  auto values = std::string_view{ xnode.attribute("values").as_string() };
  stk::ensures(
    not stdr::empty(values),
    std::format("empty '{}' attribute in '{}' node [:{}]",
                "values", "union", xnode.offset_debug())
  );

  return std::tuple{ symbol_str[0], std::move(values) | stdr::to<std::set>() };
}

auto NodeRunner(
  const pugi::xml_node& xnode,
  RewriteRule::Unions unions,
  std::string_view symmetry
) noexcept -> ::NodeRunner {
  symmetry = xnode.attribute("symmetry").as_string(std::data(symmetry));

  unions.insert_range(xnode.children("union") | stdv::transform(Union));

  const auto& tag = xnode.name();
  if (tag == "sequence"s
   or tag == "markov"s
  ) {
    auto mode = tag == "sequence"s ? TreeRunner::Mode::SEQUENCE : TreeRunner::Mode::MARKOV;
    return TreeRunner{
      mode,
      {
        std::from_range,
        xnode.children()
          | stdv::filter(std::not_fn(is_tag("union")))
          | stdv::transform(std::bind_back(NodeRunner, unions, symmetry))
      }
    };
  }
  if (tag == "one"s
   or tag == "prl"s
   or tag == "all"s
  ) {
    return RuleRunner{
      RuleNode(xnode, unions, symmetry),
      xnode.attribute("steps").as_uint(0)
    };
  }

  stk::ensures(false, std::format("unknown tag '{}' [:{}]",
                                  tag, xnode.offset_debug()));
  std::unreachable();
}

auto RuleNode(
  const pugi::xml_node& xnode,
  RewriteRule::Unions unions,
  std::string_view symmetry
) noexcept -> ::RuleNode {
  const auto& tag = xnode.name();
  auto mode =
      tag == "one"s ? ::RuleNode::Mode::ONE
    : tag == "all"s ? ::RuleNode::Mode::ALL
    :                 ::RuleNode::Mode::PRL;

  if (xnode.attribute("search").as_bool(false)) {
    return ::RuleNode{
      mode, Rules(xnode, unions, symmetry),
      std::move(unions),
      Observes(xnode),
      xnode.attribute("limit").as_uint(0),
      xnode.attribute("depthCoefficient").as_double(0.5)
    };
  }

  if (not stdr::empty(xnode.children("observe"))) {
    return ::RuleNode{
      mode, Rules(xnode, unions, symmetry),
      std::move(unions),
      Observes(xnode),
      xnode.attribute("temperature").as_double(0.0)
    };
  }

  if (not stdr::empty(xnode.children("field"))) {
    return ::RuleNode{
      mode, Rules(xnode, unions, symmetry),
      std::move(unions),
      Fields(xnode),
      xnode.attribute("temperature").as_double(0.0)
    };
  }

  return ::RuleNode{
    mode, Rules(xnode, unions, symmetry),
    std::move(unions)
  };
}

auto Rule(
  const pugi::xml_node& xnode,
  const RewriteRule::Unions& unions
) noexcept -> ::RewriteRule {
  stk::ensures(
    xnode.attribute("in"),
    std::format("missing '{}' attribute in '{}' node [:{}]",
                "in", "[rule]", xnode.offset_debug())
  );
  auto input = std::string_view{ xnode.attribute("in").as_string() };
  stk::ensures(
    not stdr::empty(input),
    std::format("empty '{}' attribute in '{}' node [:{}]",
                "in", "[rule]", xnode.offset_debug())
  );

  stk::ensures(
    xnode.attribute("out"),
    std::format("missing '{}' attribute in '{}' node [:{}]",
                "out", "[rule]", xnode.offset_debug())
  );
  auto output = std::string_view{ xnode.attribute("out").as_string() };
  stk::ensures(
    not stdr::empty(output),
    std::format("empty '{}' attribute in '{}' node [:{}]",
                "out", "[rule]", xnode.offset_debug())
  );

  stk::ensures(
    stdr::size(input) == stdr::size(output),
    std::format("attributes '{}' and '{}' of '{}' node must be of same shape [:{}]",
                "in", "out", "[rule]", xnode.offset_debug())
  );

  return RewriteRule::parse(
    unions,
    input, output,
    xnode.attribute("p").as_double(1.0)
  );
}

auto Rules(
  const pugi::xml_node& xnode,
  const RewriteRule::Unions& unions,
  std::string_view symmetry
) noexcept -> std::vector<RewriteRule> {
  auto rules = std::vector<RewriteRule>{};

  rules.append_range(xnode.children("rule") | stdv::transform(std::bind_back(Rule, unions)));
  if (stdr::empty(rules)) {
    rules.append_range(stdv::single(xnode) | stdv::transform(std::bind_back(Rule, unions)));
  }
  
  return {
    std::from_range,
    std::move(rules)
     | stdv::transform(std::bind_back(symmetries<RewriteRule>, symmetry))
     | stdv::join
  };
}

auto Field(const pugi::xml_node& xnode) noexcept -> std::pair<char, ::Field> {
  stk::ensures(
    xnode.attribute("for"),
    std::format("missing '{}' attribute in '{}' node [:{}]",
                "for", "field", xnode.offset_debug())
  );
  auto _for = std::string_view{ xnode.attribute("for").as_string() };
  stk::ensures(
    not stdr::empty(_for),
    std::format("empty '{}' attribute in '{}' node [:{}]",
                "for", "field", xnode.offset_debug())
  );
  stk::ensures(
    stdr::size(_for) == 1,
    std::format("only one character allowed for '{}' attribute of '{}' node [:{}]",
                "for", "field", xnode.offset_debug())
  );

  stk::ensures(
    xnode.attribute("on"),
    std::format("missing '{}' attribute in '{}' node [:{}]",
                "on", "field", xnode.offset_debug())
  );
  auto substrate = std::string_view{ xnode.attribute("on").as_string() };
  stk::ensures(
    not stdr::empty(substrate),
    std::format("empty '{}' attribute in '{}' node [:{}]",
                "on", "field", xnode.offset_debug())
  );

  stk::ensures(
    xnode.attribute("from") or xnode.attribute("to"),
    std::format("missing one of '{}' or '{}' attributes in '{}' node [:{}]",
                "from", "to", "field", xnode.offset_debug())
  );
  stk::ensures(
    not (xnode.attribute("from") and xnode.attribute("to")),
    std::format("only one of '{}' or '{}' attributes allowed in '{}' node [:{}]",
                "from", "to", "field", xnode.offset_debug())
  );

  auto inversed = not xnode.attribute("to");
  auto zero = std::string_view{ xnode.attribute("from").as_string(xnode.attribute("to").as_string()) };
  stk::ensures(
    not stdr::empty(zero),
    std::format("empty '{}' attribute in '{}' node [:{}]",
                inversed ? "from" : "to", "field", xnode.offset_debug())
  );

  return std::pair{
    _for[0],
    ::Field{
      xnode.attribute("recompute").as_bool(false),
      xnode.attribute("essential").as_bool(false),
      inversed,
      { std::from_range, substrate },
      { std::from_range, zero }
    }
  };
}

auto Fields(const pugi::xml_node& xnode) noexcept -> ::Fields {
  return xnode.children("field")
    | stdv::transform(Field)
    | stdr::to<::Fields>();
}

auto Observe(const pugi::xml_node& xnode) noexcept -> std::pair<char, ::Observe> {
  stk::ensures(
    xnode.attribute("value"),
    std::format("missing '{}' attribute in '{}' node [:{}]",
                "value", "observe", xnode.offset_debug())
  );
  auto value = std::string_view{ xnode.attribute("value").as_string() };
  stk::ensures(
    not stdr::empty(value),
    std::format("empty '{}' attribute in '{}' node [:{}]",
                "value", "observe", xnode.offset_debug())
  );
  stk::ensures(
    stdr::size(value) == 1,
    std::format("only one character allowed value '{}' attribute of '{}' node [:{}]",
                "value", "observe", xnode.offset_debug())
  );

  auto from = std::string_view{ xnode.attribute("from").as_string() };
  stk::ensures(
    not xnode.attribute("from") or not stdr::empty(from),
    std::format("empty '{}' attribute in '{}' node [:{}]",
                "from", "observe", xnode.offset_debug())
  );
  stk::ensures(
    not xnode.attribute("from") or stdr::size(from) == 1,
    std::format("only one character allowed for '{}' attribute of '{}' node [:{}]",
                "from", "observe", xnode.offset_debug())
  );

  return std::pair{
    value[0],
    ::Observe{
      not xnode.attribute("from") ? std::nullopt : std::optional{ from[0] },
      { std::from_range, std::string{ xnode.attribute("to").as_string() } }
    }
  };
}

auto Observes(const pugi::xml_node& xnode) noexcept -> ::Observes {
  return { std::from_range, xnode.children("observe") | stdv::transform(Observe) };
}

auto Palette(const pugi::xml_document& xpalette) noexcept -> ColorPalette {
  return {
    std::from_range,
    xpalette.child("colors").children("color")
      | stdv::transform([](const auto& xcolor) static noexcept {
          stk::ensures(
            xcolor.attribute("symbol"),
            std::format("missing '{}' attribute in '{}' node [:{}]",
                        "symbol", "color", xcolor.offset_debug())
          );
          auto symbol_str = std::string_view{ xcolor.attribute("symbol").as_string() };
          stk::ensures(
            not stdr::empty(symbol_str),
            std::format("empty '{}' attribute in '{}' node [:{}]",
                        "symbol", "color", xcolor.offset_debug())
          );
          stk::ensures(
            stdr::size(symbol_str) == 1,
            std::format("only one character allowed for '{}' attribute of '{}' node [:{}]",
                        "symbol", "color", xcolor.offset_debug())
          );
        
          stk::ensures(
            xcolor.attribute("value"),
            std::format("missing '{}' attribute in '{}' node [:{}]",
                        "value", "color", xcolor.offset_debug())
          );
          auto value = std::string_view{ xcolor.attribute("value").as_string() };
          stk::ensures(
            not stdr::empty(value),
            std::format("empty '{}' attribute in '{}' node [:{}]",
                        "value", "color", xcolor.offset_debug())
          );
          stk::ensures(
            stdr::size(value) == 6u,
            std::format("attribute '{}' should be a rgb hex value in '{}' node [:{}]",
                        "value", "color", xcolor.offset_debug())
          );
        
          return std::tuple{ symbol_str[0], Color{
            fromBase<stk::u8>({ stdr::cbegin(value),     stdr::cbegin(value) + 2 }, 16),
            fromBase<stk::u8>({ stdr::cbegin(value) + 2, stdr::cbegin(value) + 4 }, 16),
            fromBase<stk::u8>({ stdr::cbegin(value) + 4, stdr::cend(value)       }, 16),
          }};
      })
    };
}

auto document(std::span<const std::byte> buffer) noexcept -> pugi::xml_document {
  auto xdocument = pugi::xml_document{};
  auto result = xdocument.load_buffer(std::data(buffer), std::size(buffer));
  stk::ensures(
    result,
    std::format("Error while parsing xml (<buffer>:{}) : {}",
                result.offset, result.description())
  );
  return xdocument;
}

auto document(const std::filesystem::path& filepath) noexcept -> pugi::xml_document {
  auto xdocument = pugi::xml_document{};
  auto result = xdocument.load_file(filepath.c_str());
  stk::ensures(result, std::format("Error while parsing xml ({}:{}) : {}", filepath.generic_string(), result.offset, result.description()));
  return xdocument;
}

}

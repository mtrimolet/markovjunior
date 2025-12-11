module parser;

import log;

import symmetry;

namespace stk  = stormkit;
namespace stdr = std::ranges;
namespace stdv = std::views;

using namespace std::literals;

namespace parser {

constexpr auto is_tag(std::string_view tag) noexcept -> decltype(auto) {
  return [tag](const pugi::xml_node& c) noexcept {
    return c.name() == tag;
  };
}

auto get_string(const pugi::xml_node& xnode, auto name) -> std::string_view {
  auto attr = xnode.attribute(name);
  
  stk::ensures(
    attr,
    std::format("missing '{}' attribute in '{}' node [:{}]",
                name, xnode.name(), xnode.offset_debug())
  );

  auto result = std::string_view{ attr.as_string() };

  stk::ensures(
    not stdr::empty(result),
    std::format("empty '{}' attribute in '{}' node [:{}]",
                name, xnode.name(), xnode.offset_debug())
  );

  return result;
}

auto get_char(const pugi::xml_node& xnode, auto name) -> char {
  auto result = get_string(xnode, name);

  stk::ensures(
    stdr::size(result) == 1,
    std::format("only one character allowed for '{}' attribute of '{}' node [:{}]",
                name, xnode.name(), xnode.offset_debug())
  );

  return result[0];
}

auto get_optchar(const pugi::xml_node& xnode, auto name) -> std::optional<char> {
  return xnode.attribute(name) ? std::optional{ get_char(xnode, name) } : std::nullopt;
}

auto get_charset(const pugi::xml_node& xnode, auto name) -> std::unordered_set<char> {
  auto result_str = get_string(xnode, name);
  auto result = result_str | stdr::to<std::unordered_set>();

  stk::ensures(
    stdr::size(result) == stdr::size(result_str),
    std::format("duplicate value in '{}' attribute of '{}' node [:{}]",
                name, xnode.name(), xnode.offset_debug())
  );

  return result;
}

auto Model(const pugi::xml_document& xmodel) noexcept -> ::Model {
  const auto& xnode = xmodel.first_child();

  auto symbols = get_string(xnode, "values");

  auto unions = RewriteRule::Unions{};
  unions.emplace(RewriteRule::IGNORED_SYMBOL, symbols | stdr::to<std::unordered_set>());
  unions.insert_range(symbols | stdv::transform([](auto c) static noexcept { 
    return std::pair{ c, std::unordered_set{ c } };
  }));

  auto program = NodeRunner(xnode, unions);
  if (not std::holds_alternative<TreeRunner>(program)) {
    // program = TreeRunner{ TreeRunner::Mode::MARKOV, { std::move(program) } };
    //                                                 ^ error: no matching function for call to '__construct_at'
    auto nodes = std::vector<::NodeRunner>{};
    nodes.emplace_back(std::move(program));
    program = TreeRunner{ TreeRunner::Mode::MARKOV, std::move(nodes) };
  }

  return ::Model{
    // title,
    std::string{ symbols },
    std::move(unions),
    xnode.attribute("origin").as_bool(false),
    std::move(program)
  };
}

auto Union(const pugi::xml_node& xnode) noexcept -> decltype(auto) {
  auto symbol = get_char(xnode, "symbol");
  auto values = get_charset(xnode, "values");

  return std::tuple{ symbol, std::move(values) };
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
    return { TreeRunner{
      mode,
      {
        std::from_range,
        xnode.children()
          | stdv::filter(std::not_fn(is_tag("union")))
          | stdv::transform(std::bind_back(NodeRunner, unions, symmetry))
      }
    } };
  }
  if (tag == "one"s
   or tag == "prl"s
   or tag == "all"s
  ) {
    return { RuleRunner{
      RuleNode(xnode, unions, symmetry),
      xnode.attribute("steps").as_uint(0)
    } };
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
  auto input  = get_string(xnode, "in");
  auto output = get_string(xnode, "out");

  stk::ensures(
    stdr::size(input) == stdr::size(output),
    std::format("attributes '{}' and '{}' of '{}' node must be of same shape [:{}]",
                "in", "out", xnode.name(), xnode.offset_debug())
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
     | stdv::as_rvalue
  };
}

auto Field(const pugi::xml_node& xnode) noexcept -> std::pair<char, ::Field> {
  auto _for = get_char(xnode, "for");
  auto substrate = get_charset(xnode, "on");

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
  auto zero = inversed ? get_charset(xnode, "from") : get_charset(xnode, "to");

  return std::pair{
    _for,
    ::Field{
      xnode.attribute("recompute").as_bool(false),
      xnode.attribute("essential").as_bool(false),
      inversed, substrate, zero
    }
  };
}

auto Fields(const pugi::xml_node& xnode) noexcept -> ::Fields {
  return xnode.children("field")
    | stdv::transform(Field)
    | stdr::to<::Fields>();
}

auto Observe(const pugi::xml_node& xnode) noexcept -> std::pair<char, ::Observe> {
  auto value = get_char(xnode, "value");
  auto from  = get_optchar(xnode, "from");

  return std::pair{
    value,
    ::Observe{
      from,
      get_charset(xnode, "to")
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
          auto symbol = get_char(xcolor, "symbol");
          auto value  = get_string(xcolor, "value");
        
          stk::ensures(
            stdr::size(value) == 6u,
            std::format("attribute '{}' should be a rgb hex value in '{}' node [:{}]",
                        "value", "color", xcolor.offset_debug())
          );
        
          return std::tuple{ symbol, Color{
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

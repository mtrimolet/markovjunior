module engine.rewriterule;

import stormkit.core;
import log;
import utils;

namespace stk  = stormkit;
namespace stdr = std::ranges;
namespace stdv = std::views;

auto RewriteRule::parse(
  const Unions& unions,
  std::string_view input,
  std::string_view output,
  double p
) noexcept -> RewriteRule {
  return {
    Grid<Input>::parse(input, [&unions](auto raw) noexcept -> Input {
      return raw == IGNORED_SYMBOL ? Input {} : Input { unions.contains(raw) ? unions.at(raw) : std::unordered_set{ raw } };
    }),
    Grid<Output>::parse(output, [](auto raw) noexcept -> Output {
      return raw == IGNORED_SYMBOL ? Output {} : Output { raw };
    }),
    p
  };
}

RewriteRule::RewriteRule(Grid<Input>&& _input, Grid<Output>&& _output, double p, bool _is_copy) noexcept
: input{std::move(_input)},
  output{std::move(_output)},
  draw{p},
  is_copy{_is_copy},
  ishifts{
    std::from_range,
    stdv::zip(input, mdiota(input.area()))
      | stdv::transform([](auto&& p) noexcept {
          auto [i, u] = p;
          // TODO this must change when fixing size of state representation
          return i.value_or(std::unordered_set{ IGNORED_SYMBOL }) 
            | stdv::transform([u](auto c) noexcept {
                return std::tuple{ c, u };
            });
      })
      | stdv::join
  },
  oshifts{
    std::from_range,
    stdv::zip(output, mdiota(output.area()))
      | stdv::transform([](auto&& p) noexcept {
          auto [o, u] = p;
          return std::tuple{ o.value_or(IGNORED_SYMBOL), u };
      })
  }
{}

auto RewriteRule::get_ishifts(char c) const noexcept -> std::vector<Area3::Offset>{
  auto shifts = std::vector<Area3::Offset>{};

  auto ignored_bucket = ishifts.bucket(IGNORED_SYMBOL);
  auto bucket         = ishifts.bucket(c);

  shifts.append_range(
    stdr::subrange(ishifts.cbegin(ignored_bucket), ishifts.cend(ignored_bucket))
      | stdv::transform(stk::monadic::get<1>())
  );
  shifts.append_range(
    stdr::subrange(ishifts.cbegin(bucket), ishifts.cend(bucket))
      | stdv::transform(stk::monadic::get<1>())
  );

  return shifts;
}

auto RewriteRule::get_oshifts(char c) const noexcept -> std::vector<Area3::Offset>{
  auto shifts = std::vector<Area3::Offset>{};

  auto ignored_bucket = oshifts.bucket(IGNORED_SYMBOL);
  auto bucket         = oshifts.bucket(c);

  shifts.append_range(
    stdr::subrange(oshifts.cbegin(ignored_bucket), oshifts.cend(ignored_bucket))
      | stdv::transform(stk::monadic::get<1>())
  );
  shifts.append_range(
    stdr::subrange(oshifts.cbegin(bucket), oshifts.cend(bucket))
      | stdv::transform(stk::monadic::get<1>())
  );

  return shifts;
}

auto RewriteRule::operator==(const RewriteRule& other) const noexcept -> bool {
  return input    == other.input
     and output   == other.output
     and draw.p() == other.draw.p();
}

auto RewriteRule::backward_neighborhood() const noexcept -> Area3 {
  const auto a = output.area();
  const auto shift = Area3::Offset{1, 1, 1} - static_cast<Area3::Offset>(a.size);
  return a + shift;
}

auto RewriteRule::identity() const noexcept -> RewriteRule {
  return {
    { std::from_range, input, input.extents },
    { std::from_range, output, input.extents },
    draw.p(),
    false
  };
}

auto RewriteRule::xreflected() const noexcept -> RewriteRule {
  return {
    input.xreflected(),
    output.xreflected(),
    draw.p(),
    true
  };
}

auto RewriteRule::xyrotated() const noexcept -> RewriteRule {
  return {
    input.xyrotated(),
    output.xyrotated(),
    draw.p(),
    true
  };
}

auto RewriteRule::zyrotated() const noexcept -> RewriteRule {
  return {
    input.zyrotated(),
    output.zyrotated(),
    draw.p(),
    true
  };
}

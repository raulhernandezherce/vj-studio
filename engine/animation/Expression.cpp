#include "animation/Expression.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

namespace vj::animation {

struct Expression::Node {
  std::function<double(const std::map<std::string, double>&)> fn;
};

namespace {

using Vars = std::map<std::string, double>;
using Eval = std::function<double(const Vars&)>;
constexpr int kMaxDepth = 64;

class Parser {
 public:
  explicit Parser(const std::string& s) : s_(s) {}
  Eval parse() {
    Eval e = additive();
    skip();
    if (i_ != s_.size()) fail("símbolo inesperado");
    return e;
  }

 private:
  [[noreturn]] void fail(const std::string& what) { throw std::runtime_error(what + " en la posición " + std::to_string(i_ + 1)); }
  void skip() {
    while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_;
  }
  bool eat(char c) {
    skip();
    if (i_ < s_.size() && s_[i_] == c) {
      ++i_;
      return true;
    }
    return false;
  }
  Eval additive() {
    Eval a = multiplicative();
    for (;;) {
      if (eat('+')) {
        Eval b = multiplicative();
        a = [a, b](const Vars& v) { return a(v) + b(v); };
      } else if (eat('-')) {
        Eval b = multiplicative();
        a = [a, b](const Vars& v) { return a(v) - b(v); };
      } else {
        return a;
      }
    }
  }
  Eval multiplicative() {
    Eval a = unary();
    for (;;) {
      if (eat('*')) {
        Eval b = unary();
        a = [a, b](const Vars& v) { return a(v) * b(v); };
      } else if (eat('/')) {
        Eval b = unary();
        a = [a, b](const Vars& v) {
          const double d = b(v);
          return d == 0 ? 0.0 : a(v) / d;
        };
      } else if (eat('%')) {
        Eval b = unary();
        a = [a, b](const Vars& v) {
          const double d = b(v);
          return d == 0 ? 0.0 : std::fmod(a(v), d);
        };
      } else {
        return a;
      }
    }
  }
  Eval unary() {
    if (eat('-')) {
      Eval a = unary();
      return [a](const Vars& v) { return -a(v); };
    }
    if (eat('+')) return unary();
    return power();
  }
  Eval power() {
    Eval a = atom();
    if (eat('^')) {
      Eval b = unary();  // asociativa por la derecha
      return [a, b](const Vars& v) { return std::pow(a(v), b(v)); };
    }
    return a;
  }
  Eval atom() {
    skip();
    if (++depth_ > kMaxDepth) fail("expresión demasiado anidada");
    struct Leave {
      int& d;
      ~Leave() { --d; }
    } leave{depth_};
    if (eat('(')) {
      Eval e = additive();
      if (!eat(')')) fail("falta )");
      return e;
    }
    if (i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '.')) {
      size_t used = 0;
      const double value = std::stod(s_.substr(i_), &used);
      i_ += used;
      return [value](const Vars&) { return value; };
    }
    if (i_ < s_.size() && (std::isalpha(static_cast<unsigned char>(s_[i_])) || s_[i_] == '_')) {
      const size_t start = i_;
      while (i_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_])) || s_[i_] == '_')) ++i_;
      const std::string name = s_.substr(start, i_ - start);
      if (eat('(')) return call(name);
      if (name == "pi") return [](const Vars&) { return 3.14159265358979323846; };
      if (name == "e") return [](const Vars&) { return 2.71828182845904523536; };
      return [name](const Vars& v) {
        auto it = v.find(name);
        return it == v.end() ? 0.0 : it->second;
      };
    }
    fail("se esperaba un valor");
  }
  Eval call(const std::string& name) {
    std::vector<Eval> args;
    if (!eat(')')) {
      do args.push_back(additive());
      while (eat(','));
      if (!eat(')')) fail("falta )");
    }
    const auto need = [&](size_t n) {
      if (args.size() != n) fail(name + " necesita " + std::to_string(n) + " argumentos");
    };
    const auto one = [&](double (*f)(double)) -> Eval {
      need(1);
      Eval a = args[0];
      return [a, f](const Vars& v) { return f(a(v)); };
    };
    if (name == "sin") return one([](double x) { return std::sin(x); });
    if (name == "cos") return one([](double x) { return std::cos(x); });
    if (name == "tan") return one([](double x) { return std::tan(x); });
    if (name == "abs") return one([](double x) { return std::abs(x); });
    if (name == "floor") return one([](double x) { return std::floor(x); });
    if (name == "ceil") return one([](double x) { return std::ceil(x); });
    if (name == "round") return one([](double x) { return std::round(x); });
    if (name == "fract") return one([](double x) { return x - std::floor(x); });
    if (name == "sqrt") return one([](double x) { return x < 0 ? 0.0 : std::sqrt(x); });
    if (name == "exp") return one([](double x) { return std::exp(x); });
    if (name == "log") return one([](double x) { return x <= 0 ? 0.0 : std::log(x); });
    if (name == "pow") {
      need(2);
      Eval a = args[0], b = args[1];
      return [a, b](const Vars& v) { return std::pow(a(v), b(v)); };
    }
    if (name == "min" || name == "max") {
      need(2);
      Eval a = args[0], b = args[1];
      const bool lo = name == "min";
      return [a, b, lo](const Vars& v) { return lo ? std::min(a(v), b(v)) : std::max(a(v), b(v)); };
    }
    if (name == "step") {
      need(2);
      Eval a = args[0], b = args[1];
      return [a, b](const Vars& v) { return b(v) < a(v) ? 0.0 : 1.0; };
    }
    if (name == "clamp") {
      need(3);
      Eval x = args[0], a = args[1], b = args[2];
      return [x, a, b](const Vars& v) { return std::min(std::max(x(v), a(v)), b(v)); };
    }
    if (name == "mix") {
      need(3);
      Eval a = args[0], b = args[1], t = args[2];
      return [a, b, t](const Vars& v) { return a(v) + (b(v) - a(v)) * t(v); };
    }
    if (name == "smoothstep") {
      need(3);
      Eval a = args[0], b = args[1], x = args[2];
      return [a, b, x](const Vars& v) {
        const double lo = a(v), hi = b(v);
        const double t = hi == lo ? 0.0 : std::clamp((x(v) - lo) / (hi - lo), 0.0, 1.0);
        return t * t * (3 - 2 * t);
      };
    }
    fail("función desconocida: " + name);
  }

  const std::string& s_;
  size_t i_ = 0;
  int depth_ = 0;
};

}  // namespace

Expression::Expression(const std::string& text) {
  auto node = std::make_shared<Node>();
  node->fn = Parser(text).parse();
  root_ = std::move(node);
}

Expression::~Expression() = default;

double Expression::eval(const std::map<std::string, double>& v) const {
  const double r = root_->fn(v);
  return std::isfinite(r) ? r : 0.0;
}

}  // namespace vj::animation

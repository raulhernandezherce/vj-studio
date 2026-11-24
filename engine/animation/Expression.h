#pragma once
#include <map>
#include <memory>
#include <string>

namespace vj::animation {

// Fórmula compilada: + - * / % ^, paréntesis, variables y funciones.
// Funciones: sin cos tan abs floor ceil round fract sqrt exp log pow min max clamp mix step
// smoothstep. Constantes: pi, e. Sin bucles ni estado: siempre termina.
class Expression {
 public:
  // Lanza std::runtime_error con la posición del error.
  explicit Expression(const std::string& text);
  ~Expression();
  double eval(const std::map<std::string, double>& variables) const;

 private:
  struct Node;
  std::shared_ptr<const Node> root_;
};

}  // namespace vj::animation

#pragma once
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <memory>

#include "gfx/IRenderBackend.h"

// Contexto OpenGL 4.1 oculto. Sin GPU, las pruebas se omiten en vez de fallar.
class GlContext : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!glfwInit()) GTEST_SKIP() << "sin GLFW";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window_ = glfwCreateWindow(64, 36, "test", nullptr, nullptr);
    if (!window_) GTEST_SKIP() << "sin contexto OpenGL 4.1";
    glfwMakeContextCurrent(window_);
    backend_ = vj::gfx::makeOpenGL41Backend();
  }
  void TearDown() override {
    backend_.reset();
    if (window_) glfwDestroyWindow(window_);
  }
  GLFWwindow* window_ = nullptr;
  std::unique_ptr<vj::gfx::IRenderBackend> backend_;
};

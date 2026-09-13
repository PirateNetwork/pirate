# Copyright (c) 2026 Pirate Chain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

include_guard(GLOBAL)

# Checks for C++ features required to compile this project.

include(CheckCXXSourceCompiles)

function(check_cxx_features)
  set(CMAKE_REQUIRED_QUIET TRUE)

  message(STATUS "Checking for required C++ features")

  # Basic C++17 syntax: structured bindings, if-init, constexpr-if,
  # <optional>, <variant>, <string_view>. This mirrors the autotools
  # AX_CXX_COMPILE_STDCXX check that sets HAVE_CXX17.
  check_cxx_source_compiles("
    #include <optional>
    #include <string_view>
    #include <tuple>
    #include <variant>

    int main() {
      std::optional<int> o = 42;
      auto [a, b] = std::tuple{1, 2};
      if (int v = a + b; v > 0) { o = v; }
      std::string_view sv = \"pirate\";
      std::variant<int, double> var = 1.0;
      return o.value() + static_cast<int>(sv.size()) + (a * b) + var.index();
    }
  " HAVE_CXX17)

  if(NOT HAVE_CXX17)
    message(FATAL_ERROR
      "Compiler does not support the required C++17 features.\n"
      "Use a newer compiler (see doc/dependencies.md)."
    )
  endif()
  set(HAVE_CXX17 1 PARENT_SCOPE)

  message(STATUS "Checking for required C++ features - done")
endfunction()

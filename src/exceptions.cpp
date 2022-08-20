#include "exceptions.hpp"
#include <cxxabi.h>


std::string
demangle(const std::type_info &type)
{
  const std::string name_mangled(type.name());
  const char *name = abi::__cxa_demangle(name_mangled.c_str(), NULL, NULL, NULL);

  return name ? std::string(name) : name_mangled;
}

invalid_cast::invalid_cast(const std::type_info &from, const std::type_info &to)
    : std::runtime_error("invalid conversion from '" + demangle(from) + "' to '" + demangle(to) +
                         "'"),
      type_from(from), type_to(to)
{}

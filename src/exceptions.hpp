#pragma once
#include <stdexcept>


class invalid_cast : public std::runtime_error
{
public:
  invalid_cast(const std::type_info &from, const std::type_info &to);

  const std::type_info &type_from;
  const std::type_info &type_to;
};

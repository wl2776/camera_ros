#include "types.hpp"
#include <libcamera/controls.h>


#define CASE_CLAMP(T)                                                                              \
  case libcamera::ControlType##T:                                                                  \
    return clamp<ControlTypeMap<libcamera::ControlType##T>::type>(value, min, max);

#define CASE_NONE(T)                                                                               \
  case libcamera::ControlType##T:                                                                  \
    return {};

namespace std
{
CTRectangle
clamp(const CTRectangle &val, const CTRectangle &lo, const CTRectangle &hi)
{
  const int x = std::clamp(val.x, lo.x, hi.x);
  const int y = std::clamp(val.y, lo.y, hi.y);
  unsigned int width = std::clamp(x + val.width, lo.x + lo.width, hi.x + hi.width) - x;
  unsigned int height = std::clamp(y + val.height, lo.y + lo.height, hi.y + hi.height) - y;

  return CTRectangle {x, y, width, height};
}
} // namespace std


template<typename T>
libcamera::ControlValue
clamp_array(const libcamera::ControlValue &value, const libcamera::ControlValue &min,
            const libcamera::ControlValue &max)
{
  const libcamera::Span<const T> v = value.get<libcamera::Span<const T>>();
  const libcamera::Span<const T> a = min.get<libcamera::Span<const T>>();
  const libcamera::Span<const T> b = max.get<libcamera::Span<const T>>();

  std::vector<T> vclamp(v.size());

  for (size_t i = 0; i < v.size(); i++)
    vclamp[i] = std::clamp(v[i], a[i], b[i]);

  return libcamera::ControlValue(libcamera::Span<const T>(vclamp));
}

template<typename T,
         std::enable_if_t<!std::is_same<std::remove_cv_t<T>, CTBool>::value, bool> = true>
libcamera::ControlValue
clamp(const libcamera::ControlValue &value, const libcamera::ControlValue &min,
      const libcamera::ControlValue &max)
{
  return value.isArray() ? clamp_array<T>(value, min, max)
                         : std::clamp(value.get<T>(), min.get<T>(), max.get<T>());
}

template<typename T,
         std::enable_if_t<std::is_same<std::remove_cv_t<T>, CTBool>::value, bool> = true>
const libcamera::ControlValue &
clamp(const libcamera::ControlValue &value, const libcamera::ControlValue & /*min*/,
      const libcamera::ControlValue & /*max*/)
{
  return value;
}

libcamera::ControlValue
clamp(const libcamera::ControlValue &value, const libcamera::ControlValue &min,
      const libcamera::ControlValue &max)
{
  if (min.type() != max.type())
    throw std::runtime_error("minimum (" + std::to_string(min.type()) + ") and maximum (" +
                             std::to_string(max.type()) + ") types mismatch");

  switch (value.type()) {
    CASE_NONE(None)
    CASE_CLAMP(Bool)
    CASE_CLAMP(Byte)
    CASE_CLAMP(Integer32)
    CASE_CLAMP(Integer64)
    CASE_CLAMP(Float)
    CASE_CLAMP(String)
    CASE_CLAMP(Rectangle)
    CASE_CLAMP(Size)
  }

  return {};
}


bool
operator<(const libcamera::Rectangle &lhs, const libcamera::Rectangle &rhs)
{
  // check if lhs rectangle is completely enclosed by rhs rectangle
  return lhs.x > rhs.x && lhs.y > rhs.y && (lhs.x + lhs.width) < (rhs.x + rhs.width) &&
         (lhs.y + lhs.height) < (rhs.y + rhs.height);
}

bool
operator>(const libcamera::Rectangle &lhs, const libcamera::Rectangle &rhs)
{
  // check if lhs rectangle completely enclosed the rhs rectangle
  return lhs.x < rhs.x && lhs.y < rhs.y && (lhs.x + lhs.width) > (rhs.x + rhs.width) &&
         (lhs.y + lhs.height) > (rhs.y + rhs.height);
}

template<typename T>
bool
less(const libcamera::ControlValue &lhs, const libcamera::ControlValue &rhs)
{
  if (lhs.isArray()) {
    const libcamera::Span<const T> va = lhs.get<libcamera::Span<const T>>();
    if (rhs.isArray()) {
      // array-array comparison
      const libcamera::Span<const T> vb = rhs.get<libcamera::Span<const T>>();
      // check if any lhs element is less than its corresponding rhs element
      for (size_t i = 0; i < va.size(); i++)
        if (va[i] < vb[i])
          return true;
      return false;
    }
    else {
      // array-scalar comparison
      const T vb = rhs.get<T>();
      for (size_t i = 0; i < va.size(); i++)
        if (va[i] < vb)
          return true;
      return false;
    }
  }
  else {
    // scalar-scalar comparison
    return lhs.get<T>() < rhs.get<T>();
  }
}

template<typename T>
bool
greater(const libcamera::ControlValue &lhs, const libcamera::ControlValue &rhs)
{
  if (lhs.isArray()) {
    const libcamera::Span<const T> va = lhs.get<libcamera::Span<const T>>();
    if (rhs.isArray()) {
      // array-array comparison
      const libcamera::Span<const T> vb = rhs.get<libcamera::Span<const T>>();
      // check if any lhs element is greater than its corresponding rhs element
      for (size_t i = 0; i < va.size(); i++)
        if (va[i] > vb[i])
          return true;
      return false;
    }
    else {
      // array-scalar comparison
      const T vb = rhs.get<T>();
      for (size_t i = 0; i < va.size(); i++)
        if (va[i] > vb)
          return true;
      return false;
    }
  }
  else {
    // scalar-scalar comparison
    return lhs.get<T>() > rhs.get<T>();
  }
}

#define CASE_LESS(T)                                                                               \
  case libcamera::ControlType##T:                                                                  \
    return less<ControlTypeMap<libcamera::ControlType##T>::type>(lhs, rhs);

#define CASE_GREATER(T)                                                                            \
  case libcamera::ControlType##T:                                                                  \
    return greater<ControlTypeMap<libcamera::ControlType##T>::type>(lhs, rhs);

bool
operator<(const libcamera::ControlValue &lhs, const libcamera::ControlValue &rhs)
{
  assert(lhs.type() == rhs.type() &&
         ((lhs.numElements() == rhs.numElements()) || (rhs.numElements() == 1)));

  switch (lhs.type()) {
    CASE_NONE(None)
    CASE_LESS(Bool)
    CASE_LESS(Byte)
    CASE_LESS(Integer32)
    CASE_LESS(Integer64)
    CASE_LESS(Float)
    CASE_LESS(String)
    CASE_LESS(Rectangle)
    CASE_LESS(Size)
  }

  throw std::runtime_error("unhandled control type " + std::to_string(lhs.type()));
}

bool
operator>(const libcamera::ControlValue &lhs, const libcamera::ControlValue &rhs)
{
  assert(lhs.type() == rhs.type() &&
         ((lhs.numElements() == rhs.numElements()) || (rhs.numElements() == 1)));

  switch (lhs.type()) {
    CASE_NONE(None)
    CASE_GREATER(Bool)
    CASE_GREATER(Byte)
    CASE_GREATER(Integer32)
    CASE_GREATER(Integer64)
    CASE_GREATER(Float)
    CASE_GREATER(String)
    CASE_GREATER(Rectangle)
    CASE_GREATER(Size)
  }

  throw std::runtime_error("should not be reached");
  return {};
}

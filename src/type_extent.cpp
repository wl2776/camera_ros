#include "type_extent.hpp"
#include <libcamera/base/span.h>
#include <libcamera/control_ids.h>
#include <libcamera/controls.h>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "config.hpp"


template<typename T, std::enable_if_t<!libcamera::details::is_span<T>::value, bool> = true>
std::size_t
get_extent(const libcamera::Control<T> &)
{
  return 0;
}

template<typename T, std::enable_if_t<libcamera::details::is_span<T>::value, bool> = true>
std::size_t
get_extent(const libcamera::Control<T> &)
{
  return libcamera::Control<T>::type::extent;
}

#define IF(T)                                                                                      \
  if (id->id() == libcamera::controls::T.id()) {                                                   \
    return get_extent(libcamera::controls::T);                                                     \
  }


std::size_t
get_extent(const libcamera::ControlId *id)
{
  IF(AeEnable)
  IF(AeLocked)
  IF(AeMeteringMode)
  IF(AeConstraintMode)
  IF(AeExposureMode)
  IF(ExposureValue)
  IF(ExposureTime)
  IF(AnalogueGain)
  IF(Brightness)
  IF(Contrast)
  IF(Lux)
  IF(AwbEnable)
  IF(AwbMode)
  IF(AwbLocked)
  IF(ColourGains)
  IF(ColourTemperature)
  IF(Saturation)
  IF(SensorBlackLevels)
  IF(Sharpness)
  IF(FocusFoM)
  IF(ColourCorrectionMatrix)
  IF(ScalerCrop)
  IF(DigitalGain)
  IF(FrameDuration)
  IF(FrameDurationLimits)
  IF(SensorTimestamp)
#if HAVE_AF_MODE
  IF(AfMode)
#endif
#if HAVE_AF_RANGE
  IF(AfRange)
#endif
#if HAVE_AF_SPEED
  IF(AfSpeed)
#endif
#if HAVE_AF_METERING
  IF(AfMetering)
#endif
#if HAVE_AF_WINDOWS
  IF(AfWindows)
#endif
#if HAVE_AF_TRIGGER
  IF(AfTrigger)
#endif
#if HAVE_AF_PAUSE
  IF(AfPause)
#endif
#if HAVE_LENS_POSITION
  IF(LensPosition)
#endif
#if HAVE_AF_STATE
  IF(AfState)
#endif
#if HAVE_AF_PAUSE_STATE
  IF(AfPauseState)
#endif

  throw std::runtime_error("control " + id->name() + " (" + std::to_string(id->id()) +
                           ") not handled");
}

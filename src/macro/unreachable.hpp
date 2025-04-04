#ifndef SUPERGENIUS_UNREACHABLE_HPP
#define SUPERGENIUS_UNREACHABLE_HPP

/**
 * @brief This file declares UNREACHABLE macro. Use it to prevent compiler
 * warnings.
 */

#if defined(__GNUC__)
#define UNREACHABLE __builtin_unreachable();
#elif defined(_MSC_VER)
#define UNREACHABLE __assume(false);
#else
template <unsigned int LINE>
class Unreachable_At_Line {};
#define UNREACHABLE throw Unreachable_At_Line<__LINE__>();  // NOLINT
#endif

#undef GCC_VERSION

#endif

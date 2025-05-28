

#ifndef SUPERGENIUS_rocksdb_UTIL_HPP
#define SUPERGENIUS_rocksdb_UTIL_HPP

#include <rocksdb/status.h>
#include <gsl/span>

#include "outcome/outcome.hpp"
#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "storage/database_error.hpp"

namespace sgns::storage 
{

  template <typename T>
    outcome::result<T> error_as_result(const rocksdb::Status &s)
  {
    if (s.ok())
    {
      return DatabaseError::OK;
    }

    if (s.IsNotFound()) 
    {
      return DatabaseError::NOT_FOUND;
    }

    if (s.IsIOError()) 
    {
      return DatabaseError::IO_ERROR;
    }

    if (s.IsInvalidArgument()) 
    {
      return DatabaseError::INVALID_ARGUMENT;
    }

    if (s.IsCorruption()) 
    {
      return DatabaseError::CORRUPTION;
    }

    if (s.IsNotSupported()) 
    {
      return DatabaseError::NOT_SUPPORTED;
    }

    return DatabaseError::UNKNOWN;
  }

  template <typename T>
  outcome::result<T> error_as_result(const rocksdb::Status &s,
                                            const base::Logger &logger) 
  {
    logger->error(s.ToString());
    return error_as_result<T>(s);
  }

  inline rocksdb::Slice make_slice(const base::Buffer &buf) 
  {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto *ptr = reinterpret_cast<const char *>(buf.data());
    size_t n = buf.size();
    return rocksdb::Slice{ptr, n};
  }

  inline gsl::span<const uint8_t> make_span(const rocksdb::Slice &s) 
  {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto *ptr = reinterpret_cast<const uint8_t *>(s.data());
    return gsl::make_span(ptr, s.size());
  }

  inline base::Buffer make_buffer(const rocksdb::Slice &s) 
  {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto *ptr = reinterpret_cast<const uint8_t *>(s.data());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    return { ptr, ptr + s.size() };
  }

}  // namespace sgns::storage

#endif  // SUPERGENIUS_rocksdb_UTIL_HPP



#include "storage/rocksdb/rocksdb_cursor.hpp"

#include "storage/rocksdb/rocksdb_util.hpp"

namespace sgns::storage 
{

  rocksdb::Cursor::Cursor(std::shared_ptr<Iterator> it)
      : i_(std::move(it)) {}

  outcome::result<void> rocksdb::Cursor::seekToFirst() 
  {
    i_->SeekToFirst();
    return outcome::success();
  }

  outcome::result<void> rocksdb::Cursor::seek(const Buffer &key) 
  {
    i_->Seek(make_slice(key));
    return outcome::success();
  }

  outcome::result<void> rocksdb::Cursor::seekToLast() 
  {
    i_->SeekToLast();
    return outcome::success();
  }

  bool rocksdb::Cursor::isValid() const 
  {
    return i_->Valid();
  }

  outcome::result<void> rocksdb::Cursor::next() 
  {
    i_->Next();
    return outcome::success();
  }

  outcome::result<void> rocksdb::Cursor::prev() 
  {
    i_->Prev();
    return outcome::success();
  }

  outcome::result<Buffer> rocksdb::Cursor::key() const 
  {
    return make_buffer(i_->key());
  }

  outcome::result<Buffer> rocksdb::Cursor::value() const 
  {
    return make_buffer(i_->value());
  }

}  // namespace sgns::storage

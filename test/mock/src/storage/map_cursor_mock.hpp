

#ifndef SUPERGENIUS_TEST_MOCK_CORE_STORAGE_MAP_CURSOR_MOCK
#define SUPERGENIUS_TEST_MOCK_CORE_STORAGE_MAP_CURSOR_MOCK

#include "storage/face/map_cursor.hpp"

#include <gmock/gmock.h>

namespace sgns::storage::face {

  template <typename K, typename V>
  class MapCursorMock: public MapCursor<K, V> {
   public:
    MOCK_METHOD0(seekToFirst, outcome::result<void> ());

    MOCK_METHOD1_T(seek, outcome::result<void> (const K &key));

    MOCK_METHOD0(seekToLast, outcome::result<void> ());

    MOCK_CONST_METHOD0(isValid, bool());

    MOCK_METHOD0(next, outcome::result<void>());
    MOCK_METHOD0(prev, outcome::result<void>());

    MOCK_CONST_METHOD0_T(key, outcome::result<K>());
    MOCK_CONST_METHOD0_T(value, outcome::result<V>());
  };

}


#endif  // SUPERGENIUS_TEST_MOCK_CORE_STORAGE_MAP_CURSOR_MOCK

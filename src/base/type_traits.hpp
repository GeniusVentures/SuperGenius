#ifndef SUPERGENIUS_TYPE_TRAITS
#define SUPERGENIUS_TYPE_TRAITS

namespace sgns {

  // SFINAE-way for detect shared pointer
  template <typename T>
  struct is_shared_ptr : std::false_type {};
  template <typename T>
  struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

  // SFINAE-way for detect unique pointer
  template <typename T>
  struct is_unique_ptr : std::false_type {};
  template <typename T>
  struct is_unique_ptr<std::unique_ptr<T>> : std::true_type {};

  // SFINAE-way for detect unique and smart pointers both
  template <typename T>
  struct is_smart_ptr : std::false_type {};
  template <typename T>
  struct is_smart_ptr<std::unique_ptr<T>> : std::true_type {};
  template <typename T>
  struct is_smart_ptr<std::shared_ptr<T>> : std::true_type {};

}  // namespace sgns
#endif  // SUPERGENIUS_TYPE_TRAITS

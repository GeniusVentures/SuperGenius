#include <crdt/hierarchical_key.hpp>
#include <boost/algorithm/string.hpp>
namespace sgns::crdt
{

  HierarchicalKey::HierarchicalKey(const std::string& s) 
  {
    std::string key = s;
    // Add slash to beginning if missing
    if (key.empty() || key[0] != '/')
    {
      key = "/" + key;
    }

    // Remove trailing slash 
    if (key.size() > 1 && key[key.size() - 1] == '/')
    {
      key.erase(key.begin() + key.size() - 1);
    }

    this->key_ = key;
  }

  HierarchicalKey::HierarchicalKey(const HierarchicalKey& aKey)
  {
    this->key_ = aKey.key_;
  }

  bool HierarchicalKey::operator==(const HierarchicalKey& aKey)
  {
    return this->key_ == aKey.key_;
  }

  bool HierarchicalKey::operator!=(const HierarchicalKey& aKey)
  {
    return !(*this == aKey);
  }

  HierarchicalKey HierarchicalKey::ChildString(const std::string& aString) const
  {
    return HierarchicalKey(this->key_ + "/" + aString);
  }

  bool HierarchicalKey::IsTopLevel() const
  {
    return GetList().size() == 1;
  }

  std::vector<std::string> HierarchicalKey::GetList() const
  {
    std::vector<std::string> listOfNames;
    if (!this->key_.empty())
    {
      boost::split(listOfNames, this->key_, boost::is_any_of("/"));
      listOfNames.erase(listOfNames.begin());
    }
    return listOfNames;
  }
} // namespace sgns::crdt

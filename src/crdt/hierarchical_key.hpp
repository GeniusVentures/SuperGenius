#ifndef SUPERGENIUS_HIERARCHICAL_KEY_HPP
#define SUPERGENIUS_HIERARCHICAL_KEY_HPP

#include <string>
#include <vector>

namespace sgns::crdt
{
  /** @brief A Key represents the unique identifier of an object,
  * inspired by file systems and Google App Engine key model.
  *  Keys are meant to be unique across a system. Keys are hierarchical,
  * incorporating more and more specific namespaces. Thus keys can be deemed
  * 'children' or 'ancestors' of other keys::
  *     Key("/Comedy")
  *     Key("/Comedy/MontyPython")
  * Also, every namespace can be parametrized to embed relevant object
  * information. For example, the Key `name` (most specific namespace) could
  * include the object type::
  *     Key("/Comedy/MontyPython/Actor:JohnCleese")
  *     Key("/Comedy/MontyPython/Sketch:CheeseShop")
  *    Key("/Comedy/MontyPython/Sketch:CheeseShop/Character:Mousebender")
  */
  class HierarchicalKey
  {
  public:

    HierarchicalKey() = default;

    /** Constructs a key from {@param s}.
    */
    HierarchicalKey(const std::string& s);

    /** Copy constructor
    */
    HierarchicalKey(const HierarchicalKey&);

    virtual ~HierarchicalKey() = default;

    bool operator==(const HierarchicalKey&);

    bool operator!=(const HierarchicalKey&);

    HierarchicalKey& operator=(const HierarchicalKey&) = default;

    void SetKey(const std::string& aKey ) { key_ = aKey; };

    std::string GetKey() const { return key_; };

    /** ChildString returns the `child` Key of this Key -- string helper.
    *   NewKey("/Comedy/MontyPython").ChildString("Actor:JohnCleese")
    *   NewKey("/Comedy/MontyPython/Actor:JohnCleese")
    */
    HierarchicalKey ChildString(const std::string& s) const;

    /** List returns the `list` representation of this Key.
    *   NewKey("/Comedy/MontyPython/Actor:JohnCleese").List()
    *   ["Comedy", "MontyPythong", "Actor:JohnCleese"]
    */
    std::vector<std::string> GetList() const;

    bool IsTopLevel() const;

    inline bool operator==(const HierarchicalKey& rhs) const
    {
      return key_ == rhs.key_;
    }

    inline bool operator!=(const HierarchicalKey& rhs) const
    {
      return !operator==(rhs);
    }

  private:

    std::string key_;
  };
} // namespace sgns::crdt

#endif //SUPERGENIUS_HIERARCHICAL_KEY_HPP

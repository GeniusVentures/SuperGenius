#include "crdt/hierarchical_key.hpp"
#include <boost/algorithm/string.hpp>

namespace sgns::crdt
{

    HierarchicalKey::HierarchicalKey( std::string key )
    {
        // Add slash to beginning if missing
        if ( key.empty() || key[0] != '/' )
        {
            key.insert( key.begin(), '/' );
        }

        // Remove trailing slash
        if ( key.size() > 1 && key[key.size() - 1] == '/' )
        {
            key.erase( key.begin() + key.size() - 1 );
        }

        this->key_ = key;
    }

    HierarchicalKey HierarchicalKey::ChildString( std::string_view s ) const
    {
        HierarchicalKey k( *this );

        if ( !s.empty() && s[0] != '/' )
        {
            k.key_.push_back( '/' );
        }
        k.key_.append( s );

        return k;
    }

    bool HierarchicalKey::IsTopLevel() const
    {
        return GetList().size() == 1;
    }

    std::vector<std::string> HierarchicalKey::GetList() const
    {
        std::vector<std::string> listOfNames;
        if ( !this->key_.empty() )
        {
            boost::split( listOfNames, this->key_, boost::is_any_of( "/" ) );
            listOfNames.erase( listOfNames.begin() );
        }
        return listOfNames;
    }
}

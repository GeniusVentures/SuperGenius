/**
 * @file       BufferStorageFactory.hpp
 * @brief      
 * @date       2024-02-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
 #ifndef _BUFFER_STORAGE_HPP_
 #define _BUFFER_STORAGE_HPP_

class BufferStorageFactory
{
    public:
    static std::shared_ptr<sgns::storage::BufferStorage> create(const std::string &type, const std::string &path)
    {
        if (type == "rocksdb")
        {
            auto result = sgns::storage::rocksdb::create(path);
            if (result)
            {
                return result.value();
            }
            else
            {
                throw std::runtime_error("rocksdb not created");
            }
                    
        }
        else
        {

        }
    }
}

 #endif


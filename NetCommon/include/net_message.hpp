#ifndef NET_MESSAGE_HPP
#define NET_MESSAGE_HPP

#include "net_common.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace olc 
{
    namespace net 
    {
        template <typename T>
        struct message_header {
            T id{};
            uint32_t size = 0;
        };

        template <typename T>
        struct message {
            message_header<T> header{};
            std::vector<uint8_t> body;

            uint32_t size() const {
                return static_cast<uint32_t>(body.size());
            }

            // Override for std::cout compability - friendly message desription
            friend std::ostream& operator << (std::ostream& os, const message<T>& msg) {
                os << "ID:" << int(msg.header.id) << " Size:" << msg.header.size;
                return os;
            }

            template <typename DataType>
            friend message<T>& operator << (message<T>& msg, const DataType& data) {
                static_assert(std::is_standard_layout<DataType>::value, "Data is too complex to be pushed into vector");

                // Cast data to byte array so it can be inserted into body vector
                const uint8_t* raw_data = reinterpret_cast<const uint8_t*>(&data);

                msg.body.insert(msg.body.end(), raw_data, raw_data + sizeof(DataType));

                msg.header.size = msg.size();

                return msg;
            }

            template <typename DataType>
            friend message<T>& operator >> (message<T>& msg, DataType& data) {
                static_assert(std::is_standard_layout<DataType>::value, "Data is too complex to be read from message");

                size_t type_size = sizeof(DataType);

                if (msg.body.size() < type_size) {
                    throw std::runtime_error("Trying to read more data than is available in packet");
                }
                
                // Index from where we copy data
                size_t start_index = msg.body.size() - type_size;

                // Copy one item of given data type
                std::memcpy(&data, msg.body.data() + start_index, type_size);


                // Pop last item
                msg.body.resize(start_index);

                // Update size
                msg.header.size = msg.size();

                return msg;
            }
        };
        
        // This is a promis that connection class exists
        template <typename T>
        class connection;

        template <typename T>
        struct owned_message {
            std::shared_ptr<connection<T>> remote = nullptr;
            message<T> msg;

            friend std::ostream& operator << (std::ostream& os, const owned_message<T>& msg) {
                os << msg.msg;
                return os;
            }
        };
    }
}

#endif // NET_MESSAGE_HPP
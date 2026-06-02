#ifndef COMMAND_HPP
#define COMMAND_HPP

#include <iostream>
#include <string>
#include <vector>

#include "MasterServer.hpp"

enum class ArgType {
    Word,
    Integer,
    Remainder
};

class Command {
public:
    Command(std::initializer_list<ArgType> expectedArgs) : m_expectedArgs(expectedArgs) {};
    virtual ~Command() = default;

protected:
    virtual void Execute(MasterServer& server) = 0;

    bool ValidateAndParse() {
        
    };
protected:
    std::vector<std::string> m_parsedArgs;
private:
    std::vector<ArgType> m_expectedArgs;
};

#endif // COMMAND_HPP
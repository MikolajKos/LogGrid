#ifndef COMMAND_HPP
#define COMMAND_HPP

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "MasterServer.hpp"

enum class ArgType {
    Word,
    Integer,
    Remainder
};

struct CommandMetadata {
    std::string name;
    std::string syntax;
    std::string description;
};

class Command {
public:
    Command(CommandMetadata meta, std::initializer_list<ArgType> expectedArgs) : m_meta(std::move(meta)), m_expectedArgs(expectedArgs) {};
    virtual ~Command() = default;

public:
    virtual void Execute(MasterServer& server) = 0;

    bool ValidateAndParse(const std::string& rawArgs) {
        std::stringstream ss(rawArgs);
        std::string currentToken;

        m_parsedArgs.clear();

        for (size_t i = 0; i < m_expectedArgs.size(); ++i) {
            ArgType expectedType = m_expectedArgs[i];
                        
            if (expectedType == ArgType::Word || expectedType == ArgType::Integer) {
                // if arg is expected but stringstream empty, return false
                if (!(ss >> currentToken)) {
                    std::cout << "[ERROR] Missing argument for " << m_meta.name << "\n";
                    std::cout << "Usage: " << m_meta.name << " " << m_meta.syntax << "\n";
                    return false;
                }

                if (expectedType == ArgType::Word) {
                    m_parsedArgs.push_back(currentToken);
                }
                else if (expectedType == ArgType::Integer) {
                    if (IsInteger(currentToken)) {
                        m_parsedArgs.push_back(currentToken);
                    }
                    else {
                        std::cout << "[ERROR] Argument " << i+1 << " should be a number\n";
                        return false;
                    }
                }
            }
            else if (expectedType == ArgType::Remainder) {
                // Read whats left
                std::getline(ss >> std::ws, currentToken);

                if (currentToken.empty()) {
                    std::cout << "[ERROR] Missing argument for " << m_meta.name << "\n";
                    std::cout << "Usage: " << m_meta.name << " " << m_meta.syntax << "\n";
                    return false;
                }
                
                m_parsedArgs.push_back(currentToken);
            }
        }

        return true;
    };

    // Access method that is being used in help command
    const CommandMetadata& GetCommandMetadata() const { return m_meta; }

private:
    bool IsInteger(const std::string& str) {
        std::string::const_iterator it = str.begin();
        while(it != str.end() && std::isdigit(*it)) ++it;
        
        return !str.empty() && it == str.end();
    }

protected:
    std::vector<std::string> m_parsedArgs;
private:
    std::vector<ArgType> m_expectedArgs;
    CommandMetadata m_meta;
};

#endif // COMMAND_HPP
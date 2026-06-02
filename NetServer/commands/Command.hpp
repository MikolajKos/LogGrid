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

    bool ValidateAndParse(const std::string& rawArgs) {
        std::stringstream ss(rawArgs);
        std::string currentToken;

        m_parsedArgs.clear();

        for (size_t i = 0; i < m_expectedArgs.size(); ++i) {
            ArgType expectedType = m_expectedArgs[i];
            
            // if arg is expected but stringstream empty, return false
            if (!(ss >> currentToken)) {
                std::cout << "[ERROR] Missing argument\n";
                return false;
            }

            if (expectedType == ArgType::Word || expectedType == ArgType::Integer) {
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
                std::string word;

                // Read all whats left
                while (ss.rdbuf()->in_avail() != 0) {
                    ss >> word;
                    currentToken += word;
                }

                m_parsedArgs.push_back(currentToken);
            }
        }

        return true;
    };

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
};

#endif // COMMAND_HPP
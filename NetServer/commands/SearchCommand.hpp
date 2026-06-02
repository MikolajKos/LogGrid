#ifndef SEARCH_COMMAND_HPP
#define SEARCH_COMMAND_HPP

#include "Command.hpp"

class SearchCommand : public Command {
public:
    SearchCommand() : Command(
        {
            "SEARCH",
            "search <file_path> <regex_pattern>",
            "Searches for a regex pattern in a specific file"
        },
        { ArgType::Word, ArgType::Word }
    ) {}
    
    void Execute(MasterServer& server) override {
        // Prepare arguments
        std::string filepath = m_parsedArgs[0];
        std::string keyword = m_parsedArgs[1];

        server.StartSearch(filepath, keyword);
    }
};

#endif // SEARCH_COMMAND_HPP
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "WorkerClient.hpp"

WorkerClient::WorkerClient() {}

void WorkerClient::ProcessTask(const LogSystem::TaskPayload& task) {    
    std::ifstream file(task.filename, std::ios::binary);

    if (!file.is_open()) {
        std::cout << "[WORKER] Could not open file: " << task.filename << "\n";
        return;
    }
    
    uint64_t startByte = task.start_line;
    uint64_t endByte = task.end_line;

    file.seekg(startByte);

    // It means we are somewhere in the middle of a text
    if (startByte != 0) {
        // If prev char is '\n' it means that we start perfectly and we dont skip line
        file.seekg(startByte - 1);
        char prevChar;
        file.get(prevChar);

        if (prevChar != '\n') {
            std::string dummy;
            std::getline(file, dummy);
        }
    }

    // Now we can read
    std::string line;

    uint64_t currentPos = static_cast<uint64_t>(file.tellg());

    while (currentPos <= endByte && std::getline(file, line)) {
        // For Windows format (CRLF) delete '\r'
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.find(task.keyword) != std::string::npos) {
            SendMessage(LogSystem::LogSearchMsg::Worker_FoundLine, line);
        }

        // Position update
        std::streampos pos = file.tellg();

        // On EOF getline returns -1, but negative number cannot be store within uint64_t
        if (pos == std::streampos(-1)) {
            break;
        }

        currentPos = static_cast<uint64_t>(pos);
    }

    SendMessage(LogSystem::LogSearchMsg::Worker_TaskDone);
}

void WorkerClient::OnMessage(olc::net::message<LogSystem::LogSearchMsg>& msg) {
    switch (msg.header.id) {
        case LogSystem::LogSearchMsg::Server_SearchTask: {
            std::cout << "[WORKER] New task was received\n";
            
            LogSystem::TaskPayload task;
            msg >> task;
            
            ProcessTask(task);
            break;
        }
        case LogSystem::LogSearchMsg::Server_JobFinished: {
            break;
        }
        default: {
            std::cout << "[WORKER] Undefined message type received\n";
            break;
        }
    }
}

void WorkerClient::SendMessage(const LogSystem::LogSearchMsg msgType, const std::string& line) {
    olc::net::message<LogSystem::LogSearchMsg> msg;

    msg.header.id = msgType;

    if (!line.empty()) {
        // Create payload structure
        LogSystem::ResultPayload payload;

        // Important to leave one free byte for null-terminator '\0'
        strncpy(payload.text, line.c_str(), sizeof(payload.text) - 1);
        
        // sizeof - 1 because 255 is last index not 256! Rookie mistake
        payload.text[sizeof(payload.text) - 1] = '\0';

        msg << payload;
    }

    Send(msg);
}
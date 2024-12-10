#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <future>

// -------------------- Data Structures --------------------
struct LogEntry {
    std::string timestamp;
    std::string level; // INFO, WARN, ERROR
    std::string message;
};

struct ResultSummary {
    std::unordered_map<std::string, int> frequencyByLevel;
    std::unordered_map<std::string, int> errorCounts;
};

// -------------------- Log Reader --------------------
std::vector<LogEntry> parseLogs(const std::string& logSource) {
    std::vector<LogEntry> logs;
    std::stringstream ss;
    
    if (logSource.find(".log") != std::string::npos) {
        std::ifstream file(logSource);
        if (!file.is_open()) {
            std::cerr << "Failed to open log file.\n";
            return logs;
        }
        ss << file.rdbuf();
    } else {
        // Execute `log show` command
        FILE* pipe = popen(("log show " + logSource).c_str(), "r");
        if (!pipe) {
            std::cerr << "Failed to execute log show.\n";
            return logs;
        }
        char buffer[128];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            ss << buffer;
        }
        pclose(pipe);
    }
    
    // Simple parser (Assumes logs are in format: timestamp level message)
    std::string line;
    while (std::getline(ss, line)) {
        std::istringstream iss(line);
        LogEntry entry;
        if (!(iss >> entry.timestamp >> entry.level)) continue;
        std::getline(iss, entry.message);
        logs.push_back(entry);
    }
    return logs;
}

// -------------------- Log Processor --------------------
ResultSummary processLogs(const std::vector<LogEntry>& logs) {
    ResultSummary summary;
    for (const auto& entry : logs) {
        summary.frequencyByLevel[entry.level]++;
        if (entry.level == "ERROR") {
            summary.errorCounts[entry.message]++;
        }
    }
    return summary;
}

// -------------------- Aggregator --------------------
ResultSummary aggregateResults(const std::vector<ResultSummary>& partialResults) {
    ResultSummary aggregated;
    for (const auto& res : partialResults) {
        for (const auto& [level, count] : res.frequencyByLevel) {
            aggregated.frequencyByLevel[level] += count;
        }
        for (const auto& [error, count] : res.errorCounts) {
            aggregated.errorCounts[error] += count;
        }
    }
    return aggregated;
}

// -------------------- Thread Pool --------------------
std::vector<std::vector<LogEntry>> splitLogs(const std::vector<LogEntry>& logs, int numChunks) {
    std::vector<std::vector<LogEntry>> chunks;
    int chunkSize = logs.size() / numChunks;
    for(int i = 0; i < numChunks; ++i) {
        auto start = logs.begin() + i * chunkSize;
        auto end = (i == numChunks -1) ? logs.end() : start + chunkSize;
        chunks.emplace_back(start, end);
    }
    return chunks;
}

// -------------------- CLI Interface --------------------
int main(int argc, char* argv[]) {
    if(argc < 2){
        std::cout << "Usage: " << argv[0] << " <log_source>\n";
        std::cout << "log_source can be a .log file or additional arguments for `log show`\n";
        return 1;
    }
    
    std::string logSource = argv[1];
    std::vector<LogEntry> logs = parseLogs(logSource);
    if(logs.empty()){
        std::cerr << "No logs to process.\n";
        return 1;
    }
    
    int numThreads = std::thread::hardware_concurrency();
    auto chunks = splitLogs(logs, numThreads);
    std::vector<std::future<ResultSummary>> futures;
    for(auto& chunk : chunks){
        futures.emplace_back(std::async(std::launch::async, processLogs, chunk));
    }
    
    std::vector<ResultSummary> partialResults;
    for(auto &fut : futures){
        partialResults.push_back(fut.get());
    }
    
    ResultSummary finalSummary = aggregateResults(partialResults);
    
    // Display Results
    std::cout << "Frequency by Level:\n";
    for(const auto& [level, count] : finalSummary.frequencyByLevel){
        std::cout << level << ": " << count << "\n";
    }
    
    std::cout << "\nFrequent Errors:\n";
    for(const auto& [error, count] : finalSummary.errorCounts){
        std::cout << error << ": " << count << "\n";
    }
    
    return 0;
}
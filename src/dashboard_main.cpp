#include "dashboard_websocket.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <csignal>
#include <sstream>

// --- Configuration ---
const std::string OPPORTUNITY_FILE_PATH = "arbitrage_opportunities.csv";
const int PORT = 8080;

std::atomic<bool> g_shutdown{false};

void signal_handler(int) {
    g_shutdown.store(true);
}

// Helper function to parse a line from the CSV file
std::string parse_csv_line_to_json(const std::string& line) {
    std::stringstream ss(line);
    std::string item;
    std::vector<std::string> tokens;
    while (std::getline(ss, item, ',')) {
        tokens.push_back(item);
    }

    // Ensure we have enough columns to avoid errors
    if (tokens.size() < 9) {
        return ""; // Return empty string if the line is not valid
    }

    // Create a JSON message from the parsed CSV data
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"opportunity\","
        << "\"opportunity\":{"
        << "\"symbol\":\"" << tokens[1] << "\","
        << "\"buy_exchange\":\"" << tokens[2] << "\","
        << "\"sell_exchange\":\"" << tokens[3] << "\","
        << "\"buy_price\":" << tokens[4] << ","
        << "\"sell_price\":" << tokens[5] << ","
        << "\"profit_bps\":" << tokens[6] << ","
        << "\"approved\":" << (tokens[8] == "0" ? "\"true\"" : "\"false\"") << "," // Assuming 0 is APPROVED
        << "\"reason\":\"From live engine\""
        << "}}";
    return oss.str();
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    arbisim::DashboardWebSocketServer server(PORT);
    server.start();

    std::cout << "\n✅ ArbiSim Live Dashboard Bridge is running..." << std::endl;
    std::cout << "   - Watching file: " << OPPORTUNITY_FILE_PATH << std::endl;
    std::cout << "   - Broadcasting on: ws://localhost:" << PORT << std::endl;
    std::cout << "\nFirst, run arbisim.exe. Then open dashboard.html." << std::endl;
    std::cout << "Press Ctrl+C to shut down." << std::endl;

    std::ifstream file_monitor(OPPORTUNITY_FILE_PATH);
    long last_position = 0;

    // Move to the end of the file to start, so we only read new lines
    if (file_monitor.is_open()) {
        file_monitor.seekg(0, std::ios::end);
        last_position = file_monitor.tellg();
    }

    while (!g_shutdown.load()) {
        file_monitor.clear(); // Clear any error flags
        file_monitor.seekg(last_position);

        std::string line;
        if (std::getline(file_monitor, line)) {
            if (!line.empty()) {
                std::cout << "-> Detected new opportunity. Broadcasting to dashboard..." << std::endl;
                std::string json_message = parse_csv_line_to_json(line);
                if (!json_message.empty()) {
                    server.queue_message(json_message);
                }
                last_position = file_monitor.tellg();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Check for file updates every half-second
    }

    std::cout << "\n🛑 Shutting down dashboard bridge..." << std::endl;
    server.stop();
    std::cout << "✅ Bridge stopped." << std::endl;

    return 0;
}
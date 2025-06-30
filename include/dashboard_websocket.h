#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>

namespace arbisim
{

    class MessageBuilder
    {
    public:
        static std::string create_price_update_message(const std::string &exchange, double price);
        static std::string create_placeholder_opportunity();
    };

    class DashboardWebSocketServer
    {
    public:
        DashboardWebSocketServer(int port = 8080);
        ~DashboardWebSocketServer();
        void start();
        void stop();
        // Renamed this method to avoid conflicts with the system's send() function
        void queue_message(const std::string &message);

    private:
        void run_server();
        void handle_client(int client_fd);
        void serve_dashboard_html(int client_fd);
        void broadcast_messages();
        void broadcast_to_clients(const std::string &message);

        std::thread server_thread_;
        std::atomic<bool> running_{false};
        int port_;
        std::queue<std::string> message_queue_;
        std::mutex queue_mutex_;
        std::condition_variable queue_cv_;
        std::vector<int> client_sockets_;
        std::mutex clients_mutex_;
    };

} // namespace arbisim
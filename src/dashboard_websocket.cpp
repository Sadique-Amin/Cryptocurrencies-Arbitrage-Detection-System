#include "dashboard_websocket.h"
#include <iostream>
#include <sstream>
#include <iomanip>

// --- Windows Specific Includes ---
// This is the key fix. These are needed for the networking functions below.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib") // Link against the Winsock library
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace arbisim
{

    // --- MessageBuilder Implementation ---
    // (This part was correct and remains the same)
    std::string MessageBuilder::create_price_update_message(const std::string &exchange, double price)
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "{\"type\":\"price_update\",\"exchange\":\"" << exchange << "\",\"price\":" << price << "}";
        return oss.str();
    }

    std::string MessageBuilder::create_placeholder_opportunity()
    {
        std::ostringstream oss;
        oss << "{"
            << "\"type\":\"opportunity\","
            << "\"opportunity\":{"
            << "\"symbol\":\"BTC/USD\","
            << "\"buy_exchange\":\"Test-Buy\","
            << "\"sell_exchange\":\"Test-Sell\","
            << "\"buy_price\":50000.10,"
            << "\"sell_price\":50050.25,"
            << "\"profit_bps\":10.0,"
            << "\"approved\":true,"
            << "\"reason\":\"Test opportunity\""
            << "}}";
        return oss.str();
    }

    // --- DashboardWebSocketServer Implementation ---

    DashboardWebSocketServer::DashboardWebSocketServer(int port) : port_(port) {}

    DashboardWebSocketServer::~DashboardWebSocketServer()
    {
        stop();
    }

    void DashboardWebSocketServer::start()
    {
        if (running_.exchange(true))
            return;
        server_thread_ = std::thread([this]()
                                     { this->run_server(); });
    }

    void DashboardWebSocketServer::stop()
    {
        if (!running_.exchange(false))
            return;
        queue_cv_.notify_all();
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            for (int socket : client_sockets_)
            {
#ifdef _WIN32
                closesocket(socket);
#else
                close(socket);
#endif
            }
            client_sockets_.clear();
        }
        if (server_thread_.joinable())
        {
            server_thread_.join();
        }
    }

    // This is the public method to add a message to the broadcast queue.
    void DashboardWebSocketServer::queue_message(const std::string &message)
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            message_queue_.push(message);
        }
        queue_cv_.notify_one();
    }

    void DashboardWebSocketServer::run_server()
    {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            std::cerr << "WSAStartup failed" << std::endl;
            return;
        }
#endif

        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0)
        {
            std::cerr << "Socket creation failed" << std::endl;
            return;
        }

        int opt = 1;
#ifdef _WIN32
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#else
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port_);

        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        {
            std::cerr << "Bind failed on port " << port_ << std::endl;
#ifdef _WIN32
            closesocket(server_fd);
#else
            close(server_fd);
#endif
            return;
        }

        if (listen(server_fd, 5) < 0)
        {
            std::cerr << "Listen failed" << std::endl;
#ifdef _WIN32
            closesocket(server_fd);
#else
            close(server_fd);
#endif
            return;
        }

        std::cout << "✅ Dashboard server listening on http://localhost:" << port_ << std::endl;

        std::thread broadcaster_thread([this]()
                                       { this->broadcast_messages(); });

        while (running_.load())
        {
            sockaddr_in client_addr{};
#ifdef _WIN32
            int client_len = sizeof(client_addr);
#else
            socklen_t client_len = sizeof(client_addr);
#endif
            int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
            if (client_fd >= 0)
            {
                handle_client(client_fd);
            }
        }

        broadcaster_thread.join();

#ifdef _WIN32
        closesocket(server_fd);
        WSACleanup();
#else
        close(server_fd);
#endif
    }

    void DashboardWebSocketServer::handle_client(int client_fd)
    {
        char buffer[1024] = {0};
        int bytes_read = recv(client_fd, buffer, 1024, 0);

        if (bytes_read > 0)
        {
            std::string request(buffer, bytes_read);
            if (request.find("Upgrade: websocket") != std::string::npos)
            {
                std::string response =
                    "HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
                    "\r\n";
                ::send(client_fd, response.c_str(), response.length(), 0);
                {
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    client_sockets_.push_back(client_fd);
                }
                std::cout << "📱 Dashboard client connected" << std::endl;
                return;
            }
            else
            {
                serve_dashboard_html(client_fd);
            }
        }
#ifdef _WIN32
        closesocket(client_fd);
#else
        close(client_fd);
#endif
    }

    void DashboardWebSocketServer::serve_dashboard_html(int client_fd)
    {
        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "\r\n"
            "<!DOCTYPE html><html><body><h1>ArbiSim Server</h1>"
            "<p>Please open dashboard.html in your browser.</p>"
            "</body></html>";
        ::send(client_fd, response.c_str(), response.length(), 0);
    }

    void DashboardWebSocketServer::broadcast_messages()
    {
        while (running_.load())
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]
                           { return !message_queue_.empty() || !running_.load(); });
            while (!message_queue_.empty())
            {
                std::string message = message_queue_.front();
                message_queue_.pop();
                lock.unlock();
                broadcast_to_clients(message);
                lock.lock();
            }
        }
    }

    void DashboardWebSocketServer::broadcast_to_clients(const std::string &message)
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        std::string frame;
        size_t len = message.length();
        frame += (char)0x81;
        if (len < 126)
        {
            frame += (char)len;
        }
        else
        {
            frame += (char)126;
            frame += (char)((len >> 8) & 0xFF);
            frame += (char)(len & 0xFF);
        }
        frame += message;

        auto it = client_sockets_.begin();
        while (it != client_sockets_.end())
        {
            if (::send(*it, frame.c_str(), frame.length(), 0) <= 0)
            {
#ifdef _WIN32
                closesocket(*it);
#else
                close(*it);
#endif
                it = client_sockets_.erase(it);
                std::cout << "📱 Dashboard client disconnected" << std::endl;
            }
            else
            {
                ++it;
            }
        }
    }

} // namespace arbisim
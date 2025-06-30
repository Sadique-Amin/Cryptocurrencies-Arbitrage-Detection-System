#pragma once
#include <thread>
#include <functional>
#include <atomic>
#include <chrono>
#include <random>
#include <iostream>
#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include "arbisim_core.h"

namespace arbisim
{

    // Simple key-value parser for basic data extraction (no JSON needed)
    class SimpleDataParser
    {
    public:
        std::map<std::string, std::string> data;

        void parse_key_value_pairs(const std::string &input)
        {
            // Parse simple formats like: key1=value1,key2=value2
            // or quoted strings: "key1":"value1","key2":"value2"

            std::istringstream stream(input);
            std::string token;

            while (std::getline(stream, token, ','))
            {
                size_t eq_pos = token.find('=');
                size_t colon_pos = token.find(':');

                size_t sep_pos = (eq_pos != std::string::npos) ? eq_pos : colon_pos;

                if (sep_pos != std::string::npos)
                {
                    std::string key = token.substr(0, sep_pos);
                    std::string value = token.substr(sep_pos + 1);

                    // Remove quotes and whitespace
                    key.erase(std::remove(key.begin(), key.end(), '\"'), key.end());
                    key.erase(std::remove(key.begin(), key.end(), ' '), key.end());
                    value.erase(std::remove(value.begin(), value.end(), '\"'), value.end());
                    value.erase(std::remove(value.begin(), value.end(), ' '), value.end());

                    data[key] = value;
                }
            }
        }

        bool has(const std::string &key) const
        {
            return data.find(key) != data.end();
        }

        std::string get(const std::string &key) const
        {
            auto it = data.find(key);
            return (it != data.end()) ? it->second : "";
        }

        double get_double(const std::string &key) const
        {
            auto it = data.find(key);
            if (it != data.end())
            {
                try
                {
                    return std::stod(it->second);
                }
                catch (...)
                {
                    return 0.0;
                }
            }
            return 0.0;
        }
    };

    // Base class for all exchange feeds
    class ExchangeFeedBase
    {
    protected:
        std::thread worker_thread_;
        std::atomic<bool> running_{false};
        std::function<void(const MarketUpdate &)> update_callback_;
        std::string symbol_ = "BTCUSDT";
        std::string exchange_name_;

    public:
        explicit ExchangeFeedBase(const std::string &exchange_name)
            : exchange_name_(exchange_name) {}

        virtual ~ExchangeFeedBase() { stop(); }

        void set_symbol(const std::string &symbol)
        {
            symbol_ = symbol;
            std::transform(symbol_.begin(), symbol_.end(), symbol_.begin(), ::toupper);
        }

        void set_update_callback(std::function<void(const MarketUpdate &)> callback)
        {
            update_callback_ = callback;
        }

        virtual void start() = 0;
        virtual void stop()
        {
            if (!running_.exchange(false))
                return;
            if (worker_thread_.joinable())
            {
                worker_thread_.join();
            }
        }

        const std::string &exchange_name() const { return exchange_name_; }
    };

    // Binance feed simulator (realistic behavior, no real WebSocket needed)
    class BinanceFeed : public ExchangeFeedBase
    {
    private:
        double base_price_ = 50000.0;
        double volatility_ = 0.001; // 0.1% volatility

    public:
        BinanceFeed() : ExchangeFeedBase("binance") {}

        void start() override
        {
            if (running_.exchange(true))
                return;

            worker_thread_ = std::thread([this]()
                                         {
            std::random_device rd;
            std::mt19937 gen(rd());
            
            // Binance typically has tight spreads and fast updates
            std::normal_distribution<> price_dist(base_price_, base_price_ * volatility_);
            std::normal_distribution<> spread_dist(0.3, 0.1);  // ~$0.30 spread
            std::uniform_int_distribution<> update_delay(35, 45); // 35-45ms updates
            
            while (running_.load()) {
                double mid_price = price_dist(gen);
                double half_spread = std::abs(spread_dist(gen)) / 2.0;
                
                double bid = mid_price - half_spread;
                double ask = mid_price + half_spread;
                
                if (update_callback_) {
                    MarketUpdate bid_update(MarketUpdate::BID_UPDATE, symbol_, exchange_name_, bid, 150.0);
                    update_callback_(bid_update);
                    
                    MarketUpdate ask_update(MarketUpdate::ASK_UPDATE, symbol_, exchange_name_, ask, 150.0);
                    update_callback_(ask_update);
                }
                
                int delay = update_delay(gen);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            } });
        }
    };

    // Coinbase Pro feed simulator
    class CoinbaseFeed : public ExchangeFeedBase
    {
    private:
        double base_price_ = 50000.0;
        double volatility_ = 0.0012; // Slightly higher volatility

    public:
        CoinbaseFeed() : ExchangeFeedBase("coinbase") {}

        void start() override
        {
            if (running_.exchange(true))
                return;

            worker_thread_ = std::thread([this]()
                                         {
            std::random_device rd;
            std::mt19937 gen(rd());
            
            // Coinbase typically has wider spreads than Binance
            std::normal_distribution<> price_dist(base_price_, base_price_ * volatility_);
            std::normal_distribution<> spread_dist(0.8, 0.2);  // ~$0.80 spread
            std::uniform_int_distribution<> update_delay(50, 70); // 50-70ms updates
            
            while (running_.load()) {
                double mid_price = price_dist(gen);
                double half_spread = std::abs(spread_dist(gen)) / 2.0;
                
                double bid = mid_price - half_spread;
                double ask = mid_price + half_spread;
                
                if (update_callback_) {
                    MarketUpdate bid_update(MarketUpdate::BID_UPDATE, symbol_, exchange_name_, bid, 120.0);
                    update_callback_(bid_update);
                    
                    MarketUpdate ask_update(MarketUpdate::ASK_UPDATE, symbol_, exchange_name_, ask, 120.0);
                    update_callback_(ask_update);
                }
                
                int delay = update_delay(gen);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            } });
        }
    };

    // Kraken feed simulator
    class KrakenFeed : public ExchangeFeedBase
    {
    private:
        double base_price_ = 50000.0;
        double volatility_ = 0.0015; // Higher volatility, sometimes laggy

    public:
        KrakenFeed() : ExchangeFeedBase("kraken") {}

        void start() override
        {
            if (running_.exchange(true))
                return;

            worker_thread_ = std::thread([this]()
                                         {
            std::random_device rd;
            std::mt19937 gen(rd());
            
            // Kraken often has wider spreads and more volatile pricing
            std::normal_distribution<> price_dist(base_price_, base_price_ * volatility_);
            std::normal_distribution<> spread_dist(1.2, 0.4);  // ~$1.20 spread
            std::uniform_int_distribution<> update_delay(70, 150); // Variable delays (laggy)
            
            while (running_.load()) {
                double mid_price = price_dist(gen);
                double half_spread = std::abs(spread_dist(gen)) / 2.0;
                
                double bid = mid_price - half_spread;
                double ask = mid_price + half_spread;
                
                if (update_callback_) {
                    MarketUpdate bid_update(MarketUpdate::BID_UPDATE, symbol_, exchange_name_, bid, 80.0);
                    update_callback_(bid_update);
                    
                    MarketUpdate ask_update(MarketUpdate::ASK_UPDATE, symbol_, exchange_name_, ask, 80.0);
                    update_callback_(ask_update);
                }
                
                int delay = update_delay(gen);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            } });
        }
    };

    // Bybit feed simulator (often has arbitrage opportunities)
    class BybitFeed : public ExchangeFeedBase
    {
    private:
        double base_price_ = 50000.0;
        double volatility_ = 0.002; // Higher volatility

    public:
        BybitFeed() : ExchangeFeedBase("bybit") {}

        void start() override
        {
            if (running_.exchange(true))
                return;

            worker_thread_ = std::thread([this]()
                                         {
            std::random_device rd;
            std::mt19937 gen(rd());
            
            // Bybit sometimes has pricing discrepancies
            std::normal_distribution<> price_dist(base_price_, base_price_ * volatility_);
            std::normal_distribution<> spread_dist(0.5, 0.3);
            std::uniform_real_distribution<> lag_factor(0.98, 1.02); // Price lag simulation
            std::uniform_int_distribution<> update_delay(45, 65); // 45-65ms updates
            
            while (running_.load()) {
                double mid_price = price_dist(gen) * lag_factor(gen); // Simulate pricing lag
                double half_spread = std::abs(spread_dist(gen)) / 2.0;
                
                double bid = mid_price - half_spread;
                double ask = mid_price + half_spread;
                
                if (update_callback_) {
                    MarketUpdate bid_update(MarketUpdate::BID_UPDATE, symbol_, exchange_name_, bid, 200.0);
                    update_callback_(bid_update);
                    
                    MarketUpdate ask_update(MarketUpdate::ASK_UPDATE, symbol_, exchange_name_, ask, 200.0);
                    update_callback_(ask_update);
                }
                
                int delay = update_delay(gen);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            } });
        }
    };

    // Exchange feed manager
    class ExchangeManager
    {
    private:
        std::vector<std::unique_ptr<ExchangeFeedBase>> feeds_;
        std::function<void(const MarketUpdate &)> update_callback_;

    public:
        void add_exchange(std::unique_ptr<ExchangeFeedBase> feed)
        {
            if (update_callback_)
            {
                feed->set_update_callback(update_callback_);
            }
            feeds_.push_back(std::move(feed));
        }

        void set_symbol(const std::string &symbol)
        {
            for (auto &feed : feeds_)
            {
                feed->set_symbol(symbol);
            }
        }

        void set_update_callback(std::function<void(const MarketUpdate &)> callback)
        {
            update_callback_ = callback;
            for (auto &feed : feeds_)
            {
                feed->set_update_callback(callback);
            }
        }

        void start_all()
        {
            std::cout << "Starting " << feeds_.size() << " exchange feeds..." << std::endl;
            for (auto &feed : feeds_)
            {
                std::cout << "  Starting " << feed->exchange_name() << " feed" << std::endl;
                feed->start();
            }
        }

        void stop_all()
        {
            std::cout << "Stopping all exchange feeds..." << std::endl;
            for (auto &feed : feeds_)
            {
                feed->stop();
            }
        }

        size_t exchange_count() const { return feeds_.size(); }

        std::vector<std::string> get_exchange_names() const
        {
            std::vector<std::string> names;
            for (const auto &feed : feeds_)
            {
                names.push_back(feed->exchange_name());
            }
            return names;
        }
    };

} // namespace arbisim
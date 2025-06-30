#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <algorithm>

namespace arbisim
{

    // High-precision timestamp for latency tracking
    using Timestamp = std::chrono::time_point<std::chrono::high_resolution_clock>;
    using Duration = std::chrono::nanoseconds;

    inline Timestamp now()
    {
        return std::chrono::high_resolution_clock::now();
    }

    inline uint64_t timestamp_ns()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::high_resolution_clock::now().time_since_epoch())
                                         .count());
    }

    // Market data structures
    struct PriceLevel
    {
        double price = 0.0;
        double quantity = 0.0;
        uint64_t timestamp_ns = 0;

        PriceLevel() = default;
        PriceLevel(double p, double q) : price(p), quantity(q), timestamp_ns(arbisim::timestamp_ns()) {}
    };

    struct MarketUpdate
    {
        enum Type
        {
            BID_UPDATE,
            ASK_UPDATE,
            TRADE
        };

        Type type;
        std::string symbol;
        std::string exchange;
        double price;
        double quantity;
        uint64_t timestamp_ns;
        uint64_t sequence_id;

        MarketUpdate() = default;
        MarketUpdate(Type t, const std::string &sym, const std::string &exch,
                     double p, double q, uint64_t seq = 0)
            : type(t), symbol(sym), exchange(exch), price(p), quantity(q),
              timestamp_ns(arbisim::timestamp_ns()), sequence_id(seq) {}
    };

    // Lock-free order book (simplified for speed)
    class FastOrderBook
    {
    private:
        static constexpr size_t MAX_LEVELS = 10;

        struct BookSide
        {
            std::array<PriceLevel, MAX_LEVELS> levels;
            std::atomic<size_t> count{0};
            mutable std::atomic<uint64_t> last_update_ns{0};
        };

        BookSide bids_;
        BookSide asks_;
        std::string symbol_;
        std::string exchange_;

    public:
        explicit FastOrderBook(const std::string &symbol, const std::string &exchange)
            : symbol_(symbol), exchange_(exchange) {}

        // Update bid side (thread-safe for single writer)
        void update_bid(double price, double quantity)
        {
            auto &bids = bids_;
            size_t count = bids.count.load();

            // Find insertion point or update existing
            for (size_t i = 0; i < count; ++i)
            {
                if (bids.levels[i].price == price)
                {
                    // Update existing level
                    bids.levels[i].quantity = quantity;
                    bids.levels[i].timestamp_ns = arbisim::timestamp_ns();
                    bids.last_update_ns.store(arbisim::timestamp_ns());
                    return;
                }
                else if (bids.levels[i].price < price)
                {
                    // Insert here, shift others down
                    for (size_t j = std::min(count, MAX_LEVELS - 1); j > i; --j)
                    {
                        bids.levels[j] = bids.levels[j - 1];
                    }
                    bids.levels[i] = PriceLevel(price, quantity);
                    if (count < MAX_LEVELS)
                    {
                        bids.count.store(count + 1);
                    }
                    bids.last_update_ns.store(arbisim::timestamp_ns());
                    return;
                }
            }

            // Append at end if there's space
            if (count < MAX_LEVELS)
            {
                bids.levels[count] = PriceLevel(price, quantity);
                bids.count.store(count + 1);
                bids.last_update_ns.store(arbisim::timestamp_ns());
            }
        }

        // Update ask side (thread-safe for single writer)
        void update_ask(double price, double quantity)
        {
            auto &asks = asks_;
            size_t count = asks.count.load();

            // Find insertion point or update existing
            for (size_t i = 0; i < count; ++i)
            {
                if (asks.levels[i].price == price)
                {
                    // Update existing level
                    asks.levels[i].quantity = quantity;
                    asks.levels[i].timestamp_ns = arbisim::timestamp_ns();
                    asks.last_update_ns.store(arbisim::timestamp_ns());
                    return;
                }
                else if (asks.levels[i].price > price)
                {
                    // Insert here, shift others up
                    for (size_t j = std::min(count, MAX_LEVELS - 1); j > i; --j)
                    {
                        asks.levels[j] = asks.levels[j - 1];
                    }
                    asks.levels[i] = PriceLevel(price, quantity);
                    if (count < MAX_LEVELS)
                    {
                        asks.count.store(count + 1);
                    }
                    asks.last_update_ns.store(arbisim::timestamp_ns());
                    return;
                }
            }

            // Append at end if there's space
            if (count < MAX_LEVELS)
            {
                asks.levels[count] = PriceLevel(price, quantity);
                asks.count.store(count + 1);
                asks.last_update_ns.store(arbisim::timestamp_ns());
            }
        }

        // Get best bid/ask (lock-free read)
        std::pair<double, double> get_best_bid_ask() const
        {
            double best_bid = 0.0, best_ask = 0.0;

            if (bids_.count.load() > 0)
            {
                best_bid = bids_.levels[0].price;
            }
            if (asks_.count.load() > 0)
            {
                best_ask = asks_.levels[0].price;
            }

            return {best_bid, best_ask};
        }

        // Get spread
        double get_spread() const
        {
            auto [bid, ask] = get_best_bid_ask();
            return (ask > 0 && bid > 0) ? (ask - bid) : 0.0;
        }

        // Get mid price
        double get_mid_price() const
        {
            auto [bid, ask] = get_best_bid_ask();
            return (ask > 0 && bid > 0) ? (ask + bid) / 2.0 : 0.0;
        }

        const std::string &symbol() const { return symbol_; }
        const std::string &exchange() const { return exchange_; }
    };

    // Arbitrage opportunity detector
    struct ArbitrageOpportunity
    {
        std::string symbol;
        std::string buy_exchange;
        std::string sell_exchange;
        double buy_price;
        double sell_price;
        double profit_bps; // basis points
        uint64_t detected_at_ns;
        uint64_t latency_ns; // time from market update to detection

        ArbitrageOpportunity() = default;
        ArbitrageOpportunity(const std::string &sym, const std::string &buy_exch,
                             const std::string &sell_exch, double buy_px, double sell_px,
                             uint64_t update_time_ns)
            : symbol(sym), buy_exchange(buy_exch), sell_exchange(sell_exch),
              buy_price(buy_px), sell_price(sell_px), detected_at_ns(arbisim::timestamp_ns()),
              latency_ns(detected_at_ns - update_time_ns)
        {

            profit_bps = ((sell_price - buy_price) / buy_price) * 10000.0;
        }
    };

    class ArbitrageDetector
    {
    private:
        std::unordered_map<std::string, std::unordered_map<std::string, std::unique_ptr<FastOrderBook>>> books_;
        double min_profit_bps_ = 5.0; // Minimum 0.5 bps profit

    public:
        void add_orderbook(const std::string &symbol, const std::string &exchange)
        {
            books_[symbol][exchange] = std::make_unique<FastOrderBook>(symbol, exchange);
        }

        void set_min_profit_bps(double bps)
        {
            min_profit_bps_ = bps;
        }

        FastOrderBook *get_orderbook(const std::string &symbol, const std::string &exchange)
        {
            auto sym_it = books_.find(symbol);
            if (sym_it != books_.end())
            {
                auto exch_it = sym_it->second.find(exchange);
                if (exch_it != sym_it->second.end())
                {
                    return exch_it->second.get();
                }
            }
            return nullptr;
        }

        // Check for arbitrage opportunities across all exchanges for a symbol
        std::vector<ArbitrageOpportunity> check_arbitrage(const std::string &symbol,
                                                          uint64_t update_time_ns)
        {
            std::vector<ArbitrageOpportunity> opportunities;

            auto sym_it = books_.find(symbol);
            if (sym_it == books_.end() || sym_it->second.size() < 2)
            {
                return opportunities;
            }

            // Compare all pairs of exchanges
            auto &exchanges = sym_it->second;
            for (auto it1 = exchanges.begin(); it1 != exchanges.end(); ++it1)
            {
                for (auto it2 = std::next(it1); it2 != exchanges.end(); ++it2)
                {

                    auto [bid1, ask1] = it1->second->get_best_bid_ask();
                    auto [bid2, ask2] = it2->second->get_best_bid_ask();

                    // Check if we can buy on exchange 1 and sell on exchange 2
                    if (ask1 > 0 && bid2 > 0 && bid2 > ask1)
                    {
                        double profit_bps = ((bid2 - ask1) / ask1) * 10000.0;
                        if (profit_bps >= min_profit_bps_)
                        {
                            opportunities.emplace_back(symbol, it1->first, it2->first,
                                                       ask1, bid2, update_time_ns);
                        }
                    }

                    // Check if we can buy on exchange 2 and sell on exchange 1
                    if (ask2 > 0 && bid1 > 0 && bid1 > ask2)
                    {
                        double profit_bps = ((bid1 - ask2) / ask2) * 10000.0;
                        if (profit_bps >= min_profit_bps_)
                        {
                            opportunities.emplace_back(symbol, it2->first, it1->first,
                                                       ask2, bid1, update_time_ns);
                        }
                    }
                }
            }

            return opportunities;
        }
    };

} // namespace arbisim
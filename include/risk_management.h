#pragma once
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <vector>
#include "arbisim_core.h"

namespace arbisim
{

    // Position tracking for risk management
    struct Position
    {
        std::string exchange;
        std::string symbol;
        double quantity = 0.0;       // Positive = long, negative = short
        double avg_price = 0.0;      // Average entry price
        double unrealized_pnl = 0.0; // Current P&L
        uint64_t last_update_ns = 0;

        Position() = default;
        Position(const std::string &exch, const std::string &sym)
            : exchange(exch), symbol(sym) {}
    };

    // Trade execution record
    struct Trade
    {
        uint64_t trade_id;
        uint64_t timestamp_ns;
        std::string symbol;
        std::string buy_exchange;
        std::string sell_exchange;
        double quantity;
        double buy_price;
        double sell_price;
        double gross_pnl;
        double net_pnl; // After fees
        double fees;
        std::string status; // "simulated", "pending", "filled", "failed"

        Trade() = default;
        Trade(uint64_t id, const ArbitrageOpportunity &opp, double qty)
            : trade_id(id), timestamp_ns(::arbisim::timestamp_ns()), symbol(opp.symbol),
              buy_exchange(opp.buy_exchange), sell_exchange(opp.sell_exchange),
              quantity(qty), buy_price(opp.buy_price), sell_price(opp.sell_price),
              status("simulated")
        {

            gross_pnl = (sell_price - buy_price) * quantity;
            fees = calculate_fees(qty, buy_price, sell_price);
            net_pnl = gross_pnl - fees;
        }

    private:
        double calculate_fees(double qty, double buy_px, double sell_px)
        {
            // Typical crypto exchange fees: 0.1% per side
            const double fee_rate = 0.001;
            return (qty * buy_px + qty * sell_px) * fee_rate;
        }
    };

    // Risk management system
    class RiskManager
    {
    private:
        // Risk limits
        double max_position_size_ = 1.0;      // Max position per exchange (BTC)
        double max_total_exposure_ = 5.0;     // Max total exposure (BTC)
        double max_single_trade_size_ = 0.1;  // Max single trade size (BTC)
        double min_profit_after_fees_ = 10.0; // Min profit after fees (bps)
        double max_daily_loss_ = 1000.0;      // Max daily loss ($)
        double max_drawdown_ = 0.05;          // Max 5% drawdown

        // Current state - using regular doubles with mutex protection instead of atomic<double>
        std::unordered_map<std::string, Position> positions_; // key: exchange_symbol
        std::vector<Trade> trade_history_;
        std::atomic<uint64_t> next_trade_id_{1};

        // Use regular doubles with mutex protection (std::atomic<double> doesn't have fetch_add in all compilers)
        double daily_pnl_ = 0.0;
        double total_pnl_ = 0.0;
        double max_balance_ = 10000.0; // Starting balance

        mutable std::mutex risk_mutex_;

        // Performance tracking
        std::atomic<uint64_t> opportunities_seen_{0};
        std::atomic<uint64_t> opportunities_taken_{0};
        std::atomic<uint64_t> opportunities_rejected_{0};

    public:
        // Risk check for arbitrage opportunity
        enum class RiskDecision
        {
            APPROVED,
            REJECTED_POSITION_LIMIT,
            REJECTED_EXPOSURE_LIMIT,
            REJECTED_TRADE_SIZE,
            REJECTED_PROFIT_TOO_LOW,
            REJECTED_DAILY_LOSS,
            REJECTED_DRAWDOWN,
            REJECTED_EXCHANGE_LIMIT
        };

        struct RiskAssessment
        {
            RiskDecision decision = RiskDecision::REJECTED_PROFIT_TOO_LOW;
            double recommended_size = 0.0;
            std::string reason;
            double expected_pnl = 0.0;
            double fees = 0.0;
            double net_profit_bps = 0.0;
        };

        RiskAssessment assess_opportunity(const ArbitrageOpportunity &opp)
        {
            std::lock_guard<std::mutex> lock(risk_mutex_);
            opportunities_seen_.fetch_add(1);

            RiskAssessment assessment;

            // Calculate optimal trade size
            double max_size_by_position = calculate_max_size_by_position(opp);
            double max_size_by_exposure = calculate_max_size_by_exposure();
            double recommended_size = std::min({max_single_trade_size_, max_size_by_position, max_size_by_exposure});

            if (recommended_size <= 0.001)
            { // Minimum viable trade size
                assessment.decision = RiskDecision::REJECTED_TRADE_SIZE;
                assessment.reason = "Trade size too small";
                return assessment;
            }

            // Calculate expected P&L after fees
            Trade simulated_trade(next_trade_id_.load(), opp, recommended_size);
            assessment.expected_pnl = simulated_trade.gross_pnl;
            assessment.fees = simulated_trade.fees;
            assessment.net_profit_bps = (simulated_trade.net_pnl / (recommended_size * opp.buy_price)) * 10000.0;

            // Check minimum profit threshold
            if (assessment.net_profit_bps < min_profit_after_fees_)
            {
                assessment.decision = RiskDecision::REJECTED_PROFIT_TOO_LOW;
                assessment.reason = "Net profit below threshold (" +
                                    std::to_string(assessment.net_profit_bps) + " < " +
                                    std::to_string(min_profit_after_fees_) + " bps)";
                return assessment;
            }

            // Check daily loss limit
            if (daily_pnl_ < -max_daily_loss_)
            {
                assessment.decision = RiskDecision::REJECTED_DAILY_LOSS;
                assessment.reason = "Daily loss limit exceeded";
                return assessment;
            }

            // Check drawdown limit
            double current_balance = max_balance_ + total_pnl_;
            double drawdown = (max_balance_ - current_balance) / max_balance_;
            if (drawdown > max_drawdown_)
            {
                assessment.decision = RiskDecision::REJECTED_DRAWDOWN;
                assessment.reason = "Drawdown limit exceeded (" +
                                    std::to_string(drawdown * 100) + "%)";
                return assessment;
            }

            // All checks passed
            assessment.decision = RiskDecision::APPROVED;
            assessment.recommended_size = recommended_size;
            assessment.reason = "Trade approved";
            opportunities_taken_.fetch_add(1);

            return assessment;
        }

        // Execute approved trade
        bool execute_trade(const ArbitrageOpportunity &opp, double size)
        {
            std::lock_guard<std::mutex> lock(risk_mutex_);

            uint64_t trade_id = next_trade_id_.fetch_add(1);
            Trade trade(trade_id, opp, size);

            // Update positions
            update_position(opp.buy_exchange, opp.symbol, size, opp.buy_price);
            update_position(opp.sell_exchange, opp.symbol, -size, opp.sell_price);

            // Update P&L (thread-safe with mutex)
            daily_pnl_ += trade.net_pnl;
            total_pnl_ += trade.net_pnl;

            // Update max balance if we have a new high
            double current_balance = max_balance_ + total_pnl_;
            if (current_balance > max_balance_)
            {
                max_balance_ = current_balance;
            }

            // Record trade
            trade_history_.push_back(trade);

            return true;
        }

        // Risk monitoring and reporting
        struct RiskReport
        {
            double total_exposure = 0.0;
            double daily_pnl = 0.0;
            double total_pnl = 0.0;
            double current_drawdown = 0.0;
            size_t active_positions = 0;
            size_t total_trades = 0;
            double win_rate = 0.0;
            double avg_profit_per_trade = 0.0;
            uint64_t opportunities_seen = 0;
            uint64_t opportunities_taken = 0;
            double take_rate = 0.0;
        };

        RiskReport generate_report() const
        {
            std::lock_guard<std::mutex> lock(risk_mutex_);

            RiskReport report;
            report.daily_pnl = daily_pnl_;
            report.total_pnl = total_pnl_;
            report.total_trades = trade_history_.size();
            report.opportunities_seen = opportunities_seen_.load();
            report.opportunities_taken = opportunities_taken_.load();

            // Calculate exposure
            for (const auto &[key, pos] : positions_)
            {
                report.total_exposure += std::abs(pos.quantity * pos.avg_price);
                if (std::abs(pos.quantity) > 0.001)
                {
                    report.active_positions++;
                }
            }

            // Calculate drawdown
            double current_balance = max_balance_ + total_pnl_;
            report.current_drawdown = (max_balance_ - current_balance) / max_balance_;

            // Calculate performance metrics
            if (report.total_trades > 0)
            {
                double total_profit = 0.0;
                size_t winning_trades = 0;

                for (const auto &trade : trade_history_)
                {
                    total_profit += trade.net_pnl;
                    if (trade.net_pnl > 0)
                        winning_trades++;
                }

                report.win_rate = static_cast<double>(winning_trades) / report.total_trades;
                report.avg_profit_per_trade = total_profit / report.total_trades;
            }

            if (report.opportunities_seen > 0)
            {
                report.take_rate = static_cast<double>(report.opportunities_taken) / report.opportunities_seen;
            }

            return report;
        }

        // Configuration
        void set_risk_limits(double max_pos, double max_exp, double max_trade,
                             double min_profit, double max_loss, double max_dd)
        {
            std::lock_guard<std::mutex> lock(risk_mutex_);
            max_position_size_ = max_pos;
            max_total_exposure_ = max_exp;
            max_single_trade_size_ = max_trade;
            min_profit_after_fees_ = min_profit;
            max_daily_loss_ = max_loss;
            max_drawdown_ = max_dd;
        }

        void reset_daily_pnl()
        {
            std::lock_guard<std::mutex> lock(risk_mutex_);
            daily_pnl_ = 0.0;
        }

    private:
        double calculate_max_size_by_position(const ArbitrageOpportunity &opp)
        {
            // Check position limits on both exchanges
            std::string buy_key = opp.buy_exchange + "_" + opp.symbol;
            std::string sell_key = opp.sell_exchange + "_" + opp.symbol;

            double buy_current = 0.0;
            double sell_current = 0.0;

            auto buy_it = positions_.find(buy_key);
            if (buy_it != positions_.end())
            {
                buy_current = buy_it->second.quantity;
            }

            auto sell_it = positions_.find(sell_key);
            if (sell_it != positions_.end())
            {
                sell_current = sell_it->second.quantity;
            }

            double max_buy_size = max_position_size_ - buy_current;
            double max_sell_size = max_position_size_ + sell_current; // We're going short

            return std::min(max_buy_size, max_sell_size);
        }

        double calculate_max_size_by_exposure()
        {
            double current_exposure = 0.0;
            for (const auto &[key, pos] : positions_)
            {
                current_exposure += std::abs(pos.quantity * pos.avg_price);
            }

            double remaining_exposure = max_total_exposure_ - current_exposure;
            return remaining_exposure / 50000.0; // Rough conversion to BTC assuming $50k price
        }

        void update_position(const std::string &exchange, const std::string &symbol,
                             double quantity, double price)
        {
            std::string key = exchange + "_" + symbol;
            auto &pos = positions_[key];

            if (pos.exchange.empty())
            {
                pos.exchange = exchange;
                pos.symbol = symbol;
            }

            // Update average price and quantity
            if ((pos.quantity > 0 && quantity > 0) || (pos.quantity < 0 && quantity < 0))
            {
                // Same direction - update average price
                double total_value = pos.quantity * pos.avg_price + quantity * price;
                pos.quantity += quantity;
                if (std::abs(pos.quantity) > 0.001)
                {
                    pos.avg_price = total_value / pos.quantity;
                }
            }
            else
            {
                // Different direction - reduce position or flip
                pos.quantity += quantity;
                if (std::abs(pos.quantity) < 0.001)
                {
                    pos.avg_price = 0.0; // Position closed
                }
                else if ((pos.quantity > 0 && quantity < 0) || (pos.quantity < 0 && quantity > 0))
                {
                    pos.avg_price = price; // Position flipped
                }
            }

            pos.last_update_ns = timestamp_ns();
        }
    };

} // namespace arbisim
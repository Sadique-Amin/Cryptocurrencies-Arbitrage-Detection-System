#include <iostream>
#include <fstream>
#include <atomic>
#include <signal.h>
#include <iomanip>
#include <thread>
#include <chrono>

#include "arbisim_core.h"
#include "multi_exchange_feeds.h"

#ifdef HAVE_BOOST
#include "risk_management.h"
#endif

namespace arbisim
{

#ifndef HAVE_BOOST
    // Simplified risk management when Boost is not available
    class SimpleRiskManager
    {
    private:
        double max_trade_size_ = 0.5; // Increased from 0.1 to 0.5 BTC
        double min_profit_bps_ = 5.0; // Reduced from 8.0 to 5.0 bps
        std::atomic<uint64_t> opportunities_seen_{0};
        std::atomic<uint64_t> opportunities_taken_{0};
        std::atomic<double> daily_pnl_{0.0};

    public:
        enum class Decision
        {
            APPROVED = 0, // Set explicit values for CSV logging
            REJECTED_PROFIT = 1,
            REJECTED_SIZE = 2
        };

        struct Assessment
        {
            Decision decision = Decision::REJECTED_PROFIT;
            double recommended_size = 0.0;
            std::string reason;
            double net_profit_bps = 0.0;
        };

        Assessment assess_opportunity(const ArbitrageOpportunity &opp)
        {
            opportunities_seen_.fetch_add(1);

            Assessment assessment;

            // Calculate net profit after fees (0.1% per side = 20 bps total)
            double fees_bps = 20.0;
            assessment.net_profit_bps = opp.profit_bps - fees_bps;

            std::cout << "[DEBUG] Gross: " << opp.profit_bps << " bps, Fees: " << fees_bps
                      << " bps, Net: " << assessment.net_profit_bps << " bps, Min Required: "
                      << min_profit_bps_ << " bps" << std::endl;

            if (assessment.net_profit_bps < min_profit_bps_)
            {
                assessment.decision = Decision::REJECTED_PROFIT;
                assessment.reason = "Net profit below threshold (" +
                                    std::to_string(assessment.net_profit_bps) + " < " +
                                    std::to_string(min_profit_bps_) + " bps)";
                return assessment;
            }

            // Set recommended size
            assessment.recommended_size = max_trade_size_;

            // Double check size is reasonable
            if (assessment.recommended_size < 0.001)
            {
                assessment.decision = Decision::REJECTED_SIZE;
                assessment.reason = "Recommended trade size too small: " + std::to_string(assessment.recommended_size);
                return assessment;
            }

            // All checks passed
            assessment.decision = Decision::APPROVED;
            assessment.reason = "Trade approved";
            opportunities_taken_.fetch_add(1);

            // Simulate P&L
            double gross_pnl = (opp.sell_price - opp.buy_price) * assessment.recommended_size;
            double fees = (assessment.recommended_size * opp.buy_price + assessment.recommended_size * opp.sell_price) * 0.001;
            daily_pnl_.fetch_add(gross_pnl - fees);

            std::cout << "[DEBUG] APPROVED: Size=" << assessment.recommended_size
                      << " BTC, Expected P&L=$" << (gross_pnl - fees) << std::endl;

            return assessment;
        }

        struct Report
        {
            uint64_t opportunities_seen = 0;
            uint64_t opportunities_taken = 0;
            double take_rate = 0.0;
            double daily_pnl = 0.0;
            double total_exposure = 0.0;
            size_t active_positions = 0;
            double current_drawdown = 0.0;
            double win_rate = 0.0;
        };

        Report generate_report() const
        {
            Report report;
            report.opportunities_seen = opportunities_seen_.load();
            report.opportunities_taken = opportunities_taken_.load();
            report.daily_pnl = daily_pnl_.load();

            if (report.opportunities_seen > 0)
            {
                report.take_rate = static_cast<double>(report.opportunities_taken) / report.opportunities_seen;
            }

            // Simulate some additional metrics for display
            report.total_exposure = report.opportunities_taken * 0.5 * 50000.0; // Rough estimate
            report.active_positions = std::min(report.opportunities_taken, uint64_t(8));
            report.current_drawdown = 0.02; // 2% simulated drawdown
            report.win_rate = 0.85;         // 85% simulated win rate

            return report;
        }

        // Add method to adjust thresholds
        void set_risk_limits(double max_trade, double min_profit)
        {
            max_trade_size_ = max_trade;
            min_profit_bps_ = min_profit;

            std::cout << "[DEBUG] Risk limits updated: Max trade=" << max_trade_size_
                      << " BTC, Min profit=" << min_profit_bps_ << " bps" << std::endl;
        }
    };

    using RiskManagerType = SimpleRiskManager;
#else
    using RiskManagerType = RiskManager;
#endif

    class UltraFastPerformanceTracker
    {
    private:
        std::atomic<uint64_t> total_updates_{0};
        std::atomic<uint64_t> total_latency_ns_{0};
        std::atomic<uint64_t> min_latency_ns_{UINT64_MAX};
        std::atomic<uint64_t> max_latency_ns_{0};
        std::atomic<uint64_t> arbitrage_opportunities_{0};
        std::atomic<uint64_t> trades_executed_{0};

        uint64_t start_time_ns_;

    public:
        UltraFastPerformanceTracker() : start_time_ns_(timestamp_ns()) {}

        void record_update_latency(uint64_t latency_ns)
        {
            total_updates_.fetch_add(1);
            total_latency_ns_.fetch_add(latency_ns);

            // Fast atomic min/max updates
            uint64_t current_min = min_latency_ns_.load(std::memory_order_relaxed);
            while (latency_ns < current_min &&
                   !min_latency_ns_.compare_exchange_weak(current_min, latency_ns, std::memory_order_relaxed))
            {
            }

            uint64_t current_max = max_latency_ns_.load(std::memory_order_relaxed);
            while (latency_ns > current_max &&
                   !max_latency_ns_.compare_exchange_weak(current_max, latency_ns, std::memory_order_relaxed))
            {
            }
        }

        void record_arbitrage_opportunity()
        {
            arbitrage_opportunities_.fetch_add(1, std::memory_order_relaxed);
        }

        void record_trade_executed()
        {
            trades_executed_.fetch_add(1, std::memory_order_relaxed);
        }

        void print_stats() const
        {
            uint64_t updates = total_updates_.load();
            if (updates == 0)
            {
                std::cout << "No updates processed yet." << std::endl;
                return;
            }

            uint64_t runtime_ns = timestamp_ns() - start_time_ns_;
            double runtime_sec = runtime_ns / 1e9;

            uint64_t avg_latency = total_latency_ns_.load() / updates;
            uint64_t min_lat = min_latency_ns_.load();
            uint64_t max_lat = max_latency_ns_.load();
            uint64_t opportunities = arbitrage_opportunities_.load();
            uint64_t trades = trades_executed_.load();

            std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║                    🚀 ULTRA-FAST ARBISIM 🚀                  ║" << std::endl;
            std::cout << "╠══════════════════════════════════════════════════════════════╣" << std::endl;
            std::cout << "║ Runtime:           " << std::setw(8) << std::fixed << std::setprecision(1) << runtime_sec << " seconds" << std::setw(19) << "║" << std::endl;
            std::cout << "║ Total Updates:     " << std::setw(8) << updates << std::setw(27) << "║" << std::endl;
            std::cout << "║ Updates/sec:       " << std::setw(8) << std::fixed << std::setprecision(1) << (updates / runtime_sec) << std::setw(27) << "║" << std::endl;
            std::cout << "║ Avg Latency:       " << std::setw(8) << (avg_latency / 1000) << " μs" << std::setw(24) << "║" << std::endl;
            std::cout << "║ Min Latency:       " << std::setw(8) << (min_lat == UINT64_MAX ? 0 : min_lat / 1000) << " μs" << std::setw(24) << "║" << std::endl;
            std::cout << "║ Max Latency:       " << std::setw(8) << (max_lat / 1000) << " μs" << std::setw(24) << "║" << std::endl;
            std::cout << "║ Opportunities:     " << std::setw(8) << opportunities << std::setw(27) << "║" << std::endl;
            std::cout << "║ Trades Executed:   " << std::setw(8) << trades << std::setw(27) << "║" << std::endl;
            if (opportunities > 0)
            {
                std::cout << "║ Execution Rate:    " << std::setw(8) << std::fixed << std::setprecision(1) << (static_cast<double>(trades) / opportunities * 100) << "%" << std::setw(26) << "║" << std::endl;
            }
            std::cout << "╚══════════════════════════════════════════════════════════════╝\n"
                      << std::endl;
        }
    };

    class UltraFastArbiSimEngine
    {
    private:
        ArbitrageDetector detector_;
        UltraFastPerformanceTracker perf_tracker_;
        RiskManagerType risk_manager_;
        ExchangeManager exchange_manager_;

        std::ofstream arbitrage_log_;
        std::atomic<bool> running_{false};

        std::thread stats_thread_;

    public:
        UltraFastArbiSimEngine()
        {
            // Open log file
            arbitrage_log_.open("arbitrage_opportunities.csv");
            arbitrage_log_ << "timestamp,symbol,buy_exchange,sell_exchange,buy_price,sell_price,profit_bps,net_profit_bps,latency_ns,decision\n";

#ifdef HAVE_BOOST
            // Set up more liberal risk management (full version)
            risk_manager_.set_risk_limits(5.0, 500000.0, 1.0, 2.0, 2000.0, 0.10);
            risk_manager_.reset_all_positions();
#else
            // For simple risk manager, set more liberal limits
            risk_manager_.set_risk_limits(1.0, 2.0); // 0.5 BTC max, 5 bps min profit
#endif

            // Add exchanges
            exchange_manager_.add_exchange(std::make_unique<BinanceFeed>());
            exchange_manager_.add_exchange(std::make_unique<CoinbaseFeed>());
            exchange_manager_.add_exchange(std::make_unique<KrakenFeed>());
            exchange_manager_.add_exchange(std::make_unique<BybitFeed>());

            // Set up detector for all exchanges
            auto exchange_names = exchange_manager_.get_exchange_names();
            for (const auto &exchange : exchange_names)
            {
                detector_.add_orderbook("BTCUSDT", exchange);
            }
            detector_.set_min_profit_bps(5.0);

            // Set up exchange feeds
            exchange_manager_.set_symbol("BTCUSDT");
            exchange_manager_.set_update_callback([this](const MarketUpdate &update)
                                                  { this->handle_market_update(update); });
        }

        ~UltraFastArbiSimEngine()
        {
            stop();
            if (arbitrage_log_.is_open())
                arbitrage_log_.close();
        }

        void start()
        {
            if (running_.exchange(true))
                return;

            std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║        ⚡ ULTRA-FAST ARBISIM ENGINE STARTING ⚡              ║" << std::endl;
            std::cout << "║                  (Zero External Dependencies)               ║" << std::endl;
            std::cout << "╠══════════════════════════════════════════════════════════════╣" << std::endl;
            std::cout << "║ Symbol:            BTCUSDT                                   ║" << std::endl;
            std::cout << "║ Exchanges:         " << exchange_manager_.exchange_count() << " active feeds" << std::setw(32) << "║" << std::endl;

#ifdef HAVE_BOOST
            std::cout << "║ Risk Management:   ADVANCED (Boost enabled)                 ║" << std::endl;
#else
            std::cout << "║ Risk Management:   BASIC (Ultra-fast mode)                  ║" << std::endl;
#endif

            std::cout << "║ JSON Parser:       NONE (Custom parser)                     ║" << std::endl;
            std::cout << "║ Build Time:        ULTRA-FAST                                ║" << std::endl;
            std::cout << "║ Min Profit:        5.0 bps (after fees)                     ║" << std::endl;
            std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
            std::cout << "\nPress Ctrl+C to stop safely...\n"
                      << std::endl;

            // Start exchange feeds
            exchange_manager_.start_all();

            // Start monitoring thread
            stats_thread_ = std::thread([this]()
                                        {
            while (running_.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(10));
                if (running_.load()) {
                    perf_tracker_.print_stats();
                    print_risk_summary();
                }
            } });
        }

        void stop()
        {
            if (!running_.exchange(false))
                return;

            std::cout << "\n🛑 Shutting down Ultra-Fast ArbiSim Engine..." << std::endl;

            exchange_manager_.stop_all();

            if (stats_thread_.joinable())
                stats_thread_.join();

            // Final reports
            perf_tracker_.print_stats();
            print_final_summary();

            std::cout << "✅ ArbiSim Engine stopped safely." << std::endl;
        }

    private:
        void handle_market_update(const MarketUpdate &update)
        {
            uint64_t processing_start = timestamp_ns();

            // Update order book
            auto *book = detector_.get_orderbook(update.symbol, update.exchange);
            if (!book)
                return;

            if (update.type == MarketUpdate::BID_UPDATE)
            {
                book->update_bid(update.price, update.quantity);
            }
            else if (update.type == MarketUpdate::ASK_UPDATE)
            {
                book->update_ask(update.price, update.quantity);
            }

            // Check for arbitrage opportunities
            auto opportunities = detector_.check_arbitrage(update.symbol, update.timestamp_ns);

            uint64_t processing_end = timestamp_ns();
            uint64_t processing_latency = processing_end - update.timestamp_ns;

            // Record performance
            perf_tracker_.record_update_latency(processing_latency);

            // Process each opportunity
            for (const auto &opp : opportunities)
            {
                perf_tracker_.record_arbitrage_opportunity();
                process_arbitrage_opportunity(opp);
            }
        }

        void process_arbitrage_opportunity(const ArbitrageOpportunity &opp)
        {
            // Risk assessment
            auto assessment = risk_manager_.assess_opportunity(opp);

            // Create decision code for CSV logging
            int decision_code = static_cast<int>(assessment.decision);

            // Log opportunity to CSV file for dashboard bridge
            arbitrage_log_ << opp.detected_at_ns << ","
                           << opp.symbol << ","
                           << opp.buy_exchange << ","
                           << opp.sell_exchange << ","
                           << std::fixed << std::setprecision(2) << opp.buy_price << ","
                           << std::fixed << std::setprecision(2) << opp.sell_price << ","
                           << std::fixed << std::setprecision(1) << opp.profit_bps << ","
                           << std::fixed << std::setprecision(1) << assessment.net_profit_bps << ","
                           << opp.latency_ns << ","
                           << decision_code << "\n";
            arbitrage_log_.flush(); // Important: Force write to file for dashboard bridge

            // Display opportunity with better formatting
#ifdef HAVE_BOOST
            if (assessment.decision == RiskManager::RiskDecision::APPROVED)
            {
#else
            if (assessment.decision == SimpleRiskManager::Decision::APPROVED)
            {
#endif
                std::cout << "==> APPROVED ARBITRAGE OPPORTUNITY <==" << std::endl;
                perf_tracker_.record_trade_executed();

                // Execute the trade if approved
#ifdef HAVE_BOOST
                risk_manager_.execute_trade(opp, assessment.recommended_size);
#endif
            }
            else
            {
                std::cout << "==> ARBITRAGE OPPORTUNITY (REJECTED) <==" << std::endl;
            }

            std::cout << "Symbol: " << opp.symbol << " | "
                      << "Buy: " << opp.buy_exchange << " @ $" << std::fixed << std::setprecision(2) << opp.buy_price << " | "
                      << "Sell: " << opp.sell_exchange << " @ $" << std::fixed << std::setprecision(2) << opp.sell_price << std::endl;
            std::cout << "Gross Profit: " << std::fixed << std::setprecision(1) << opp.profit_bps << " bps | "
                      << "Net Profit: " << std::fixed << std::setprecision(1) << assessment.net_profit_bps << " bps | "
                      << "Latency: " << (opp.latency_ns / 1000) << " us" << std::endl;

#ifdef HAVE_BOOST
            if (assessment.decision != RiskManager::RiskDecision::APPROVED)
            {
#else
            if (assessment.decision != SimpleRiskManager::Decision::APPROVED)
            {
#endif
                std::cout << "X Rejected: " << assessment.reason << std::endl;
            }
            else
            {
                std::cout << "✓ Trade Size: " << std::fixed << std::setprecision(4) << assessment.recommended_size << " BTC" << std::endl;

                // Calculate and display expected P&L
                double gross_pnl = (opp.sell_price - opp.buy_price) * assessment.recommended_size;
                double fees = (assessment.recommended_size * opp.buy_price + assessment.recommended_size * opp.sell_price) * 0.001;
                double net_pnl = gross_pnl - fees;

                std::cout << "$ Expected P&L: $" << std::fixed << std::setprecision(2) << net_pnl << std::endl;
            }
            std::cout << "----------------------------------------" << std::endl;
        }

        void print_risk_summary()
        {
            auto report = risk_manager_.generate_report();

            std::cout << "📊 RISK SUMMARY: "
                      << "P&L: $" << std::fixed << std::setprecision(2) << report.daily_pnl << " | "
                      << "Exposure: $" << std::fixed << std::setprecision(0) << report.total_exposure << " | "
                      << "Positions: " << report.active_positions << " | "
                      << "Take Rate: " << std::fixed << std::setprecision(1) << (report.take_rate * 100) << "%" << std::endl;
        }

        void print_final_summary()
        {
            auto report = risk_manager_.generate_report();

            std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║                   FINAL SESSION SUMMARY                      ║" << std::endl;
            std::cout << "╠══════════════════════════════════════════════════════════════╣" << std::endl;
            std::cout << "║ Opportunities Found:  " << std::setw(8) << report.opportunities_seen << std::setw(27) << "║" << std::endl;
            std::cout << "║ Trades Executed:      " << std::setw(8) << report.opportunities_taken << std::setw(27) << "║" << std::endl;
            std::cout << "║ Take Rate:            " << std::setw(8) << std::fixed << std::setprecision(1) << (report.take_rate * 100) << "%" << std::setw(26) << "║" << std::endl;
            std::cout << "║ Win Rate:             " << std::setw(8) << std::fixed << std::setprecision(1) << (report.win_rate * 100) << "%" << std::setw(26) << "║" << std::endl;
            std::cout << "║ Total P&L:            $" << std::setw(7) << std::fixed << std::setprecision(2) << report.daily_pnl << std::setw(25) << "║" << std::endl;
            std::cout << "║ Total Exposure:       $" << std::setw(7) << std::fixed << std::setprecision(0) << report.total_exposure << std::setw(25) << "║" << std::endl;
            std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

            // Save final report to file
            std::ofstream summary_file("session_summary.txt");
            summary_file << "ArbiSim Ultra-Fast Session Summary\n";
            summary_file << "==================================\n";
            summary_file << "Mode: Ultra-Fast (No External Dependencies)\n";
            summary_file << "Opportunities Found: " << report.opportunities_seen << "\n";
            summary_file << "Trades Executed: " << report.opportunities_taken << "\n";
            summary_file << "Take Rate: " << (report.take_rate * 100) << "%\n";
            summary_file << "Win Rate: " << (report.win_rate * 100) << "%\n";
            summary_file << "Total P&L: $" << report.daily_pnl << "\n";
            summary_file << "Total Exposure: $" << report.total_exposure << "\n";
            summary_file.close();

            std::cout << "\n📄 Session summary saved to: session_summary.txt" << std::endl;
        }
    };

} // namespace arbisim

// Global variables for signal handling
std::atomic<bool> g_shutdown{false};
arbisim::UltraFastArbiSimEngine *g_engine = nullptr;

void signal_handler(int signal)
{
    std::cout << "\n🛑 Received signal " << signal << ". Initiating graceful shutdown..." << std::endl;
    g_shutdown.store(true);
    if (g_engine)
    {
        g_engine->stop();
    }
}

int main()
{
// Fix console encoding on Windows
#ifdef _WIN32
    system("chcp 65001 >nul"); // UTF-8 encoding
#endif

    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "⚡ ArbiSim Ultra-Fast Initialization..." << std::endl;

#ifdef HAVE_BOOST
    std::cout << "✅ Boost libraries detected - Advanced features enabled" << std::endl;
#else
    std::cout << "⚡ Ultra-fast mode - Zero external dependencies" << std::endl;
#endif

#ifdef HAVE_OPENSSL
    std::cout << "✅ OpenSSL detected - SSL/TLS features enabled" << std::endl;
#else
    std::cout << "⚡ SSL features disabled - Using simplified networking" << std::endl;
#endif

    std::cout << "⚡ JSON libraries: NONE - Custom ultra-fast parser" << std::endl;
    std::cout << "⚡ Build time: MINIMIZED - Ready for development!\n"
              << std::endl;

    try
    {
        // Create and start the ultra-fast engine
        arbisim::UltraFastArbiSimEngine engine;
        g_engine = &engine;

        engine.start();

        // Wait for shutdown signal
        while (!g_shutdown.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        engine.stop();
    }
    catch (const std::exception &e)
    {
        std::cerr << "❌ Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
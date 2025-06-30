#include "../include/arbisim_core.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <random>

using namespace arbisim;

void test_orderbook_performance()
{
    FastOrderBook book("BTCUSDT", "test_exchange");

    // Performance test: 1 million updates
    const int num_updates = 1000000;
    std::vector<double> bid_prices(num_updates);
    std::vector<double> ask_prices(num_updates);

    // Generate random price updates
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> price_dist(49900.0, 50100.0);

    for (int i = 0; i < num_updates; ++i)
    {
        bid_prices[i] = price_dist(gen);
        ask_prices[i] = bid_prices[i] + 1.0 + (rand() % 10);
    }

    auto start = std::chrono::high_resolution_clock::now();

    // Execute updates
    for (int i = 0; i < num_updates; ++i)
    {
        book.update_bid(bid_prices[i], 100.0);
        book.update_ask(ask_prices[i], 100.0);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    double avg_latency_ns = static_cast<double>(duration.count()) / (2 * num_updates);

    std::cout << "=== Order Book Performance Test ===" << std::endl;
    std::cout << "Updates processed: " << (2 * num_updates) << std::endl;
    std::cout << "Total time: " << duration.count() / 1e6 << " ms" << std::endl;
    std::cout << "Average latency per update: " << static_cast<int>(avg_latency_ns) << " ns" << std::endl;
    std::cout << "Updates per second: " << static_cast<int>((2.0 * num_updates) / (duration.count() / 1e9)) << std::endl;

    // Verify correctness
    auto [best_bid, best_ask] = book.get_best_bid_ask();
    std::cout << "Final best bid: $" << best_bid << std::endl;
    std::cout << "Final best ask: $" << best_ask << std::endl;
    std::cout << "Final spread: $" << book.get_spread() << std::endl;
    std::cout << "===================================" << std::endl;
}

void test_arbitrage_detection_performance()
{
    ArbitrageDetector detector;
    detector.add_orderbook("BTCUSDT", "exchange1");
    detector.add_orderbook("BTCUSDT", "exchange2");
    detector.set_min_profit_bps(1.0);

    auto *book1 = detector.get_orderbook("BTCUSDT", "exchange1");
    auto *book2 = detector.get_orderbook("BTCUSDT", "exchange2");

    // Set up crossed books to create arbitrage opportunities
    book1->update_bid(50000.0, 100.0);
    book1->update_ask(50002.0, 100.0);
    book2->update_bid(50001.0, 100.0);
    book2->update_ask(50003.0, 100.0);

    const int num_checks = 100000;
    auto start = std::chrono::high_resolution_clock::now();

    int total_opportunities = 0;
    for (int i = 0; i < num_checks; ++i)
    {
        auto opportunities = detector.check_arbitrage("BTCUSDT", timestamp_ns());
        total_opportunities += opportunities.size();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    double avg_latency_ns = static_cast<double>(duration.count()) / num_checks;

    std::cout << "\n=== Arbitrage Detection Performance ===" << std::endl;
    std::cout << "Arbitrage checks: " << num_checks << std::endl;
    std::cout << "Total opportunities found: " << total_opportunities << std::endl;
    std::cout << "Average latency per check: " << static_cast<int>(avg_latency_ns) << " ns" << std::endl;
    std::cout << "Checks per second: " << static_cast<int>(num_checks / (duration.count() / 1e9)) << std::endl;
    std::cout << "======================================" << std::endl;
}

int main()
{
    std::cout << "ArbiSim Performance Tests\n"
              << std::endl;

    test_orderbook_performance();
    test_arbitrage_detection_performance();

    std::cout << "\nAll performance tests completed!" << std::endl;
    return 0;
}
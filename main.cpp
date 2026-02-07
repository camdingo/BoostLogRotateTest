#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/attributes/named_scope.hpp>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <iostream>

namespace logging = boost::log;
namespace src = boost::log::sources;
namespace sinks = boost::log::sinks;
namespace keywords = boost::log::keywords;
namespace attrs = boost::log::attributes;

// Configuration
const size_t ROTATION_SIZE = 100  * 1024;  // 100MB like your production
const int NUM_THREADS = 4;                        // Simulate concurrent logging
const bool USE_ASYNC = false;                     // Toggle async vs sync sink
const bool AUTO_FLUSH = true;                     // Toggle auto flush

std::atomic<bool> rotation_detected(false);
std::atomic<int> rotation_count(0);

// Custom rotation handler to detect when rotation occurs
void on_rotation(sinks::text_file_backend::stream_type& stream) {
    rotation_count++;
    rotation_detected = true;
    std::cout << "\n!!! ROTATION #" << rotation_count << " DETECTED !!!" << std::endl;
    std::cout << "    Time: " << std::chrono::system_clock::now().time_since_epoch().count() << std::endl;
    std::cout << "    Thread: " << std::this_thread::get_id() << std::endl;
    std::cout << std::flush;
}

void init_logging() {
    logging::add_common_attributes();
    logging::core::get()->add_global_attribute("Scope", attrs::named_scope());

    typedef sinks::synchronous_sink<sinks::text_file_backend> sync_sink_t;
    typedef sinks::asynchronous_sink<sinks::text_file_backend> async_sink_t;

    if (USE_ASYNC) {
        std::cout << "Using ASYNC sink" << std::endl;
        auto backend = boost::make_shared<sinks::text_file_backend>(
            keywords::file_name = "app.log",
            keywords::rotation_size = ROTATION_SIZE,
            keywords::auto_flush = AUTO_FLUSH
        );

        auto sink = boost::make_shared<async_sink_t>(backend);
        
        // Use simpler formatter to avoid date_time formatting issues
        sink->set_formatter(
            logging::expressions::stream
                << "[" << logging::expressions::attr<unsigned int>("LineID")
                << "] [" << logging::trivial::severity
                << "] [TID:" << logging::expressions::attr<attrs::current_thread_id::value_type>("ThreadID")
                << "] " << logging::expressions::smessage
        );
        
        logging::core::get()->add_sink(sink);
    } else {
        std::cout << "Using SYNC sink" << std::endl;
        auto backend = boost::make_shared<sinks::text_file_backend>(
            keywords::file_name = "app.log",
            keywords::rotation_size = ROTATION_SIZE,
            keywords::auto_flush = AUTO_FLUSH
        );

        // Set rotation callback
        backend->set_close_handler(&on_rotation);

        auto sink = boost::make_shared<sync_sink_t>(backend);
        
        // Use simpler formatter to avoid date_time formatting issues
        sink->set_formatter(
            logging::expressions::stream
                << "[" << logging::expressions::attr<unsigned int>("LineID")
                << "] [" << logging::trivial::severity
                << "] [TID:" << logging::expressions::attr<attrs::current_thread_id::value_type>("ThreadID")
                << "] " << logging::expressions::smessage
        );
        
        logging::core::get()->add_sink(sink);
    }
}

void logging_thread(int thread_id, std::atomic<bool>& stop_flag) {
    src::severity_logger<logging::trivial::severity_level> lg;
    
    int counter = 0;
    while (!stop_flag) {
        // Generate varied log messages to simulate real workload
        BOOST_LOG_SEV(lg, logging::trivial::info) 
            << "[Thread-" << thread_id << "] Message #" << counter++ 
            << " - This is a sample log entry with some data to fill up the log file faster. "
            << "Adding more text here to increase the message size and trigger rotation sooner.";
        
        if (counter % 10 == 0) {
            BOOST_LOG_SEV(lg, logging::trivial::debug) 
                << "[Thread-" << thread_id << "] Debug checkpoint at message " << counter;
        }
        
        if (counter % 100 == 0) {
            BOOST_LOG_SEV(lg, logging::trivial::warning) 
                << "[Thread-" << thread_id << "] Warning: Processed " << counter << " messages";
        }

        // Check if rotation happened
        bool expected = true;
        if (rotation_detected.compare_exchange_strong(expected, false)) {
            std::cout << "[Thread-" << thread_id << "] Observed rotation notification" << std::endl;
        }

        // Simulate some work (adjust this to control log rate)
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "Boost.Log Rotation Deadlock Tester" << std::endl;
    std::cout << "====================================" << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Rotation size: " << ROTATION_SIZE / (1024*1024) << " MB" << std::endl;
    std::cout << "  Number of threads: " << NUM_THREADS << std::endl;
    std::cout << "  Sink type: " << (USE_ASYNC ? "ASYNC" : "SYNC") << std::endl;
    std::cout << "  Auto flush: " << (AUTO_FLUSH ? "ON" : "OFF") << std::endl;
    std::cout << "  Log file: app.log" << std::endl;
    std::cout << "====================================" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    std::cout << std::endl;

    init_logging();

    std::atomic<bool> stop_flag(false);
    std::vector<std::thread> threads;

    // Spawn worker threads
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(logging_thread, i, std::ref(stop_flag));
    }

    // Monitor thread - watch for hangs
    std::thread monitor([&]() {
        auto last_rotation = rotation_count.load();
        auto last_check = std::chrono::steady_clock::now();
        
        while (!stop_flag) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            auto current_rotation = rotation_count.load();
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_check).count();
            
            if (current_rotation > last_rotation) {
                std::cout << "[MONITOR] Rotation completed successfully after " 
                          << elapsed << " seconds" << std::endl;
                last_rotation = current_rotation;
            }
            last_check = now;
        }
    });

    // Wait for threads
    for (auto& t : threads) {
        t.join();
    }
    
    stop_flag = true;
    monitor.join();

    return 0;
}
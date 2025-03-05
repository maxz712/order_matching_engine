#pragma once

#include <map>
#include <deque>
#include <functional>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <boost/asio.hpp>

namespace MatchingEngine
{
// A SessionId to identify each TCP connection
using SessionId = uint64_t;

// Order types
enum class OrderType { Market, Limit, StopLoss };

// Each incoming order
struct Order
{
    uint64_t id;
    bool isBuy;
    OrderType type;
    double price;       // limit or trigger price
    double stopPrice;   // used if type == StopLoss
    uint64_t quantity;

    // Which session placed this order
    SessionId sessionId;

    std::chrono::steady_clock::time_point timestamp;

    Order(uint64_t _id, bool buy, OrderType _type, double p, double sp, uint64_t qty, SessionId sid)
      : id(_id)
      , isBuy(buy)
      , type(_type)
      , price(p)
      , stopPrice(sp)
      , quantity(qty)
      , sessionId(sid)
      , timestamp(std::chrono::steady_clock::now())
    {}
};

// When two orders match, we generate a Fill event
struct Fill
{
    uint64_t makerOrderId;
    uint64_t takerOrderId;

    // maker/taker sessions (so we know who to notify)
    SessionId makerSession;
    SessionId takerSession;

    double price;
    uint64_t quantity;
    // perspective of the taker: is the taker buying?
    bool isBuy;
};

// Forward declare the engine so OrderBook can refer to it
class MatchingEngine;

// The OrderBook manages the in-memory buy/sell lists for a single instrument
class OrderBook
{
public:
    // pass pointer to the parent engine for fill notifications
    explicit OrderBook(MatchingEngine* parent);

    // Add an order to the book; returns a list of fills that occurred
    std::vector<Fill> addOrder(Order&& order);

private:
    using PriceLevel = std::deque<Order>;

    // For buy side, we sort in descending price
    struct Descending
    {
        bool operator()(double a, double b) const { return a > b; }
    };

    // The buy book (keyed descending) and sell book (ascending)
    std::map<double, PriceLevel, Descending> buyBook;
    std::map<double, PriceLevel> sellBook;

    // We keep stop orders off-book until triggered
    std::vector<Order> stopOrders;

    double lastTradePrice = 0.0;          // track last match price for stop triggers
    MatchingEngine* parentEngine_ = nullptr;
    mutable std::mutex bookMutex;

    std::vector<Fill> matchOrder(Order& incoming);
    void consumeOrder(Order& taker, Order& maker, double matchPrice, std::vector<Fill>& fills);
    void placeLimitOrder(Order&& order);
    std::vector<Fill> checkStopOrders(double tradedPrice);
};

// The main MatchingEngine class
class MatchingEngine
{
public:
    MatchingEngine();
    ~MatchingEngine();

    void start();
    void stop();

    // The interface to place a new order
    void submitOrder(Order&& order);

    // Register/unregister session callbacks for fill notifications
    void registerSession(SessionId sid, std::function<void(const Fill&)>&& cb);
    void unregisterSession(SessionId sid);

    // Called by OrderBook to distribute fill events
    void notifyFills(const std::vector<Fill>& fills);

private:
    // A single OrderBook for demonstration
    OrderBook book_;

    // concurrency
    std::atomic<bool> running_;
    std::thread matchingThread_;
    std::mutex queueMutex_;
    std::condition_variable cv_;

    struct OrderMsg { Order order; };
    std::deque<OrderMsg> orderQueue_;

    // callbacks for real-time fill notifications
    std::unordered_map<SessionId, std::function<void(const Fill&)>> sessionCallbacks_;
    std::mutex callbackMutex_;

    void matchingLoop();
};

} // namespace MatchingEngine

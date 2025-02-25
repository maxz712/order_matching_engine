#pragma once

#include <map>
#include <deque>
#include <string>
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
using SessionId = uint64_t;

enum class OrderType { Market, Limit, StopLoss };

struct Order
{
    uint64_t id;
    std::string user;
    OrderType type;
    bool isBuy;
    double price;
    double stopPrice;
    uint64_t quantity;

    SessionId sessionId;

    std::chrono::steady_clock::time_point timestamp;

    Order(uint64_t id_,
          const std::string& usr,
          OrderType t,
          bool buy,
          double p,
          double sp,
          uint64_t qty,
          SessionId sid)
        : id(id_),
          user(usr),
          type(t),
          isBuy(buy),
          price(p),
          stopPrice(sp),
          quantity(qty),
          sessionId(sid),
          timestamp(std::chrono::steady_clock::now())
    {}
};

struct Fill
{
    uint64_t makerOrderId;
    uint64_t takerOrderId;
    SessionId makerSession;
    SessionId takerSession;

    double price;
    uint64_t quantity;
    bool isBuy;
};

struct UserAuth
{
    std::string username;
    std::string password;
};

class MatchingEngine;

// =========================
//  OrderBook
// =========================

class OrderBook
{
public:
    OrderBook(MatchingEngine* parent);

    std::vector<Fill> addOrder(Order&& order);

    std::vector<Fill> checkStopOrders(double lastTradePrice);

    double bestBid() const;
    double bestAsk() const;

private:
    using PriceLevel = std::deque<Order>;

    struct Descending
    {
        bool operator()(double a, double b) const { return a > b; }
    };

    std::map<double, PriceLevel, Descending> buyBook;
    std::map<double, PriceLevel> sellBook;
    std::vector<Order> stopOrders;
    mutable std::mutex bookMutex;

    double lastTradePrice;

    MatchingEngine* parentEngine_;

    std::vector<Fill> matchOrder(Order& incoming);
    void consumeOrder(Order& incoming, Order& existing, double matchPrice, std::vector<Fill>& fills);

    void placeLimitOrder(Order&& order);
};

// =========================
//  MatchingEngine
// =========================

class MatchingEngine
{
public:
    MatchingEngine();
    ~MatchingEngine();

    void start();
    void stop();

    std::vector<Fill> onNewOrder(Order&& order);

    void addUser(const std::string& user, const std::string& pass);
    bool authenticate(const std::string& user, const std::string& pass) const;

    void registerSession(SessionId sid, std::function<void(const Fill&)>&& callback);
    void unregisterSession(SessionId sid);

    void notifyFills(const std::vector<Fill>& fills);

private:
    OrderBook book;

    std::atomic<bool> running;
    std::thread matchingThread;
    std::condition_variable cv;
    std::mutex queueMutex, engineMutex;

    struct OrderMessage
    {
        Order order;
    };
    std::deque<OrderMessage> orderQueue;

    std::unordered_map<std::string, UserAuth> users;
    mutable std::mutex userMutex;

    std::unordered_map<SessionId, std::function<void(const Fill&)>> sessionCallbacks_;
    std::mutex callbackMutex_;

    void matchingLoop();
};

}

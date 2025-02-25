#include "matching_engine.hpp"

namespace MatchingEngine
{

// =============
// OrderBook
// =============

OrderBook::OrderBook(MatchingEngine* parent)
    : lastTradePrice(0.0),
      parentEngine_(parent)
{
}

std::vector<Fill> OrderBook::addOrder(Order&& order)
{
    std::lock_guard<std::mutex> lock(bookMutex);
    std::vector<Fill> fills;

    if (order.type == OrderType::StopLoss)
    {
        stopOrders.push_back(std::move(order));
        return fills;
    }

    fills = matchOrder(order);

    if (order.type == OrderType::Limit && order.quantity > 0)
    {
        placeLimitOrder(std::move(order));
    }

    for (auto& f : fills)
    {
        lastTradePrice = f.price;
    }
    auto stopFills = checkStopOrders(lastTradePrice);
    fills.insert(fills.end(), stopFills.begin(), stopFills.end());

    return fills;
}

std::vector<Fill> OrderBook::checkStopOrders(double tradedPrice)
{
    std::vector<Fill> allFills;
    auto it = stopOrders.begin();
    while (it != stopOrders.end())
    {
        bool triggered = false;
        if (it->isBuy)
        {
            if (tradedPrice >= it->stopPrice) {
                triggered = true;
            }
        }
        else
        {
            if (tradedPrice <= it->stopPrice) {
                triggered = true;
            }
        }

        if (triggered)
        {
            Order triggeredOrder(it->id, it->user, OrderType::Market,
                                 it->isBuy, 0.0, 0.0, it->quantity, it->sessionId);

            auto fills = matchOrder(triggeredOrder);
            for (auto& f : fills) {
                lastTradePrice = f.price;
            }
            allFills.insert(allFills.end(), fills.begin(), fills.end());

            it = stopOrders.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return allFills;
}

std::vector<Fill> OrderBook::matchOrder(Order& incoming)
{
    std::vector<Fill> fills;

    if (incoming.isBuy)
    {
        while (incoming.quantity > 0 && !sellBook.empty())
        {
            auto bestSellIt = sellBook.begin();
            double bestSellPrice = bestSellIt->first;
            if (incoming.type == OrderType::Limit && incoming.price < bestSellPrice) {
                break;
            }
            auto& level = bestSellIt->second;
            while (incoming.quantity > 0 && !level.empty())
            {
                auto& sellOrder = level.front();
                double matchPrice = sellOrder.price;
                consumeOrder(incoming, sellOrder, matchPrice, fills);

                if (sellOrder.quantity == 0) {
                    level.pop_front();
                }
                if (incoming.quantity == 0) {
                    break;
                }
            }
            if (level.empty()) {
                sellBook.erase(bestSellIt);
            }
        }
    }
    else
    {
        while (incoming.quantity > 0 && !buyBook.empty())
        {
            auto bestBuyIt = buyBook.begin();
            double bestBuyPrice = bestBuyIt->first;
            if (incoming.type == OrderType::Limit && incoming.price > bestBuyPrice) {
                break;
            }
            auto& level = bestBuyIt->second;
            while (incoming.quantity > 0 && !level.empty())
            {
                auto& buyOrder = level.front();
                double matchPrice = buyOrder.price;
                consumeOrder(buyOrder, incoming, matchPrice, fills);

                if (buyOrder.quantity == 0) {
                    level.pop_front();
                }
                if (incoming.quantity == 0) {
                    break;
                }
            }
            if (level.empty()) {
                buyBook.erase(bestBuyIt);
            }
        }
    }
    return fills;
}

void OrderBook::consumeOrder(Order& incoming, Order& existing, double matchPrice, std::vector<Fill>& fills)
{
    uint64_t tradedQty = std::min(incoming.quantity, existing.quantity);

    incoming.quantity -= tradedQty;
    existing.quantity -= tradedQty;

    Fill fill;
    fill.makerOrderId = existing.id;
    fill.makerSession = existing.sessionId;
    fill.takerOrderId = incoming.id;
    fill.takerSession = incoming.sessionId;
    fill.price = matchPrice;
    fill.quantity = tradedQty;
    fill.isBuy = incoming.isBuy;

    fills.push_back(fill);
}

void OrderBook::placeLimitOrder(Order&& order)
{
    if (order.isBuy)
    {
        auto it = buyBook.find(order.price);
        if (it == buyBook.end()) {
            buyBook[order.price] = PriceLevel{};
            it = buyBook.find(order.price);
        }
        it->second.push_back(std::move(order));
    }
    else
    {
        auto it = sellBook.find(order.price);
        if (it == sellBook.end()) {
            sellBook[order.price] = PriceLevel{};
            it = sellBook.find(order.price);
        }
        it->second.push_back(std::move(order));
    }
}

double OrderBook::bestBid() const
{
    std::lock_guard<std::mutex> lock(bookMutex);
    if (buyBook.empty()) return 0.0;
    return buyBook.begin()->first;
}

double OrderBook::bestAsk() const
{
    std::lock_guard<std::mutex> lock(bookMutex);
    if (sellBook.empty()) return 0.0;
    return sellBook.begin()->first;
}


// =============
// MatchingEngine
// =============

MatchingEngine::MatchingEngine()
    : book(this),
      running(false)
{
}

MatchingEngine::~MatchingEngine()
{
    stop();
}

void MatchingEngine::start()
{
    running.store(true);
    matchingThread = std::thread([this] { matchingLoop(); });
}

void MatchingEngine::stop()
{
    running.store(false);
    cv.notify_one();
    if (matchingThread.joinable())
        matchingThread.join();
}

std::vector<Fill> MatchingEngine::onNewOrder(Order&& order)
{
    std::unique_lock<std::mutex> lock(queueMutex);
    orderQueue.push_back(OrderMessage{ std::move(order) });
    cv.notify_one();
    return {};
}

void MatchingEngine::addUser(const std::string& user, const std::string& pass)
{
    std::lock_guard<std::mutex> lock(userMutex);
    users[user] = UserAuth{user, pass};
}

bool MatchingEngine::authenticate(const std::string& user, const std::string& pass) const
{
    std::lock_guard<std::mutex> lock(userMutex);
    auto it = users.find(user);
    if (it == users.end()) return false;
    return (it->second.password == pass);
}

void MatchingEngine::matchingLoop()
{
    while (running.load())
    {
        std::deque<OrderMessage> localQueue;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            cv.wait(lock, [this] {
                return !running.load() || !orderQueue.empty();
            });
            if (!running.load() && orderQueue.empty())
                break;
            localQueue.swap(orderQueue);
        }

        while (!localQueue.empty())
        {
            auto msg = std::move(localQueue.front());
            localQueue.pop_front();

            std::vector<Fill> fills;
            {
                std::lock_guard<std::mutex> lock(engineMutex);
                fills = book.addOrder(std::move(msg.order));
            }
            if (!fills.empty())
            {
                notifyFills(fills);
            }
        }
    }
}

// =============
// Fill Callbacks
// =============
void MatchingEngine::registerSession(SessionId sid, std::function<void(const Fill&)>&& callback)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    sessionCallbacks_[sid] = std::move(callback);
}

void MatchingEngine::unregisterSession(SessionId sid)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    sessionCallbacks_.erase(sid);
}

void MatchingEngine::notifyFills(const std::vector<Fill>& fills)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);

    for (auto& fill : fills)
    {
        auto makerIt = sessionCallbacks_.find(fill.makerSession);
        if (makerIt != sessionCallbacks_.end()) {
            makerIt->second(fill);
        }
        auto takerIt = sessionCallbacks_.find(fill.takerSession);
        if (takerIt != sessionCallbacks_.end()) {
            takerIt->second(fill);
        }
    }
}

}

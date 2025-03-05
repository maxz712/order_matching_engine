#include "matching_engine.hpp"
#include <algorithm>
#include <iostream>

namespace MatchingEngine
{

// ===================
// OrderBook
// ===================

OrderBook::OrderBook(MatchingEngine* parent)
  : parentEngine_(parent)
{
}

std::vector<Fill> OrderBook::addOrder(Order&& order)
{
    std::lock_guard<std::mutex> lock(bookMutex);

    std::vector<Fill> fills;
    if (order.type == OrderType::StopLoss)
    {
        // store stop order for future triggers
        stopOrders.push_back(std::move(order));
        return fills;
    }

    fills = matchOrder(order);

    // if it's a limit order and there's leftover quantity, place it
    if (order.type == OrderType::Limit && order.quantity > 0)
    {
        placeLimitOrder(std::move(order));
    }

    // update last trade price from any fills
    for (auto& f : fills)
    {
        lastTradePrice = f.price;
    }

    // now see if these fills triggered any stop orders
    auto triggeredFills = checkStopOrders(lastTradePrice);
    fills.insert(fills.end(), triggeredFills.begin(), triggeredFills.end());

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
            // stop buy triggers if trade price >= stopPrice
            if (tradedPrice >= it->stopPrice) {
                triggered = true;
            }
        }
        else
        {
            // stop sell triggers if trade price <= stopPrice
            if (tradedPrice <= it->stopPrice) {
                triggered = true;
            }
        }

        if (triggered)
        {
            // convert to a market order
            Order triggeredOrder(it->id, it->isBuy, OrderType::Market,
                                 0.0, 0.0, it->quantity, it->sessionId);
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
        // match buy against best sells
        while (incoming.quantity > 0 && !sellBook.empty())
        {
            auto bestSellIt = sellBook.begin(); // lowest price
            double bestSellPrice = bestSellIt->first;
            if (incoming.type == OrderType::Limit && incoming.price < bestSellPrice) {
                break; // no match
            }
            auto& level = bestSellIt->second;
            while (incoming.quantity > 0 && !level.empty())
            {
                auto& maker = level.front();
                double matchPrice = maker.price;
                consumeOrder(incoming, maker, matchPrice, fills);
                if (maker.quantity == 0) {
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
        // match sell against best buys
        while (incoming.quantity > 0 && !buyBook.empty())
        {
            auto bestBuyIt = buyBook.begin(); // highest price first
            double bestBuyPrice = bestBuyIt->first;
            if (incoming.type == OrderType::Limit && incoming.price > bestBuyPrice) {
                break; // no match
            }
            auto& level = bestBuyIt->second;
            while (incoming.quantity > 0 && !level.empty())
            {
                auto& maker = level.front();
                double matchPrice = maker.price;
                consumeOrder(incoming, maker, matchPrice, fills);
                if (maker.quantity == 0) {
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

void OrderBook::consumeOrder(Order& taker, Order& maker, double matchPrice, std::vector<Fill>& fills)
{
    uint64_t traded = std::min(taker.quantity, maker.quantity);
    taker.quantity -= traded;
    maker.quantity -= traded;

    Fill fill;
    fill.takerOrderId   = taker.id;
    fill.takerSession   = taker.sessionId;
    fill.makerOrderId   = maker.id;
    fill.makerSession   = maker.sessionId;
    fill.price          = matchPrice;
    fill.quantity       = traded;
    fill.isBuy          = taker.isBuy; // from the taker's perspective

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

// ===================
// MatchingEngine
// ===================

MatchingEngine::MatchingEngine()
  : book_(this),
    running_(false)
{
}

MatchingEngine::~MatchingEngine()
{
    stop();
}

void MatchingEngine::start()
{
    running_.store(true);
    matchingThread_ = std::thread([this]{ matchingLoop(); });
}

void MatchingEngine::stop()
{
    running_.store(false);
    cv_.notify_one();
    if (matchingThread_.joinable()) {
        matchingThread_.join();
    }
}

void MatchingEngine::matchingLoop()
{
    while (running_.load())
    {
        std::deque<OrderMsg> localQueue;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            cv_.wait(lock, [this] {
                return !running_.load() || !orderQueue_.empty();
            });
            if (!running_.load() && orderQueue_.empty()) {
                break;
            }
            localQueue.swap(orderQueue_);
        }

        // Process orders
        while (!localQueue.empty())
        {
            auto msg = std::move(localQueue.front());
            localQueue.pop_front();

            std::vector<Fill> fills = book_.addOrder(std::move(msg.order));
            if (!fills.empty()) {
                notifyFills(fills);
            }
        }
    }
}

void MatchingEngine::submitOrder(Order&& order)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        orderQueue_.push_back(OrderMsg{std::move(order)});
    }
    cv_.notify_one();
}

// session callbacks
void MatchingEngine::registerSession(SessionId sid, std::function<void(const Fill&)>&& cb)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    sessionCallbacks_[sid] = std::move(cb);
}

void MatchingEngine::unregisterSession(SessionId sid)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    sessionCallbacks_.erase(sid);
}

void MatchingEngine::notifyFills(const std::vector<Fill>& fills)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    for (auto& f : fills)
    {
        // notify maker
        auto mit = sessionCallbacks_.find(f.makerSession);
        if (mit != sessionCallbacks_.end()) {
            mit->second(f);
        }
        // notify taker
        auto tit = sessionCallbacks_.find(f.takerSession);
        if (tit != sessionCallbacks_.end()) {
            tit->second(f);
        }
    }
}

} // namespace MatchingEngine

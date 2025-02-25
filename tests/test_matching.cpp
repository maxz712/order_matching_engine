#include <gtest/gtest.h>
#include "matching_engine.hpp"
#include <string>

class DummyEngine : public MatchingEngine::MatchingEngine
{
public:
    DummyEngine() = default;
};

TEST(OrderBookTest, LimitOrderMatch)
{
    DummyEngine dummy;
    MatchingEngine::OrderBook ob(&dummy);

    MatchingEngine::Order buyOrder(1, "Alice", MatchingEngine::OrderType::Limit, true,
                   100.0, 0.0, 50, 10);
    auto fills1 = ob.addOrder(std::move(buyOrder));
    EXPECT_TRUE(fills1.empty());

    MatchingEngine::Order sellOrder(2, "Bob", MatchingEngine::OrderType::Limit, false,
                    99.0, 0.0, 50, 20);
    auto fills2 = ob.addOrder(std::move(sellOrder));

    ASSERT_EQ(fills2.size(), 1u);
    EXPECT_EQ(fills2[0].quantity, 50u);
    EXPECT_EQ(fills2[0].price, 100.0);
    EXPECT_EQ(ob.bestBid(), 0.0);
    EXPECT_EQ(ob.bestAsk(), 0.0);
}

TEST(OrderBookTest, PartialFill)
{
    DummyEngine dummy;
    MatchingEngine::OrderBook ob(&dummy);

    MatchingEngine::Order buyOrder(1, "Alice", MatchingEngine::OrderType::Limit, true,
                   100.0, 0.0, 100, 10);
    ob.addOrder(std::move(buyOrder));

    MatchingEngine::Order sellOrder(2, "Bob", MatchingEngine::OrderType::Limit, false,
                    99.0, 0.0, 50, 20);
    auto fills = ob.addOrder(std::move(sellOrder));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].quantity, 50u);
    EXPECT_EQ(fills[0].price, 100.0);

    EXPECT_EQ(ob.bestBid(), 100.0);
    EXPECT_EQ(ob.bestAsk(), 0.0);
}

TEST(OrderBookTest, MarketOrderMatch)
{
    DummyEngine dummy;
    MatchingEngine::OrderBook ob(&dummy);

    ob.addOrder(MatchingEngine::Order(1, "Bob", MatchingEngine::OrderType::Limit, false,
                      101.0, 0.0, 50, 20));

    auto fills = ob.addOrder(MatchingEngine::Order(2, "Alice", MatchingEngine::OrderType::Market, true,
                                   0.0, 0.0, 20, 10));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].quantity, 20u);
    EXPECT_EQ(fills[0].price, 101.0);

    EXPECT_EQ(ob.bestAsk(), 101.0);
}

TEST(OrderBookTest, StopOrderTrigger)
{
    DummyEngine dummy;
    MatchingEngine::OrderBook ob(&dummy);

    ob.addOrder(MatchingEngine::Order(1, "Alice", MatchingEngine::OrderType::Limit, true,
                      100.0, 0.0, 50, 10));

    ob.addOrder(MatchingEngine::Order(2, "Bob", MatchingEngine::OrderType::StopLoss, false,
                      0.0, 101.0, 30, 20));

    auto fills = ob.addOrder(MatchingEngine::Order(3, "Carol", MatchingEngine::OrderType::Limit, false,
                                   100.0, 0.0, 10, 30));
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].price, 100.0);

    double bid = ob.bestBid();

    EXPECT_EQ(bid, 100.0);

    EXPECT_TRUE(true);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

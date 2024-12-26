#ifndef ORDER_H
#define ORDER_H

#include <atomic>
#include <string>

struct Order {
    enum class Side {
        BUY,
        SELL
    };

    int order_id;
    Side side;
    std::string ticker;
    double price;
    int quantity;
    long long timestamp;
    bool is_cancelled = false;
    static std::atomic<int> next_order_id;

    Order(Side s, const std::string& tk, double p, int q, long long ts);

    static int generate_order_id();

    void cancel_order();
};

#endif //ORDER_H

#include <atomic>

struct Order {
    enum class Side {
        BUY,
        SELL
    };

    enum class Type {
        LIMIT,
        MARKET,
        STOP
    };

    int order_id;
    Side side;
    Type type;
    double price;
    double trigger_price;
    int quantity;
    long long timestamp;

    static std::atomic<int> next_order_id;

    Order(Side s, Type t, double p, double tp, int q, long long ts)
        : order_id(generate_order_id()), side(s), type(t), price(p), trigger_price(tp), quantity(q), timestamp(ts) {}

    static int generate_order_id() {
        return next_order_id++;
    }
};

std::atomic<int> Order::next_order_id(0);

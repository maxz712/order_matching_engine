#include <atomic>
#include <string>

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
    std::string ticker;
    double price;
    double trigger_price;
    int quantity;
    int remainingQuantity;
    long long timestamp;
    bool is_cancelled = false;
    bool is_fully_executed = false;
    static std::atomic<int> next_order_id;

    Order(Side s, Type t, double p, double tp, int q, long long ts)
        : order_id(generate_order_id()), side(s), type(t), price(p), trigger_price(tp), quantity(q), timestamp(ts) {}

    static int generate_order_id() {
        return next_order_id++;
    }

    void execute(int executed_quantity) {
        remainingQuantity -= executed_quantity;
        if (remainingQuantity == 0) {
            is_fully_executed = true;
        }
    }

    bool is_partially_executed() const {
        return !is_fully_executed && remainingQuantity < quantity;
    }

    bool is_completely_executed() const {
        return is_fully_executed;
    }

};

std::atomic<int> Order::next_order_id(0);

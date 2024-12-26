#include "order.h"

std::atomic<int> Order::next_order_id(0);

Order::Order(Side s, const std::string& tk, double p, int q, long long ts)
    : order_id(generate_order_id()), side(s), ticker(tk), price(p), quantity(q), timestamp(ts) {}

int Order::generate_order_id() {
    return next_order_id++;
}

void Order::cancel_order() {
    is_cancelled = true;
}

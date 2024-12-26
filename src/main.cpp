#include "order/order.h"
#include <iostream>

int main() {
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    Order order1(Order::Side::BUY, "APPL", 101.1, 3, now);
    std::cout << "Buy Order ID: " << order1.order_id << "\n";
    return 0;
}

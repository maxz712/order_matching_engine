#include "matching_engine.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <sstream>
#include <atomic>
#include <memory>
#include <iostream>

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;

namespace Server
{
static std::atomic<uint64_t> gSessionIdCounter{1};

class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(tcp::socket socket,
            ssl::context& sslContext,
            MatchingEngine::MatchingEngine& engine)
      : sslStream_(std::move(socket), sslContext)
      , engine_(engine)
      , sessionId_(gSessionIdCounter.fetch_add(1))
    {
    }

    void start()
    {
        // Register fill callback
        engine_.registerSession(sessionId_, [weakSelf = weak_from_this()](const MatchingEngine::Fill& fill){
            if (auto self = weakSelf.lock()) {
                self->onFill(fill);
            }
        });

        // Async SSL handshake
        auto self(shared_from_this());
        sslStream_.async_handshake(ssl::stream_base::server,
            [this, self](const boost::system::error_code& ec){
                if (!ec) {
                    doRead();
                } else {
                    std::cerr << "Handshake failed: " << ec.message() << std::endl;
                }
            });
    }

    void stop()
    {
        engine_.unregisterSession(sessionId_);
        // Close socket
        boost::system::error_code ignored;
        sslStream_.lowest_layer().close(ignored);
    }

private:
    // Continuously read lines (commands)
    void doRead()
    {
        auto self(shared_from_this());
        boost::asio::async_read_until(sslStream_, buffer_, '\n',
            [this, self](boost::system::error_code ec, std::size_t length){
                if (!ec) {
                    std::string line(
                        boost::asio::buffers_begin(buffer_.data()),
                        boost::asio::buffers_begin(buffer_.data()) + length
                    );
                    buffer_.consume(length);

                    processLine(line);
                    doRead();  // read the next command
                }
                else {
                    stop();
                }
            });
    }

    // parse an order command, e.g.:
    // ORDER buy limit 100.0 10
    // ORDER sell stop 101 20
    // ORDER buy market 0 15
    void processLine(const std::string& line)
    {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "ORDER")
        {
            std::string sideStr, typeStr;
            double price;
            uint64_t qty;

            iss >> sideStr >> typeStr >> price >> qty;

            bool isBuy = (sideStr == "buy");
            MatchingEngine::OrderType ot = MatchingEngine::OrderType::Limit;
            double stopP = 0.0;

            if (typeStr == "market")
            {
                ot = MatchingEngine::OrderType::Market;
                // price is not used, but we read it anyway
            }
            else if (typeStr == "stop")
            {
                ot = MatchingEngine::OrderType::StopLoss;
                stopP = price; // put the price as stopPrice
                price = 0.0;
            }
            else if (typeStr == "limit")
            {
                ot = MatchingEngine::OrderType::Limit;
            }

            static std::atomic<uint64_t> globalOrderId{1};
            uint64_t thisOrderId = globalOrderId.fetch_add(1);

            // Construct order
            MatchingEngine::Order order(
                thisOrderId,
                isBuy,
                ot,
                price,
                stopP,
                qty,
                sessionId_
            );
            // Submit to engine
            engine_.submitOrder(std::move(order));

            writeLine("ORDER ACCEPTED\n");
        }
        else
        {
            writeLine("Unknown command\n");
        }
    }

    // Called by the engine when this session's order is filled
    void onFill(const MatchingEngine::Fill& fill)
    {
        // Format a message
        std::ostringstream ss;
        ss << "FILL: maker=" << fill.makerOrderId
           << " taker=" << fill.takerOrderId
           << " price=" << fill.price
           << " qty=" << fill.quantity
           << " isBuy=" << (fill.isBuy ? "true" : "false") << "\n";
        writeLine(ss.str());
    }

    void writeLine(const std::string& msg)
    {
        auto self(shared_from_this());
        boost::asio::async_write(sslStream_,
            boost::asio::buffer(msg),
            [this, self](boost::system::error_code ec, std::size_t){
                if (ec) {
                    stop();
                }
            });
    }

private:
    ssl::stream<tcp::socket> sslStream_;
    boost::asio::streambuf buffer_;
    MatchingEngine::MatchingEngine& engine_;
    MatchingEngine::SessionId sessionId_;
};

class Server
{
public:
    Server(boost::asio::io_context& ioc, unsigned short port,
           ssl::context& sslContext,
           MatchingEngine::MatchingEngine& engine)
      : acceptor_(ioc, tcp::endpoint(tcp::v4(), port))
      , sslContext_(sslContext)
      , engine_(engine)
    {
        doAccept();
    }

private:
    void doAccept()
    {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket){
                if (!ec) {
                    auto session = std::make_shared<Session>(
                        std::move(socket), sslContext_, engine_);
                    session->start();
                }
                doAccept();
            });
    }

    tcp::acceptor acceptor_;
    ssl::context& sslContext_;
    MatchingEngine::MatchingEngine& engine_;
};

} // namespace Server

#include "matching_engine.hpp"
#include <iostream>
#include <sstream>
#include <atomic>
#include <memory>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;

namespace Server
{
static std::atomic<uint64_t> gSessionCounter{1};

class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(tcp::socket socket,
            ssl::context& sslContext,
            MatchingEngine::MatchingEngine& engine)
        : sslStream_(std::move(socket), sslContext)
        , engine_(engine)
        , sessionId_(gSessionCounter.fetch_add(1))
    {
    }

    void start()
    {
        engine_.registerSession(sessionId_, [thisWeak = weak_from_this()](const MatchingEngine::Fill& fill)
        {
            if (auto self = thisWeak.lock())
            {
                self->onFill(fill);
            }
        });

        auto self(shared_from_this());
        sslStream_.async_handshake(ssl::stream_base::server,
            [this, self](const boost::system::error_code& ec)
            {
                if (!ec)
                {
                    doRead();
                }
                else
                {
                    std::cerr << "Handshake failed: " << ec.message() << std::endl;
                }
            });
    }

    void stop()
    {
        engine_.unregisterSession(sessionId_);

        boost::system::error_code ignored_ec;
        sslStream_.lowest_layer().close(ignored_ec);
    }

private:
    void doRead()
    {
        auto self(shared_from_this());
        boost::asio::async_read_until(sslStream_, buffer_, '\n',
            [this, self](boost::system::error_code ec, std::size_t length)
            {
                if (!ec)
                {
                    std::string line(
                        boost::asio::buffers_begin(buffer_.data()),
                        boost::asio::buffers_begin(buffer_.data()) + length
                    );
                    buffer_.consume(length);
                    processLine(line);
                    doRead();
                }
                else
                {
                    if (ec != boost::asio::error::eof)
                    {
                        std::cerr << "Read error: " << ec.message() << std::endl;
                    }
                    stop();
                }
            });
    }

    void processLine(const std::string& line)
    {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "LOGIN")
        {
            std::string user, pass;
            iss >> user >> pass;
            bool ok = engine_.authenticate(user, pass);
            if (ok)
            {
                currentUser_ = user;
                writeLine("LOGIN OK\n");
            }
            else
            {
                writeLine("LOGIN FAILED\n");
            }
        }
        else if (cmd == "ORDER")
        {
            if (currentUser_.empty())
            {
                writeLine("ERROR: Not logged in\n");
                return;
            }
            static uint64_t globalOrderId = 1;
            std::string sideStr, typeStr;
            double price;
            uint64_t qty;
            iss >> sideStr >> typeStr >> price >> qty;

            bool isBuy = (sideStr == "buy");
            MatchingEngine::OrderType ot = MatchingEngine::OrderType::Limit;
            double stopP = 0.0;
            if (typeStr == "market")
                ot = MatchingEngine::OrderType::Market;
            else if (typeStr == "stop")
            {
                ot = MatchingEngine::OrderType::StopLoss;
                stopP = price;
                price = 0.0;
            }

            MatchingEngine::Order order(
                globalOrderId++,
                currentUser_,
                ot,
                isBuy,
                price,
                stopP,
                qty,
                sessionId_
            );
            engine_.onNewOrder(std::move(order));

            writeLine("ORDER ACCEPTED\n");
        }
        else
        {
            writeLine("Unknown command\n");
        }
    }

    void onFill(const MatchingEngine::Fill& fill)
    {
        std::ostringstream ss;
        ss << "FILL: maker=" << fill.makerOrderId
           << " taker=" << fill.takerOrderId
           << " price=" << fill.price
           << " qty=" << fill.quantity
           << " isBuy=" << (fill.isBuy ? "true" : "false")
           << "\n";
        writeLine(ss.str());
    }

    void writeLine(const std::string& msg)
    {
        auto self(shared_from_this());
        boost::asio::async_write(sslStream_, boost::asio::buffer(msg),
            [this, self](boost::system::error_code ec, std::size_t)
            {
                if (ec)
                {
                    std::cerr << "Write error: " << ec.message() << std::endl;
                    stop();
                }
            });
    }

private:
    ssl::stream<tcp::socket> sslStream_;
    boost::asio::streambuf buffer_;
    MatchingEngine::MatchingEngine& engine_;
    MatchingEngine::SessionId sessionId_;
    std::string currentUser_;
};

class Server
{
public:
    Server(boost::asio::io_context& ioc,
           unsigned short port,
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
            [this](boost::system::error_code ec, tcp::socket socket)
            {
                if (!ec)
                {
                    auto session = std::make_shared<Session>(
                        std::move(socket),
                        sslContext_,
                        engine_);
                    session->start();
                }
                doAccept();
            });
    }

    tcp::acceptor acceptor_;
    ssl::context& sslContext_;
    MatchingEngine::MatchingEngine& engine_;
};

}

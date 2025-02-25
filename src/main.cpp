#include "matching_engine.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <iostream>
#include <csignal>

#include "server.cpp"

int main()
{
    namespace ssl = boost::asio::ssl;
    MatchingEngine::MatchingEngine engine;
    engine.addUser("alice", "alicepass");
    engine.addUser("bob",   "bobpass");

    engine.start();

    boost::asio::io_context ioc;
    ssl::context ctx(ssl::context::tls_server);

    ctx.set_options(
        ssl::context::default_workarounds
        | ssl::context::no_sslv2
        | ssl::context::single_dh_use);

    ctx.use_certificate_chain_file("../server.crt");
    ctx.use_private_key_file("../server.key", ssl::context::pem);

    unsigned short port = 12345;
    try
    {
        Server::Server server(ioc, port, ctx, engine);

        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code&, int){ ioc.stop(); });

        std::cout << "Server running on port " << port << std::endl;
        ioc.run();
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    engine.stop();
    return 0;
}

#include "matching_engine.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <csignal>
#include <iostream>

#include "server.cpp"

int main()
{
    // Prepare the matching engine
    MatchingEngine::MatchingEngine engine;
    engine.start();

    // Setup Boost.Asio
    boost::asio::io_context ioc;
    namespace ssl = boost::asio::ssl;

    ssl::context ctx(ssl::context::tls_server);
    // Load self-signed cert, key, etc.
    ctx.use_certificate_chain_file("../server.crt");
    ctx.use_private_key_file("../server.key", ssl::context::pem);
    ctx.set_options(ssl::context::default_workarounds
                    | ssl::context::no_sslv2
                    | ssl::context::single_dh_use);

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

    // Cleanup
    engine.stop();
    return 0;
}

#include "McpHost.h"
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include <mutex>

using boost::asio::ip::tcp;
using json = nlohmann::json;

namespace ed {

struct McpHost::Impl {
    boost::asio::io_context io;
    std::unique_ptr<tcp::acceptor> acceptor;
    std::shared_ptr<tcp::socket> socket;
    boost::asio::streambuf buf;

    mass::McpQueue queue;      // network thread submits, UI thread drains
    // Touched only by the UI thread while draining. It lives across frames so
    // that state it carries between calls — the loaded atlas — survives the
    // client's one-request-per-round-trip pattern.
    mass::McpServer server;
    std::mutex mx;
    std::string lastTool;

    void doAccept() {
        socket = std::make_shared<tcp::socket>(io);
        acceptor->async_accept(*socket, [this](boost::system::error_code ec) {
            if (!ec) doRead(); else doAccept();
        });
    }

    void doRead() {
        boost::asio::async_read_until(*socket, buf, '\n',
            [this](boost::system::error_code ec, std::size_t) {
                if (ec) {
                    boost::system::error_code ig; socket->close(ig);
                    doAccept();
                    return;
                }
                std::istream is(&buf);
                std::string line;
                std::getline(is, line);
                if (line.empty()) { doRead(); return; }

                json req = json::parse(line, nullptr, /*allow_exceptions*/ false);
                if (req.is_discarded()) {
                    reply({ {"jsonrpc","2.0"}, {"id", nullptr},
                            {"error", { {"code",-32700}, {"message","parse error"} }} });
                    doRead();
                    return;
                }
                if (req.value("method", "") == "tools/call") {
                    std::lock_guard<std::mutex> lk(mx);
                    lastTool = req["params"].value("name", "");
                }
                // Park it for the UI thread and block this one until the next
                // frame answers: the client sees a normal synchronous reply.
                std::future<json> fut = queue.submit(std::move(req));
                reply(fut.get());
                doRead();
            });
    }

    void reply(const json& resp) {
        boost::system::error_code ec;
        std::string out = resp.dump() + "\n";
        boost::asio::write(*socket, boost::asio::buffer(out), ec);
    }
};

McpHost::~McpHost() { stop(); }

void McpHost::start(unsigned short port) {
    if (mRunning.load()) return;
    mPort = port;
    mImpl = std::make_shared<Impl>();
    try {
        mImpl->acceptor = std::make_unique<tcp::acceptor>(
            mImpl->io, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), port));
        mImpl->doAccept();
    } catch (const std::exception&) {
        mImpl.reset();
        return;
    }
    mRunning.store(true);
    auto impl = mImpl;
    mThread = std::thread([impl]() { impl->io.run(); });
}

void McpHost::stop() {
    if (mImpl) {
        boost::asio::post(mImpl->io, [this]() {
            if (mImpl->acceptor) { boost::system::error_code ig; mImpl->acceptor->close(ig); }
            if (mImpl->socket)   { boost::system::error_code ig; mImpl->socket->close(ig); }
        });
        mImpl->io.stop();
    }
    if (mThread.joinable()) mThread.join();
    mRunning.store(false);
    mImpl.reset();
}

std::string McpHost::lastTool() {
    if (!mImpl) return {};
    std::lock_guard<std::mutex> lk(mImpl->mx);
    return mImpl->lastTool;
}

int McpHost::poll(Model& model) {
    if (!mImpl || mImpl->queue.pending() == 0) return 0;

    // editor Model -> libmassedit Model, via the .mass JSON both agree on
    std::string err;
    auto converted = mass::Model::FromJsonString(model.ToJsonString(), &err);
    if (!converted) return 0;

    mass::Model work = std::move(*converted);
    work.assignUids();
    mass::Index ix;
    ix.build(work);

    int n = mImpl->queue.drain(mImpl->server, work, ix);
    if (n <= 0) return 0;

    if (auto back = Model::FromJsonString(work.ToJsonString(), &err))
        model = std::move(*back);
    mServed.fetch_add(n);
    return n;
}

} // namespace ed

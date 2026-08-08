#pragma once
// In-process MCP server for the Arena editor.
//
// A background thread accepts newline-delimited JSON-RPC over TCP (the same
// protocol as the standalone gaitnet-mcp) and parks each request on a queue.
// The UI thread drains that queue once per frame, so the model has exactly one
// writer and no locking is needed around it — the single-writer contract
// libmassedit's McpQueue was built for.
//
// The editor and libmassedit hold the same data in two separate `Model` types,
// so a request is served by converting the editor's model to libmassedit's,
// applying the tool, and converting back. The .mass JSON is the interchange
// format, which keeps the two decoupled and makes the conversion lossless.
#include "MassModel.h"
#include <Mcp.h>
#include <Index.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace ed {

class McpHost {
public:
    ~McpHost();

    // Start accepting on `port`. Safe to call when already running (no-op).
    void start(unsigned short port = 8767);
    void stop();

    bool running() const { return mRunning.load(); }
    unsigned short port() const { return mPort; }
    // Requests served since start, and the name of the last tool called.
    int served() const { return mServed.load(); }
    std::string lastTool();

    // UI thread, once per frame: apply every queued request to `model`.
    // Returns the number applied; a non-zero result means the model changed and
    // the caller should refresh whatever it derives from it.
    int poll(Model& model);

private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
    std::thread mThread;
    std::atomic<bool> mRunning{false};
    std::atomic<int> mServed{0};
    unsigned short mPort = 8767;
};

} // namespace ed

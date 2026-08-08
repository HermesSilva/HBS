#pragma once
// libmassedit — MCP server core. Transport-agnostic JSON-RPC dispatch that maps
// MCP tool calls onto the libmassedit operations over a live mass::Model. This is
// the brain both a standalone server and the in-process Arena bridge host.
//
// Co-edit model: mutations must run on one thread (Arena's UI thread). McpQueue
// lets any thread submit a request and get a future; the owner thread drains the
// queue once per frame, applying every request via McpServer on that single
// thread — no locks on the model, no data races.
#include "MassModel.h"
#include "Index.h"
#include "Atlas.h"
#include <nlohmann/json.hpp>
#include <deque>
#include <mutex>
#include <future>
#include <string>

namespace mass {

class McpServer {
public:
    // Handle one JSON-RPC request against the given model+index. Mutating tools
    // rebuild the index. Returns a JSON-RPC response object.
    nlohmann::json handle(const nlohmann::json& request, Model& m, Index& ix);

    // Names of all tools this server exposes.
    static std::vector<std::string> toolNames();

    // Tools the host adds on top of the model-editing ones, for things only it
    // can do — Arena registers `screenshot`, which needs its GL context. The
    // handler runs on the draining thread, gets the tool's arguments, and
    // returns its result; returning null means "not mine", and the request
    // falls through to the unknown-tool error.
    using HostTool = std::function<nlohmann::json(const std::string& name,
                                                 const nlohmann::json& args)>;
    void setHostTools(std::vector<std::string> names, HostTool fn) {
        mHostToolNames = std::move(names);
        mHostTool = std::move(fn);
    }

private:
    Atlas mAtlas;          // loaded via the load_atlas tool
    bool  mAtlasLoaded = false;
    std::vector<std::string> mHostToolNames;
    HostTool mHostTool;

    nlohmann::json callTool(const std::string& name, const nlohmann::json& args,
                            Model& m, Index& ix, bool& mutated);
};

// Thread-safe request queue: submit from any thread, drain on the owner thread.
class McpQueue {
public:
    // Enqueue a request; the returned future resolves when the owner drains it.
    std::future<nlohmann::json> submit(nlohmann::json request);

    // Owner-thread: process all queued requests through `server` over model+index,
    // fulfilling each future. Returns how many were processed.
    int drain(McpServer& server, Model& m, Index& ix);

    size_t pending();

private:
    struct Item { nlohmann::json req; std::promise<nlohmann::json> resp; };
    std::mutex mMx;
    std::deque<Item> mQ;
};

} // namespace mass

// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "p2p/http_connection.h"
#include "p2p/http_msg_creator.h"
#include "utility/io/tcpserver.h"
#include "utility/io/asyncevent.h"
#include "utility/io/timer.h"
#include "utility/helpers.h"
#include "utility/logger.h"

using namespace beam;

namespace {

const uint16_t PORT = 8765;
std::string THIS_PATH;
io::AsyncEvent::Trigger g_stopEvent;

class DummyHttpServer {
public:
    DummyHttpServer(io::Reactor& reactor) :
        _msgCreator(2000),
        _reactor(reactor)
    {
        _server = io::TcpServer::create(
            _reactor,
            io::Address::localhost().port(PORT),
            BIND_THIS_MEMFN(on_stream_accepted)
        );
    }
private:
    void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
        if (errorCode == 0) {
            LOG_DEBUG() << "Stream accepted";
            _theConnection = std::make_unique<HttpConnection>(
                222,
                BaseConnection::inbound,
                BIND_THIS_MEMFN(on_request),
                10000,
                1024,
                std::move(newStream)
            );
        } else {
            LOG_ERROR() << "Server error " << io::error_str(errorCode);
            g_stopEvent();
        }
    }

    bool on_request(uint64_t, const HttpMsgReader::Message& msg) {
        if (msg.what != HttpMsgReader::http_message || !msg.msg) {
            LOG_ERROR() << "Request error";
            g_stopEvent();
        }

        const std::string& path = msg.msg->get_path();

        // Create http response
        io::SharedBuffer body;
        int code = 200;
        const char* message = "OK";
        bool stop = (path == "/stop");
        if (!stop && path != "/") {
            try {
                body = io::map_file_read_only(path.c_str());
            } catch (const std::exception& e) {
                LOG_DEBUG() << e.what();
                code = 404;
                message = "Not found";
            }
        }

        static const HeaderPair headers[] = {
            {"Server", "DummyHttpServer"},
            {"Host", msg.msg->get_header("host").c_str() }
        };

        if (_theConnection && _msgCreator.create_response(
            _serialized,
            code,
            message,
            headers,
            sizeof(headers) / sizeof(HeaderPair),
            1,
            "text/plain",
            body.size
        )) {
            if (!body.empty()) _serialized.push_back(body);
            _theConnection->write_msg(_serialized);
            _theConnection->shutdown();
        } else {
            LOG_ERROR() << "Cannot create response";
            g_stopEvent();
        }

        _theConnection.reset();
        _serialized.clear();
        if (stop) {
            g_stopEvent();
        }
        return false;
    }

    HttpMsgCreator _msgCreator;
    io::SerializedMsg _serialized;
    io::Reactor& _reactor;
    io::TcpServer::Ptr _server;
    HttpConnection::Ptr _theConnection;
};

class DummyHttpClient {
public:
    DummyHttpClient(io::Reactor& reactor) :
        _msgCreator(1000),
        _reactor(reactor),
        _timer(io::Timer::create(_reactor))
    {
        _timer->start(100, false, BIND_THIS_MEMFN(on_timer));
    }

    int uncompleted = 1;

private:
    void on_timer() {
        if (!_reactor.tcp_connect(io::Address::localhost().port(PORT), 333, BIND_THIS_MEMFN(on_connected), 1000)) {
            LOG_ERROR() << "Connect failed";
            g_stopEvent();
        }
    }

    void on_connected(uint64_t, io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
        if (errorCode != 0) {
            LOG_ERROR() << "Connect failed, " << io::error_str(errorCode);
            g_stopEvent();
            return;
        }

        _theConnection = std::make_unique<HttpConnection>(
            222,
            BaseConnection::outbound,
            BIND_THIS_MEMFN(on_response),
            10*1024*1024,
            1024,
            std::move(newStream)
        );

        static const HeaderPair headers[] = {
            {"Host", "zzz.xxx" }
        };

        if (_msgCreator.create_request(
            _serialized,
            "GET",
            THIS_PATH.c_str(),
            headers, 1
        )) {
            _theConnection->write_msg(_serialized);
            _serialized.clear();
        } else {
            LOG_ERROR() << "Cannot send request";
            g_stopEvent();
        }
    }

    bool on_response(uint64_t, const HttpMsgReader::Message& msg) {
        if (msg.what != HttpMsgReader::http_message || !msg.msg) {
            if (msg.what == HttpMsgReader::connection_error && msg.connectionError == io::EC_EOF) {
                _theConnection.reset();
            }
            else {
                g_stopEvent();
            }
            return false;
        }

        size_t bodySize=0;
        const void* body = msg.msg->get_body(bodySize);
        if (bodySize) {
            try {
                io::SharedBuffer fileContent = io::map_file_read_only(THIS_PATH.c_str());
                if (fileContent.size == bodySize && !memcmp(body, fileContent.data, bodySize)) {
                    uncompleted = 0;
                    LOG_INFO() << "file received successfully, size=" << bodySize;
                } else {
                    LOG_INFO() << "file receive error" << TRACE(bodySize) << TRACE(fileContent.size);
                }
            } catch (const std::exception& e) {
                LOG_ERROR() << e.what();
            }
        }

        _theConnection.reset();
        g_stopEvent();
        return false;
    }

    HttpMsgCreator _msgCreator;
    io::SerializedMsg _serialized;
    io::Reactor& _reactor;
    HttpConnection::Ptr _theConnection;
    io::Timer::Ptr _timer;
};

void get_this_path() {
    char buf[4096];
    size_t sz=4096;
    uv_exepath(buf, &sz);
    THIS_PATH = std::string(buf);
}

int http_server_test() {
    int nErrors = 0;

    try {
        get_this_path();
        io::Reactor::Ptr reactor = io::Reactor::create();
        io::AsyncEvent::Ptr stopEvent = io::AsyncEvent::create(*reactor, [&reactor]() {reactor->stop();});
        g_stopEvent = stopEvent;
        DummyHttpServer server(*reactor);
        DummyHttpClient client(*reactor);
        reactor->run();
        nErrors = client.uncompleted;
    } catch (const std::exception& e) {
        LOG_ERROR() << e.what();
        nErrors = 255;
    }

    return nErrors;
}

} //namespace

int main() {
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);
    return http_server_test();
}
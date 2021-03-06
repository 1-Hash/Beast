//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_ACCEPT_IPP
#define BEAST_WEBSOCKET_IMPL_ACCEPT_IPP

#include <beast/http/message.hpp>
#include <beast/http/parser_v1.hpp>
#include <beast/http/read.hpp>
#include <beast/http/string_body.hpp>
#include <beast/http/write.hpp>
#include <beast/core/handler_alloc.hpp>
#include <beast/core/prepare_buffers.hpp>
#include <beast/core/detail/type_traits.hpp>
#include <boost/assert.hpp>
#include <memory>
#include <type_traits>

namespace beast {
namespace websocket {

//------------------------------------------------------------------------------

// Respond to an upgrade HTTP request
template<class NextLayer>
template<class Handler>
class stream<NextLayer>::response_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        stream<NextLayer>& ws;
        http::response<http::string_body> resp;
        Handler h;
        error_code final_ec;
        bool cont;
        int state = 0;

        template<class DeducedHandler,
            class Body, class Headers>
        data(DeducedHandler&& h_, stream<NextLayer>& ws_,
            http::request<Body, Headers> const& req,
                bool cont_)
            : ws(ws_)
            , resp(ws_.build_response(req))
            , h(std::forward<DeducedHandler>(h_))
            , cont(cont_)
        {
            // can't call stream::reset() here
            // otherwise accept_op will malfunction
            //
            if(resp.status != 101)
                final_ec = error::handshake_failed;
        }
    };

    std::shared_ptr<data> d_;

public:
    response_op(response_op&&) = default;
    response_op(response_op const&) = default;

    template<class DeducedHandler, class... Args>
    response_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h), ws,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, false);
    }

    void operator()(
        error_code ec, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, response_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, response_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    bool asio_handler_is_continuation(response_op* op)
    {
        return op->d_->cont;
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, response_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class NextLayer>
template<class Handler>
void 
stream<NextLayer>::response_op<Handler>::
operator()(error_code ec, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            // send response
            d.state = 1;
            http::async_write(d.ws.next_layer(),
                d.resp, std::move(*this));
            return;

        // sent response
        case 1:
            d.state = 99;
            ec = d.final_ec;
            if(! ec)
                d.ws.open(detail::role_type::server);
            break;
        }
    }
    d.h(ec);
}

//------------------------------------------------------------------------------

// read and respond to an upgrade request
//
template<class NextLayer>
template<class Handler>
class stream<NextLayer>::accept_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        stream<NextLayer>& ws;
        http::request<http::string_body> req;
        Handler h;
        bool cont;
        int state = 0;

        template<class DeducedHandler, class Buffers>
        data(DeducedHandler&& h_, stream<NextLayer>& ws_,
                Buffers const& buffers)
            : ws(ws_)
            , h(std::forward<DeducedHandler>(h_))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
        {
            using boost::asio::buffer_copy;
            using boost::asio::buffer_size;
            ws.reset();
            ws.stream_.buffer().commit(buffer_copy(
                ws.stream_.buffer().prepare(
                    buffer_size(buffers)), buffers));
        }
    };

    std::shared_ptr<data> d_;

public:
    accept_op(accept_op&&) = default;
    accept_op(accept_op const&) = default;

    template<class DeducedHandler, class... Args>
    accept_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h), ws,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, 0, false);
    }

    void operator()(error_code const& ec)
    {
        (*this)(ec, 0);
    }

    void operator()(error_code const& ec,
        std::size_t bytes_transferred, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, accept_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, accept_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    bool asio_handler_is_continuation(accept_op* op)
    {
        return op->d_->cont;
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, accept_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class NextLayer>
template<class Handler>
void
stream<NextLayer>::accept_op<Handler>::
operator()(error_code const& ec,
    std::size_t bytes_transferred, bool again)
{
    beast::detail::ignore_unused(bytes_transferred);
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            // read message
            d.state = 1;
            http::async_read(d.ws.next_layer(),
                d.ws.stream_.buffer(), d.req,
                    std::move(*this));
            return;

        // got message
        case 1:
            // respond to request
#if 1
            // VFALCO I have no idea why passing std::move(*this) crashes
            d.state = 99;
            d.ws.async_accept(d.req, *this);
#else
            response_op<Handler>{
                std::move(d.h), d.ws, d.req, true};
#endif
            return;
        }
    }
    d.h(ec);
}

template<class NextLayer>
template<class AcceptHandler>
typename async_completion<
    AcceptHandler, void(error_code)>::result_type
stream<NextLayer>::
async_accept(AcceptHandler&& handler)
{
    static_assert(is_AsyncStream<next_layer_type>::value,
        "AsyncStream requirements requirements not met");
    return async_accept(boost::asio::null_buffers{},
        std::forward<AcceptHandler>(handler));
}

template<class NextLayer>
template<class ConstBufferSequence, class AcceptHandler>
typename async_completion<
    AcceptHandler, void(error_code)>::result_type
stream<NextLayer>::
async_accept(ConstBufferSequence const& bs, AcceptHandler&& handler)
{
    static_assert(is_AsyncStream<next_layer_type>::value,
        "AsyncStream requirements requirements not met");
    static_assert(beast::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    beast::async_completion<
        AcceptHandler, void(error_code)
            > completion(handler);
    accept_op<decltype(completion.handler)>{
        completion.handler, *this, bs};
    return completion.result.get();
}

template<class NextLayer>
template<class Body, class Headers, class AcceptHandler>
typename async_completion<
    AcceptHandler, void(error_code)>::result_type
stream<NextLayer>::
async_accept(http::request<Body, Headers> const& req,
    AcceptHandler&& handler)
{
    static_assert(is_AsyncStream<next_layer_type>::value,
        "AsyncStream requirements requirements not met");
    beast::async_completion<
        AcceptHandler, void(error_code)
            > completion(handler);
    reset();
    response_op<decltype(completion.handler)>{
        completion.handler, *this, req,
            boost_asio_handler_cont_helpers::
                is_continuation(completion.handler)};
    return completion.result.get();
}

template<class NextLayer>
void
stream<NextLayer>::
accept()
{
    static_assert(is_SyncStream<next_layer_type>::value,
        "SyncStream requirements not met");
    error_code ec;
    accept(boost::asio::null_buffers{}, ec);
    if(ec)
        throw system_error{ec};
}

template<class NextLayer>
void
stream<NextLayer>::
accept(error_code& ec)
{
    static_assert(is_SyncStream<next_layer_type>::value,
        "SyncStream requirements not met");
    accept(boost::asio::null_buffers{}, ec);
}

template<class NextLayer>
template<class ConstBufferSequence>
void
stream<NextLayer>::
accept(ConstBufferSequence const& buffers)
{
    static_assert(is_SyncStream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    error_code ec;
    accept(buffers, ec);
    if(ec)
        throw system_error{ec};
}

template<class NextLayer>
template<class ConstBufferSequence>
void
stream<NextLayer>::
accept(ConstBufferSequence const& buffers, error_code& ec)
{
    static_assert(is_SyncStream<next_layer_type>::value,
        "SyncStream requirements not met");
    static_assert(beast::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    reset();
    stream_.buffer().commit(buffer_copy(
        stream_.buffer().prepare(
            buffer_size(buffers)), buffers));
    http::request<http::string_body> m;
    http::read(next_layer(), stream_.buffer(), m, ec);
    if(ec)
        return;
    accept(m, ec);
}

template<class NextLayer>
template<class Body, class Headers>
void
stream<NextLayer>::
accept(http::request<Body, Headers> const& request)
{
    static_assert(is_SyncStream<next_layer_type>::value,
        "SyncStream requirements not met");
    error_code ec;
    accept(request, ec);
    if(ec)
        throw system_error{ec};
}

template<class NextLayer>
template<class Body, class Headers>
void
stream<NextLayer>::
accept(http::request<Body, Headers> const& req,
    error_code& ec)
{
    static_assert(is_SyncStream<next_layer_type>::value,
        "SyncStream requirements not met");
    reset();
    auto const res = build_response(req);
    http::write(stream_, res, ec);
    if(ec)
        return;
    if(res.status != 101)
    {
        ec = error::handshake_failed;
        // VFALCO TODO Respect keep alive setting, perform
        //             teardown if Connection: close.
        return;
    }
    open(detail::role_type::server);
}

//------------------------------------------------------------------------------

} // websocket
} // beast

#endif

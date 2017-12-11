/*
*   Calvin Neo
*   Copyright (C) 2017  Calvin Neo <calvinneo@calvinneo.com>
*   https://github.com/CalvinNeo/ATP
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License along
*   with this program; if not, write to the Free Software Foundation, Inc.,
*   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "atp_impl.h"
#include "udp_util.h"
#include "atp.h"

atp_context * atp_init(){
    get_context().init();
    return &get_context();
}

static void alarm_handler(int interval){
    atp_timer_event(&get_context(), 1000);
    alarm(1);
}

static ATP_PROC_RESULT sys_loop(atp_socket * socket, std::function<int(atp_socket*)> predicate){ 
    sigfunc_t * origin_sigfunc = setup_signal(SIGALRM, alarm_handler);
    alarm(1);

    socket->sys_cache = new char [ATP_SYSCACHE_MAX];
    while (true) {
        struct sockaddr_in peer_addr; socklen_t peer_len = sizeof(peer_addr);
        sockaddr * ppeer_addr = (SA *)&peer_addr;
        int n = recvfrom(socket->sockfd, socket->sys_cache, ATP_SYSCACHE_MAX, 0, ppeer_addr, &peer_len);
        if (n < 0){
            if(errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN){
                // normal
            }else{
                break;
            }
        }else{
            ATPAddrHandle handle_to((const SA *)&peer_addr);
            socket->process(handle_to, socket->sys_cache, n);
        }
        if (predicate(socket) == ATP_PROC_FINISH)
        {
            return ATP_PROC_OK;
        }else{
            continue;
        }
    }
    delete [] socket->sys_cache;
    socket->sys_cache = nullptr;
    alarm(0);
    setup_signal(SIGALRM, origin_sigfunc);
    return ATP_PROC_OK;
}

int atp_getfd(atp_socket * socket){
    return socket->sockfd;
}

atp_socket * atp_create_socket(atp_context * context){
    ATPSocket * socket = new ATPSocket(context);
    int sockfd = socket->init(AF_INET, SOCK_DGRAM, 0);
    // now this socket is registered to context
    // but it will not be able to locate until is connected
    // thus it will have a (addr:port), and `register_to_look_up` will be called
    // and the socket will be insert into context->look_up
    context->sockets.push_back(socket);
    return socket;
}

ATP_PROC_RESULT atp_async_connect(atp_socket * socket, const struct sockaddr * to, socklen_t tolen){
    ATPAddrHandle handle(to);
    return socket->connect(to);
}

ATP_PROC_RESULT atp_connect(atp_socket * socket, const struct sockaddr * to, socklen_t tolen){
    ATPAddrHandle handle(to);
    int n;
    socket->connect(to);
    sys_loop(socket, [](atp_socket * socket){
        if (socket->conn_state >= CS_CONNECTED)
        {
            return ATP_PROC_FINISH;
        }
        return ATP_PROC_OK;
    });
    return ATP_PROC_OK;
}

ATP_PROC_RESULT atp_listen(atp_socket * socket, uint16_t host_port){
    assert(socket != nullptr);
    return socket->listen(host_port);
}

ATP_PROC_RESULT atp_accept(atp_socket * socket){
    sys_loop(socket, [](atp_socket * socket){
        if (socket->conn_state >= CS_CONNECTED)
        {
            return ATP_PROC_FINISH;
        }
        return ATP_PROC_OK;
    });
    return ATP_PROC_OK;
}

ATP_PROC_RESULT atp_write(atp_socket * socket, void * buf, size_t length){
    assert(socket != nullptr);
    return socket->write(buf, length);
}

ATP_PROC_RESULT atp_process_udp(atp_context * context, int sockfd, const char * buf, size_t len, const struct sockaddr * to, socklen_t tolen){
    assert(context != nullptr);
    ATPAddrHandle handle_to(to);
    if (handle_to.host_port() == 0 && handle_to.host_addr() == 0)
    {
        // error
        #if defined (ATP_LOG_AT_DEBUG)
            log_debug(context, "Can't locate socket:[0.0.0.0:00000]");
        #endif
        return ATP_PROC_ERROR;
    }
    const ATPPacket * pkt = reinterpret_cast<const ATPPacket *>(buf);
    bool is_first = pkt->get_syn() && !(pkt->get_ack());
    if (is_first)
    {
        // find in listen
        ATPSocket * socket = context->find_socket_by_fd(handle_to, sockfd);
        if (socket == nullptr)
        {
            return ATP_PROC_ERROR;
        }else{
            return socket->process(handle_to, buf, len);
        }
    } else{
        ATPSocket * socket = context->find_socket_by_head(handle_to, pkt);
        if (socket == nullptr)
        {
            return ATP_PROC_ERROR;
        }else{
            return socket->process(handle_to, buf, len);
        }
    }
}

ATP_PROC_RESULT atp_close(atp_socket * socket){
    assert(socket != nullptr);
    #if defined (ATP_LOG_AT_DEBUG)
        log_debug(socket, "User called atp_close");
    #endif
    socket->close();
    sys_loop(socket, [](atp_socket * socket){
        if (socket->conn_state == CS_DESTROY)
        {
            return ATP_PROC_FINISH;
        }
        return ATP_PROC_OK;
    });
    return ATP_PROC_OK;
}


ATP_PROC_RESULT atp_async_close(atp_socket * socket){
    assert(socket != nullptr);
    return socket->close();
}

void atp_set_callback(atp_socket * socket, int callback_type, atp_callback_func * proc){
    socket->callbacks[callback_type] = proc;
}

ATP_PROC_RESULT atp_eof(atp_socket * socket){
    return socket->readable();
}

ATP_PROC_RESULT atp_timer_event(atp_context * context, uint64_t interval){
    for(ATPSocket * socket: context->sockets){
        ATP_PROC_RESULT result = socket->check_timeout();

    }
}

bool atp_destroyed(atp_socket * socket){
    return socket == nullptr ? true : socket->conn_state == CS_DESTROY;
}
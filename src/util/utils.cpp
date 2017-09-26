#include <fstream>

#include <unistd.h>

#include "utils.h"


std::string read_entire_file(const char *name)
{
    std::ifstream F(name, std::ios_base::in|std::ios_base::binary);
    if(!F.is_open())
        throw std::runtime_error(SB()<<"Failed to read file "<<name);
    std::ostringstream S;
    S << F.rdbuf();
    return S.str();
}

const char* SocketError::what() const throw()
{
    return strerror(code);
}


Socket::Socket(SOCKET s) :sock(s)
{
    if(sock==INVALID_SOCKET)
        throw SocketError(SOCKERRNO);
}

Socket::Socket(int domain, int type, int protocol)
    :sock(epicsSocketCreate(domain, type, protocol))
{
    if(sock==INVALID_SOCKET)
        throw SocketError(SOCKERRNO);
}

void Socket::bind(osiSockAddr& ep) const
{
    if(::bind(sock, &ep.sa, sizeof(ep))!=0)
        throw SocketError(SOCKERRNO);

    osiSocklen_t len = sizeof(ep);
    if(::getsockname(sock, &ep.sa, &len)!=0)
        throw SocketError(SOCKERRNO);
}

void Socket::sendall(const char* buf, size_t buflen) const
{
    while(buflen) {
        ssize_t ret = ::send(sock, buf, buflen, 0);
        if(ret<=0)
            throw SocketError(SOCKERRNO);

        buf += ret;
        buflen -= buflen;
    }
}

void Socket::recvall(char* buf, size_t buflen) const
{
    while(buflen) {
        ssize_t ret = ::recv(sock, buf, buflen, 0);
        if(ret<=0)
            throw SocketError(SOCKERRNO);

        buf += ret;
        buflen -= buflen;
    }
}

void Socket::sendto(const osiSockAddr& dest, const char* buf, size_t buflen) const
{
    ssize_t ret = ::sendto(sock, buf, buflen, 0, &dest.sa, sizeof(dest));
    if(ret==SOCK_EWOULDBLOCK)
        throw SocketBusy();
    else if(ret<0)
        throw SocketError(SOCKERRNO);
    else if(size_t(ret)!=buflen)
        throw std::runtime_error("Incomplete sendto()");
}

size_t Socket::recvfrom(osiSockAddr& src, char* buf, size_t buflen) const
{
    osiSocklen_t len = sizeof(src);
    ssize_t ret = ::recvfrom(sock, buf, buflen, 0, &src.sa, &len);
    if(ret==SOCK_EWOULDBLOCK)
        throw SocketBusy();
    else if(ret<0)
        throw SocketError(SOCKERRNO);
    return size_t(ret);
}

void Socket::pipe(Socket& rx, Socket& tx)
{
    int fds[2] = {-1, -1};
    if(::pipe(fds)!=0)
        throw SocketError(SOCKERRNO);

    Socket temp_rx(fds[0]);
    Socket temp_tx(fds[1]);
    rx.swap(temp_rx);
    tx.swap(temp_tx);
}

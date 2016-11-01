#include "server.h"

#include <cstdint>
#include <cstddef>
#include <array>
#include <string>
#include <boost/asio.hpp>

#include <iostream>
#include <functional>
#include <algorithm>
#include <numeric>

using namespace std;
using namespace boost::asio;
using boost::asio::ip::tcp;
using boost_error = boost::system::error_code;

server::ptr server::create(uint16_t port)
{
    server *p = new server(port);
    return ptr{p};
}

server::server(uint16_t port) :
    acceptor{service, tcp::endpoint{tcp::v4(), port}}
{
}

void server::start()
{
    auto client = talk_to_client::create(shared_from_this(), service);
    acceptor.async_accept(client->socket(),
                          [this, client](boost_error error)
                          { handle_accept(client, error); });
    service.run();
}

void server::remove_client(server::talk_to_client::ptr client)
{
    clients_by_name.erase(client->name());
    clients_by_endpoint.erase(client->socket().remote_endpoint());
}

bool server::rename_client(server::talk_to_client::ptr client,
                           const string &old_name,
                           const string &new_name)
{
    auto it = clients_by_name.find(new_name);
    if (it == clients_by_name.end() || it->second == client)
    {
        if (it == clients_by_name.end())
        {
            clients_by_name.erase(old_name);
        }
        clients_by_name[new_name] = client;
        client->name(new_name);
        return true;
    }
    else
    {
        return false;
    }
}

void server::handle_accept(server::talk_to_client::ptr new_client,
                           boost::system::error_code error)
{
    if (!error)
    {
        cout << "connected : " <<
            talk_to_client::to_string(new_client->socket().remote_endpoint()) <<
                endl;

        new_client->start();
        auto client = talk_to_client::create(shared_from_this(), service);
        acceptor.async_accept(client->socket(),
                              [this, client](boost_error error)
                              { handle_accept(client, error); });
    }
}

const unordered_map<std::string, server::talk_to_client::request_handler>
    server::talk_to_client::request_handlers =
{
    {"connect", &server::talk_to_client::connect_handler},
    {"get_list", &server::talk_to_client::get_list_handler},
    {"get_info", &server::talk_to_client::get_info_handler},
    {"start_dialog", &server::talk_to_client::start_dialog_handler}
};

server::talk_to_client::talk_to_client(weak_ptr serv,
                                       boost::asio::io_service &service) :
    serv{serv}, sock{service}
{
}

server::talk_to_client::ptr server::talk_to_client::create(weak_ptr serv,
        boost::asio::io_service &service)
{
    talk_to_client *p = new talk_to_client{serv, service};
    return ptr{p};
}

void server::talk_to_client::start()
{
    start_read();
}

void server::talk_to_client::send_start_dialog(string name)
{
    if (start_dialog_sending)
    {
        return;
    }

    string msg = "start_dialog " + name + "\r\n";
    if (msg.size() > dialog_buf.size())
    {
        cout << "too big start_dialog message" << endl;
        msg.resize(dialog_buf.size());
    }

    copy(msg.begin(), msg.end(), dialog_buf.begin());
    start_dialog_sending = true;
    async_write(sock, buffer(dialog_buf, msg.size()),
                [self = shared_from_this()](boost_error ec, size_t)
                { if (ec) self->serv.lock()->remove_client(self);
                  self->start_dialog_sending = false; });
}

void server::talk_to_client::start_read()
{
    using namespace std::placeholders;

    server::ptr s = serv.lock();
    s->clients_by_endpoint[sock.remote_endpoint()] = shared_from_this();
    async_read(sock, buffer(buf),
               bind(&talk_to_client::read_complete, shared_from_this(), _1, _2),
               bind(&talk_to_client::read, shared_from_this(), _1, _2));
}

size_t server::talk_to_client::read_complete(boost::system::error_code error,
                                             size_t bytes)
{
    if (error)
    {
        serv.lock()->remove_client(shared_from_this());
        return 0;
    }

    auto end = buf.begin() + bytes;
    auto start_it = find_if(buf.begin(), end,
                            [](char c){ return !isspace(c); });
    if (start_it == end)
    {
        return 1;
    }

    auto finish_it = find_if(next(start_it), end,
                             [](char c){ return isspace(c) && c != ' '; });
    if (finish_it == end)
    {
        return 1;
    }

    return 0;
}

void server::talk_to_client::read(boost::system::error_code error, size_t bytes)
{
    if (error)
    {
        serv.lock()->remove_client(shared_from_this());
        return;
    }

    auto end = buf.begin() + bytes;
    auto start_it = find_if(buf.begin(), end,
                            [](char c){ return !isspace(c); });
    auto finish_it = find_if(next(start_it), end,
                             [](char c){ return isspace(c) && c != ' '; });

    string request{start_it, finish_it};
    handle_request(move(request));
}

void server::talk_to_client::handle_request(string request)
{
    string title = get_token(&request);

    auto it = request_handlers.find(title);
    string res;
    if (it != request_handlers.end())
    {
        if (title == "connect" || !name_value.empty())
        {
            res = (this->*(it->second))(move(request));
            if (res.empty())
            {
                res = "<INVALID REQUEST DATA>";
            }
        }
        else
        {
            res = "<NOT REGISTERED USER>";
        }
    }
    else
    {
        res = "<INVALID REQUEST COMMAND>";
    }
    res += "\r\n";

    if (res.size() > buf.size())
    {
        cout << "too big answer" << endl;
        res.resize(buf.size());
    }

    copy(res.begin(), res.end(), buf.begin());
    async_write(sock, buffer(buf, res.size()),
                [self = shared_from_this()](boost_error ec, size_t)
                { if (ec) self->serv.lock()->remove_client(self);
                  else self->start_read(); });
}

string server::talk_to_client::connect_handler(string request_body)
{
    string name = get_token(&request_body);
    string address_str = get_token(&request_body);
    string port_str = get_token(&request_body);

    if (port_str.empty() || !request_body.empty())
    {
        return "";
    }

    boost_error ec;
    ip::address address = ip::address_v4::from_string(address_str, ec);
    if (ec)
    {
        return "";
    }
    size_t idx;
    int port_int = stoi(port_str, &idx);
    if (idx != port_str.length() ||
        port_int < numeric_limits<uint16_t>::min() ||
        port_int > numeric_limits<uint16_t>::max())
    {
        return "";
    }
    uint16_t port = port_int;

    if (!serv.lock()->rename_client(shared_from_this(), name_value, name))
    {
        return "<CLIENT WITH THAT NAME ALREADY EXISTS>";
    }
    private_endpoint = {address, port};
    return "confirm_connection";
}

string server::talk_to_client::get_list_handler(string request_body)
{
    if (!request_body.empty())
    {
        return "";
    }

    server::ptr s = serv.lock();
    string res = "list";
    for (const auto &p : s->clients_by_name)
    {
        if (res.size() + 1 + p.second->name().size() > MAX_BUF_SIZE)
        {
            break;
        }
        res += " " + p.second->name();
    }

    return res;
}

string server::talk_to_client::get_info_handler(string request_body)
{
    string name = get_token(&request_body);
    if (name.empty() || !request_body.empty())
    {
        return "";
    }

    server::ptr s = serv.lock();
    auto it = s->clients_by_name.find(name);
    if (it != s->clients_by_name.end())
    {
        talk_to_client_ptr client = it->second;
        return "info " + to_string(client->private_endpoint) + " " +
                to_string(client->socket().remote_endpoint());
    }
    else
    {
        return "<GET_INFO : UNKNOWN CLIENT>";
    }
}

string server::talk_to_client::start_dialog_handler(string request_body)
{
    string name = get_token(&request_body);
    if (name.empty() || !request_body.empty())
    {
        return "";
    }

    server::ptr s = serv.lock();
    auto it = s->clients_by_name.find(name);
    if (it != s->clients_by_name.end())
    {
        talk_to_client_ptr client = it->second;
        client->send_start_dialog(name_value);
        return "accepted";
    }
    else
    {
        return "<START_DIALOG : UNKNOWN CLIENT>";
    }
}

string server::talk_to_client::to_string(tcp::endpoint endpoint)
{
    return endpoint.address().to_string() + " " + ::to_string(endpoint.port());
}

string server::talk_to_client::get_token(string *s)
{
    size_t space_index = s->find_first_of(' ');
    size_t token_index = space_index == string::npos ? s->length() :
                                                       space_index;

    string res = s->substr(0, token_index);
    if (token_index != s->length())
    {
        *s = s->substr(token_index + 1);
    }
    else
    {
        s->clear();
    }
    return res;
}

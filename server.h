#ifndef SERVER_H
#define SERVER_H

#include <cstdint>
#include <cstddef>
#include <array>
#include <string>
#include <unordered_map>
#include <map>
#include <memory>
#include <boost/asio.hpp>

class server : public std::enable_shared_from_this<server>
{
    server(uint16_t port);

public:
    using ptr = std::shared_ptr<server>;
    using weak_ptr = std::weak_ptr<server>;
    static ptr create(uint16_t port);
    void start();

private:
    boost::asio::io_service service;
    boost::asio::ip::tcp::acceptor acceptor;

    class talk_to_client : public std::enable_shared_from_this<talk_to_client>
    {
        talk_to_client(weak_ptr serv, boost::asio::io_service &service);

    public:
        using ptr = std::shared_ptr<talk_to_client>;
        static ptr create(weak_ptr serv, boost::asio::io_service &service);

        boost::asio::ip::tcp::socket &socket() { return sock; }
        const std::string &name() const { return name_value; }
        void name(const std::string &new_name) { name_value = new_name; }

        void start();
        void send_start_dialog(std::string name);

        static std::string to_string(boost::asio::ip::tcp::endpoint endpoint);

    private:
        weak_ptr serv;
        boost::asio::ip::tcp::socket sock;
        std::string name_value;
        boost::asio::ip::tcp::endpoint private_endpoint;

        static constexpr size_t MAX_BUF_SIZE = 1024;
        std::array<char, MAX_BUF_SIZE> buf;
        bool start_dialog_sending = false;
        std::array<char, MAX_BUF_SIZE> dialog_buf;

        void start_read();
        size_t read_complete(boost::system::error_code error, size_t bytes);
        void read(boost::system::error_code error, size_t bytes);

        void handle_request(std::string request);
        using request_handler = std::string(talk_to_client::*)(std::string);
        static const std::unordered_map<std::string, request_handler>
            request_handlers;
        std::string connect_handler(std::string request_body);
        std::string get_list_handler(std::string request_body);
        std::string get_info_handler(std::string request_body);
        std::string start_dialog_handler(std::string request_body);

        static std::string get_token(std::string *s);
    };
    using talk_to_client_ptr = std::shared_ptr<talk_to_client>;
    friend class talk_to_client;

    std::unordered_map<std::string, talk_to_client_ptr> clients_by_name;
    std::map<boost::asio::ip::tcp::endpoint, talk_to_client_ptr>
        clients_by_endpoint; //endpoint don't have ready hash function
    void remove_client(talk_to_client::ptr client);
    bool rename_client(talk_to_client::ptr client,
                       const std::string &old_name,
                       const std::string &new_name);

    void handle_accept(talk_to_client::ptr new_client,
                       boost::system::error_code error);
};

#endif // SERVER_H

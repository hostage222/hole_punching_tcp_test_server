#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <exception>

#include "server.h"

using namespace std;

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        cerr << "Usage: test_server <port>" << endl;
        return -1;
    }

    try
    {
        server::ptr p = server::create({static_cast<uint16_t>(atoi(argv[1]))});
        p->start();
    }
    catch (exception &e)
    {
        cerr << "Exception: " << e.what() << endl;
    }
    catch (...)
    {
        cerr << "Undefined exception" << endl;
    }

    return 0;
}

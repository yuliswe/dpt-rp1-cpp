#include <iostream>
#include <string>

namespace dpt {

    using namespace std;

class Logger
{
    public:
        static char endl;
        template <typename T> 
        void write(T const& msg) {
            cerr << msg;
        }
        
};

template <typename T> 
Logger& operator<<(Logger& out, T const& msg)
{
    out.write(msg);
    return out;
}

}


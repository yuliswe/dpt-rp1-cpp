#include <string>
#include <memory>
namespace dpt 
{

using namespace std;

class DptFile 
{
private:
    string m_id;
public:
    static shared_ptr<DptFile> fromId(string const& id);
    uint8_t readByte();
};

};

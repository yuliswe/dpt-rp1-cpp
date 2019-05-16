#include <string>
#include <boost/filesystem.hpp>
#include <sqlite3.h>

namespace dpt {
    using namespace std;
    using boost::filesystem::path;

    typedef boost::filesystem::path rpath;

    enum ColIndex {
        RelPath = 0,
        LocalRev = 1,
        DptRev = 2,
    };

    class RevDB {
        private:
            sqlite3* m_db;
        public:
            void open(path const& db);
            vector<string> getByRelPath(rpath const& relpath) const;
            vector<string> getByDptRev(string const& relpath) const;
            vector<string> getByLocalRev(string const& relpath) const;
            void putRev(rpath const& relpath, string const& local_md5, string const& dpt_rev) const;
            void reset();
            void close();
    };

    string md5(path const& file);
};

#ifndef dtree_h
#define dtree_h

#include <memory>
#include <vector>
#include <chrono>
#include <boost/date_time.hpp>
#include <boost/filesystem.hpp>

/* Document Tree */

namespace dpt {

    using std::shared_ptr;
    using std::vector;
    using std::string;
    using std::pair;
    using std::chrono::time_point;
    using std::chrono::system_clock;
    using boost::filesystem::path;
    
    typedef boost::filesystem::path rpath; /* a relative path */

    class DNode {
        private:
            vector<shared_ptr<DNode>> m_children;
            time_t m_last_modified_time;
            string m_filename;
            string m_id;
            string m_rev;
            bool m_is_dir = false;
            path m_path;
            rpath m_rel_path;

        public:
            time_t lastModifiedTime() const;        
            void setLastModifiedTime(time_t const& time);
            vector<shared_ptr<DNode>> children() const;
            void addChild(shared_ptr<DNode> child);
            bool isDir() const;
            void setIsDir(bool);
            string const& id() const;
            void setId(string const& id);
            string const& filename() const;
            void setFilename(string const&);
            string const& rev() const;
            void setRev(string const&);
            void setPath(boost::filesystem::path const&);
            void setRelPath(boost::filesystem::path const&);
            boost::filesystem::path const& path() const;
            rpath const& relPath() const;
            vector<shared_ptr<DNode>> allFiles() const; 
    };

    typedef DNode LNode;

    void symmetricDiff(vector<shared_ptr<DNode>> const& a, 
                       vector<shared_ptr<DNode>> const& b, 
                       vector<shared_ptr<DNode>>& only_a, 
                       vector<shared_ptr<DNode>>& only_b, 
                       vector<pair<shared_ptr<DNode>,shared_ptr<DNode>>>& both);
};


#endif

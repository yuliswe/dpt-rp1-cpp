#include <string>
#include <map>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <boost/property_tree/json_parser.hpp>
#include <boost/filesystem.hpp>
#include <NFHTTP/NFHTTP.h>
#include <dtree.h>
#include <revdb.h>

namespace dpt {

    using std::vector;
    using std::string;
    using std::map;
    using std::shared_ptr;
    using std::pair;
    using std::make_shared;

    using boost::property_tree::ptree;
    using boost::filesystem::path;

    enum DryRunFlag {
        NormalRun = 0,
        DryRun = 1,
    };

    class Json : public ptree {
        public:
            static Json fromString(string const& body);
            string toString() const;
    };

    class DptResponse : public nativeformat::http::Response {
        public:
            string body() const;
    };

    class DptRequest : public nativeformat::http::Request {
        public:
            string body() const;
    };

    class Dpt
    {
        private:
            // address m_ip;
            string m_hostname = "digitalpaper.local";
            unsigned m_port = 8443;
            map<string,string> m_cookies;
            shared_ptr<LNode> m_local_tree = make_shared<DNode>();
            shared_ptr<DNode> m_dpt_tree = make_shared<DNode>();
            path m_sync_dir;
            path m_client_id_path;
            path m_private_key_path;
            RevDB m_rev_db;
            unordered_map<string,shared_ptr<DNode>> m_dpt_path_nodes;
            unordered_map<string,shared_ptr<LNode>> m_local_path_nodes;
            unordered_map<string,shared_ptr<DNode>> m_dpt_revision_nodes;
            unordered_map<string,shared_ptr<LNode>> m_local_revision_nodes;
            std::function<void(string)> m_messager = [](string) { };
            
            vector<shared_ptr<DNode const>> m_local_only_nodes;
            vector<shared_ptr<DNode const>> m_dpt_only_nodes;
            vector<pair<shared_ptr<LNode const>,shared_ptr<DNode const>>> m_moved_nodes;
            vector<pair<shared_ptr<LNode const>,shared_ptr<DNode const>>> m_modified_nodes;

            vector<shared_ptr<DNode const>> m_prepared_overwrite_from_dpt; /* (dpt,local) pair */
            vector<shared_ptr<LNode const>> m_prepared_overwrite_to_dpt; /* (local,dpt) pair */
            vector<pair<shared_ptr<LNode const>, shared_ptr<DNode const>>> m_prepared_local_move;
            vector<pair<shared_ptr<DNode const>, shared_ptr<LNode const>>> m_prepared_dpt_move;
            vector<shared_ptr<LNode const>> m_prepared_local_new;
            vector<shared_ptr<DNode const>> m_prepared_dpt_new;
            vector<shared_ptr<LNode const>> m_prepared_local_delete;
            vector<shared_ptr<DNode const>> m_prepared_dpt_delete;

        protected:
            void gitInit();
            string getNonce(string client_id);
            void updateLocalNode(shared_ptr<DNode> node, path const& local_path);
            void computeSyncFilesInNode(shared_ptr<DNode const> local, shared_ptr<DNode const> dpt);
            void overwriteToDpt(path const& local, path const& dpt);
            void overwriteFromDpt(path const& dpt, path const& local);
            void deleteFromDpt(path const& file);
            void deleteFromLocal(path const& file);
            void moveBetweenLocal(path const& from, path const& to);
            void moveBetweenDpt(path const& from, path const& to);
            void copyBetweenLocal(path const& from, path const& to);
            void copyBetweenDpt(path const& from, path const& to);
            void updateRevDB();
            void updateRevForNode(shared_ptr<LNode const> local, shared_ptr<DNode const> dpt);
            void computeSyncFiles();
            void reportComputedSyncFiles();
            void syncAllFiles();
            void dbOpen();
            void dbClose();
            string baseUrl() const;
            void updateLocalTree();
            void updateDptTree();
            void updateDptNode(shared_ptr<DNode> node);
            string git(string const& command) const;

        public:
            ~Dpt();
            unsigned port() const;
            void setPort(unsigned port);
            string hostname() const;
            void authenticate();
            void setupSyncDir();
            void setClientIdPath(path const& client_id_path);
            void setPrivateKeyPath(path const& client_key_path);
            Json sendJson(string const& method, string const& url, Json const& json = Json()) const;
            shared_ptr<DptRequest> httpRequest(string url) const;
            shared_ptr<DptResponse> sendRequest(shared_ptr<DptRequest> request) const;
            string readResponse(shared_ptr<DptResponse> response) const;
            void dptOpenDocument(string document_id) const;
            vector<path> dptOpenDocuments() const;
            void syncTime() const;
            path syncDir() const;
            void setSyncDir(path const&);
            void safeSyncAllFiles(DryRunFlag dryrun = NormalRun);
            void setMessager(std::function<void(string)>);
    };

    class HttpSigner {
        private:
            path m_private_key_path;

        public:
            HttpSigner(path const& private_key_path);
            string sign(string nonce);
    };

    bool syncable(path const& path);

};

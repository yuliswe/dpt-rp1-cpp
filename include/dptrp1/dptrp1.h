#ifndef dptrp1_h
#define dptrp1_h

#include <string>
#include <map>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <boost/property_tree/json_parser.hpp>
#include <boost/filesystem.hpp>
#include <boost/asio.hpp>
#include <NFHTTP/NFHTTP.h>
#include <iostream>
#include "dtree.h"
#include "revdb.h"
#include "git.h"
#include <atomic>

namespace dpt {

    using std::vector;
    using std::string;
    using std::map;
    using std::shared_ptr;
    using std::pair;
    using std::make_shared;

    using boost::property_tree::ptree;
    using boost::filesystem::path;

    inline atomic<int> interrupt_flag = 0;

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

    struct GitCommit {
        string commit;
        std::tm time;
        string iso8601_time;
        string message;
        string title;
    };

    ostream& operator<<(ostream& out, GitCommit& commit);

    class Dpt
    {
        private:
            // address m_ip;
            string m_hostname = "digitalpaper.local";
            unsigned m_port = 8443;
            map<string,string> m_cookies;
            shared_ptr<LNode> m_local_tree = make_shared<DNode>();
            shared_ptr<DNode> m_dpt_tree = make_shared<DNode>();
            shared_ptr<Git> m_git;
            path m_sync_dir;
            path m_client_id_path;
            path m_private_key_path;
            path m_git_path;
            RevDB m_rev_db;
            vector<shared_ptr<GitCommit>> m_git_commits;
            unordered_map<string,shared_ptr<DNode>> m_dpt_path_nodes;
            unordered_map<string,shared_ptr<LNode>> m_local_path_nodes;
            unordered_map<string,shared_ptr<DNode>> m_dpt_revision_nodes;
            unordered_map<string,shared_ptr<LNode>> m_local_revision_nodes;
            std::function<void(string const&)> m_messager = [](string const&) { };
            
            ostream* m_logger = &std::cout;
            ostream& logger() const;
            
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
            shared_ptr<vector<uint8_t>> readDptFileBytes(shared_ptr<DNode const> node, size_t offset, size_t size) const;
            void writeDptFileBytes(shared_ptr<DNode const> node, size_t offset, size_t total, shared_ptr<vector<uint8_t>> bytes) const;
            size_t bisectDptFileBytes(shared_ptr<DNode const> node, istream& local) const;

        public:
            ~Dpt();
            inline unsigned port() const noexcept;
            inline void setPort(unsigned port) noexcept;
            inline string hostname() const noexcept;
            void setMessager(std::function<void(string const&)>) noexcept;
            void setLogger(ostream&) noexcept;
            bool resolveHost(boost::asio::ip::address* addr = nullptr) const;
            void authenticate();
            void setupSyncDir();
            void setClientIdPath(path const& client_id_path);
            void setPrivateKeyPath(path const& client_key_path);
            Json sendJson(string const& method, string const& url, Json const& json = Json()) const;
            shared_ptr<DptRequest> httpRequest(string const& url) const;
            shared_ptr<DptResponse> sendRequest(shared_ptr<DptRequest> request) const;
            string readResponse(shared_ptr<DptResponse> response) const;
            void dptOpenDocument(path const& dpt);
            void dptQuickUploadAndOpen(path const& local);
            vector<path> dptOpenDocuments() const;
            void syncTime() const;
            path syncDir() const;
            void setSyncDir(path const&);
            void safeSyncAllFiles(DryRunFlag dryrun = NormalRun);
            void initSyncDir();
            void updateGitCommits();
            void extractGitCommit(string const& commit, path const& dest);
            vector<shared_ptr<GitCommit>> listGitCommits(size_t limit = 100) const;
    };

    class HttpSigner {
        private:
            path m_private_key_path;

        public:
            HttpSigner(path const& private_key_path);
            string sign(string nonce);
    };

    bool syncable(path const& path);
    size_t readLocalFilesize(istream& infile);
    shared_ptr<vector<uint8_t>> readLocalFileBytes(istream& infile, size_t offset, size_t size);
};

#endif

#include <dptrp1/dptrp1.h>
#include <boost/property_tree/json_parser.hpp>
#include <NFHTTP/NFHTTP.h>
#include <sstream>
#include <iostream>
#include <boost/asio/error.hpp>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <ctime>
#include <iomanip>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <algorithm>
#include <memory>
#include <queue>
#include <unordered_set>
#include <git2.h>
#include <csignal>
#include <dptrp1/exception.h>

using namespace dpt;
using namespace std;

using std::shared_ptr;

using boost::filesystem::path;
using boost::filesystem::is_directory;
using boost::filesystem::create_directory;
using boost::filesystem::is_regular_file;
using boost::filesystem::directory_iterator;
using boost::filesystem::last_write_time;
using boost::filesystem::last_write_time;

using nativeformat::http::Request;
using nativeformat::http::Response;

bool Dpt::resolveHost(boost::asio::ip::address* addr) const
{
    try {
        boost::asio::io_service svc;
        boost::asio::io_context ctx;
        using resolver = boost::asio::ip::tcp::resolver;
        resolver res(ctx);
        resolver::query query(hostname(), to_string(port()));
        resolver::iterator iter = res.resolve(query);
        resolver::iterator end;
        if (end == iter) {
            return false;
        }
        if (addr) {
            *addr = iter->endpoint().address();
        }
        return true;
    } catch (boost::wrapexcept<boost::system::system_error> err) {
        return false;
    }
}

// Dpt::Dpt(address const& ip, unsigned port)
// {
//     m_ip = ip;
//     m_port = port;
// }

void Dpt::authenticate()
{
    assert(! m_private_key_path.empty() && "don't forget to set private key");
    assert(! m_client_id_path.empty() && "don't forget to set client id");

    try {
        m_messager("Authenticating...");
        m_cookies.clear();
        
        string client_id;
        ifstream inf(m_client_id_path.string(), std::ios_base::in);
        inf >> client_id;
        #if DEBUG_AUTH
            logger() << "using client_id: " << client_id << endl;
        #endif 

        using boost::property_tree::ptree;
        using boost::property_tree::write_json;
        /* prepare signature */
        HttpSigner signer(m_private_key_path);
        string nonce = getNonce(client_id);
        #if DEBUG_AUTH
            logger() << "received nonce: " << nonce << endl;
        #endif 
        string nonce_signed = signer.sign(nonce);
        /* write data to send */
        ptree data;
        data.put("client_id", client_id);
        data.put("nonce_signed", nonce_signed);
        #if DEBUG_AUTH
            logger() << "using nonce_signed: " << nonce_signed << endl;
        #endif 
        ostringstream buf;
        write_json(buf, data, false);
        string json = buf.str();
        /* send request */
        auto request = httpRequest("/auth");
        request->setMethod("PUT");
        request->setData((unsigned char*)json.c_str(), json.length());
        request->headerMap()["Content-Type"] = "application/json";
        auto response = sendRequest(request);
        auto const& headers = response->headerMap();
        string const& set_cookie = headers.at("Set-Cookie");
        string const& credentials = set_cookie.substr(12, 64);
        #if DEBUG_AUTH
            logger() << "using credential: " << credentials << endl;
        #endif 
        m_cookies["Credentials"] = credentials;

    } catch(...) {
        m_messager("Failed to Connect DPT-RP1");
        throw;
    }
}

unsigned Dpt::port() const noexcept { return m_port; }
string Dpt::hostname() const noexcept { return m_hostname; }

string HttpSigner::sign(string nonce)
{
    int error = 0;
    /* get rsa key */
    RSA* rsa = nullptr;
    FILE* fp = fopen(m_private_key_path.c_str(), "r");
    assert(fp && "Cannot open file if fails.");
    PEM_read_RSAPrivateKey(fp, &rsa, nullptr, nullptr);
    fclose(fp);
    /* sign with rsa-sha256 */
    EVP_MD_CTX* mdctx = EVP_MD_CTX_create();
    EVP_PKEY* evp_pkey = EVP_PKEY_new();
    EVP_PKEY_set1_RSA(evp_pkey, rsa);
    EVP_MD const* md = EVP_sha256();
    EVP_PKEY_CTX* pkey_ctx;
    error = EVP_DigestSignInit(mdctx, &pkey_ctx, md, NULL, evp_pkey);
    error = EVP_DigestSignUpdate(mdctx, nonce.c_str(), nonce.length());
    unsigned char sig[256];
    size_t siglen;
    error = EVP_DigestSignFinal(mdctx, sig, &siglen);
    assert(sig);
    assert(siglen == 256);
    EVP_PKEY_free(evp_pkey);
    EVP_MD_CTX_destroy(mdctx);
    /* conver to base 64 string */
    unsigned char encoded[512];
    error = EVP_EncodeBlock(encoded, sig, siglen);
    assert(strlen((char*)encoded) > 0);
    return string(reinterpret_cast<char*>(encoded));
}

HttpSigner::HttpSigner(path const& private_key_path)
{
    m_private_key_path = private_key_path;
}

shared_ptr<DptRequest> Dpt::httpRequest(string const& url) const
{
    auto request = nativeformat::http::createRequest(baseUrl() + url, std::unordered_map<std::string, std::string>());
    if (m_cookies.find("Credentials") != m_cookies.end()) {
        request->headerMap()["Cookie"] = "Credentials=" + m_cookies.at("Credentials");
    }
    return static_pointer_cast<DptRequest>(request);
}

shared_ptr<DptResponse> Dpt::sendRequest(shared_ptr<DptRequest> request) const
{    
    #if DEBUG_REQUEST
        logger() << "request: " << request->serialise() << endl;
        logger() << "request body: " << request->body().substr(0,1000) << endl;
    #endif
    static auto client = nativeformat::http::createClient(nativeformat::http::standardCacheLocation(), "NFHTTP-" + nativeformat::http::version());
    auto resp = static_pointer_cast<DptResponse>(client->performRequestSynchronously(request));
    #if DEBUG_REQUEST
        logger() << "response: " << resp->serialise() << endl;
        logger() << "response body: "<< resp->body().substr(0,1000) << endl;
    #endif
    if (resp->statusCode() / 100 != 2) {
        logger() << "Received error response: " << resp->serialise() << endl;
        logger() << "Request responsible for the error was: " << request->serialise() << endl;
        throw "request failure";
    }
    return resp;
}

Json Dpt::sendJson(string const& method, string const& url, Json const& json) const
{
    auto request = httpRequest(url);
    request->setMethod(method);
    if (! json.empty()) {
        string json_str = json.toString();
        request->setData((unsigned char*)json_str.c_str(), json_str.length());
    }
    request->headerMap()["Content-Type"] = "application/json";
    auto response = sendRequest(request);
    string body = readResponse(response);
    return Json::fromString(body);
}

Json Json::fromString(string const& json) {    
    if (json.empty()) {
        return Json();
    }
    std::istringstream ss(json);
    Json data; 
    ptree* ptr = &data;
    boost::property_tree::read_json(ss, *ptr);
    return data;
}

string Dpt::readResponse(shared_ptr<DptResponse> response) const
{
    size_t len;
    unsigned char const* data = response->data(len);
    string rtv(reinterpret_cast<char const*>(data), len);
    return rtv;
}

string Dpt::getNonce(string client_id)
{
    Json json = sendJson("GET", "/auth/nonce/" + client_id);
    return json.get<string>("nonce");
}

string Dpt::baseUrl() const
{
    return "https://" + hostname() + ":" + std::to_string(port());
}

void Dpt::dptOpenDocument(path const& p)
{
    string const document_id = m_dpt_path_nodes[p.string()]->id();
    Json ptree;
    ptree.put("document_id", document_id);
    sendJson("PUT", "/viewer/controls/open2", ptree);
}

string Json::toString() const
{
    ostringstream os;
    boost::property_tree::write_json(os, *this, false);
    return os.str();
}

void Dpt::syncTime() const
{
    /* get current time */
    auto const now = std::time(nullptr);
    ostringstream os;
    os << std::put_time(std::gmtime(&now), "%FT%TZ"); // "2019-05-23T01:07:03Z";
    /* set time on DPT-RP1 */
    Json js;
    js.put("value", os.str());
    sendJson("PUT", "/system/configs/datetime", js);
}

string DptResponse::body() const
{
    size_t len;
    const unsigned char* body = this->data(len);
    return string(reinterpret_cast<char const*>(body), len);
}

string DptRequest::body() const
{
    size_t len;
    const unsigned char* body = this->data(len);
    return string(reinterpret_cast<char const*>(body), len);
}

void Dpt::updateDptTree()
{
    Json js = sendJson("GET", "/documents2?entry_type=all");
    m_dpt_path_nodes.clear();
    for (auto const& kv : js.get_child("entry_list")) {
        auto const& val = kv.second;
        string parent_path = path(val.get<string>("entry_path")).parent_path().string();
        if (m_dpt_path_nodes.find(parent_path) == m_dpt_path_nodes.end()) {
            m_dpt_path_nodes[parent_path] = make_shared<DNode>();
            auto parent = m_dpt_path_nodes[parent_path];
            assert(parent);
        }
        auto parent = m_dpt_path_nodes[parent_path];
        assert(parent);
        string self_path = val.get<string>("entry_path");
        if (m_dpt_path_nodes.find(self_path) == m_dpt_path_nodes.end()) {
            m_dpt_path_nodes[self_path] = make_shared<DNode>();
        }
        auto self = m_dpt_path_nodes[self_path];
        parent->addChild(self);
        /* must set all self's properties here */
        self->setId(val.get<string>("entry_id"));
        self->setFilename(val.get<string>("entry_name"));
        self->setIsDir(val.get<string>("entry_type") == "folder");
        self->setPath(val.get<string>("entry_path"));
        if (self->isDir()) {
            self->setRev("folder");
        } else {
            self->setFilesize(val.get<size_t>("file_size"));
            self->setRev(val.get<string>("file_revision"));
            self->setIsNote(val.get<string>("document_type") == "note");
        }
        /* get relpath */
        path::iterator p = self->path().begin();
        path relpath;
        for (p++; p != self->path().end(); p++) {
            relpath /= *p;
        }
        self->setRelPath(relpath);
    }
    m_dpt_tree = m_dpt_path_nodes["Document"];
    m_dpt_tree->setIsDir(true);
    m_dpt_tree->setId("root");
    m_dpt_tree->setFilename("Document");
    m_dpt_tree->setPath("Document");
    m_dpt_tree->setRelPath("");
    /* build revision node map */
    m_dpt_revision_nodes.clear();
    for (auto const& kv : m_dpt_path_nodes) {
        m_dpt_revision_nodes[kv.second->rev()] = kv.second;
    }
}

void Dpt::updateLocalTree()
{
    assert(is_directory(m_sync_dir));
    m_local_path_nodes.clear();
    m_local_tree = make_shared<DNode>();
    updateLocalNode(m_local_tree, m_sync_dir);
    /* build revision node map */
    m_local_revision_nodes.clear();
    for (auto const& kv : m_local_path_nodes) {
        m_local_revision_nodes[kv.second->rev()] = kv.second;
    }
}

void Dpt::updateLocalNode(shared_ptr<DNode> node, path const& local_path)
{
    m_local_path_nodes[local_path.string()] = node;
    /* must set all node's properties here */
    node->setLastModifiedTime(last_write_time(local_path));
    node->setFilename(local_path.filename().string());
    node->setPath(local_path);
    /* get relpath */
    path::iterator p = local_path.begin();
    // remove m_sync_dir from the prefix of local_path
    for (auto const _ : m_sync_dir) {
        (void)_;
        p++;
    }
    // add the rest to relpath
    path relpath;
    while (p != local_path.end()) {
        relpath /= *p;
        p++;
    }
    node->setRelPath(relpath);
    node->setRev(dpt::md5(node->path()));
    if (is_directory(local_path)) {
        node->setIsDir(true);
        for (auto const& i : directory_iterator(local_path)) {
            /* ignore hidden files */
            if (i.path().filename().string()[0] == '.') {
                continue;
            }
            /* only look at pdf files */
            if (is_regular_file(i) && i.path().extension() != ".pdf") {
                continue;
            }
            auto child = make_shared<DNode>();
            ifstream inf(i.path().c_str(), ios_base::binary|ios_base::in);
            child->setFilesize(readLocalFilesize(inf));
            node->addChild(child);
            updateLocalNode(child, i.path());
        }
    }
}

void Dpt::setSyncDir(path const& p)
{
    m_sync_dir = p;
}

path Dpt::syncDir() const
{
    return m_sync_dir;
}

void Dpt::moveBetweenDpt(path const& source, path const& dest)
{
    #if DEBUG_FILE_IO
    logger() << "moving dpt~>dpt: " << source << " ~> " << dest << endl;
    #endif
    shared_ptr<DNode const> source_node = m_dpt_path_nodes[source.string()];
    shared_ptr<DNode const> dest_parent_node = m_dpt_path_nodes[dest.parent_path().string()];
    if (source_node->isDir()) {
        Json js;
        js.put("parent_folder_id", dest_parent_node->id());
        js.put("folder_name", dest.filename().string());
        auto resp = sendJson("PUT", "/folder2/" + source_node->id(), js);
    } else {
        Json js;
        js.put("parent_folder_id", dest_parent_node->id());
        js.put("file_name", dest.filename().string());
        auto resp = sendJson("PUT", "/documents2/" + source_node->id(), js);
    }
}

void Dpt::moveBetweenLocal(path const& source, path const& dest)
{
    #if DEBUG_FILE_IO
    logger() << "moving local~>local: " << source << " ~> " << dest << endl;
    #endif
    copyBetweenLocal(source, dest);
    deleteFromLocal(source);
}

void Dpt::copyBetweenDpt(path const& source, path const& dest)
{
    #if DEBUG_FILE_IO
    logger() << "copying dpt~>dpt: " << source << " ~> " << dest << endl;
    #endif
    /* BFS */
    shared_ptr<DNode const> source_node = m_dpt_path_nodes[source.string()];
    std::queue<shared_ptr<DNode const>> que;
    que.push(source_node);
    while (! que.empty()) {
        shared_ptr<DNode const> n = que.front();
        que.pop();
        /* compute dest path */
        auto p = n->path().begin();
        for (auto const& _ : source) {
            (void)_;
            p++; // skip source as prefix of n->path()
        }
        // then make n_dest_path = dest + the rest of source
        path n_dest_path = dest;
        while (p != n->path().end()) {
            n_dest_path /= *p;
            p++;
        }
        shared_ptr<DNode const> dest_parent_node = m_dpt_path_nodes[n_dest_path.parent_path().string()];
        if (n->isDir()) {
            for (shared_ptr<DNode> c : n->children()) {
                que.push(c);
            }
            /* process directory */
            #if DEBUG_FILE_IO
            logger() << "creating dpt directory: " << n_dest_path << endl;
            #endif
            Json js;
            Json doc_copy_info;
            js.put("parent_folder_id", dest_parent_node->id());
            doc_copy_info.put("parent_folder_id", dest_parent_node->id());
            js.add_child("doc_copy_info", doc_copy_info);
            auto resp = sendJson("POST", "folders2", js);
            shared_ptr<DNode> new_node = make_shared<DNode>();
            new_node->setId(resp.get<string>("folder_id"));
            m_dpt_path_nodes[n_dest_path.string()] = new_node;
        } else {
            /* process file */
            #if DEBUG_FILE_IO
            logger() << "copying dpt file: " << n_dest_path << endl;
            #endif 
            Json js;
            Json doc_copy_info;
            js.put("parent_folder_id", dest_parent_node->id());
            doc_copy_info.put("parent_folder_id", dest_parent_node->id());
            js.add_child("doc_copy_info", doc_copy_info);
            auto resp = sendJson("POST", "/documents/" + n->id() + "/copy", js);
        }
    }
}

void Dpt::copyBetweenLocal(path const& source, path const& dest)
{
    // #if DEBUG_FILE_IO
    logger() << "copying local~>local: " << source << " ~> " << dest << endl;
    // #endif
    /* BFS */
    std::queue<path> que;
    que.push(source);
    while (! que.empty()) {
        path const n = que.front();
        que.pop();
        /* compute dest path */
        auto p = n.begin();
        for (auto const& _ : source) {
            (void)_;
            p++; // skip source as prefix of n
        }
        // then make n_dest = dest + the rest of source
        path n_dest = dest;
        while (p != n.end()) {
            n_dest /= *p;
            p++;
        }
        if (is_directory(n)) {
            for (auto const& i : directory_iterator(n)) {
                que.push(i.path());
            }
            /* process directory */
            // #if DEBUG_FILE_IO
            logger() << "creating local directory: " << n_dest << endl;
            // #endif
            boost::system::error_code error;
            boost::filesystem::copy_directory(n, n_dest, error);
        } else {
            /* process file */
            // #if DEBUG_FILE_IO
            logger() << "copying local file: " << n_dest << endl;
            // #endif 
            boost::system::error_code error;
            boost::filesystem::copy_file(n, n_dest, error);
        }
    }
}

vector<path> Dpt::dptOpenDocuments() const
{
    auto resp = sendJson("GET", "/viewer/status/current_viewing");
    vector<path> rtv;
    for (auto js : resp.get_child("views")) {
        rtv.push_back(path(js.second.get<string>("entry_path")));
    }
    return rtv;
}

void Dpt::computeSyncFiles()
{
    /* The following conditions must hold:
        - revision must be unique for each different file
        - revision must change if the file is modified
        - revision must stay the same when the file is moved
    */
    m_prepared_overwrite_to_dpt.clear();
    m_prepared_overwrite_from_dpt.clear();
    m_prepared_local_delete.clear();
    m_prepared_dpt_delete.clear();
    m_prepared_local_new.clear();
    m_prepared_dpt_new.clear();
    m_prepared_local_move.clear();
    m_prepared_dpt_move.clear();
    m_local_only_nodes.clear();
    m_dpt_only_nodes.clear();
    m_moved_nodes.clear();
    m_modified_nodes.clear();
    computeSyncFilesInNode(m_local_tree, m_dpt_tree);
    /* now try to match some only_local and only_dpt nodes */
    vector<shared_ptr<DNode const>> unmatchable_local_nodes;
    vector<shared_ptr<DNode const>> unmatchable_dpt_nodes;
    unordered_map<string,shared_ptr<DNode const>> prevpaths__nodes;
    unordered_set<shared_ptr<DNode const>> matched_local_nodes;
    /* find unmatchable dpt nodes */
    for (const auto local_node : m_local_only_nodes) {
        auto db_row = m_rev_db.getByLocalRev(local_node->rev());
        if (! db_row.empty()) {
            rpath prevpath = db_row[RelPath];
            prevpaths__nodes[prevpath.string()] = local_node;
        }
    }
    for (auto const dpt_node : m_dpt_only_nodes) {
        auto db_row = m_rev_db.getByDptRev(dpt_node->rev());
        if (! db_row.empty()) {
            rpath prevpath = db_row[RelPath];
            auto const local_node = prevpaths__nodes.find(prevpath.string());
            if (local_node == prevpaths__nodes.end()) {
                unmatchable_dpt_nodes.push_back(dpt_node);
            } else {
                matched_local_nodes.insert(local_node->second);
                m_moved_nodes.push_back(make_pair(local_node->second, dpt_node));
            }
        } else {
            unmatchable_dpt_nodes.push_back(dpt_node);
        }
    }
    for (auto const local_node : m_local_only_nodes) {
        if (matched_local_nodes.find(local_node) == matched_local_nodes.end()) {
            unmatchable_local_nodes.push_back(local_node);
        }
    }
    m_local_only_nodes = unmatchable_local_nodes;
    m_dpt_only_nodes = unmatchable_dpt_nodes;
    /* decide what to do with these nodes */
    for (auto const& local : m_local_only_nodes) {
        #if DEBUG_CONFLICT
            logger() << "--------------------- processing ---------------------" << endl
                 << "local: " << local->filename() << endl 
                 << "dpt: no match" << endl 
                 << "------------------------------------------------------" << endl;
        #endif
        vector<string> db_row = m_rev_db.getByRelPath(local->relPath());
        if (db_row.empty()) {
            /* if local file was not seen before */
            m_prepared_local_new.push_back(local);
        } else {
            if (db_row[LocalRev] == local->rev()) {
                /* if local file was seen before, unmodified, 
                    and dpt file is not found */
                m_prepared_local_delete.push_back(local);
            } else {
                /* if local file is modified */
                m_prepared_local_new.push_back(local);
            }
        }
    }
    for (auto const& dpt : m_dpt_only_nodes) {
        #if DEBUG_CONFLICT
            logger() << "--------------------- processing ---------------------" << endl
                 << "local: no match" << endl 
                 << "dpt: " << dpt->filename() << endl 
                 << "------------------------------------------------------" << endl;
        #endif
        vector<string> db_row = m_rev_db.getByRelPath(dpt->relPath());
        if (db_row.empty()) {
            /* if dpt file was not seen before */
            m_prepared_dpt_new.push_back(dpt);
        } else {
            if (db_row[DptRev] == dpt->rev()) {
                /* if dpt file was seen before, unmodified, 
                    and dpt file is not found*/
                m_prepared_dpt_delete.push_back(dpt);
            } else {
                /* if dpt file is modified */
                m_prepared_dpt_new.push_back(dpt);
            }
        }
    }
    auto both_nodes = m_modified_nodes;
    auto moved_nodes_offset = both_nodes.end();
    both_nodes.insert(moved_nodes_offset, m_moved_nodes.begin(), m_moved_nodes.end());
    for (auto ld = both_nodes.begin(); ld != both_nodes.end(); ld++) {
        auto const& local = ld->first;
        auto const& dpt = ld->second;
        #if DEBUG_CONFLICT
            logger() << "--------------------- processing ---------------------" << endl
                 << "local: " << local->filename() << " " << local->filesize() << endl 
                 << "dpt: " << dpt->filename() << " " << dpt->filesize() << endl
                 << "------------------------------------------------------" << endl;
        #endif
        vector<string> db_row = m_rev_db.getByLocalRev(local->rev());
        if (db_row.empty()) {
            db_row = m_rev_db.getByDptRev(dpt->rev());
        }
        if (db_row.empty()) {
            /* both files are new, conflict! */
            #if DEBUG_CONFLICT
                logger() << "both files are new, conflict." << endl;
            #endif
            // if (local->lastModifiedTime() > dpt->lastModifiedTime()) {
            //     /* local is newer */
            //     m_prepared_overwrite_to_dpt.push_back(local);
            //     if (local->relPath() != dpt->relPath()) {
            //         m_prepared_dpt_delete.push_back(dpt);
            //     }
            // } else {
                /* dpt is newer */
                m_prepared_overwrite_from_dpt.push_back(dpt);
                if (local->relPath() != dpt->relPath()) {
                    m_prepared_local_delete.push_back(local);
                }
            // }
        } else {
            #if DEBUG_CONFLICT
                logger() << "relpath found in db" << endl;
            #endif
            if (db_row[LocalRev] == local->rev()) {
                #if DEBUG_CONFLICT
                    logger() << "local version unchanged" << endl;
                #endif
                if (db_row[DptRev] == dpt->rev()) {
                    #if DEBUG_CONFLICT
                        logger() << "dpt version unchanged" << endl;
                    #endif
                    /* file is unchanged */
                    if (local->relPath() != dpt->relPath()) {
                        #if DEBUG_CONFLICT
                            logger() << "two files have different paths" << endl;
                        #endif
                        /* need to sync path */
                        if (local->lastModifiedTime() > dpt->lastModifiedTime()) {
                            #if DEBUG_CONFLICT
                                logger() << "local version have later last-modified date. will move dpt file" << endl;
                            #endif
                            m_prepared_dpt_move.push_back(make_pair(dpt, local));
                        } else {
                            #if DEBUG_CONFLICT
                                logger() << "dpt version have later last-modified date. will move local file/" << endl;
                            #endif
                            m_prepared_local_move.push_back(make_pair(dpt, local));
                        }
                    }
                } else {
                    #if DEBUG_CONFLICT
                        logger() << "dpt version has been modified and is the newer version" << endl;
                    #endif
                    /* dpt file is newer */
                    m_prepared_overwrite_from_dpt.push_back(dpt);
                    if (local->relPath() != dpt->relPath()) {
                        #if DEBUG_CONFLICT
                            logger() << "two files have different paths. local version will be deleted." << endl;
                        #endif
                        m_prepared_local_delete.push_back(local);
                    }
                }
            } else {
                #if DEBUG_CONFLICT
                    logger() << "local version has been modified" << endl;
                #endif
                if (db_row[DptRev] == dpt->rev()) {
                    #if DEBUG_CONFLICT
                        logger() << "dpt version unchanged" << endl;
                    #endif
                    /* local file is newer */
                    m_prepared_overwrite_to_dpt.push_back(local);
                    if (local->relPath() != dpt->relPath()) {
                        #if DEBUG_CONFLICT
                            logger() << "two files have different paths. dpt version will be deleted." << endl;
                        #endif
                        m_prepared_dpt_delete.push_back(dpt);
                    }
                } else {
                    #if DEBUG_CONFLICT
                        logger() << "dpt version has been modified. conflict!" << endl;
                    #endif
                    /* both are modified, conflict! */
                    if (local->lastModifiedTime() > dpt->lastModifiedTime()) {
                        #if DEBUG_CONFLICT
                            logger() << "local version have later last-modified date. use local file and delete dpt file" << endl;
                        #endif
                        /* local is newer */
                        m_prepared_overwrite_to_dpt.push_back(local);
                        if (local->relPath() != dpt->relPath()) {
                            m_prepared_dpt_delete.push_back(dpt);
                        }
                    } else {
                        #if DEBUG_CONFLICT
                            logger() << "dpt version have later last-modified date. use dpt file and delete local file" << endl;
                        #endif
                        /* dpt is newer */
                        m_prepared_overwrite_from_dpt.push_back(dpt);
                        if (local->relPath() != dpt->relPath()) {
                            m_prepared_local_delete.push_back(local);
                        }
                    }
                }
            }
        }
    }
}

void Dpt::updateRevDB()
{
    m_rev_db.reset();
    updateRevForNode(m_local_tree, m_dpt_tree);
}

void Dpt::updateRevForNode(shared_ptr<DNode const> local, shared_ptr<DNode const> dpt)
{
    assert(local->isDir() == dpt->isDir());
    assert(local->relPath() == dpt->relPath());
    m_rev_db.putRev(local->relPath(), local->rev(), dpt->rev());
    if (local->isDir()) {
        vector<shared_ptr<DNode>> only_local;
        vector<shared_ptr<DNode>> only_dpt;
        vector<pair<shared_ptr<DNode>,shared_ptr<DNode>>> both;
        symmetricDiff(local->children(), dpt->children(), only_local, only_dpt, both);
        if (! (only_local.empty() && only_dpt.empty())) {
//*
    logger() << "only_local: ";
    for (auto const& i : only_local) { logger() << "(" << i->filename() << ") "; }
    logger() << endl;
    logger() << "only_dpt: ";
    for (auto const& i : only_dpt) { logger() << "(" << i->filename() << ") "; }
    logger() << endl;
    logger() << "both: ";
    for (auto const& i : both) { logger() << "(" << i.first->filename() << ") "; }
    logger() << endl;
//*/
            throw "local tree and dpt tree are not identical";
        }
        /* for dirs & files that exist at both ends */
        for (auto const& i : both) {
            auto const& local_node = i.first;
            auto const& dpt_node = i.second;
            updateRevForNode(local_node, dpt_node);
        }
    }
}

void Dpt::computeSyncFilesInNode(shared_ptr<DNode const> local, shared_ptr<DNode const> dpt)
{
    assert(local->isDir());
    assert(dpt->isDir());
    vector<shared_ptr<DNode>> only_local;
    vector<shared_ptr<DNode>> only_dpt;
    vector<pair<shared_ptr<DNode>,shared_ptr<DNode>>> both;
    symmetricDiff(local->children(), dpt->children(), only_local, only_dpt, both);
#if DEBUG
/*
    logger() << "only_local: ";
    for (auto const& i : only_local) { logger() << "(" << i->filename() << ") "; }
    logger() << endl;
    logger() << "only_dpt: ";
    for (auto const& i : only_dpt) { logger() << "(" << i->filename() << ") "; }
    logger() << endl;
    logger() << "both: ";
    for (auto const& i : both) { logger() << "(" << i.first->filename() << ") "; }
    logger() << endl;
//*/
#endif
    m_local_only_nodes.insert(m_local_only_nodes.end(),only_local.begin(), only_local.end());
    m_dpt_only_nodes.insert(m_dpt_only_nodes.end(),only_dpt.begin(), only_dpt.end());
    /* for dirs & files that exist at both ends */
    for (auto const& i : both) {
        auto const& local_node = i.first;
        auto const& dpt_node = i.second;
        assert(local_node->isDir() == dpt_node->isDir());
        if (local_node->isDir()) {
            computeSyncFilesInNode(local_node, dpt_node);
        } else {
            m_modified_nodes.push_back(i);
        }
    }
}

size_t Dpt::bisectDptFileBytes(shared_ptr<DNode const> node, istream& local) const
{
    #if DEBUG_FILE_IO
        logger() << "bisecting file for differences..." << endl;
    #endif
    size_t const dpt_filesize = node->filesize();
    size_t const local_filesize = readLocalFilesize(local);
    /* bisection */
    int i = 0; // start
    int j = min(dpt_filesize, local_filesize) - 1;
    while (i < j) {
        int k = (i+j)/2;
        auto const local_bytes = readLocalFileBytes(local, k, 1);
        auto const dpt_bytes = readDptFileBytes(node, k, 1);
        char b1 = local_bytes->operator[](0);
        char b2 = dpt_bytes->operator[](0);
        if (b1 == b2) {
            if (k <= i) {
                break;
            }
            i = k;
        } else {
            if (k >= j) {
                break;
            }
            j = k;
        }
    }
    return i;
}

void Dpt::overwriteFromDpt(path const& source, path const& dest)
{
    #if DEBUG_FILE_IO
        logger() << "copying dpt~>local: " << source << " ~> " << dest << endl;
    #endif
    // boost::system::error_code error;
    // create_directories(dest.parent_path(), error);
    /* BFS */
    auto source_node = m_dpt_path_nodes[source.string()];
    std::queue<shared_ptr<DNode>> que;
    que.push(source_node);
    while (! que.empty()) {
        shared_ptr<DNode> n = que.front();
        que.pop();
        /* compute dest path */
        auto p = n->path().begin();
        for (auto const& _ : source) {
            (void)_;
            p++; // skip source as prefix of n->path()
        }
        // then make n_dest_path = dest + the rest of source
        path n_dest_path = dest;
        while (p != n->path().end()) {
            n_dest_path /= *p;
            p++;
        }
        if (n->isDir()) {
            for (shared_ptr<DNode> c : n->children()) {
                que.push(c);
            }
            /* process directory */
            #if DEBUG_FILE_IO
                logger() << "creating directory: " << n_dest_path << endl;
            #endif
            create_directory(n_dest_path);
        } else {
            /* process file */
            size_t const dpt_filesize = readDptFilesize(n);
            size_t const KB = 1024; // 1MB in bytes
            /* if local file exists, then bisect for the first byte two files diverse,
                and only download the different part */
            shared_ptr<LNode> local_node;
            if (m_local_path_nodes.find(n_dest_path.string()) == m_local_path_nodes.end()) {
                ofstream of(n_dest_path.string(), ios_base::binary|ios_base::out|ios_base::trunc);
                local_node = make_shared<LNode>();
                m_local_path_nodes[n_dest_path.string()] = local_node;
                local_node->setPath(n_dest_path);
                // todo: set properties
            }
            
            fstream iof(n_dest_path.string(), ios_base::binary|ios_base::out|ios_base::in);
            size_t const local_filesize = readLocalFilesize(iof);
            size_t offset = 0;
            if (! n->isNote()) {
                // doesn't quite work with notes
                offset = bisectDptFileBytes(n, iof); // where the difference starts
            }
            #if DEBUG_FILE_IO
                logger() << "writing file (offset=" 
                         << offset << ","
                         << std::hex << offset << std::dec 
                         << "): " << n_dest_path << endl;
            #endif
            
            iof.seekp(offset, ios_base::beg);
            while (offset < dpt_filesize) {
                auto const data = readDptFileBytes(n, offset, 128*KB);
                size_t const bytes = data->size();
                iof.write(reinterpret_cast<char*>(data->data()), bytes);
                offset = min(offset+bytes, dpt_filesize);
                int percentage = (offset*100)/dpt_filesize;
                m_messager("Downloading " + n->filename() + " " + to_string(percentage) + "%");
                #if DEBUG_FILE_IO
                    logger() << "writing " << percentage << "% " << offset << "/" << dpt_filesize << endl;
                #endif
            }
            /* pad zeros */
            if (offset < local_filesize) {
                #if DEBUG_FILE_IO
                    logger() << "padding zeros..." << endl;
                #endif
                while (offset < local_filesize) {
                    iof.put('\0');
                    offset++;
                }
            }
            #if DEBUG_FILE_IO
                logger() << "done writing file" << endl;
            #endif
        }
    }
}

shared_ptr<vector<uint8_t>> Dpt::readDptFileBytes(shared_ptr<DNode const> n, size_t offset, size_t size) const
{
    /* handle interrupt for lengthy operation */
    if (dpt::interrupt_flag) {
        throw SyncInterrupted();
    }
    auto request = httpRequest("/documents/" + n->id() + "/file");
    request->setMethod("GET");
    request->headerMap()["Range"] = "bytes=" + to_string(offset) + "-" + to_string(offset+size-1);
    auto response = sendRequest(request);
    size_t bytes;
    unsigned char const* data = response->data(bytes);
    auto const rtv = make_shared<vector<uint8_t>>(data, data+bytes);
    assert(rtv->size() == bytes);
    return rtv;
}

bool dpt::syncable(path const& path)
{
    if (path.filename().empty()) {
        return false;
    }
    if (path.filename().string()[0] == '.') {
        return false;
    }
    return is_directory(path) || (is_regular_file(path) && path.extension() == ".pdf");
}

size_t dpt::readLocalFilesize(istream& inf)
{
    inf.seekg(0, inf.end);
    return inf.tellg();
}

size_t Dpt::readDptFilesize(shared_ptr<DNode> node)
{
    /* Sometimes DPT-RP1 reports wrong filesize initially. This looks like a bug
        on DPT-RP1. We can force DPT-RP1 to update the file by reading a random
        byte of the file */
    readDptFileBytes(node, 0, 1);
    Json json = sendJson("GET", "/documents/" + node->id());
    node->setFilesize(json.get<size_t>("file_size"));
    return node->filesize();
}

shared_ptr<vector<uint8_t>> dpt::readLocalFileBytes(istream& inf, size_t offset, size_t size)
{
    size_t filesize = readLocalFilesize(inf);
    size = min(size, filesize - offset);
    inf.seekg(offset);
    auto buffer = make_shared<vector<uint8_t>>(size);
    inf.read(reinterpret_cast<char*>(buffer->data()), size);
    return buffer;
}

void Dpt::overwriteToDpt(path const& source, path const& dest) {
    #if DEBUG_FILE_IO
        logger() << "copying local~>dpt: " << source << " ~> " << dest << endl;
    #endif
    /* keep track of created folder ids */
    shared_ptr<DNode const> dest_parent_node = m_dpt_path_nodes[dest.parent_path().string()];
    /* BFS */
    std::queue<path> que;
    que.push(source);
    while (! que.empty()) {
        path n = que.front();
        que.pop();
        if (! syncable(n)) {
            continue;
        }
        /* compute dest path */
        auto p = n.begin();
        for (auto const& _ : source) {
            (void)_;
            p++; // skip source as prefix of n->path()
        }
        // then make n_dest_path = dest + the rest of source
        path n_dest_path = dest;
        while (p != n.end()) {
            n_dest_path /= *p;
            p++;
        }
        if (is_directory(n)) {
            for (auto const& i : directory_iterator(n)) {
                que.push(i.path());
            }
            /* create directory under n */
            #if DEBUG_FILE_IO
            logger() << "creating directory: " << n_dest_path << endl;
            #endif
            auto new_node = make_shared<DNode>();
            new_node->setPath(n_dest_path.string());
            new_node->setFilename(n_dest_path.filename().string());
            new_node->setIsDir(true);
            m_dpt_path_nodes[n_dest_path.string()] = new_node;
            Json js;
            js.put("parent_folder_id", m_dpt_path_nodes[n_dest_path.parent_path().string()]->id());
            js.put("folder_name", n_dest_path.filename().string());
            Json resp = sendJson("POST", "/folders2", js);
            string new_id = resp.get<string>("folder_id");
            new_node->setId(new_id);
        } else {
            /* process file */
            ifstream infile(n.string(), ios_base::binary|ios_base::in);
            size_t const local_filesize = readLocalFilesize(infile);
            size_t const KB = 1024;
            /* if local file exists, then bisect for the first byte two files diverse,
                and only download the different part */
            shared_ptr<DNode> dpt_node;
            // if file exists
            if (m_dpt_path_nodes.find(n_dest_path.string()) == m_dpt_path_nodes.end()) {
                #if DEBUG_FILE_IO
                    logger() << "creating file on dpt: " << n_dest_path << endl;
                #endif
                Json js;
                js.put("parent_folder_id", m_dpt_path_nodes[n_dest_path.parent_path().string()]->id());
                js.put("file_name", n_dest_path.filename().string());
                Json resp = sendJson("POST", "/documents2", js);
                dpt_node = make_shared<DNode>();
                dpt_node->setPath(n_dest_path.string());
                dpt_node->setFilename(n_dest_path.filename().string());
                dpt_node->setId(resp.get<string>("document_id"));
                dpt_node->setIsDir(false);
                dpt_node->setFilesize(0);
                m_dpt_path_nodes[n_dest_path.string()] = dpt_node;
            }
            dpt_node = m_dpt_path_nodes[n_dest_path.string()];
            /* partially write to file is not supported on DPT */ 
            size_t offset = 0; 
            size_t const new_filesize = local_filesize;
            
            #if DEBUG_FILE_IO
                logger() << "writing file (offset=" 
                         << offset << ","
                         << std::hex << offset << std::dec 
                         << "): " << n_dest_path << endl;
            #endif
            while (offset < local_filesize) {
                auto const data = readLocalFileBytes(infile, offset, 128*KB);
                size_t bytes = data->size();
                writeDptFileBytes(dpt_node, offset, new_filesize, data);
                offset = min(offset+bytes, local_filesize);
                int percentage = (offset*100)/local_filesize;
                m_messager("Uploading " + dpt_node->filename() + " " + to_string(percentage) + "%");
                #if DEBUG_FILE_IO
                    logger() << "writing " << percentage << "% " << offset << "/" << local_filesize << endl;
                #endif
            }
            /* pad zeros */
            /*
            if (offset < new_filesize) {
                #if DEBUG_FILE_IO
                    logger() << "padding zeros..." << endl;
                #endif
                while (offset < new_filesize) {
                    auto zeros = make_shared<vector<uint8_t>>();
                    zeros->push_back('\0');
                    writeDptFileBytes(dpt_node, offset, new_filesize, zeros);
                    offset++;
                }
            }
            */
            #if DEBUG_FILE_IO
                logger() << "done writing file" << endl;
            #endif
        }
    }
}

void Dpt::writeDptFileBytes(shared_ptr<DNode const> node, size_t offset, size_t total, shared_ptr<vector<uint8_t>> bytes) const
{
    /* handle interrupt for lengthy operation */
    if (dpt::interrupt_flag) {
        throw SyncInterrupted();
    }
    assert(total);
    assert(bytes->size());
    assert(offset + bytes->size() <= total);
    auto request = httpRequest("/documents/" + node->id() + "/file?offset_bytes=" + to_string(offset) + "&total_bytes=" + to_string(total) + "&last_byte=" + to_string(offset + bytes->size()));
    request->setMethod("PUT");
    request->headerMap()["Content-Type"] = "multipart/form-data; boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW";
    /* build header */
    stringstream ss(ios_base::in|ios_base::out|ios_base::binary);
    ss << "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" 
        << node->path().string() 
        << "\"\r\nContent-Type: application/pdf\r\n\r\n";
    ss.write(reinterpret_cast<char*>(bytes->data()), bytes->size());
    ss << "\r\n------WebKitFormBoundary7MA4YWxkTrZu0gW--\r\n";
    ss.seekp(0, ss.end);
    size_t sslen = ss.tellp();
    ss.seekp(0);
    vector<char> message(sslen);
    ss.read(message.data(), sslen);
    request->setData(reinterpret_cast<unsigned char const*>(message.data()), sslen);
    sendRequest(request);
}


void Dpt::deleteFromLocal(path const& file) {
    boost::filesystem::remove_all(file);
}

void Dpt::syncAllFiles()
{
    m_messager("Syncing Device Time...");
    syncTime();
    for (auto const& i : m_prepared_dpt_delete) {
        m_messager("Syncing " + i->filename()+ "...");
        deleteFromDpt(i->path());
    }
    for (auto const& i : m_prepared_local_delete) {
        m_messager("Syncing " + i->filename()+ "...");
        deleteFromLocal(i->path());
    }
    for (auto const& i : m_prepared_dpt_new) {
        m_messager("Syncing " + i->filename()+ "...");
        overwriteFromDpt(i->path(), m_sync_dir / i->relPath());
    }
    for (auto const& i : m_prepared_local_new) {
        m_messager("Syncing " + i->filename()+ "...");
        overwriteToDpt(i->path(), "Document" / i->relPath());
    }
    for (auto const& i : m_prepared_overwrite_to_dpt) {
        m_messager("Syncing " + i->filename()+ "...");
        overwriteToDpt(i->path(), "Document" / i->relPath());
    }
    for (auto const& i : m_prepared_overwrite_from_dpt) {
        m_messager("Syncing " + i->filename()+ "...");
        overwriteFromDpt(i->path(), m_sync_dir / i->relPath());
    }
    for (auto const& i : m_prepared_local_move) {
        m_messager("Syncing " + i.first->filename()+ "...");
        moveBetweenLocal(i.first->path(), m_sync_dir / i.second->relPath());
    }
    for (auto const& i : m_prepared_dpt_move) {
        m_messager("Syncing " + i.first->filename()+ "...");
        moveBetweenDpt(i.first->path(), "Document" / i.second->relPath());
    }
}

void Dpt::deleteFromDpt(path const& dpt)
{
    auto const& node = m_dpt_path_nodes[dpt.string()];
    if (node->isDir()) {
        sendJson("DELETE", "/folders/" + node->id());
    } else {
        sendJson("DELETE", "/documents/" + node->id());
    }
}

void Dpt::reportComputedSyncFiles()
{
    if (! m_prepared_dpt_delete.empty()) {
        logger() << "These files will be deleted from DPT-RP1:" << endl;
        for (auto const& i : m_prepared_dpt_delete) {
            logger() << " - ";
            if (i->isDir()) {
                logger() << "(folder) ";
            }
            logger() << i->relPath() << endl;
        }
        logger() << endl;
    }
    if (! m_prepared_local_delete.empty()) {
        logger() << "These local files will be deleted:" << endl;
        for (auto const& i : m_prepared_local_delete) {
            logger() << " - ";
            if (i->isDir()) {
                logger() << "(folder) ";
            }
            logger() << i->relPath() << endl;
        }
        logger() << endl;
    }
    if (! m_prepared_local_new.empty()) {
        logger() << "These files will be created on DPT-RP1:" << endl;
        for (auto const& i : m_prepared_local_new) {
            logger() << " - ";
            if (i->isDir()) {
                logger() << i->relPath() << " (folder)" << endl;
            } else {
                logger() << i->relPath() << " (" << i->filesize() << ")"<< endl;
            }
        }
        logger() << endl;
    }
    if (! m_prepared_dpt_new.empty()) {
        logger() << "These local files will be created:" << endl;
        for (auto const& i : m_prepared_dpt_new) {
            logger() << " - ";
            if (i->isDir()) {
                logger() << i->relPath() << " (folder)" << endl;
            } else {
                logger() << i->relPath() << " (" << i->filesize() << ")"<< endl;
            }
        }
        logger() << endl;
    }
    if (! m_prepared_overwrite_to_dpt.empty()) {
        logger() << "These files will be updated on DPT-RP1:" << endl;
        for (auto const& i : m_prepared_overwrite_to_dpt) {
            logger() << " - ";
            if (i->isDir()) {
                logger() << i->relPath() << " (folder)" << endl;
            } else {
                logger() << i->relPath() << " (" << i->filesize() << ")"<< endl;
            }
        }
        logger() << endl;
    }
    if (! m_prepared_overwrite_from_dpt.empty()) {
        logger() << "These local files will be updated:" << endl;
        for (auto const& i : m_prepared_overwrite_from_dpt) {
            logger() << " - ";
            if (i->isDir()) {
                logger() << i->relPath() << " (folder)" << endl;
            } else {
                logger() << i->relPath() << " (" << i->filesize() << ")"<< endl;
            }
        }
        logger() << endl;
    }
    if (! m_prepared_dpt_move.empty()) {
        logger() << "These DPT-RP1 files will be moved:" << endl;
        for (auto const& i : m_prepared_dpt_move) {
            logger() << " - ";
            if (i.first->isDir()) {
                logger() << "(folder) ";
            }
            logger() << i.first->relPath() << " ~> " << i.second->relPath() << endl;
        }
        logger() << endl;
    }
    if (! m_prepared_local_move.empty()) {
        logger() << "These local files will be moved:" << endl;
        for (auto const& i : m_prepared_local_move) {
            logger() << " - ";
            if (i.first->isDir()) {
                logger() << "(folder) ";
            }
            logger() << i.first->relPath() << " ~> " << i.second->relPath() << endl;
        }
        logger() << endl;
    }
}

void Dpt::safeSyncAllFiles(DryRunFlag dryrun)
{
    dpt::interrupt_flag = 0;
    {
        m_messager("Computing Differences...");
        dbOpen();
        updateLocalTree();
        updateDptTree();
        computeSyncFiles();
        reportComputedSyncFiles();
        dbClose(); // git checkout would invalidate db connection
        // do not write to db if there's not change
        // git would think there's a modification
        if (m_prepared_dpt_delete.empty()
            && m_prepared_local_delete.empty()
            && m_prepared_dpt_new.empty()
            && m_prepared_local_new.empty()
            && m_prepared_dpt_move.empty()
            && m_prepared_local_move.empty()
            && m_prepared_overwrite_from_dpt.empty()
            && m_prepared_overwrite_to_dpt.empty())
        {
            logger() << "All files are identical." << endl;
            m_messager("All Up-to-Date");
            return;
        }
        if (dryrun) {
            logger() << "Action aborted due to dry-run flag." << endl;
            return;
        }
    }
    {   
        m_messager("Creating Backup...");
        /* 
            create presync checkpoint 
                this must be done before the try block
                because otherwise if error happens git reset will cause 
                user to lose data!
        */
        m_git->checkout("master");
        m_git->addAll();
        string status = m_git->status();
        m_git->commit("<local pre-sync checkpoint>\n\n" + status);
    }
    try {
            signal(SIGINT, [](int sig) {
                if (SIGINT == sig) {
                    dpt::interrupt_flag = 1;
                }
            });  
            {   
                m_messager("Creating Backup...");
                /* backup files about to be changed on dpt */
                gptr<git_status_list> stats;
                gptr<git_reference> head;
                m_git->status(head, stats);
                m_git->branch("dpt");
                m_git->checkout("dpt");
                for (auto const& dpt : m_prepared_dpt_delete) {
                    overwriteFromDpt(dpt->path(), m_sync_dir / dpt->relPath());
                }
                for (auto const& local : m_prepared_overwrite_to_dpt) {
                    path dptpath = "Document" / local->relPath();
                    auto dpt = m_dpt_path_nodes.find(dptpath.string());
                    if (dpt != m_dpt_path_nodes.end()) {
                        overwriteFromDpt(dpt->second->path(), local->path());
                    }
                }
                // TO-do: handle move
                m_git->addAll();
                string status = m_git->status();
                std::replace(status.begin(),status.end(), '\'', '\"');
                m_git->commit("<dpt pre-sync checkpoint>\n\n" + status);
                m_git->tag("dpt_" + string(git_oid_tostr_s(git_reference_target(head))).substr(0,7));
            }
            {   
                m_messager("Syncing...");
                /* start syncing */
                m_git->checkout("master");
                logger() << "Syncing started. Do not disconnect!" << endl;
                dbOpen();
                syncAllFiles();
                updateLocalTree();
                updateDptTree();
                updateRevDB();
                dbClose(); // git checkout would invalidate db connection
                logger() << "All files are synced." << endl;
                m_messager("All Up-to-Date");
            }
    } catch(SyncInterrupted) {
        m_messager("Sync Stopped");
        logger() << "interrupted" << endl;
        m_git->checkout("master");
        m_git->addAll();
        m_git->resetHard();
        throw;
    } catch (...) {
        logger() << "An error happend during syncing, changes will be reverted." << endl;
        m_messager("Sync Failed");
        m_git->checkout("master");
        m_git->addAll();
        m_git->resetHard();
        throw;
    }
    {
        m_git->addAll();
        string status = m_git->status();
        m_git->commit("<local post-sync checkpoint>\n\n" + status);
    }
}

void Dpt::setPrivateKeyPath(path const& key) {
    #if DEBUG_AUTH
    logger() << "using private key: " << key << endl;
    #endif
    m_private_key_path = key;
}

void Dpt::setClientIdPath(path const& id) {
    #if DEBUG_AUTH
    logger() << "using client id: " << id << endl;
    #endif
    m_client_id_path = id;
}

void Dpt::setPort(unsigned port) noexcept { m_port = port; }

void Dpt::dbOpen()
{
    using namespace boost;
    assert(! m_sync_dir.empty() && "don't forget to set sync dir");
    path rev_db = m_sync_dir / ".rev";
    if (! filesystem::exists(rev_db)) {
        filesystem::copy_file("rev_db", rev_db);
    }
    m_rev_db.open(rev_db);
}

void Dpt::dbClose()
{
    m_rev_db.close();
}

string Dpt::git(string const& command) const
{
    cerr << "deprecated warning: git(\"" << command << "\") called" << endl;
    assert(! m_sync_dir.empty() && "don't forget to set sync dir");
    if (command == "add --all") {
        /* git does not track emtpy dirs, a dirty trick is needed */
        queue<path> que;
        que.push(m_sync_dir);
        while (! que.empty()) {
            auto const p = que.front();
            que.pop();
            if (is_directory(p)) {
                bool has_child = false;
                for (auto const c : directory_iterator(p)) {
                    que.push(c.path());
                    has_child = true;
                }
                if (! has_child) {
                    path f = p / ".gitkeep";
                    ofstream of(f.string());
                    of << "1" << endl;
                }
            }
        }
    }
    std::array<char, 128> buffer;
    std::string result;
    string git_cmd;
    if (m_git_path.empty()) {
        git_cmd = "git " + command;
    } else {
        git_cmd = "\"" + m_git_path.string() + "\" " + command;
    }
    #if DEBUG_GIT
        logger() << git_cmd << endl;
    #endif
    string shell_cmd = "cd " + m_sync_dir.string() + " && " + git_cmd + " 2>&1 ";
    FILE* pipe = popen(shell_cmd.c_str(), "r");
    if (!pipe) {
        logger() << "Couldn't start command." << endl;
        return 0;
    }
    while (fgets(buffer.data(), 128, pipe) != NULL) {
        result += buffer.data();
    }
    pclose(pipe);
    #if DEBUG_GIT
        logger() << result << std::endl;
    #endif
    return result;
}

Dpt::~Dpt()
{
    dbClose();
}

void Dpt::setupSyncDir()
{
    using namespace boost;
    assert(! m_sync_dir.empty() && "don't forget to set sync dir");
    /* resolve path issue with non-ascii filename */
    // git("config core.quotepath false"); 
    path git_ignore = m_sync_dir / ".gitignore";
    if (! filesystem::exists(git_ignore)) {
        filesystem::copy_file("gitignore", git_ignore);
    }
    path hidden_dir = m_sync_dir / ".app";
    if (! filesystem::exists(hidden_dir)) {
        filesystem::create_directory(hidden_dir);
    }
    path rev_db = m_sync_dir / ".rev";
    if (! filesystem::exists(rev_db)) {
        filesystem::copy_file("rev_db", rev_db);
    }
    m_git = make_shared<Git>(m_sync_dir);
}

void Dpt::setMessager(std::function<void(string const&)> me) noexcept
{
    m_messager = me;
}

void Dpt::setLogger(ostream& log) noexcept
{
    m_logger = &log;
}

ostream& Dpt::logger() const { return *m_logger; }

void Dpt::dptQuickUploadAndOpen(path const& local)
{
    try {
        // TODO: make this code faster
        updateDptTree();
        if (m_dpt_path_nodes.find("Document/Received") == m_dpt_path_nodes.end()) {
            Json js;
            js.put("parent_folder_id", "root");
            js.put("folder_name", "Received");
            Json resp = sendJson("POST", "/folders2", js);
            string new_id = resp.get<string>("folder_id");
            updateDptTree();
        }
        path p = "Document/Received" / local.filename();
        if (m_dpt_path_nodes.find(p.string()) == m_dpt_path_nodes.end()) {    
            m_messager("Uploading " + local.filename().string() + "...");
            overwriteToDpt(local, p);
        } 
        m_messager("Opening " + local.filename().string() + "...");
        dptOpenDocument(p);
        m_messager("All Up-to-Date");
    } catch(...) {
        m_messager("Failed to Upload " + local.filename().string());
        throw;
    }
}

vector<shared_ptr<GitCommit>> Dpt::listGitCommits(size_t limit) const
{
    size_t lim = min(limit, m_git_commits.size());
    return vector<shared_ptr<GitCommit>>(m_git_commits.begin(), m_git_commits.begin() + lim);
}

void Dpt::updateGitCommits() 
{
    assert(! m_sync_dir.empty() && "don't forget to set sync dir");
    m_git_commits.clear();
    auto&& list = m_git->history(100);
    cerr << list.size() << endl;
    for (auto& gptr_commit : list) 
    {
        shared_ptr<GitCommit> commit = make_shared<GitCommit>();
        git_oid const* oid = git_commit_id(gptr_commit);
        commit->commit = git_oid_tostr_s(oid);
        commit->message = git_commit_message(gptr_commit);
        size_t newline_pos = commit->message.find('\n', 0);
        if (newline_pos == string::npos) {
            commit->title = commit->message;
        } else {
            commit->title = commit->message.substr(0,newline_pos);
        }
        time_t commit_time = git_commit_time(gptr_commit);
        char time_str[26];
        commit->time = *std::gmtime(&commit_time);
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%dT%T+00:00", &commit->time);
        commit->iso8601_time = std::move(time_str);
        m_git_commits.push_back(commit);
    }
}

void Dpt::extractGitCommit(string const& commit, path const& dest) 
{
    assert(! m_sync_dir.empty() && "don't forget to set sync dir");
    {
        m_git->addAll();
        string status = m_git->status();
        m_git->commit("<pre-extraction-sync checkpoint>\n\n" + status);
    }
    try {
        m_git->checkout(commit);
        copyBetweenLocal(m_sync_dir, dest);
    } catch (...) {
        m_git->checkout("master");
        throw;    
    }
    m_git->checkout("master");
}

ostream& dpt::operator<<(ostream& out, GitCommit& commit)
{
    return out << commit.commit << endl
               << commit.iso8601_time << endl
               << commit.title << endl
               << commit.message << endl;
}

void Dpt::stop() {
    m_messager("Stopping...");
    dpt::interrupt_flag = 1;
}

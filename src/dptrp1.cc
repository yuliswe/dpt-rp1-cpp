#include <dptrp1.h>
#include <boost/property_tree/json_parser.hpp>
#include <boost/asio.hpp>
#include <NFHTTP/NFHTTP.h>
#include <sstream>
#include <iostream>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <ctime>
#include <iomanip>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <algorithm>
#include <memory>
#include <queue>
#include <unordered_set>

using namespace dpt;
using namespace std;

using std::shared_ptr;

using boost::asio::ip::address;
using boost::asio::io_service;
using boost::asio::io_context;
using boost::filesystem::path;
using boost::filesystem::is_directory;
using boost::filesystem::create_directory;
using boost::filesystem::is_regular_file;
using boost::filesystem::directory_iterator;
using boost::filesystem::last_write_time;
using boost::filesystem::last_write_time;
using endpoint = boost::asio::ip::tcp::endpoint;

using nativeformat::http::Request;
using nativeformat::http::Response;

/*
Dpt::Dpt(unsigned port)
{
    using resolver = boost::asio::ip::tcp::resolver;
    m_port = port;
    io_service svc;
    io_context ctx;
    resolver res(ctx);
    resolver::query query(hostname(), "http");
    resolver::iterator iter = res.resolve(query);
    resolver::iterator end;
    endpoint ep = *iter;
    m_ip = ep.address();
}
*/

// Dpt::Dpt(address const& ip, unsigned port)
// {
//     m_ip = ip;
//     m_port = port;
// }

void Dpt::authenticate()
{
    assert(! m_private_key_path.empty() && "don't forget to set private key");
    assert(! m_client_id_path.empty() && "don't forget to set client id");

    m_messager("Connecting to DPT-RP1...");

    try {

        string client_id;
        ifstream inf(m_client_id_path.string(), std::ios_base::in);
        inf >> client_id;

        using boost::property_tree::ptree;
        using boost::property_tree::write_json;
        /* prepare signature */
        HttpSigner signer(m_private_key_path);
        string nonce = getNonce(client_id);
        string nonce_signed = signer.sign(nonce);
        /* write data to send */
        ptree data;
        data.put("client_id", client_id);
        data.put("nonce_signed", nonce_signed);
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
            cerr << "using credential: " << credentials << endl;
        #endif 
        m_cookies["Credentials"] = credentials;

    } catch(...) {
        m_messager("Failed to Connect DPT-RP1");
        throw;
    }
}

unsigned Dpt::port() const { return m_port; }
string Dpt::hostname() const { return m_hostname; }

string HttpSigner::sign(string nonce)
{
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
    assert(EVP_DigestSignInit(mdctx, &pkey_ctx, md, NULL, evp_pkey));
    assert(EVP_DigestSignUpdate(mdctx, nonce.c_str(), nonce.length()));
    size_t siglen = 256;
    unsigned char sig[siglen];
    assert(EVP_DigestSignFinal(mdctx, sig, &siglen));
    assert(sig);
    assert(siglen == 256);
    EVP_PKEY_free(evp_pkey);
    EVP_MD_CTX_destroy(mdctx);
    /* conver to base 64 string */
    unsigned char encoded[512];
    assert(EVP_EncodeBlock(encoded, sig, siglen));
    size_t encode_len = strlen((char*)encoded);
    assert(encode_len > 0);
    return string((char*)encoded);
}

HttpSigner::HttpSigner(path const& private_key_path)
{
    m_private_key_path = private_key_path;
}

shared_ptr<DptRequest> Dpt::httpRequest(string url) const
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
        cerr << "request: " << request->serialise() << endl;
        cerr << "request body: " << request->body().substr(0,1000) << endl;
    #endif
    static auto client = nativeformat::http::createClient(nativeformat::http::standardCacheLocation(), "NFHTTP-" + nativeformat::http::version());
    auto resp = static_pointer_cast<DptResponse>(client->performRequestSynchronously(request));
    #if DEBUG_REQUEST
        cerr << "response: " << resp->serialise() << endl;
        cerr << "response body: "<< resp->body().substr(0,1000) << endl;
    #endif
    if (resp->statusCode() < 200 || resp->statusCode() >= 300) {
        cerr << "Received error response: " << resp->serialise() << endl;
        cerr << "Request responsible for the error was: " << request->serialise() << endl;
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

void Dpt::dptOpenDocument(string document_id) const
{
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
            self->setRev(val.get<string>("file_revision"));
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
    cerr << "moving dpt~>dpt: " << source << " ~> " << dest << endl;
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
    cerr << "moving local~>local: " << source << " ~> " << dest << endl;
    #endif
    copyBetweenLocal(source, dest);
    deleteFromLocal(source);
}

void Dpt::copyBetweenDpt(path const& source, path const& dest)
{
    #if DEBUG_FILE_IO
    cerr << "copying dpt~>dpt: " << source << " ~> " << dest << endl;
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
            cerr << "creating dpt directory: " << n_dest_path << endl;
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
            cerr << "copying dpt file: " << n_dest_path << endl;
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

void Dpt::copyBetweenLocal(path const& from, path const& to)
{
    boost::system::error_code ec;
    boost::filesystem::copy(from, to, ec);
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
            cerr << "--------------------- processing ---------------------" << endl
                 << local->filename() << endl 
                 << "no matching dpt file" << endl 
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
            cerr << "--------------------- processing ---------------------" << endl
                 << "no matching local file" << endl 
                 << dpt->filename() << endl 
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
            cerr << "--------------------- processing ---------------------" << endl
                 << local->filename() << endl 
                 << dpt->filename() << endl
                 << "------------------------------------------------------" << endl;
        #endif
        vector<string> db_row = m_rev_db.getByLocalRev(local->rev());
        if (db_row.empty()) {
            /* db_row is empty can only happen for nodes in m_modified_nodes */
            assert(ld < moved_nodes_offset);
            /* both files are new, conflict! */
            #if DEBUG_CONFLICT
                cerr << "both files are new, conflict." << endl;
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
                cerr << "relpath was seen before" << endl;
            #endif
            if (db_row[LocalRev] == local->rev()) {
                #if DEBUG_CONFLICT
                    cerr << "local version was unchanged" << endl;
                #endif
                if (db_row[DptRev] == dpt->rev()) {
                    #if DEBUG_CONFLICT
                        cerr << "dpt version was unchanged" << endl;
                    #endif
                    /* file is unchanged */
                    if (local->relPath() != dpt->relPath()) {
                        #if DEBUG_CONFLICT
                            cerr << "two files have different paths" << endl;
                        #endif
                        /* need to sync path */
                        if (local->lastModifiedTime() > dpt->lastModifiedTime()) {
                            #if DEBUG_CONFLICT
                                cerr << "local version have later last-modified date. will move dpt file" << endl;
                            #endif
                            m_prepared_dpt_move.push_back(make_pair(dpt, local));
                        } else {
                            #if DEBUG_CONFLICT
                                cerr << "dpt version have later last-modified date. will move local file/" << endl;
                            #endif
                            m_prepared_local_move.push_back(make_pair(dpt, local));
                        }
                    }
                } else {
                    #if DEBUG_CONFLICT
                        cerr << "dpt version has been modified and is the newer version" << endl;
                    #endif
                    /* dpt file is newer */
                    m_prepared_overwrite_from_dpt.push_back(dpt);
                    if (local->relPath() != dpt->relPath()) {
                        #if DEBUG_CONFLICT
                            cerr << "two files have different paths. local version will be deleted." << endl;
                        #endif
                        m_prepared_local_delete.push_back(local);
                    }
                }
            } else {
                #if DEBUG_CONFLICT
                    cerr << "local version has been modified" << endl;
                #endif
                if (db_row[DptRev] == dpt->rev()) {
                    #if DEBUG_CONFLICT
                        cerr << "dpt version was unchanged" << endl;
                    #endif
                    /* local file is newer */
                    m_prepared_overwrite_to_dpt.push_back(local);
                    if (local->relPath() != dpt->relPath()) {
                        #if DEBUG_CONFLICT
                            cerr << "two files have different paths. dpt version will be deleted." << endl;
                        #endif
                        m_prepared_dpt_delete.push_back(dpt);
                    }
                } else {
                    #if DEBUG_CONFLICT
                        cerr << "dpt version has been modified. conflict!" << endl;
                    #endif
                    /* both are modified, conflict! */
                    if (local->lastModifiedTime() > dpt->lastModifiedTime()) {
                        #if DEBUG_CONFLICT
                            cerr << "local version have later last-modified date. use local file and delete dpt file" << endl;
                        #endif
                        /* local is newer */
                        m_prepared_overwrite_to_dpt.push_back(local);
                        if (local->relPath() != dpt->relPath()) {
                            m_prepared_dpt_delete.push_back(dpt);
                        }
                    } else {
                        #if DEBUG_CONFLICT
                            cerr << "dpt version have later last-modified date. use dpt file and delete local file" << endl;
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
    cerr << "only_local: ";
    for (auto const& i : only_local) { cerr << "(" << i->filename() << ") "; }
    cerr << endl;
    cerr << "only_dpt: ";
    for (auto const& i : only_dpt) { cerr << "(" << i->filename() << ") "; }
    cerr << endl;
    cerr << "both: ";
    for (auto const& i : both) { cerr << "(" << i.first->filename() << ") "; }
    cerr << endl;
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
    cerr << "only_local: ";
    for (auto const& i : only_local) { cerr << "(" << i->filename() << ") "; }
    cerr << endl;
    cerr << "only_dpt: ";
    for (auto const& i : only_dpt) { cerr << "(" << i->filename() << ") "; }
    cerr << endl;
    cerr << "both: ";
    for (auto const& i : both) { cerr << "(" << i.first->filename() << ") "; }
    cerr << endl;
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

void Dpt::overwriteFromDpt(path const& source, path const& dest)
{
    #if DEBUG_FILE_IO
    cerr << "copying dpt~>local: " << source << " ~> " << dest << endl;
    #endif
    // boost::system::error_code error;
    // create_directories(dest.parent_path(), error);
    /* BFS */
    auto source_node = m_dpt_path_nodes[source.string()];
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
        if (n->isDir()) {
            for (shared_ptr<DNode> c : n->children()) {
                que.push(c);
            }
            /* process directory */
            #if DEBUG_FILE_IO
            cerr << "creating directory: " << n_dest_path << endl;
            #endif
            create_directory(n_dest_path);
        } else {
            /* process file */
            cerr << "receiving: " << n_dest_path << endl;
            auto request = httpRequest("/documents/" + n->id() + "/file");
            request->setMethod("GET");
            auto response = sendRequest(request);
            #if DEBUG_FILE_IO
            cerr << "writing file: " << n_dest_path << endl;
            #endif
            ofstream of(n_dest_path.string(), std::ios_base::binary|std::ios_base::trunc);
            size_t bytes;
            unsigned char const* data = response->data(bytes);
            of.write(reinterpret_cast<char const*>(data), bytes);
        }
    }
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

void Dpt::overwriteToDpt(path const& source, path const& dest) {
    #if DEBUG_FILE_IO
    cerr << "copying local~>dpt: " << source << " ~> " << dest << endl;
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
            cerr << "creating directory: " << n_dest_path << endl;
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
            #if DEBUG_FILE_IO
            cerr << "creating file: " << n_dest_path << endl;
            #endif
            Json js;
            js.put("parent_folder_id", m_dpt_path_nodes[n_dest_path.parent_path().string()]->id());
            js.put("file_name", n_dest_path.filename().string());
            Json resp = sendJson("POST", "/documents2", js);
            string file_id = resp.get<string>("document_id");
            #if DEBUG_FILE_IO
            cerr << "writing file: " << n_dest_path << endl;
            #endif
            ifstream inf(n.string(), std::ios_base::in|std::ios_base::binary);
            /* get size of file */
            inf.seekg(0,inf.end);
            size_t size = inf.tellg();
            inf.seekg(0);
            char buffer[size];
            inf.read(buffer, size);
            /* send requet */
            auto request = httpRequest("/documents/" + file_id + "/file");
            request->setMethod("PUT");
            request->headerMap()["Content-Type"] = "multipart/form-data; boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW";
            stringstream ss(ios_base::in|ios_base::out|ios_base::binary);
            ss << "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" 
               << n_dest_path.filename().string() 
               << "\"\r\nContent-Type: application/pdf\r\n\r\n";
            ss.write(buffer, size);
            ss << "\r\n------WebKitFormBoundary7MA4YWxkTrZu0gW--\r\n";
            ss.seekp(0, ios::end);
            stringstream::pos_type offset = ss.tellp();
            ss.seekp(0);
            char message[static_cast<size_t>(offset)];
            ss.read(message, offset);
            request->setData(reinterpret_cast<unsigned char const*>(message), offset);
            cerr << readResponse(sendRequest(request)) << endl;
        }
    }
}

void Dpt::deleteFromLocal(path const& file) {
    boost::filesystem::remove_all(file);
}

void Dpt::syncAllFiles()
{
    syncTime();
    m_messager("Syncing Device Time...");
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
        cerr << "These files will be deleted from DPT-RP1:" << endl;
        for (auto const& i : m_prepared_dpt_delete) {
            cerr << " - ";
            if (i->isDir()) {
                cerr << "(folder) ";
            }
            cerr << i->relPath() << endl;
        }
        cerr << endl;
    }
    if (! m_prepared_local_delete.empty()) {
        cerr << "These local files will be deleted:" << endl;
        for (auto const& i : m_prepared_local_delete) {
            cerr << " - ";
            if (i->isDir()) {
                cerr << "(folder) ";
            }
            cerr << i->relPath() << endl;
        }
        cerr << endl;
    }
    if (! m_prepared_local_new.empty()) {
        cerr << "These files will be created on DPT-RP1:" << endl;
        for (auto const& i : m_prepared_local_new) {
            cerr << " - ";
            if (i->isDir()) {
                cerr << "(folder) ";
            }
            cerr << i->relPath() << endl;
        }
        cerr << endl;
    }
    if (! m_prepared_dpt_new.empty()) {
        cerr << "These local files will be created:" << endl;
        for (auto const& i : m_prepared_dpt_new) {
            cerr << " - ";
            if (i->isDir()) {
                cerr << "(folder) ";
            }
            cerr << i->relPath() << endl;
        }
        cerr << endl;
    }
    if (! m_prepared_overwrite_to_dpt.empty()) {
        cerr << "These files will be updated on DPT-RP1:" << endl;
        for (auto const& i : m_prepared_overwrite_to_dpt) {
            cerr << " - ";
            if (i->isDir()) {
                cerr << "(folder) ";
            }
            cerr << i->relPath() << endl;
        }
        cerr << endl;
    }
    if (! m_prepared_overwrite_from_dpt.empty()) {
        cerr << "These local files will be updated:" << endl;
        for (auto const& i : m_prepared_overwrite_from_dpt) {
            cerr << " - ";
            if (i->isDir()) {
                cerr << "(folder) ";
            }
            cerr << i->relPath() << endl;
        }
        cerr << endl;
    }
    if (! m_prepared_dpt_move.empty()) {
        cerr << "These DPT-RP1 files will be moved:" << endl;
        for (auto const& i : m_prepared_dpt_move) {
            cerr << " - ";
            if (i.first->isDir()) {
                cerr << "(folder) ";
            }
            cerr << i.first->relPath() << " ~> " << i.second->relPath() << endl;
        }
        cerr << endl;
    }
    if (! m_prepared_local_move.empty()) {
        cerr << "These local files will be moved:" << endl;
        for (auto const& i : m_prepared_local_move) {
            cerr << " - ";
            if (i.first->isDir()) {
                cerr << "(folder) ";
            }
            cerr << i.first->relPath() << " ~> " << i.second->relPath() << endl;
        }
        cerr << endl;
    }
}

void Dpt::safeSyncAllFiles(DryRunFlag dryrun)
{
    {   
        m_messager("Creating Backup...");
        /* 
            create presync checkpoint 
                this must be done before the try block
                because otherwise if error happens git reset will cause 
                user lose data!
        */
        git("add --all");
        string status = git("status");
        std::replace(status.begin(),status.end(), '\'', '\"');
        git("commit -m '<pre-sync checkpoint>\n\n" + status + "'");
    }
    try {
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
            cerr << "All files are identical." << endl;
            m_messager("All Up-to-Date");
            return;
        }
        if (dryrun) {
            cerr << "Action was aborted due to dry-run flag." << endl;
            return;
        }
        {   
            m_messager("Creating Backup...");
            /* backup files about to be changed on dpt */
            git("checkout dpt");
            git("merge master -X theirs -m '<pre-dpt-backup checkpoint>\n\n merge master into dpt branch'");
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
            git("add --all");
            string status = git("status");
            std::replace(status.begin(),status.end(), '\'', '\"');
            git("commit -m '<post-dpt-backup checkpoint>\n\n" + status + "'");
        }
        {   
            m_messager("Syncing...");
            /* start syncing */
            git("checkout master");
            cerr << "Syncing started. Do not disconnect!" << endl;
            dbOpen();
            syncAllFiles();
            updateLocalTree();
            updateDptTree();
            updateRevDB();
            dbClose(); // git checkout would invalidate db connection
            cerr << "All files are synced." << endl;
            m_messager("All Up-to-Date");
        }
    } catch (...) {
        cerr << "An error happend during syncing, changes will be reverted." << endl;
        m_messager("Sync Failed");
        git("checkout master");
        git("add --all");
        git("reset --hard");
        throw;
    }
    {
        git("add --all");
        string status = git("status");
        std::replace(status.begin(),status.end(), '\'', '\"');
        git("commit -m '<post-sync checkpoint>\n\n" + status + "'");
    }
}

void Dpt::setPrivateKeyPath(path const& key) {
    #if DEBUG_AUTH
    cerr << "using private key: " << key << endl;
    #endif
    m_private_key_path = key;
}

void Dpt::setClientIdPath(path const& id) {
    #if DEBUG_AUTH
    cerr << "using client id: " << id << endl;
    #endif
    m_client_id_path = id;
}

void Dpt::setPort(unsigned port)
{
    m_port = port;
}

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
    string git_cmd = "git " + command;
    #if DEBUG_GIT
        cerr << git_cmd << endl;
    #endif
    string shell_cmd = "cd " + m_sync_dir.string() + " && " + git_cmd + " 2>&1 ";
    FILE* pipe = popen(shell_cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "Couldn't start command." << std::endl;
        return 0;
    }
    while (fgets(buffer.data(), 128, pipe) != NULL) {
        result += buffer.data();
    }
    pclose(pipe);
    #if DEBUG_GIT
        std::cout << result << std::endl;
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
    if (! filesystem::exists(m_sync_dir / ".git")) {
        git("init");
        git("branch dpt");
    }
    path rev_db = m_sync_dir / ".rev";
    if (! filesystem::exists(rev_db)) {
        filesystem::copy_file("rev_db", rev_db);
    }
}

void Dpt::setMessager(std::function<void(string)> me) 
{
    m_messager = me;
}
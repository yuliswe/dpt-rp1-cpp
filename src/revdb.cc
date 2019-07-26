#include <dptrp1/revdb.h>
#include <iostream>
#include <openssl/md5.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <sqlite3.h>

using namespace std;
using namespace dpt;
using namespace boost;
using boost::filesystem::path;

void RevDB::open(path const& db)
{
  sqlite3_open(db.c_str(), &m_db);
}

void RevDB::close()
{
  sqlite3_close_v2(m_db);
}

vector<string> RevDB::getByRelPath(rpath const& q) const
{
  string rel_path;
  string local_md5;
  string dpt_rev;
  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(m_db, "SELECT * FROM files WHERE rel_path = ?", -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, q.c_str(), -1, nullptr);
  vector<string> rtv;
  if (SQLITE_ROW == sqlite3_step(stmt)) {
    rel_path = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 0));
    local_md5 = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 1));
    dpt_rev = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 2));
    rtv.push_back(rel_path);
    rtv.push_back(local_md5);
    rtv.push_back(dpt_rev);
  }
  sqlite3_finalize(stmt);
  return rtv;
}

vector<string> RevDB::getByDptRev(string const& q) const
{
  string rel_path;
  string local_md5;
  string dpt_rev;
  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(m_db, "SELECT * FROM files WHERE dpt_rev = ?", -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, q.c_str(), -1, nullptr);
  vector<string> rtv;
  if (SQLITE_ROW == sqlite3_step(stmt)) {
    rel_path = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 0));
    local_md5 = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 1));
    dpt_rev = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 2));
    rtv.push_back(rel_path);
    rtv.push_back(local_md5);
    rtv.push_back(dpt_rev);
  }
  sqlite3_finalize(stmt);
  return rtv;
}

vector<string> RevDB::getByLocalRev(string const& q) const
{
  string rel_path;
  string local_md5;
  string dpt_rev;
  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(m_db, "SELECT * FROM files WHERE local_md5 = ?", -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, q.c_str(), -1, nullptr);
  vector<string> rtv;
  if (SQLITE_ROW == sqlite3_step(stmt)) {
    rel_path = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 0));
    local_md5 = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 1));
    dpt_rev = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 2));
    rtv.push_back(rel_path);
    rtv.push_back(local_md5);
    rtv.push_back(dpt_rev);
  }
  sqlite3_finalize(stmt);
  return rtv;
}

void RevDB::putRev(path const& rel_path, string const& local_md5, string const& dpt_rev) const
{
  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(m_db, "INSERT INTO files VALUES (?,?,?)", -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, rel_path.c_str(), -1, nullptr);
  sqlite3_bind_text(stmt, 2, local_md5.c_str(), -1, nullptr);
  sqlite3_bind_text(stmt, 3, dpt_rev.c_str(), -1, nullptr);
  bool success;
  int result = sqlite3_step(stmt);
  while (result == SQLITE_BUSY) {
    result = sqlite3_step(stmt);
  }
  if (result == SQLITE_DONE) {
    success = true;
  } else {
    success = false;
    cerr << sqlite3_errmsg(m_db) << endl;
  }
  sqlite3_finalize(stmt);
  if (! success) {
    throw "insertion failed";
  }
}

void RevDB::reset()
{
  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(m_db, "DELETE FROM files", -1, &stmt, nullptr);
  bool success;
  int result = sqlite3_step(stmt);
  while (result == SQLITE_BUSY) {
    result = sqlite3_step(stmt);
  }
  if (result == SQLITE_DONE) {
    success = true;
  } else {
    success = false;
    cerr << sqlite3_errmsg(m_db) << endl;
  }
  sqlite3_finalize(stmt);
  if (! success) {
    throw "deletion failed";
  }
}


string dpt::md5(path const& fpath)
{
  std::ifstream file(fpath.c_str(), std::ifstream::binary);
  MD5_CTX md5Context;
  MD5_Init(&md5Context);
  char buf[1024 * 16];
  while (file.good()) {
    file.read(buf, sizeof(buf));
    MD5_Update(&md5Context, buf, file.gcount());
  }
  unsigned char result[MD5_DIGEST_LENGTH];
  MD5_Final(result, &md5Context);
  std::stringstream md5string;
  md5string << std::hex << std::uppercase << std::setfill('0');
  for (const auto &byte: result) {
    md5string << std::setw(2) << (int)byte;
  }
  return md5string.str();
}

#include <dptrp1/dtree.h>
#include <iostream>
#include <unordered_map>

using namespace std;
using std::shared_ptr;
using std::vector;
using std::string;
using std::chrono::time_point;
using std::chrono::system_clock;
using dpt::DNode;
using boost::filesystem::path;

vector<shared_ptr<DNode>> DNode::children() const {
  return m_children;
}

void DNode::addChild(shared_ptr<DNode> child) {
  m_children.push_back(child);
}

time_t DNode::lastModifiedTime() const
{
  return m_last_modified_time;
}

void DNode::setLastModifiedTime(time_t const& t) {
  m_last_modified_time = t;
}

bool DNode::isDir() const
{
  return m_is_dir;
}

string const& DNode::id() const
{
  return m_id;
}

void DNode::setId(string const& id)
{
  m_id = id;
}

void DNode::setFilename(string const& n) {
  m_filename = n;
}

string const& DNode::filename() const {
  return m_filename;
}

void DNode::setRev(string const& n) {
  m_rev = n;
}

string const& DNode::rev() const {
  return m_rev;
}

void DNode::setPath(boost::filesystem::path const& p) {
  m_path = p;
}

path const& DNode::path() const {
  return m_path;
}

void DNode::setRelPath(boost::filesystem::path const& p) {
  m_rel_path = p;
}

path const& DNode::relPath() const {
  return m_rel_path;
}

void DNode::setIsDir(bool d) {
  m_is_dir = d;
}

vector<shared_ptr<DNode>> DNode::allFiles() const {
  assert(isDir());
  vector<shared_ptr<DNode>> rtv;
  vector<shared_ptr<DNode>> stack = children();
  while (! stack.empty()) {
    shared_ptr<DNode> n = stack.back();
    stack.pop_back();
    if (n->isDir()) {
      stack.insert(stack.end(), n->children().begin(), n->children().end());
    } else {
      rtv.push_back(n);
    }
  }
  return rtv;
}

void dpt::symmetricDiff(vector<shared_ptr<DNode>> const& a,
            vector<shared_ptr<DNode>> const& b,
            vector<shared_ptr<DNode>>& only_a,
            vector<shared_ptr<DNode>>& only_b,
            vector<pair<shared_ptr<DNode>,shared_ptr<DNode>>>& both)
{
  only_a.clear();
  only_b.clear();
  both.clear();

  std::unordered_map<string, shared_ptr<DNode>> hash_a;
  std::unordered_map<string, shared_ptr<DNode>> hash_b;

  // hash a
  for (auto const& p : a) {
    hash_a[p->filename()] = p;
  }

  // hash b
  for (auto const& p : b) {
    hash_b[p->filename()] = p;
    auto const i = hash_a.find(p->filename());
    if (i != hash_a.end()) {
      // if also found in a
      both.push_back(make_pair(i->second, p));
    } else {
      // else only in b
      only_b.push_back(p);
    }
  }

  for (auto const& p : a) {
    if (hash_b.find(p->filename()) == hash_b.end()) {
      only_a.push_back(p);
    }
  }
}

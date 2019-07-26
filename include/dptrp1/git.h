#pragma once

#include <boost/filesystem.hpp>
#include <memory>
#include <git2.h>

namespace dpt {

using boost::filesystem::path;
using std::shared_ptr;

using namespace std;

template<class git_type>
class gptr {
  git_type* m_ptr;
public:
  explicit gptr(git_type* other); // constructor from raw ptr
  explicit gptr(gptr&& other); // move construct
  gptr() = default; // default constructor
  ~gptr(); // destructor
  gptr(gptr const& other) = delete; // no copy construct
  gptr& operator=(gptr const& other) = delete; // no assignment
  git_type* get();
  git_type const* get() const;
  operator git_type*();
  operator git_type**();
};

class Git {
public:
  Git(path const& dirpath);
  void commit(string const& msg);
  void tag(string const& tagname);
  void checkout(string const& msg);
  void branch(string const& name);
  bool hasChanges();
  void resetHard();
  string status();
  string status(gptr<git_reference>& head, gptr<git_status_list>& stats);
  void addAll();
  path dir() const;
  void insertGitKeepFiles();
  vector<gptr<git_commit>> history(size_t limit);

private:
  static int m_initializer;
  gptr<git_repository> m_repo;
  path m_repo_path;
};

};

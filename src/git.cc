#include <dptrp1/git.h>
#include <iostream>
#include <sstream>
#include <queue>

using namespace std;
using namespace dpt;
using boost::filesystem::path;
using boost::filesystem::directory_iterator;

template<class git_type>
gptr<git_type>::gptr(git_type* other) {
  m_ptr = other;
};

template<class git_type>
gptr<git_type>::gptr(gptr&& other)
{
  m_ptr = other.m_ptr;
  other.m_ptr = nullptr;
}

template<class git_type>
gptr<git_type>::operator git_type*() { return m_ptr; }

template<class git_type>
git_type* gptr<git_type>::get() { return m_ptr; }

template<class git_type>
git_type const* gptr<git_type>::get() const { return m_ptr; }

template<class git_type>
gptr<git_type>::operator git_type**() { return &m_ptr; }

#define make_gptr(name) \
template<> gptr<name>::~gptr() { m_ptr && (name##_free(m_ptr),0); } \
template class dpt::gptr<name>; \

make_gptr(git_object)
make_gptr(git_commit)
make_gptr(git_signature)
make_gptr(git_repository)
make_gptr(git_status_list)
make_gptr(git_reference)
make_gptr(git_index)
make_gptr(git_annotated_commit)
make_gptr(git_revwalk)

Git::Git(path const& dir)
{
  m_repo_path = dir;
  int error = 0;
  error = git_repository_open(m_repo, dir.c_str());
  if (error) {
    error = git_repository_init(m_repo, dir.c_str(), false);
    cerr << "initialized repository " << dir << endl;
    commit("initial commit");
    git_repository_open(m_repo, dir.c_str());
  }
}

int Git::m_initializer = git_libgit2_init();

void Git::commit(string const& msg)
{
  gptr<git_index> index;
  git_repository_index(index, m_repo);
  git_oid tree_id;
  git_index_write_tree(&tree_id, index);
  git_tree* tree;
  git_tree_lookup(&tree, m_repo, &tree_id);
  gptr<git_signature> sig;
  git_signature_now(sig, "dpt", "dpt");
  gptr<git_reference> head_ref;
  git_repository_head(head_ref, m_repo);
  if (head_ref.get()) {
    gptr<git_commit> head_commit;
    git_commit_lookup(head_commit, m_repo, git_reference_target(head_ref));
    const git_commit* parents[] = { head_commit };
    git_oid commit_id;
    git_commit_create(&commit_id, m_repo, "HEAD", sig, sig, "UTF-8", msg.c_str(), tree, 1, parents);
    cerr << "new commit: " << git_oid_tostr_s(&commit_id) << endl;
  } else {
    git_oid commit_id;
    git_commit_create(&commit_id, m_repo, "HEAD", sig, sig, "UTF-8", msg.c_str(), tree, 0, nullptr);
    cerr << "root commit: " << git_oid_tostr_s(&commit_id) << endl;
  }
}

void Git::checkout(string const& refish)
{
  git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;

  /* Checkout a different tree */
  gptr<git_object> treeish;
  gptr<git_reference> ref;
  git_revparse_ext(treeish, ref, m_repo, refish.c_str());
  /* git_revparse_ext cannot find detach head? */
  if (ref.get() != nullptr) {
    git_checkout_tree(m_repo, treeish, &opts);
    git_repository_set_head(m_repo, git_reference_name(ref));
  } else {
    gptr<git_annotated_commit> acommit;
    git_oid oid;
    git_oid_fromstr(&oid, refish.c_str());
    git_annotated_commit_lookup(acommit, m_repo, &oid);
    git_repository_set_head_detached_from_annotated(m_repo, acommit);
    git_checkout_head(m_repo, &opts);
  }
}

void Git::branch(string const& name)
{
  int err = 0;
  gptr<git_reference> head_ref;
  git_repository_head(head_ref, m_repo);
  gptr<git_annotated_commit> head_commit;
  git_annotated_commit_from_ref(head_commit, m_repo, head_ref);
  git_reference* newbranch;
  err = git_branch_create_from_annotated(&newbranch, m_repo, name.c_str(), head_commit, true);
  if (err) {
    git_error const* error = git_error_last();
    cerr << "error: " << error->message << endl;
  }
  cerr << "created branch: " << git_reference_name(newbranch) << endl;
}

void Git::addAll()
{
  gptr<git_index> index;
  git_repository_index(index, m_repo);
  gptr<git_status_list> stats;
  gptr<git_reference> head_ref;
  status(head_ref, stats);
  auto callback = [](char const* path, unsigned flag, void* index) -> int {
    if (flag & GIT_STATUS_IGNORED) {
      cerr << "ignoring " << path << endl;
    } else if (flag & GIT_STATUS_WT_DELETED) {
      git_index_remove_bypath(static_cast<git_index*>(index), path);
      cerr << "removing " << path << endl;
    } else {
      git_index_add_bypath(static_cast<git_index*>(index), path);
      cerr << "adding " << path << endl;
    }
    return 0;
  };
  git_status_foreach(m_repo, callback, index.get());
  git_index_write(index);
}

bool Git::hasChanges()
{
  gptr<git_reference> head;
  git_repository_head(head, m_repo);
  git_status_options opts = GIT_STATUS_OPTIONS_INIT;
  opts.flags |= GIT_STATUS_OPT_INCLUDE_UNTRACKED;
  opts.flags |= GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;
  opts.flags |= GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX;
  opts.flags |= GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR;
  opts.flags |= GIT_STATUS_OPT_SORT_CASE_INSENSITIVELY;
  gptr<git_status_list> stats;
  git_status_list_new(stats, m_repo, &opts);
  int count = git_status_list_entrycount(stats);;
  return count > 0;
}

string Git::status() {
  gptr<git_status_list> stats;
  gptr<git_reference> head;
  return status(head, stats);
}

void Git::insertGitKeepFiles()
{
  // git does not track emtpy dirs, a dirty trick is needed
  queue<path> que;
  que.push(m_repo_path);
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

string Git::status(gptr<git_reference>& head, gptr<git_status_list>& stats)
{
  insertGitKeepFiles();
  // perform git status
  ostringstream oss;
  git_repository_head(head, m_repo);
  oss << "on branch: " << git_reference_name(head) << endl;
  git_status_options opts = GIT_STATUS_OPTIONS_INIT;
  opts.flags |= GIT_STATUS_OPT_INCLUDE_UNTRACKED;
  opts.flags |= GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;
  opts.flags |= GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX;
  opts.flags |= GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR;
  opts.flags |= GIT_STATUS_OPT_SORT_CASE_INSENSITIVELY;
  git_status_list_new(stats, m_repo, &opts);
  int count = git_status_list_entrycount(stats);
  if (count == 0) {
    oss << "nothing new" << endl;
  } else {
    oss << "total changes: " << count << endl;
  }
  for (int i = 0; i < count; i++) {
    git_status_entry const* entry = git_status_byindex(stats,i);
    git_diff_delta const* delta;
    if (entry->head_to_index) {
      oss << "staged: ";
      delta = entry->head_to_index;
    }
    if (entry->index_to_workdir) {
      oss << "unstaged: ";
      delta = entry->index_to_workdir;
    }
    if (entry->status & GIT_STATUS_CURRENT) {
      oss << "unchanged: ";
    }
    if (entry->status & (GIT_STATUS_INDEX_NEW|GIT_STATUS_WT_NEW)) {
      oss << "new: ";
    }
    if (entry->status & (GIT_STATUS_INDEX_MODIFIED|GIT_STATUS_WT_MODIFIED)) {
      oss << "modified: ";
    }
    if (entry->status & (GIT_STATUS_INDEX_DELETED|GIT_STATUS_WT_DELETED)) {
      oss << "deleted: ";
    }
    if (entry->status & (GIT_STATUS_INDEX_RENAMED|GIT_STATUS_WT_RENAMED)) {
      oss << "renamed: ";
    }
    if (entry->status & (GIT_STATUS_INDEX_TYPECHANGE|GIT_STATUS_WT_TYPECHANGE)) {
      oss << "typechange: ";
    }
    if (entry->status & GIT_STATUS_IGNORED) {
      oss << "ignored: ";
    }
    if (entry->status & GIT_STATUS_CONFLICTED) {
      oss << "conflict: ";
    }
    if (delta) {
      git_diff_file const old_file = delta->old_file;
      git_diff_file const new_file = delta->new_file;
      oss << old_file.path << endl;
    }
  }
  return oss.str();
}

void Git::resetHard()
{
  gptr<git_reference> head_ref;
  git_repository_head(head_ref, m_repo);
  gptr<git_annotated_commit> head_commit;
  git_annotated_commit_from_ref(head_commit, m_repo, head_ref);
  git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
  git_reset_from_annotated(m_repo, head_commit, GIT_RESET_HARD, &opts);
}

path Git::dir() const
{
  return m_repo_path;
}

void Git::tag(string const& tagname)
{
  gptr<git_signature> sig;
  git_signature_now(sig, "dpt", "dpt");
  gptr<git_reference> head_ref;
  git_repository_head(head_ref, m_repo);
  gptr<git_object> head_obj;
  git_reference_peel(head_obj, head_ref, GIT_OBJECT_COMMIT);
  git_oid head_oid;
  git_tag_create(&head_oid, m_repo, tagname.c_str(), head_obj, sig, "", false);
}

vector<gptr<git_commit>> Git::history(size_t limit)
{
  gptr<git_revwalk> walk;
  git_revwalk_new(walk, m_repo);
  git_revwalk_sorting(walk,GIT_SORT_TIME);
  git_revwalk_push_head(walk);
  /* add dpt branch */
  git_tag_foreach(m_repo, [](char const* tagname, git_oid* oid, void* walk) -> int {
    git_revwalk_push_ref(static_cast<git_revwalk*>(walk), tagname);
    return 0;
  }, walk.get());
  vector<shared_ptr<git_commit>> rtv;
  git_oid oid;
  vector<gptr<git_commit>> output;
  while (git_revwalk_next(&oid, walk) == 0 && limit > 0) {
    gptr<git_commit> commit;
    git_commit_lookup(commit, m_repo, &oid);
    /* must use move, otherwise gptr destructor would be called, deleting the pointer */
    output.push_back(std::move(commit));
    limit--;
  }
  return output;
}

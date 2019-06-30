#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file

#include "catch.hpp"
#include <dptrp1/git.h>
#include <memory>
#include <boost/filesystem.hpp>
#include <git2.h>
#include <sstream>
#include <iostream>

using namespace boost::filesystem;
using namespace std;
using namespace dpt;
using namespace Catch;

namespace dpt {

    typedef boost::filesystem::path repo_path;
    typedef boost::filesystem::path abspath;

    abspath test_root_path() {
        return current_path() / "git-tests";
    }

    repo_path to_repo_path(abspath file, abspath repo) {
        string s1 = file.string();
        string s2 = repo.string();
        return s1.substr(s2.size() + 1);
    }

    int get_unique_int() {
        static unsigned unique_int = 0;
        return unique_int++;
    }

    string get_unique_str() {
        return to_string(get_unique_int());
    }

    abspath create_file_in(abspath parent, string content = "", string name = get_unique_str())
    {
        path fpath = parent / name;
        std::ofstream file(fpath.string(), ios_base::out|ios_base::trunc);
        if (content.size()) {
            file << content;
        }
        return fpath;
    }

    abspath create_directory_in(abspath parent, string name = get_unique_str())
    {
        path fpath = parent / name;
        create_directory(fpath);
        return fpath;
    }

    shared_ptr<Git> setup_git_repo(string repo_name = "repo_" + get_unique_str()) {
        if (! exists(test_root_path())) {
            create_directory(test_root_path());
        }
        abspath repo = test_root_path() / repo_name;
        if (exists(repo)) {
            remove_all(repo);
        }
        create_directory(repo);
        shared_ptr<Git> git = make_shared<Git>(repo);
        assert(git);
        return git;
    }
}

TEST_CASE("setting up git repo") {
    auto git = setup_git_repo();
    SECTION("git status") {
        gptr<git_status_list> stats;
        gptr<git_reference> head;
        git->status(head, stats);   
        REQUIRE(string(git_reference_name(head)) == "refs/heads/master");
    }    
}

TEST_CASE("create an empty file") {
    auto git = setup_git_repo();
    repo_path fpath = to_repo_path(create_file_in(git->dir(), get_unique_str()), git->dir());

    gptr<git_status_list> stats;
    gptr<git_reference> head;
    git->status(head, stats);
    REQUIRE(string(git_reference_name(head)) == "refs/heads/master");
    int n = git_status_list_entrycount(stats);
    REQUIRE(n == 1);
    for (int i = 0; i < n; i++) {
        git_status_entry const* entry = git_status_byindex(stats, i);
        REQUIRE(entry->index_to_workdir);
        REQUIRE_FALSE(entry->head_to_index);
    }
    REQUIRE_THAT(git->status(), Contains("unstaged") && Contains("new") && Contains(fpath.string()));

    SECTION("git history 100") {
        auto result = git->history(100);
        REQUIRE(result.size() == 1);
    }
        
    SECTION("git branch test") {
        git->branch("test");
        gptr<git_status_list> stats;
        gptr<git_reference> head;
        git->status(head, stats);
        REQUIRE(string(git_reference_name(head)) == "refs/heads/master");
        int n = git_status_list_entrycount(stats);
        REQUIRE(n == 1);
        for (int i = 0; i < n; i++) {
            git_status_entry const* entry = git_status_byindex(stats, i);
            REQUIRE(entry->index_to_workdir);
            REQUIRE_FALSE(entry->head_to_index);
        }

        SECTION("git checkout test") {
            git->checkout("test");
            gptr<git_status_list> stats;
            gptr<git_reference> head;
            git->status(head, stats);
            REQUIRE(string(git_reference_name(head)) == "refs/heads/test");
        }
    }

    SECTION("git add all") {
        git->addAll();
        gptr<git_status_list> stats;
        gptr<git_reference> head;
        git->status(head, stats);
        int n = git_status_list_entrycount(stats);
        REQUIRE(n == 1);
        for (int i = 0; i < n; i++) {
            git_status_entry const* entry = git_status_byindex(stats, i);
            REQUIRE_FALSE(entry->index_to_workdir);
            REQUIRE(entry->head_to_index);
        }
        REQUIRE_THAT(git->status(), Contains("staged") && Contains("new") && Contains(fpath.string()));

        SECTION("git reset hard") {
            git->resetHard();
            gptr<git_status_list> stats;
            gptr<git_reference> head;
            git->status(head, stats);
            int n = git_status_list_entrycount(stats);
            REQUIRE(n == 0);
        }

        SECTION("git commit") {
            git->commit("test commit");
            gptr<git_status_list> stats;
            gptr<git_reference> head;
            git->status(head, stats);
            int n = git_status_list_entrycount(stats);
            REQUIRE(n == 0);

            SECTION("git history 100") {
                auto result = git->history(100);
                REQUIRE(result.size() == 2);
            }

            SECTION("remove file") {
                remove_all(git->dir() / fpath);
                gptr<git_status_list> stats;
                gptr<git_reference> head;
                git->status(head, stats);
                int n = git_status_list_entrycount(stats);
                REQUIRE(n == 1);

                SECTION("git add all") {
                    git->addAll();
                    gptr<git_status_list> stats;
                    gptr<git_reference> head;
                    git->status(head, stats);
                    int n = git_status_list_entrycount(stats);
                    REQUIRE(n == 1);

                    SECTION("git commit") {
                        git->commit("remove file");
                        gptr<git_status_list> stats;
                        gptr<git_reference> head;
                        git->status(head, stats);
                        int n = git_status_list_entrycount(stats);
                        REQUIRE(n == 0);

                        SECTION("git history 100") {
                            auto result = git->history(100);
                            REQUIRE(result.size() == 3);
                        }
                    }
                }
            }
        }

        SECTION("git branch test") {
            git->branch("test");
            gptr<git_status_list> stats;
            gptr<git_reference> head;
            git->status(head, stats);
            REQUIRE(string(git_reference_name(head)) == "refs/heads/master");
            int n = git_status_list_entrycount(stats);
            REQUIRE(n == 1);
            for (int i = 0; i < n; i++) {
                git_status_entry const* entry = git_status_byindex(stats, i);
                REQUIRE_FALSE(entry->index_to_workdir);
                REQUIRE(entry->head_to_index);
            }

            SECTION("git checkout test") {
                git->checkout("test");
                gptr<git_status_list> stats;
                gptr<git_reference> head;
                git->status(head, stats);
                REQUIRE(string(git_reference_name(head)) == "refs/heads/test");
            }
        }
    }

}

TEST_CASE("create an empty directory") {
    auto git = setup_git_repo();
    repo_path dpath = to_repo_path(create_directory_in(git->dir()), git->dir());
    SECTION("git status") {
        gptr<git_status_list> stats;
        gptr<git_reference> head;
        git->status(head, stats);
        int n = git_status_list_entrycount(stats);
        REQUIRE(n == 1);
        for (int i = 0; i < n; i++) {
            git_status_entry const* entry = git_status_byindex(stats, i);
            REQUIRE(entry->index_to_workdir);
            REQUIRE_FALSE(entry->head_to_index);
        }
        REQUIRE_THAT(git->status(), Contains("unstaged") && Contains("new") && Contains(dpath.string()));
    }
    SECTION("git add all") {
        git->addAll();
        gptr<git_status_list> stats;
        gptr<git_reference> head;
        git->status(head, stats);
        int n = git_status_list_entrycount(stats);
        REQUIRE(n == 1);
        for (int i = 0; i < n; i++) {
            git_status_entry const* entry = git_status_byindex(stats, i);
            REQUIRE_FALSE(entry->index_to_workdir);
            REQUIRE(entry->head_to_index);
        }
        REQUIRE_THAT(git->status(), Contains("staged") && Contains("new") && Contains(dpath.string()));
    }
}

TEST_CASE("create a directory with a file") {
    auto git = setup_git_repo();
    repo_path dpath = to_repo_path(create_directory_in(git->dir()), git->dir());
    repo_path fpath = to_repo_path(create_file_in(git->dir() / dpath, get_unique_str()), git->dir());
    SECTION("git status") {
        gptr<git_status_list> stats;
        gptr<git_reference> head;
        git->status(head, stats);
        int n = git_status_list_entrycount(stats);
        REQUIRE(n == 1);
        for (int i = 0; i < n; i++) {
            git_status_entry const* entry = git_status_byindex(stats, i);
            REQUIRE(entry->index_to_workdir);
            REQUIRE_FALSE(entry->head_to_index);
        }
        REQUIRE_THAT(git->status(), Contains("unstaged") && Contains("new") && Contains(fpath.string()) && Contains(dpath.string()));
    }
    SECTION("git add all") {
        git->addAll();
        gptr<git_status_list> stats;
        gptr<git_reference> head;
        git->status(head, stats);
        int n = git_status_list_entrycount(stats);
        REQUIRE(n == 1);
        for (int i = 0; i < n; i++) {
            git_status_entry const* entry = git_status_byindex(stats, i);
            REQUIRE_FALSE(entry->index_to_workdir);
            REQUIRE(entry->head_to_index);
        }
        REQUIRE_THAT(git->status(), Contains("staged") && Contains("new") && Contains(fpath.string()) && Contains(dpath.string()));
    }
}

TEST_CASE("create two directories each with a file") {
    auto git = setup_git_repo();
    repo_path dpath1 = to_repo_path(create_directory_in(git->dir()), git->dir());
    repo_path fpath1 = to_repo_path(create_file_in(git->dir() / dpath1, get_unique_str()), git->dir());
    repo_path dpath2 = to_repo_path(create_directory_in(git->dir()), git->dir());
    repo_path fpath2 = to_repo_path(create_file_in(git->dir() / dpath2, get_unique_str()), git->dir());
    SECTION("git status") {
        gptr<git_status_list> stats;
        gptr<git_reference> head;
        git->status(head, stats);
        int n = git_status_list_entrycount(stats);
        REQUIRE(n == 2);
        for (int i = 0; i < n; i++) {
            git_status_entry const* entry = git_status_byindex(stats, i);
            REQUIRE(entry->index_to_workdir);
            REQUIRE_FALSE(entry->head_to_index);
        }
        REQUIRE_THAT(git->status(), Contains("unstaged") && Contains("new") && Contains(fpath1.string()) && Contains(dpath1.string()));
        REQUIRE_THAT(git->status(), Contains("unstaged") && Contains("new") && Contains(fpath2.string()) && Contains(dpath2.string()));
    }
    SECTION("git add all") {
        git->addAll();
        gptr<git_status_list> stats;
        gptr<git_reference> head;
        git->status(head, stats);
        int n = git_status_list_entrycount(stats);
        REQUIRE(n == 2);
        for (int i = 0; i < n; i++) {
            git_status_entry const* entry = git_status_byindex(stats, i);
            REQUIRE_FALSE(entry->index_to_workdir);
            REQUIRE(entry->head_to_index);
        }
        REQUIRE_THAT(git->status(), Contains("staged") && Contains("new") && Contains(fpath1.string()) && Contains(dpath1.string()));
        REQUIRE_THAT(git->status(), Contains("staged") && Contains("new") && Contains(fpath2.string()) && Contains(dpath2.string()));
    }
}

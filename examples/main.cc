#include <dptrp1/dptrp1.h>
#include <dptrp1/git.h>
#include <iostream>

using namespace std;
using namespace dpt;

int main(int argn, char** argv)
{
    try {
        Dpt dp;
        dp.setSyncDir("/Users/yuli/Documents/mydocs");
        
        dp.setClientIdPath("/Users/yuli/lab/dptid.data");
        dp.setPrivateKeyPath("/Users/yuli/lab/dptkey.data");
        if (! dp.resolveHost()) {
            throw "could not resolve host";
        }
        dp.authenticate();
        dp.setupSyncDir();
        if (argn > 1) {
            dp.safeSyncAllFiles();
        } else {
            dp.safeSyncAllFiles(DryRun);
        }
    } catch (char const* e) {
        cerr << "An error has occured: " << e << endl;
        return 1;
    }
}


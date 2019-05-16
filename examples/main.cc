#include <dptrp1.h>
#include <iostream>

using namespace std;
using namespace dpt;

int main(int argn, char** argv)
{
    try {
        Dpt dp;
        dp.setSyncDir("/Users/yuli/Documents/test");
        dp.setClientIdPath("/Users/yuli/lab/dptid.data");
        dp.setPrivateKeyPath("/Users/yuli/lab/dptkey.data");
        dp.authenticate();
        dp.setupSyncDir();
        if (argn > 1) {
            dp.safeSyncAllFiles();
        } else {
            dp.safeSyncAllFiles(DryRun);
        }
    } catch (char const* e) {
        cerr << "An error has occured: " << endl;
        cerr << e << endl;
    }
}


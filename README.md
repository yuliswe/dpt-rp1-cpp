# Welcome to dpt-rp1-cpp

This is a C++ library for file syncing with Sony DPT-RP1 reader, inspired by dpt-rp1-py.
In addition to file syncing, our library provides version control using git. The goal is to prevent file loss due to user mistakes or software bugs that may happen during sycing.

## Simplest Example

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


## Conflict Resolution Scheme

### Conflicts during syncing is resolve as the following:

DPT\Local | new | modified | deleted | renamed | renamed & modified
----------|-----|----------|---------|---------|-------------------
new       | T   | _        | _       | _       | _
modified  | _   | _        | D       | T       | x
deleted   | _   | L        | _       | L       | L
renamed   | _   | T        | D       | T       | B
r & m     | _   | x        | D       | B       | B


1. New-New Conflict
A new file was created locally by user, and a new file of the same relative path is created on DPT-RP1, or vice versa.
The content of two files are first compared. If they are the same, then this is not a conflict. 
Otherwise, the last-modified dates are compared. The later modified file is kept, and the earlier file modified is archived.

2. New-Modified Conflict
A file was modified locally, and a file of the same relative path was created on DPT-RP1, or vice versa. 
Due how the syncing works, this is logically impossible to happen.

3. New-Deleted Conflict
A file was deleted locally, and a file of the same relative path was created on DPT-RP1, or vice versa. 
Due how the syncing works, this is logically impossible to happen.

4. New-Renamed Conflict
A file was renamed locally with unchanged content, and a file of the original relative path was created on DPT-RP1, or vice versa. 
Due how the syncing works, this is logically impossible to happen.

5. New-Renamed & Modified Conflict
A file was renamed locally and has been modified, and a file of the original relative path was created on DPT-RP1, or vice versa. 
Due how the syncing works, this is logically impossible to happen.

6. Modified-Modified Conflict
Both files are modified locally and on DPT-PR1. This is the most common type of conflict.
The last-modified dates are compared, the later one is kept, and the earlier is archived.

7. Modififed-Deleted Conflict
A file was modified locally, and a file of the original relative path was deleted on DPT-RP1, or vice versa.
The modified file is kept.

8. Modified-Renamed Conflict
A file was modified locally, and is renamed on DPT-RP1 with unchanged content, or vice versa.
The last-modified dates are compared. 
The modified file is renamed if the renamed file has a later modified date,
otherwise the renamed file is renamed back to the original name. 
In either case, the modified content is kept.

9. Modified-Renamed & Modified Conflict
A file was modified locally, and is renamed on DPT-RP1 and is also modified, or vice versa.
Due to how the syncing works, Renamed & Modified is treated as the original file being deleted, and a new file being created.
So the modified file is treated as if it has Modified-Deleted Conflict, and the new file is kept.

10. Deleted-Deleted Conflict
This is not a conflict.

11. Deleted-Renamed Conflict
A file was deleted locally, and is renamed on DPT-RP1 with unchanged content, or vice versa.
The renamed file is kept.

12. Delete-Renamed & Modified Conflict
A file was deleted locally, and is renamed on DPT-RP1 and is also modified, or vice versa.
The new renamed file is kept.

13. Renamed-Renamed Conflict
A file was renamed locally and is renamed on DPT-RP1 with a different name; however, the file contents remain the same
both locally and on DPT-RP1.
In this case or vice versa, the last-modifed dates are compared, and the later file is kept.

14. Renamed-Renamed & Modified Conflict
A file was renamed locally, and is renamed on DPT-RP1 with a different name. The file on DPT-RP1 has also been modified.
Due to how the syncing works, Renamed & Modified is treated as the original file being deleted, and a new file being created.
So the renamed file is treated as if it has Deleted-Renamed Conflict, and the new file is kept.

15. Renamed & Modified-Renamed & Modifed Conflict
A file was renamed and modified both locally and on DPT-RP1.
Due to how the syncing works, Renamed & Modified is treated as the original file being deleted, and a new file being created.
So both files are treated as if they have Delete-Renamed & Modifed Conflict.

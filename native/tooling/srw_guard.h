#pragma once
#include <windows.h>
namespace history {
class SharedSrw {
    SRWLOCK* lock_;
public:
    explicit SharedSrw(SRWLOCK& lock):lock_(&lock){AcquireSRWLockShared(lock_);}
    ~SharedSrw(){ReleaseSRWLockShared(lock_);}
    SharedSrw(const SharedSrw&)=delete;
    SharedSrw& operator=(const SharedSrw&)=delete;
};
class ExclusiveSrw {
    SRWLOCK* lock_;
public:
    explicit ExclusiveSrw(SRWLOCK& lock):lock_(&lock){AcquireSRWLockExclusive(lock_);}
    ~ExclusiveSrw(){ReleaseSRWLockExclusive(lock_);}
    ExclusiveSrw(const ExclusiveSrw&)=delete;
    ExclusiveSrw& operator=(const ExclusiveSrw&)=delete;
};
}

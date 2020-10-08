#ifndef UTILPVT_H
#define UTILPVT_H

#include <memory>

namespace psc {
#if __cplusplus>=201103L
template<typename T>
using auto_ptr = std::unique_ptr<T>;
#define PTRMOVE(AUTO) std::move(AUTO)
#else
using std::auto_ptr;
#define PTRMOVE(AUTO) (AUTO)
#endif
}

#endif /* UTILPVT_H */

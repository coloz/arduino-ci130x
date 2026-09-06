#pragma once
#include <stddef.h>
#include <new>

// Small owner for cores whose embedded standard library omits <memory>.
// Like the networking objects themselves, copies/mutations are application-task serialized.
namespace espwifi {
struct SharedControl {
  void *object;
  void (*destroy)(void *);
  size_t references;
};
template<class T> class SharedPtr {
  SharedControl *control_ = nullptr;
  template<class U> static void destroy(void *p) { delete static_cast<U *>(p); }
  void release() {
    if (control_ && --control_->references == 0) {
      control_->destroy(control_->object); delete control_;
    }
    control_ = nullptr;
  }
public:
  SharedPtr() = default;
  explicit SharedPtr(T *object) { reset(object); }
  SharedPtr(const SharedPtr &other) : control_(other.control_) { if (control_) ++control_->references; }
  SharedPtr(SharedPtr &&other) noexcept : control_(other.control_) { other.control_ = nullptr; }
  ~SharedPtr() { release(); }
  SharedPtr &operator=(const SharedPtr &other) {
    if (control_ != other.control_) {
      release(); control_ = other.control_; if (control_) ++control_->references;
    }
    return *this;
  }
  void reset(T *object = nullptr) {
    release();
    if (!object) return;
    control_ = new (std::nothrow) SharedControl{object, &destroy<T>, 1};
    if (!control_) delete object;
  }
  T *get() const { return control_ ? static_cast<T *>(control_->object) : nullptr; }
  T *operator->() const { return get(); }
  T &operator*() const { return *get(); }
  explicit operator bool() const { return get() != nullptr; }
  bool operator==(const SharedPtr &other) const { return control_ == other.control_; }
};
}

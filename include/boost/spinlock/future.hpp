/* future.hpp
Non-allocating constexpr future-promise
(C) 2015 Niall Douglas http://www.nedprod.com/
File Created: May 2015


Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#ifndef BOOST_SPINLOCK_FUTURE_HPP
#define BOOST_SPINLOCK_FUTURE_HPP

#include "monad.hpp"
#include <future>

/*! \file future.hpp
\brief Provides a lightweight next generation future with N4399 Concurrency TS extensions

\headerfile include/boost/spinlock/future.hpp ""
*/

#if BOOST_SPINLOCK_IN_THREAD_SANITIZER
#define BOOST_SPINLOCK_FUTURE_MUTEX_TYPE std::mutex
#define BOOST_SPINLOCK_FUTURE_MUTEX_TYPE_DESTRUCTOR mutex
#define BOOST_SPINLOCK_FUTURE_NO_SANITIZE_LOAD(v) ((std::atomic<decltype(v)> *)(&v))->load(std::memory_order::memory_order_relaxed)
#define BOOST_SPINLOCK_FUTURE_NO_SANITIZE_STORE(v, x) ((std::atomic<decltype(v)> *)(&v))->store((x), std::memory_order::memory_order_relaxed)
#else
#define BOOST_SPINLOCK_FUTURE_MUTEX_TYPE spinlock<bool>
#define BOOST_SPINLOCK_FUTURE_MUTEX_TYPE_DESTRUCTOR spinlock<bool>
#define BOOST_SPINLOCK_FUTURE_NO_SANITIZE_LOAD(v) (v)
#define BOOST_SPINLOCK_FUTURE_NO_SANITIZE_STORE(v, x) ((v)=(x))
#endif

BOOST_SPINLOCK_V1_NAMESPACE_BEGIN
namespace lightweight_futures {
  
  template<typename R> class basic_promise;
  template<typename R> class basic_future;

  namespace detail
  {
    template<class promise_type, class future_type> struct lock_guard
    {
      promise_type *_p;
      future_type  *_f;
      lock_guard(const lock_guard &)=delete;
      lock_guard(lock_guard &&)=delete;
      BOOST_SPINLOCK_FUTURE_MSVC_HELP lock_guard(promise_type *p) : _p(nullptr), _f(nullptr)
      {
        // constexpr fold
        if(!p->_need_locks)
        {
          _p=p;
          if(p->_storage.type==promise_type::value_storage_type::storage_type::pointer)
            _f=p->_storage.pointer_;
          return;
        }
        else for(;;)
        {
          p->_lock.lock();
          if(p->_storage.type==promise_type::value_storage_type::storage_type::pointer)
          {
            if(p->_storage.pointer_->_lock.try_lock())
            {
              _p=p;
              _f=p->_storage.pointer_;
              break;
            }
          }
          else
          {
            _p=p;
            break;
          }
          p->_lock.unlock();
        }
      }
      BOOST_SPINLOCK_FUTURE_MSVC_HELP lock_guard(future_type *f) : _p(nullptr), _f(nullptr)
      {
        // constexpr fold
        if(!f->_need_locks)
        {
          _p=f->_promise;
          _f=f;
          return;
        }
        else for(;;)
        {
          f->_lock.lock();
          if(f->_promise)
          {
            if(f->_promise->_lock.try_lock())
            {
              _p=f->_promise;
              _f=f;
              break;
            }
          }
          else
          {
            _f=f;
            break;
          }
          f->_lock.unlock();
        }
      }
      BOOST_SPINLOCK_FUTURE_MSVC_HELP ~lock_guard()
      {
        unlock();
      }
      void unlock()
      {
        if(_p)
        {
          if(_p->_need_locks)
            _p->_lock.unlock();
          _p=nullptr;
        }
        if(_f)
        {
          if(_f->_need_locks)
            _f->_lock.unlock();
          _f=nullptr;
        }
      }
    };
  }

  /*! \class basic_promise
  \brief Implements the state setting side of basic_monad
  \tparam implementation_policy An implementation policy type
  
  \warning This lightweight promise is NOT thread safe up until the point you call `get_future()`, after which it becomes thread safe.
  Therefore if you have multiple threads trying to set the promise value concurrently before you have called `get_future()`, you will race.
  The chances of this being a problem in any well designed code should be non-existent, however please do contact the author if you find
  a non-contrived situation where this could happen.
  */
  template<class implementation_policy> class basic_promise
  {
    friend class basic_future<implementation_policy>;
    friend implementation_policy;
  protected:
    typedef value_storage<implementation_policy> value_storage_type;
    value_storage_type _storage;
  private:
    bool _need_locks;                 // Used to inhibit unnecessary atomic use, thus enabling constexpr collapse
    bool _detached;                   // Future has already been set and promise is now detached
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4624)
#endif
    union { BOOST_SPINLOCK_FUTURE_MUTEX_TYPE _lock; };  // Delay construction
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  public:
    //! \brief This promise has a value_type
    BOOST_STATIC_CONSTEXPR bool has_value_type = value_storage_type::has_value_type;
    //! \brief This promise has an error_type
    BOOST_STATIC_CONSTEXPR bool has_error_type = value_storage_type::has_error_type;
    //! \brief This promise has an exception_type
    BOOST_STATIC_CONSTEXPR bool has_exception_type = value_storage_type::has_exception_type;
    //! \brief The final implementation type
    typedef typename value_storage_type::implementation_type implementation_type;
    //! \brief The type potentially held by the promise
    typedef typename value_storage_type::value_type value_type;
    //! \brief The error code potentially held by the promise
    typedef typename value_storage_type::error_type error_type;
    //! \brief The exception ptr potentially held by the promise
    typedef typename value_storage_type::exception_type exception_type;

    //! \brief This promise will never throw exceptions during move construction
    BOOST_STATIC_CONSTEXPR bool is_nothrow_move_constructible = value_storage_type::is_nothrow_move_constructible;
    //! \brief This promise will never throw exceptions during move assignment
    BOOST_STATIC_CONSTEXPR bool is_nothrow_move_assignable = value_storage_type::is_nothrow_destructible && value_storage_type::is_nothrow_move_constructible;
    //! \brief This promise will never throw exceptions during destruction
    BOOST_STATIC_CONSTEXPR bool is_nothrow_destructible = value_storage_type::is_nothrow_destructible;

    //! \brief This promise type
    typedef basic_promise promise_type;
    //! \brief The future type associated with this promise type
    typedef basic_future<implementation_policy> future_type;
    //! \brief The future_errc type we use
    typedef typename implementation_policy::future_errc future_errc;
    //! \brief The future_error type we use
    typedef typename implementation_policy::future_error future_error;

    friend struct detail::lock_guard<basic_promise, future_type>;
    static_assert(std::is_move_constructible<value_type>::value || std::is_copy_constructible<value_type>::value, "Type must be move or copy constructible to be used in a lightweight basic_promise");    

    //! \brief EXTENSION: constexpr capable constructor
    BOOST_SPINLOCK_FUTURE_CONSTEXPR basic_promise() noexcept : _need_locks(false), _detached(false)
    {
    }
//// template<class Allocator> basic_promise(allocator_arg_t, Allocator a); // cannot support
    //! \brief Move constructor
    BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR basic_promise(basic_promise &&o) noexcept(is_nothrow_move_constructible) : _need_locks(o._need_locks), _detached(o._detached)
    {
      if(_need_locks) new (&_lock) BOOST_SPINLOCK_FUTURE_MUTEX_TYPE();
      detail::lock_guard<promise_type, future_type> h(&o);
      _storage=std::move(o._storage);
      if(h._f)
        h._f->_promise=this;
    }
    //! \brief Move assignment. If throws during move, destination promise is left as if default constructed i.e. any previous promise contents are destroyed.
    BOOST_SPINLOCK_FUTURE_MSVC_HELP basic_promise &operator=(basic_promise &&o) noexcept(is_nothrow_move_constructible)
    {
      this->~basic_promise();
      new (this) basic_promise(std::move(o));
      return *this;
    }
    basic_promise(const basic_promise &)=delete;
    basic_promise &operator=(const basic_promise &)=delete;
    BOOST_SPINLOCK_FUTURE_MSVC_HELP ~basic_promise() noexcept(is_nothrow_destructible)
    {
      if(!_detached)
      {
        detail::lock_guard<promise_type, future_type> h(this);
        if(h._f)
        {
          if(!h._f->is_ready())
            h._f->set_error(future_errc::broken_promise);
          h._f->_promise=nullptr;
        }
        // Destroy myself before locks exit
        _storage.clear();
      }
      if(_need_locks) _lock.~BOOST_SPINLOCK_FUTURE_MUTEX_TYPE_DESTRUCTOR();
    }
    
    //! \brief Swap this promise for another
    BOOST_SPINLOCK_FUTURE_MSVC_HELP void swap(basic_promise &o) noexcept(is_nothrow_move_constructible)
    {
      detail::lock_guard<promise_type, future_type> h1(this), h2(&o);
      _storage.swap(o._storage);
      if(h1._f)
        h1._f->_promise=&o;
      if(h2._f)
        h2._f->_promise=this;
    }
    
    //! \brief Create a future to be associated with this promise. Can be called exactly once, else throws a `future_already_retrieved`.
    BOOST_SPINLOCK_FUTURE_MSVC_HELP future_type get_future()
    {
      // If no value stored yet, I need locks on from now on
      if(!_need_locks && _storage.type==value_storage_type::storage_type::empty)
      {
        _need_locks=true;
        new (&_lock) BOOST_SPINLOCK_FUTURE_MUTEX_TYPE();
      }
      detail::lock_guard<promise_type, future_type> h(this);
      assert(!_need_locks || is_lockable_locked(_lock));
      if(h._f || _detached)
        throw future_error(future_errc::future_already_retrieved);
      future_type ret(this);
      h.unlock();
      return ret;
    }
    //! \brief EXTENSION: Does this basic_promise have a future?
    BOOST_SPINLOCK_FUTURE_MSVC_HELP bool has_future() const noexcept
    {
      //detail::lock_guard<value_type> h(this);
      return _storage.type==value_storage_type::storage_type::future || _detached;
    }
    
#define BOOST_SPINLOCK_FUTURE_IMPL(name, function) \
    name \
    { \
      detail::lock_guard<promise_type, future_type> h(this); \
      assert(!_need_locks || is_lockable_locked(_lock)); \
      if(_detached) \
        implementation_policy::_throw_error(monad_errc::already_set); \
      if(h._f) \
      { \
        assert(!_need_locks || is_lockable_locked(h._f->_lock)); \
        if(!h._f->empty()) \
          implementation_policy::_throw_error(monad_errc::already_set); \
        h._f->function; \
        h._f->_promise=nullptr; \
        _storage.clear(); \
        _detached=true; \
      } \
      else \
      { \
        if(_storage.type!=value_storage_type::storage_type::empty) \
          implementation_policy::_throw_error(monad_errc::already_set); \
        _storage.function; \
      } \
    }
    /*! \brief Sets the value to be returned by the associated future, releasing any waits occuring in other threads.
    */
    BOOST_SPINLOCK_FUTURE_IMPL(BOOST_SPINLOCK_FUTURE_MSVC_HELP void set_value(const value_type &v), set_value(v))
    /*! \brief Sets the value to be returned by the associated future, releasing any waits occuring in other threads.
    */
    BOOST_SPINLOCK_FUTURE_IMPL(BOOST_SPINLOCK_FUTURE_MSVC_HELP void set_value(value_type &&v), set_value(std::move(v)))
    /*! \brief EXTENSION: Sets the value by emplacement to be returned by the associated future, releasing any waits occuring in other threads.
    */
    BOOST_SPINLOCK_FUTURE_IMPL(template<class... Args> BOOST_SPINLOCK_FUTURE_MSVC_HELP void emplace_value(Args &&... args), emplace_value(std::forward<Args>(args)...))
    //! \brief EXTENSION: Set an error code outcome (doesn't allocate)
    BOOST_SPINLOCK_FUTURE_IMPL(BOOST_SPINLOCK_FUTURE_MSVC_HELP void set_error(error_type e), set_error(std::move(e)))
    //! \brief Sets an exception outcome
    BOOST_SPINLOCK_FUTURE_IMPL(BOOST_SPINLOCK_FUTURE_MSVC_HELP void set_exception(exception_type e), set_exception(std::move(e)))
#undef BOOST_SPINLOCK_FUTURE_IMPL
    template<typename E> void set_exception(E &&e)
    {
      set_exception(make_exception_ptr(std::forward<E>(e)));
    }
    
    // Not supported right now
//// void set_value_at_thread_exit(R v);
//// void set_exception_at_thread_exit(R v);

    //! \brief Call F when the future signals, consuming the future. Only one of these may be set.
    // template<class F> typename std::result_of<F(basic_future<value_type>)>::type then(F &&f);

    //! \brief Call F when the future signals, not consuming the future.
    // template<class F> typename std::result_of<F(basic_future<const value_type &>)>::type then(F &&f);
  };

  // TODO: basic_promise<void>, basic_promise<R&> specialisations
  // TODO: basic_future<void>, basic_future<R&> specialisations

  /*! \class basic_future
  \brief Lightweight next generation future with N4399 Concurrency TS extensions

  http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2015/n4399.html
  
  In exchange for some minor limitations, this lightweight promise-future is more than 10x faster than
  `std::promise` and `std::future` in the uncontended case. You also get deep integration with basic_monad and
  lots of cool functional programming stuff. Those limitations are:
  
  - No memory allocation is done, so if your code overrides the STL allocator for promise-future it won't port.
  - Consequently both promise and future must have sizeof greater than sizeof(T), so don't use multi-Kb sized T's
  as they'll get copied and moved around.
  - Your type T must implement either or both the copy and move constructors.
  - Your type T cannot be an error_type nor an exception_type. If you really want to promise one of those, wrap
  it in a wrapper type.
  - Don't use any of the `monad_errc` nor `future_errc` error codes for the errored return, else expect misoperation.
  - You can't set the future state at thread exit.
  - Neither promise nor future is thread safe until `get_future()` is first called and if and only if the state has not
  yet been set. In other words, if you set the state and then call `get_future()`, neither is ever threadsafe, not ever.
  This optimisation is safe as you will need some mechanism of sending the already ready future to another thread, and
  that will synchronise memory for you, however if you write code where multiple threads attempt to get the state
  concurrently, you will be in trouble. Note that if you call `get_future()` before setting the state, both promise and
  future become thread safe from the `get_future()` onwards and remain thread safe for the remainder of their lifetimes,
  so multiple threads can both set and/or get the state, with the usual exceptions being thrown if you try to do either twice.

  ## Supplying your own implementations of `basic_future<T>` ##
  To do this, simply supply a policy type of the following form. Note that this is identical to basic_monad's policy,
  except for the added members which are commented:
  \snippet future.hpp future_policy
  */
  template<class implementation_policy> class basic_future : protected basic_monad<implementation_policy>
  {
    friend implementation_policy;
  public:
    //! \brief The monad type associated with this basic_future
    typedef basic_monad<implementation_policy> monad_type;

    //! \brief This future has a value_type
    BOOST_STATIC_CONSTEXPR bool has_value_type = monad_type::has_value_type;
    //! \brief This future has an error_type
    BOOST_STATIC_CONSTEXPR bool has_error_type = monad_type::has_error_type;
    //! \brief This future has an exception_type
    BOOST_STATIC_CONSTEXPR bool has_exception_type = monad_type::has_exception_type;
    //! \brief The final implementation type
    typedef typename monad_type::implementation_type implementation_type;
    //! \brief The type potentially held by the future
    typedef typename monad_type::value_type value_type;
    //! \brief The error code potentially held by the future
    typedef typename monad_type::error_type error_type;
    //! \brief The exception ptr potentially held by the future
    typedef typename monad_type::exception_type exception_type;
    //! \brief Tag type for an empty future
    struct empty_type { typedef implementation_type parent_type; };
    //! \brief Rebind this future type into a different value_type
    template<typename U> using rebind = typename implementation_policy::template rebind<U>;

    //! \brief This future will never throw exceptions during move construction
    BOOST_STATIC_CONSTEXPR bool is_nothrow_move_constructible = monad_type::is_nothrow_move_constructible;
    //! \brief This future will never throw exceptions during move assignment
    BOOST_STATIC_CONSTEXPR bool is_nothrow_move_assignable = monad_type::is_nothrow_destructible && monad_type::is_nothrow_move_constructible;
    //! \brief This future will never throw exceptions during destruction
    BOOST_STATIC_CONSTEXPR bool is_nothrow_destructible = monad_type::is_nothrow_destructible;

    //! \brief Whether fetching value/error/exception is single shot
    BOOST_STATIC_CONSTEXPR bool is_consuming=implementation_policy::is_consuming;
    //! \brief The promise type matching this future type
    typedef basic_promise<implementation_policy> promise_type;
    //! \brief This future type
    typedef basic_future future_type;
    //! \brief The future_errc type we use
    typedef typename implementation_policy::future_errc future_errc;
    //! \brief The future_error type we use
    typedef typename implementation_policy::future_error future_error;
    
    friend class basic_promise<implementation_policy>;
    friend struct detail::lock_guard<promise_type, future_type>;
    static_assert(std::is_move_constructible<value_type>::value || std::is_copy_constructible<value_type>::value, "Type must be move or copy constructible to be used in a lightweight basic_future");    
  private:
    bool _need_locks;                 // Used to inhibit unnecessary atomic use, thus enabling constexpr collapse
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4624)
#endif
    union { BOOST_SPINLOCK_FUTURE_MUTEX_TYPE _lock; };  // Delay construction
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    promise_type *_promise;
  protected:
    // Called by basic_promise::get_future(), so currently thread safe
    BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR basic_future(promise_type *p) : monad_type(std::move(p->_storage)), _need_locks(p->_need_locks), _promise(p)
    {
      if(_need_locks) new (&_lock) BOOST_SPINLOCK_FUTURE_MUTEX_TYPE();
      // Clear the promise's storage, as we now have any state
      p->_storage.clear();
      // Do I already have a value? If so, detach, else set the promise to point to us
      if(!empty())
      {
        p->_detached=true;
        _promise=nullptr;
      }
      else
        p->_storage.set_pointer(this);
    }
    typedef detail::lock_guard<promise_type, future_type> lock_guard_type;
  public:
    //! \brief EXTENSION: constexpr capable constructor
    BOOST_SPINLOCK_FUTURE_CONSTEXPR basic_future() : _need_locks(false), _promise(nullptr)
    {
    }
    //! \brief If available for this kind of future, constructs this future type from some other future type
    template<class U, typename=decltype(implementation_policy::_construct(std::declval<U>()))> BOOST_SPINLOCK_FUTURE_CONSTEXPR basic_future(U &&o)
      : basic_future(implementation_policy::_construct(std::forward<U>(o)))
    {
    }
    //! \brief Move constructor
    BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR basic_future(basic_future &&o) noexcept(is_nothrow_move_constructible) : _need_locks(o._need_locks), _promise(nullptr)
    {
      if(_need_locks) new (&_lock) BOOST_SPINLOCK_FUTURE_MUTEX_TYPE();
      detail::lock_guard<promise_type, future_type> h(&o);
      new(this) monad_type(std::move(o));
      if(o._promise)
      {
        _promise=o._promise;
        o._promise=nullptr;
        if(h._p)
          h._p->_storage.pointer_=this;
      }
    }
    //! \brief Move assignment. If it throws during the move, the future is left as if default constructed.
    BOOST_SPINLOCK_FUTURE_MSVC_HELP basic_future &operator=(basic_future &&o) noexcept(is_nothrow_move_assignable)
    {
      this->~basic_future();
      new (this) basic_future(std::move(o));
      return *this;
    }
    basic_future(const basic_future &)=delete;
    basic_future &operator=(const basic_future &)=delete;
    BOOST_SPINLOCK_FUTURE_MSVC_HELP ~basic_future() noexcept(is_nothrow_destructible)
    {
      if(valid())
      {
        detail::lock_guard<promise_type, future_type> h(this);
        if(h._p)
          h._p->_storage.clear();
        // Destroy myself before locks exit
        monad_type::clear();
      }
      if(_need_locks) _lock.~BOOST_SPINLOCK_FUTURE_MUTEX_TYPE_DESTRUCTOR();
    }
    
    using monad_type::operator bool;
    using monad_type::operator tribool::tribool;
    using monad_type::is_ready;
    using monad_type::empty;
    using monad_type::has_value;
    using monad_type::has_error;
    using monad_type::has_exception;
    //! \brief True if the state is set or a promise is attached
    bool valid() const noexcept
    {
      if(!!_promise) return true;
      if(is_ready())
      {
        /* std::future returns valid()=true if state is broken promise
        if(has_error())
        {
          // If my value is a future_error, assume I'm invalid
          if(monad_type::_storage.error.category()==implementation_policy::future_category())
            return false;
        }*/
        return true;
      }
      return false;
    }
    
    //! \brief Swaps the future with another future
    void swap(basic_future &o) noexcept(is_nothrow_move_constructible)
    {
      detail::lock_guard<promise_type, future_type> h1(this), h2(&o);
      monad_type::swap(o._storage);
      std::swap(_need_locks, o._need_locks);
      std::swap(_promise, o._promise);
      if(h1._p)
        h1._p->_storage.pointer_=&o;
      if(h2._p)
        h2._p->_storage.pointer_=this;
    }

    //! \brief If available for this kind of future, converts this simple future into some policy determined shared future type
    BOOST_SPINLOCK_FUTURE_MSVC_HELP auto share() -> decltype(implementation_policy::_share(std::move(*this)))
    {
      if(!valid())
        throw std::future_error(std::future_errc::no_state);
      return implementation_policy::_share(std::move(*this));
    }
    
    //! \brief See basic_monad<>::get()
    using monad_type::get;
    using monad_type::get_or;
    using monad_type::get_and;
    using monad_type::get_error;
    using monad_type::get_error_or;
    using monad_type::get_error_and;
    using monad_type::get_exception;
    using monad_type::get_exception_or;
    using monad_type::get_exception_and;
    //! Compatibility with Boost.Thread
    exception_type get_exception_ptr() { return this->get_exception(); }
    
    //! \brief Wait for the future to become ready
    void wait() const
    {
      if(is_ready())
        return;
      if(!valid())
        throw std::future_error(std::future_errc::no_state);
      // TODO Actually sleep
      while(!monad_type::is_ready())
      {
        std::this_thread::yield();
      }
    }
//// template<class R, class P> future_status wait_for(const std::chrono::duration<R, P> &rel_time) const;  // TODO
//// template<class C, class D> future_status wait_until(const std::chrono::time_point<C, D> &abs_time) const;  // TODO
    
    // TODO Where F would return a basic_future<basic_future<...>>, we unwrap to a single basic_future<R>
//// template<class F> typename std::result_of<F(basic_future)>::type then(F &&f);
  };

  //! \brief Makes an already ready future
  template<typename R> inline basic_future<typename std::decay<R>::type> make_ready_future(R &&v)
  {
    return basic_future<typename std::decay<R>::type>(std::forward<R>(v));
  }
  //! \brief EXTENSION: Makes an already ready future
  template<typename R> inline basic_future<R> make_errored_future(std::error_code v)
  {
    return basic_future<R>(v);
  }
  //! \brief Makes an already ready future
  template<typename R> inline basic_future<R> make_exceptional_future(std::exception_ptr v)
  {
    return basic_future<R>(v);
  }

  // TODO
  // template<class InputIterator> ? when_all(InputIterator first, InputIterator last);
  // template<class... Futures> ? when_all(Futures &&... futures);
  // template<class Sequence> struct when_any_result;
  // template<class InputIterator> ? when_any(InputIterator first, InputIterator last);
  // template<class... Futures> ? when_any(Futures &&... futures);

  // TODO packaged_task

  namespace detail
  {
    //! [future_policy]
    template<typename R> struct future_policy;
    template<typename R> struct shared_future_policy;
    template<> struct future_policy<void>
    {
      typedef basic_monad<future_policy> monad_type;
      // In a monad policy, this is identical to monad_type. Not here.
      typedef basic_future<future_policy> implementation_type;
      typedef void value_type;
      typedef std::error_code error_type;
      typedef std::exception_ptr exception_type;
      // This type is void for monad, here it points to our future type
      typedef basic_future<future_policy> *pointer_type;
      template<typename U> using rebind = basic_future<future_policy<U>>;
      template<typename U> using rebind_policy = future_policy<U>;

      // Does getting this future's state consume it?
      BOOST_STATIC_CONSTEXPR bool is_consuming=true;
      // The type of future_errc to use for issuing errors
      typedef std::future_errc future_errc;
      // The type of future exception to use for issuing exceptions
      typedef std::future_error future_error;
      // The category of error code to use
      static const std::error_category &future_category() noexcept { return std::future_category(); }

      static BOOST_SPINLOCK_FUTURE_MSVC_HELP bool _throw_error(monad_errc ec)
      {
        switch(ec)
        {
          case monad_errc::already_set:
            throw future_error(future_errc::promise_already_satisfied);
          case monad_errc::no_state:
            throw future_error(future_errc::no_state);
          default:
            abort();
        }
      }
    protected:
      // Common preamble to the below
      template<bool is_consuming, class U> static BOOST_SPINLOCK_FUTURE_MSVC_HELP void _pre_get_value(U &&self)
      {
        if(!self.valid())
          _throw_error(monad_errc::no_state);
        if(self.has_error() || self.has_exception())
        {
          auto _self=detail::rebind_cast<monad_type>(self);
          if(self.has_error())
          {
            auto &category=_self._storage.error.category();
            // TODO Is there any way of making this user extensible? Seems daft this isn't in the STL :(
            if(category==std::future_category())
            {
              std::future_error e(_self._storage.error);
              if(is_consuming) _self.clear();
              throw e;
            }
            /*else if(category==std::iostream_category())
            {
              std::ios_base::failure e(std::move(_self._storage.error));
              if(is_consuming) _self.clear();
              throw e;
            }*/
            else
            {
              std::system_error e(_self._storage.error);
              if(is_consuming) _self.clear();
              throw e;
            }
          }
          if(self.has_exception())
          {
            std::exception_ptr e(_self._storage.exception);
            if(is_consuming) _self.clear();
            std::rethrow_exception(e);
          }
        }      
      }
      template<bool is_consuming, class U> static BOOST_SPINLOCK_FUTURE_MSVC_HELP error_type _get_error_impl(U &&self)
      {
        self.wait();
        typename implementation_type::lock_guard_type h(const_cast<U *>(&self));
        if(!self.valid())
          _throw_error(monad_errc::no_state);
        if(self.has_error())
        {
          error_type ec(self._storage.error);
          if(is_consuming) self.clear();
          return ec;
        }
        if(self.has_exception())
          return error_type((int) monad_errc::exception_present, monad_category());
        return error_type();
      }
      template<bool is_consuming, class U> static BOOST_SPINLOCK_FUTURE_MSVC_HELP exception_type _get_exception_impl(U &&self)
      {
        self.wait();
        typename implementation_type::lock_guard_type h(const_cast<U *>(&self));
        if(!self.valid())
          _throw_error(monad_errc::no_state);
        if(!self.has_error() && !self.has_exception())
          return exception_type();
        if(self.has_error())
        {
          exception_type e(std::make_exception_ptr(std::system_error(self._storage.error)));
          if(is_consuming) self.clear();
          return e;
        }
        if(self.has_exception())
        {
          exception_type e(self._storage.exception);
          if(is_consuming) self.clear();
          return e;
        }
        return exception_type();
      }
    public:
      static BOOST_SPINLOCK_FUTURE_MSVC_HELP void _get_value(implementation_type &self)
      {
        self.wait();
        implementation_type::lock_guard_type h(&self);
        _pre_get_value<is_consuming>(self);
        self.clear();
      }
      static BOOST_SPINLOCK_FUTURE_MSVC_HELP void _get_value(const implementation_type &self)
      {
        self.wait();
        implementation_type::lock_guard_type h(const_cast<implementation_type *>(&self));
        _pre_get_value<is_consuming>(self);
        const_cast<implementation_type &>(self).clear();
      }
      static BOOST_SPINLOCK_FUTURE_MSVC_HELP void _get_value(implementation_type &&self)
      {
        self.wait();
        implementation_type::lock_guard_type h(&self);
        _pre_get_value<is_consuming>(self);
        self.clear();
      }
      template<class U> static BOOST_SPINLOCK_FUTURE_MSVC_HELP error_type _get_error(const U &self)
      {
        return _get_error_impl<is_consuming>(self);
      }
      template<class U> static BOOST_SPINLOCK_FUTURE_MSVC_HELP exception_type _get_exception(const U &self)
      {
        return _get_exception_impl<is_consuming>(self);
      }
      // Makes share() available on this future. Defined out of line as need shared_future_policy defined first.
      static inline BOOST_SPINLOCK_FUTURE_MSVC_HELP basic_future<shared_future_policy<void>> _share(implementation_type &&self);
    private:
      // Disables implicit conversion from any other type of future
      template<class U> static BOOST_SPINLOCK_FUTURE_MSVC_HELP implementation_type _construct(basic_future<U> &&v);
    };
    template<typename R> struct future_policy : public future_policy<void>
    {
    protected:
      typedef future_policy<void> impl;
    public:
      typedef basic_future<future_policy> implementation_type;
      typedef R value_type;
      typedef basic_future<future_policy> *pointer_type;
      BOOST_STATIC_CONSTEXPR bool is_consuming=impl::is_consuming;

      // Called by get() &. Note we always return value_type by value.
      static BOOST_SPINLOCK_FUTURE_MSVC_HELP value_type _get_value(implementation_type &self)
      {
        self.wait();
        typename implementation_type::lock_guard_type h(&self);
        impl::_pre_get_value<is_consuming>(self);
        value_type v(std::move(self._storage.value));
        self.clear();
        return v;
      }
      // Called by get() const &. Note we always return value_type by value.
      static BOOST_SPINLOCK_FUTURE_MSVC_HELP value_type _get_value(const implementation_type &self)
      {
        self.wait();
        typename implementation_type::lock_guard_type h(const_cast<implementation_type *>(&self));
        impl::_pre_get_value<is_consuming>(self);
        value_type v(std::move(self._storage.value));
        const_cast<implementation_type &>(self).clear();
        return v;
      }
      // Called by get() &&. Note we always return value_type by value.
      static BOOST_SPINLOCK_FUTURE_MSVC_HELP value_type _get_value(implementation_type &&self)
      {
        self.wait();
        typename implementation_type::lock_guard_type h(&self);
        impl::_pre_get_value<is_consuming>(self);
        value_type v(std::move(self._storage.value));
        self.clear();
        return v;
      }
      static inline BOOST_SPINLOCK_FUTURE_MSVC_HELP basic_future<shared_future_policy<R>> _share(implementation_type &&self);
    };
    //! [future_policy]
    
    template<typename R> struct shared_future_policy;
    template<> struct shared_future_policy<void> : public future_policy<void>
    {
    protected:
      typedef future_policy<void> impl;
    public:
      typedef basic_monad<shared_future_policy> monad_type;
      typedef basic_future<shared_future_policy> implementation_type;
      typedef void value_type;
      typedef basic_future<shared_future_policy> *pointer_type;
      template<typename U> using rebind = basic_future<shared_future_policy<U>>;
      template<typename U> using rebind_policy = shared_future_policy<U>;

      BOOST_STATIC_CONSTEXPR bool is_consuming=false;

      static BOOST_SPINLOCK_FUTURE_MSVC_HELP void _get_value(implementation_type &self)
      {
        self.wait();
        implementation_type::lock_guard_type h(&self);
        impl::_pre_get_value<is_consuming>(self);
      }
      static BOOST_SPINLOCK_FUTURE_MSVC_HELP void _get_value(const implementation_type &self)
      {
        self.wait();
        implementation_type::lock_guard_type h(const_cast<implementation_type *>(&self));
        impl::_pre_get_value<is_consuming>(self);
      }
      static BOOST_SPINLOCK_FUTURE_MSVC_HELP void _get_value(implementation_type &&self)
      {
        self.wait();
        implementation_type::lock_guard_type h(&self);
        impl::_pre_get_value<is_consuming>(self);
      }
      template<class U> static BOOST_SPINLOCK_FUTURE_MSVC_HELP error_type _get_error(const U &self)
      {
        return impl::_get_error_impl<is_consuming>(self);
      }
      template<class U> static BOOST_SPINLOCK_FUTURE_MSVC_HELP exception_type _get_exception(const U &self)
      {
        return impl::_get_exception_impl<is_consuming>(self);
      }
      static BOOST_SPINLOCK_FUTURE_MSVC_HELP implementation_type _construct(basic_future<future_policy<void>> &&v)
      {
        return implementation_type(reinterpret_cast<implementation_type &&>(v));
      }
      static inline BOOST_SPINLOCK_FUTURE_MSVC_HELP basic_future<shared_future_policy<void>> _share(implementation_type &&self);
    };
    template<typename R> struct shared_future_policy : public shared_future_policy<void>
    {
    protected:
      typedef shared_future_policy<void> impl;
    public:
      typedef basic_future<shared_future_policy> implementation_type;
      typedef R value_type;
      typedef basic_future<shared_future_policy> *pointer_type;
      BOOST_STATIC_CONSTEXPR bool is_consuming=impl::is_consuming;

      // Called by get() &. Note we always return value_type by const lvalue ref.
      static BOOST_SPINLOCK_FUTURE_MSVC_HELP const value_type &_get_value(implementation_type &self)
      {
        self.wait();
        typename implementation_type::lock_guard_type h(&self);
        impl::_pre_get_value<is_consuming>(self);
        return self._storage.value;
      }
      // Called by get() const &. Note we always return value_type by const lvalue ref.
      static BOOST_SPINLOCK_FUTURE_MSVC_HELP const value_type &_get_value(const implementation_type &self)
      {
        self.wait();
        typename implementation_type::lock_guard_type h(const_cast<implementation_type *>(&self));
        impl::_pre_get_value<is_consuming>(self);
        return self._storage.value;
      }
      // Called by get() &&. Note we always return value_type by const lvalue ref.
      static BOOST_SPINLOCK_FUTURE_MSVC_HELP const value_type &_get_value(implementation_type &&self)
      {
        self.wait();
        typename implementation_type::lock_guard_type h(&self);
        impl::_pre_get_value<is_consuming>(self);
        return self._storage.value;
      }
      static BOOST_SPINLOCK_FUTURE_MSVC_HELP implementation_type _construct(basic_future<future_policy<R>> &&v)
      {
        return implementation_type(reinterpret_cast<implementation_type &&>(v));
      }
      static inline BOOST_SPINLOCK_FUTURE_MSVC_HELP basic_future<shared_future_policy<R>> _share(implementation_type &&self);
    };
    inline basic_future<shared_future_policy<void>> future_policy<void>::_share(typename future_policy<void>::implementation_type &&self)
    {
      return basic_future<shared_future_policy<void>>(reinterpret_cast<basic_future<shared_future_policy<void>> &&>(self));
    }
    template<typename R> inline basic_future<shared_future_policy<R>> future_policy<R>::_share(typename future_policy<R>::implementation_type &&self)
    {
      return basic_future<shared_future_policy<R>>(reinterpret_cast<basic_future<shared_future_policy<R>> &&>(self));
    }
  }

  template<typename R> using promise = basic_promise<detail::future_policy<R>>;
  template<typename R> using future = basic_future<detail::future_policy<R>>;
  // TEMPORARY
  template<typename R> using shared_future = basic_future<detail::shared_future_policy<R>>;

}
BOOST_SPINLOCK_V1_NAMESPACE_END

namespace std
{
  template<typename R> inline void swap(BOOST_SPINLOCK_V1_NAMESPACE::lightweight_futures::basic_promise<R> &a, BOOST_SPINLOCK_V1_NAMESPACE::lightweight_futures::basic_promise<R> &b)
  {
    a.swap(b);
  }
  template<typename R> inline void swap(BOOST_SPINLOCK_V1_NAMESPACE::lightweight_futures::basic_future<R> &a, BOOST_SPINLOCK_V1_NAMESPACE::lightweight_futures::basic_future<R> &b)
  {
    a.swap(b);
  }
}

#endif

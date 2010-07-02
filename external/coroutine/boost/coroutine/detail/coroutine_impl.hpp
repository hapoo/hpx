//  Copyright (c) 2006, Giovanni P. Deretta
//
//  This code may be used under either of the following two licences:
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy 
//  of this software and associated documentation files (the "Software"), to deal 
//  in the Software without restriction, including without limitation the rights 
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
//  copies of the Software, and to permit persons to whom the Software is 
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in 
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
//  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
//  THE SOFTWARE. OF SUCH DAMAGE.
//
//  Or:
//
//  Distributed under the Boost Software License, Version 1.0.
//  (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_COROUTINE_COROUTINE_IMPL_HPP_20060601
#define BOOST_COROUTINE_COROUTINE_IMPL_HPP_20060601
#if defined(_MSC_VER)
#pragma warning (push)
#pragma warning (disable: 4355) //this used in base member initializer
#endif 

#include <cstddef>
#include <boost/optional.hpp>
#include <boost/thread/tss.hpp>
#include <boost/coroutine/detail/argument_unpacker.hpp>
#include <boost/coroutine/detail/coroutine_accessor.hpp>
#include <boost/coroutine/detail/context_base.hpp>
#include <boost/coroutine/detail/self.hpp>
#include <boost/coroutine/detail/static.hpp>

#include <boost/config.hpp>
#include <boost/lockfree/detail/freelist.hpp>

#if defined(BOOST_WINDOWS)
# define BOOST_COROUTINE_SYMBOL_EXPORT      __declspec(dllexport)
# define BOOST_COROUTINE_SYMBOL_IMPORT      __declspec(dllimport)
#elif defined(BOOST_COROUTINE_GCC_HAVE_VISIBILITY)
# define BOOST_COROUTINE_SYMBOL_EXPORT      __attribute__((visibility("default")))
# define BOOST_COROUTINE_SYMBOL_IMPORT      __attribute__((visibility("default")))
#else
# define BOOST_COROUTINE_SYMBOL_EXPORT      /* empty */
# define BOOST_COROUTINE_SYMBOL_IMPORT      /* empty */
#endif

#if defined(BOOST_COROUTINE_EXPORTS)
# define  BOOST_COROUTINE_EXPORT       BOOST_COROUTINE_SYMBOL_EXPORT
#else
# define  BOOST_COROUTINE_EXPORT       BOOST_COROUTINE_SYMBOL_IMPORT
#endif

namespace boost { namespace coroutines { namespace detail {
        
  // This class augment the contest_base class with
  // the coroutine signature type.
  // This is mostly just a place to put
  // typesafe argument and result type pointers.
  template<typename CoroutineType, typename ContextImpl>
  class coroutine_impl:
    public context_base<ContextImpl>
  {
  public:
    template<typename DerivedType, typename ResultType>
    friend class add_result;

    typedef ContextImpl context_impl;
    typedef CoroutineType coroutine_type;
    typedef coroutine_impl<coroutine_type, context_impl> type;
    typedef context_base<context_impl> context_base_;
    typedef typename coroutine_type::arg_slot_type arg_slot_type;
    typedef typename coroutine_type::result_type result_type;
    typedef typename coroutine_type::result_slot_type result_slot_type;
    typedef void* thread_id_type;

    typedef boost::intrusive_ptr<type> pointer;
  
    template<typename DerivedType>
        coroutine_impl(DerivedType * this_, thread_id_type id, 
            std::ptrdiff_t stack_size) 
      : context_base_(*this_, stack_size),
        m_arg(0),
        m_result(0),
        m_thread_id(id)
    {}
                
    arg_slot_type * args() {
      BOOST_ASSERT(m_arg);
      return m_arg;
    };
    
    result_slot_type * result() {
      BOOST_ASSERT(m_result);
      BOOST_ASSERT(*m_result);
      return *this->m_result;
    } 

    template<typename Functor>
    static inline pointer create(Functor, thread_id_type, std::ptrdiff_t);

//     template<typename Functor>
//     static inline void rebind(pointer, Functor, thread_id_type);

    void bind_args(arg_slot_type* arg) {
      m_arg = arg;
    }

    void bind_result(result_slot_type* res) {
      *m_result = res;
    }

    // Another level of indirection is needed to handle
    // yield_to correctly.
    void bind_result_pointer(result_slot_type** resp) {
      m_result = resp;
    }

    result_slot_type** result_pointer() {
      return m_result;
    }

    // This function must be called only for void
    // coroutines. It wakes up the coroutine.
    // Entering the wait state does not cause this
    // method to throw.
    void run() {
      arg_slot_type void_args;
      result_slot_type * ptr = 0;
      
      // This dummy binding is required because
      // do_call expect args() and result()
      // to return a non NULL result.
      bind_args(&void_args);
      bind_result_pointer(&ptr);
      this->wake_up();
    }

    thread_id_type get_thread_id() const
    {
        return m_thread_id;
    }

    std::size_t get_thread_phase() const
    {
        return this->phase();
    }

  private:
    typedef detail::coroutine_self<coroutine_type> self_type;
    static boost::thread_specific_ptr<self_type*> self_;

  public:
    BOOST_COROUTINE_EXPORT static void set_self(self_type* self);
    BOOST_COROUTINE_EXPORT static self_type* get_self();

  protected:
    void rebind(thread_id_type id)
    {
        m_thread_id = id;
        this->context_base_::rebind();
    }

  protected:
    boost::optional<result_slot_type>  m_result_last;

  private:
    arg_slot_type * m_arg;
    result_slot_type ** m_result;
    thread_id_type m_thread_id;
  };

  // the TSS holds a pointer to the self instance as stored on the stack
  template<typename CoroutineType, typename ContextImpl>
  boost::thread_specific_ptr<
      typename coroutine_impl<CoroutineType, ContextImpl>::self_type*
  > coroutine_impl<CoroutineType, ContextImpl>::self_;

  /////////////////////////////////////////////////////////////////////////////
  template <typename Coroutine>
  struct coroutine_heap
  {
      ~coroutine_heap()
      {
          Coroutine* next = heap_.get();
          while (next) {
              delete next;
              next = heap_.get();
          }
      }

      Coroutine* allocate()
      {
          return heap_.get();
      }

      void deallocate(Coroutine* p)
      {
          heap_.deallocate(p);
      }

      lockfree::caching_freelist<Coroutine> heap_;
  };

  /////////////////////////////////////////////////////////////////////////////
  // This type augments coroutine_impl type with the type of the stored 
  // functor. The type of this object is erased right after construction
  // when it is assigned to a pointer to coroutine_impl. A deleter is
  // passed down to make sure that the correct derived type is deleted.
  template<typename FunctorType, typename CoroutineType, typename ContextImpl>
  class coroutine_impl_wrapper :
    public coroutine_impl<CoroutineType, ContextImpl> {
  public:
    typedef coroutine_impl_wrapper<FunctorType, CoroutineType, ContextImpl> 
        type;
    typedef CoroutineType coroutine_type;
    typedef typename CoroutineType::result_type result_type;
    typedef coroutine_impl<CoroutineType, ContextImpl> super_type;
    typedef typename super_type::thread_id_type thread_id_type;

    typedef FunctorType functor_type;

    coroutine_impl_wrapper(functor_type f, thread_id_type id,
                std::ptrdiff_t stack_size) 
      : super_type(this, id, stack_size),
        m_fun(f)
    {}

    void operator()() {
      typedef typename super_type::context_exit_status
        context_exit_status;
      context_exit_status status = super_type::ctx_exited_return;

      // loop as long this coroutine has been rebound
      do {
        boost::exception_ptr tinfo;
        try {
          this->check_exit_state();
          do_call<result_type>();
        } catch (exit_exception const&) {
          status = super_type::ctx_exited_exit;
          tinfo = boost::current_exception();
        } catch (boost::exception const&) {
          status = super_type::ctx_exited_abnormally;
          tinfo = boost::current_exception();
        } catch (std::exception const&) {
          status = super_type::ctx_exited_abnormally;
          tinfo = boost::current_exception();
        } catch (...) {
          status = super_type::ctx_exited_abnormally;
          tinfo = boost::current_exception();
        }
        this->do_return(status, tinfo);
      } while (m_state == super_type::ctx_running);

      // should not get here, never
      BOOST_ASSERT(m_state == super_type::ctx_running);
    }

private:
    struct reset_self_on_exit
    {
        typedef BOOST_DEDUCED_TYPENAME coroutine_type::self self_type;

        reset_self_on_exit(self_type* val)
        {
            super_type::set_self(val);
        }
        ~reset_self_on_exit()
        {
            super_type::set_self(NULL);
        }
    };

  public:

    //GCC workaround as per enable_if docs 
    template <int> struct dummy { dummy(int) {} };
    /*
     * Implementation for operator()
     * This is for void result types.
     * Can throw if m_fun throws. At least it can throw exit_exception.
     */
    template<typename ResultType>
    typename boost::enable_if<boost::is_void<ResultType> >::type
    do_call(dummy<0> = 0) 
    {
      BOOST_ASSERT(this->count() > 0);

      typedef BOOST_DEDUCED_TYPENAME coroutine_type::self self_type;

      // In this particular case result_slot_type is guaranteed to be
      // default constructible.
      typedef BOOST_DEDUCED_TYPENAME coroutine_type::result_slot_type 
        result_slot_type;

      {
//           boost::optional<self_type> self (coroutine_accessor::in_place(this));
          self_type self(this);
          reset_self_on_exit on_exit(&self);
          detail::unpack(m_fun, *this->args(), 
             detail::trait_tag<typename coroutine_type::arg_slot_traits>());
      }

      this->m_result_last = result_slot_type();
      this->bind_result(&*this->m_result_last);
    }

    // Same as above, but for non void result types.
    template<typename ResultType>
    typename boost::disable_if<boost::is_void<ResultType> >::type
    do_call(dummy<1> = 1) 
    {
      BOOST_ASSERT(this->count() > 0);

      typedef BOOST_DEDUCED_TYPENAME coroutine_type::self self_type;
      typedef BOOST_DEDUCED_TYPENAME coroutine_type::arg_slot_traits traits;
      typedef BOOST_DEDUCED_TYPENAME coroutine_type::result_slot_type 
        result_slot_type;

      {
//           boost::optional<self_type> self (coroutine_accessor::in_place(this));
          self_type self(this);
          reset_self_on_exit on_exit(&self);
          this->m_result_last = boost::in_place(result_slot_type(
                  detail::unpack(m_fun, *this->args(), detail::trait_tag<traits>())
              ));
      }

      this->bind_result(&*this->m_result_last);
    }

    static inline void destroy(type* p);

    void rebind(FunctorType f, thread_id_type id)
    {
        m_fun = f;
        this->super_type::rebind(id);
    }

  private:
    // the memory for the threads is managed by a lockfree caching_freelist
    typedef coroutine_heap<coroutine_impl_wrapper> heap_type;

    struct heap_tag {};

    static heap_type& get_heap()
    {
        // ensure thread-safe initialization
        static_<heap_type, heap_tag> heap;
        return heap.get();
    }

  public:
    static coroutine_impl_wrapper* allocate()
    {
        return get_heap().allocate();
    }
    static void deallocate(coroutine_impl_wrapper* wrapper)
    {
        get_heap().deallocate(wrapper);
    }

    FunctorType m_fun;
  };

  template<typename CoroutineType, typename ContextImpl>
  template<typename Functor>
  inline
  typename 
  coroutine_impl<CoroutineType, ContextImpl>::pointer
  coroutine_impl<CoroutineType, ContextImpl>::
  create(Functor f, thread_id_type id = 0, std::ptrdiff_t stack_size = default_stack_size) 
  {
      typedef coroutine_impl_wrapper<Functor, CoroutineType, ContextImpl>
          wrapper_type;

      wrapper_type* wrapper = wrapper_type::allocate();
      if (NULL == wrapper)
          return new wrapper_type(f, id, stack_size);

      wrapper->rebind(f, id);
      return wrapper;
  }

//   template<typename Functor>
//   inline void 
//   coroutine_impl<CoroutineType, ContextImpl>::rebind(
//       typename coroutine_impl<CoroutineType, ContextImpl>::pointer p,
//       Functor f, thread_id_type id) 
//   {
//       typedef 
//           coroutine_impl_wrapper<Functor, CoroutineType, ContextImpl> 
//       derived_type;
//       boost::static_pointer_cast<derived_type>(p)->rebind(f, id);
//   }

  template<typename Functor, typename CoroutineType, typename ContextImpl>
  inline void 
  coroutine_impl_wrapper<Functor, CoroutineType, ContextImpl>::destroy(type* p)
  {
      deallocate(p);
  }

} } }

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif

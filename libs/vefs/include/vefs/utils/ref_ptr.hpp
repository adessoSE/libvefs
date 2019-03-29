#pragma once

#include <cassert>

#include <typeinfo>
#include <typeindex>
#include <unordered_map>

namespace vefs::utils
{
    enum class ref_ptr_acquire_tag {};
    constexpr static ref_ptr_acquire_tag ref_ptr_acquire{};

    enum class ref_ptr_import_tag {};
    constexpr static ref_ptr_import_tag ref_ptr_import{};

    template <class T>
    class ref_ptr final
    {
    public:
        inline ref_ptr();
        inline ref_ptr(std::nullptr_t);
        // acquires ownership of ptr (-> calls add_reference_impl)
        inline ref_ptr(T *ptr, ref_ptr_acquire_tag);
        // imports ownership of an already acquired reference
        inline ref_ptr(T *ptr, ref_ptr_import_tag);
        inline ref_ptr(const ref_ptr &other);
        inline ref_ptr(ref_ptr &&other) noexcept;
        template <typename U, std::enable_if_t<std::is_convertible_v<U *, T *>, int> = 0>
        inline ref_ptr(ref_ptr<U> other) noexcept;
        inline ~ref_ptr();

        inline ref_ptr & operator=(const ref_ptr &other);
        inline ref_ptr & operator=(ref_ptr &&other);

        inline explicit operator bool() const;

        inline T & operator*() const;
        inline T * operator->() const;

        inline T * get() const;
        auto release() noexcept
            -> T *;

        friend void swap(ref_ptr &lhs, ref_ptr &rhs) noexcept
        {
            using std::swap;
            swap(lhs.mPtr, rhs.mPtr);
        }

    private:
        void add_reference_impl();
        void release_impl();

        T *mPtr;
    };

    template <class T, typename CreateTag>
    inline ref_ptr<T> make_ref_ptr(T *ptr, CreateTag tag)
    {
        return ref_ptr<T>{ ptr, tag };
    }

    template <class T, typename... Args>
    inline ref_ptr<T> make_ref_counted(Args&&... args)
    {
        return ref_ptr<T>{ new T(std::forward<Args>(args)...), ref_ptr_import };
    }


    template<class T>
    inline ref_ptr<T>::ref_ptr()
        : mPtr{ nullptr }
    {
    }

    template<class T>
    inline ref_ptr<T>::ref_ptr(std::nullptr_t)
        : ref_ptr{}
    {
    }

    template<class T>
    inline void ref_ptr<T>::add_reference_impl()
    {
        mPtr->add_reference();
    }

    template<class T>
    inline void ref_ptr<T>::release_impl()
    {
        mPtr->release();
    }

    template<class T>
    inline ref_ptr<T>::ref_ptr(T * ptr, ref_ptr_acquire_tag)
        : mPtr{ ptr }
    {
        if (mPtr)
        {
            add_reference_impl();
        }
    }

    template<class T>
    inline ref_ptr<T>::ref_ptr(T * ptr, ref_ptr_import_tag)
        : mPtr{ ptr }
    {
    }

    template<class T>
    inline ref_ptr<T>::ref_ptr(const ref_ptr &other)
        : ref_ptr{ other.mPtr, ref_ptr_acquire }
    {
    }

    template<class T>
    inline ref_ptr<T>::ref_ptr(ref_ptr &&other) noexcept
        : ref_ptr{ other.mPtr, ref_ptr_import }
    {
        other.mPtr = nullptr;
    }

    template<class T>
    template<typename U, std::enable_if_t<std::is_convertible_v<U *, T *>, int>>
    inline ref_ptr<T>::ref_ptr(ref_ptr<U> other) noexcept
        : ref_ptr{ other.release(), ref_ptr_import }
    {
    }

    template<class T>
    inline ref_ptr<T>::~ref_ptr()
    {
        if (mPtr)
        {
            release_impl();
        }
    }

    template<class T>
    inline ref_ptr<T> & ref_ptr<T>::operator=(const ref_ptr &other)
    {
        if (mPtr)
        {
            release_impl();
        }
        mPtr = other.mPtr;
        if (mPtr)
        {
            add_reference_impl();
        }

        return *this;
    }

    template<class T>
    inline ref_ptr<T> & ref_ptr<T>::operator=(ref_ptr &&other)
    {
        if (mPtr)
        {
            release_impl();
        }
        mPtr = other.mPtr;
        other.mPtr = nullptr;

        return *this;
    }

    template<class T>
    inline ref_ptr<T>::operator bool() const
    {
        return mPtr != nullptr;
    }

    template<class T>
    inline T & ref_ptr<T>::operator*() const
    {
        assert(mPtr);
        return *mPtr;
    }

    template<class T>
    inline T * ref_ptr<T>::operator->() const
    {
        assert(mPtr);
        return mPtr;
    }

    template<class T>
    inline T * ref_ptr<T>::get() const
    {
        return mPtr;
    }

    template<class T>
    inline auto ref_ptr<T>::release() noexcept
        -> T *
    {
        auto ptr = mPtr;
        mPtr = nullptr;
        return ptr;
    }

    namespace detail
    {
        template <typename T>
        class erased_ref_ops
        {
            static void add_reference(void *h) noexcept
            {
                reinterpret_cast<T *>(h)->add_reference();
            }
            static void release(void *h) noexcept
            {
                reinterpret_cast<T *>(h)->release();
            }
        };

        struct ref_ops_vtable final
        {
            using op_fn = void(*)(void *) noexcept;

            op_fn add_reference;
            op_fn release;
        };

        template <typename T>
        constexpr ref_ops_vtable ref_ops_vtable_of = {
            &erased_ref_ops<T>::add_reference,
            &erased_ref_ops<T>::release
        };
    }

    template <>
    class ref_ptr<void> final
    {
    public:
        ref_ptr() noexcept;
        ref_ptr(std::nullptr_t) noexcept;

        // acquires ownership of ptr (-> calls add_reference_impl)
        template <typename T>
        ref_ptr(T *ptr, ref_ptr_acquire_tag) noexcept;
        // imports ownership of an already acquired reference
        template <typename T>
        ref_ptr(T *ptr, ref_ptr_import_tag) noexcept;

        template <typename T>
        ref_ptr(const ref_ptr<T> &other) noexcept;
        template <typename T>
        ref_ptr(ref_ptr<T> &&other) noexcept;

        ref_ptr(const ref_ptr &other) noexcept;
        ref_ptr(ref_ptr &&other) noexcept;
        ~ref_ptr();

        template <typename T>
        auto operator=(const ref_ptr<T> &other) noexcept
            -> ref_ptr &;
        template <typename T>
        auto operator=(ref_ptr<T> &&other) noexcept
            ->ref_ptr &;

        auto operator=(const ref_ptr &other) noexcept
            ->ref_ptr &;
        auto operator=(ref_ptr &&other) noexcept
            ->ref_ptr &;

        explicit operator bool() const noexcept;

        auto raw_handle() const noexcept
            -> void *;
        template <typename T>
        auto release_as() noexcept
            -> T *;

        friend inline void swap(ref_ptr &lhs, ref_ptr &rhs) noexcept
        {
            using std::swap;
            swap(lhs.mVTable, rhs.mVTable);
            swap(lhs.mHandle, rhs.mHandle);
        }

    private:
        ref_ptr(void *h, const detail::ref_ops_vtable *vtable) noexcept;

        void add_reference_impl() noexcept;
        void release_impl() noexcept;

        const detail::ref_ops_vtable *mVTable;
        void *mHandle;
    };

    inline ref_ptr<void>::ref_ptr(void *h, const detail::ref_ops_vtable *vtable) noexcept
        : mVTable{ vtable }
        , mHandle{ h }
    {
    }

    inline ref_ptr<void>::ref_ptr() noexcept
        : mVTable{ nullptr }
        , mHandle{ nullptr }
    {
    }

    inline ref_ptr<void>::ref_ptr(std::nullptr_t) noexcept
        : ref_ptr{}
    {
    }

    inline void ref_ptr<void>::add_reference_impl() noexcept
    {
        mVTable->add_reference(mHandle);
    }

    inline void ref_ptr<void>::release_impl() noexcept
    {
        mVTable->release(mHandle);
    }

    template<class T>
    inline ref_ptr<void>::ref_ptr(T *ptr, ref_ptr_import_tag) noexcept
        : mVTable{ &detail::ref_ops_vtable_of<T> }
        , mHandle{ ptr }
    {
    }

    template<class T>
    inline ref_ptr<void>::ref_ptr(T *ptr, ref_ptr_acquire_tag) noexcept
        : ref_ptr{ ptr, ref_ptr_import }
    {
        if (mHandle)
        {
            add_reference_impl();
        }
    }

    template<class T>
    inline ref_ptr<void>::ref_ptr(const ref_ptr<T> &other) noexcept
        : ref_ptr{ other.mPtr, ref_ptr_acquire }
    {
    }

    template<class T>
    inline ref_ptr<void>::ref_ptr(ref_ptr<T> &&other) noexcept
        : ref_ptr{ other.mPtr, ref_ptr_import }
    {
        other.mPtr = nullptr;
    }

    inline ref_ptr<void>::ref_ptr(const ref_ptr &other) noexcept
        : ref_ptr{ other.mHandle, other.mVTable }
    {
        if (mHandle)
        {
            add_reference_impl();
        }
    }

    inline ref_ptr<void>::ref_ptr(ref_ptr &&other) noexcept
        : ref_ptr{ other.mHandle, other.mVTable }
    {
        other.mVTable = nullptr;
        other.mHandle = nullptr;
    }

    inline ref_ptr<void>::~ref_ptr() noexcept
    {
        if (mHandle)
        {
            release_impl();
        }
    }

    inline auto ref_ptr<void>::operator=(const ref_ptr &other) noexcept
        -> ref_ptr &
    {
        if (mHandle)
        {
            release_impl();
        }
        mVTable = other.mVTable;
        mHandle = other.mHandle;
        if (mHandle)
        {
            add_reference_impl();
        }

        return *this;
    }

    inline auto ref_ptr<void>::operator=(ref_ptr &&other) noexcept
        -> ref_ptr &
    {
        if (mHandle)
        {
            release_impl();
        }
        mHandle = other.mHandle;
        other.mVTable = nullptr;
        other.mHandle = nullptr;

        return *this;
    }

    template <typename T>
    inline auto ref_ptr<void>::operator=(const ref_ptr<T> &other) noexcept
        -> ref_ptr &
    {
        if (mHandle)
        {
            release_impl();
        }
        mHandle = other.mPtr;
        mVTable = &detail::ref_ops_vtable_of<T>;
        if (mHandle)
        {
            add_reference_impl();
        }

        return *this;
    }

    template <typename T>
    inline auto ref_ptr<void>::operator=(ref_ptr<T> &&other) noexcept
        -> ref_ptr &
    {
        if (mHandle)
        {
            release_impl();
        }
        mHandle = other.mPtr;
        mVTable = &detail::ref_ops_vtable_of<T>;
        other.mPtr = nullptr;

        return *this;
    }

    inline ref_ptr<void>::operator bool() const noexcept
    {
        return mHandle != nullptr;
    }

    inline auto ref_ptr<void>::raw_handle() const noexcept
        -> void *
    {
        return mHandle;
    }

    template<typename T>
    inline auto ref_ptr<void>::release_as() noexcept
        -> T *
    {
        auto ptr = reinterpret_cast<T *>(mHandle);
        mHandle = nullptr;
        mVTable = nullptr;
        return ptr;
    }

    template <typename T>
    inline auto reinterpret_pointer_cast(const ref_ptr<void> &ptr) noexcept
        -> ref_ptr<T>
    {
        return { reinterpret_cast<T *>(ptr.raw_handle()), ref_ptr_acquire };
    }

    template <typename T>
    inline auto reinterpret_pointer_cast(ref_ptr<void> &&ptr) noexcept
        -> ref_ptr<T>
    {
        return { ptr.release_as<T>(), ref_ptr_import };
    }


    template <typename T, typename R>
    class aliasing_ref_ptr
    {
    public:
        constexpr aliasing_ref_ptr() noexcept;
        constexpr aliasing_ref_ptr(std::nullptr_t) noexcept;

        constexpr aliasing_ref_ptr(T *ptr, ref_ptr<R> ctr) noexcept;

        constexpr aliasing_ref_ptr(const aliasing_ref_ptr &other) noexcept;
        constexpr aliasing_ref_ptr(aliasing_ref_ptr &&other) noexcept;

        constexpr auto operator=(const aliasing_ref_ptr &other) noexcept
            -> aliasing_ref_ptr &;
        constexpr auto operator=(aliasing_ref_ptr &&other) noexcept
            -> aliasing_ref_ptr &;

        constexpr explicit operator bool() const noexcept;

        constexpr auto operator*() const noexcept
            -> T &;
        constexpr auto operator->() const noexcept
            -> T *;

        constexpr auto get() const noexcept
            -> T *;
        constexpr auto get_handle() const noexcept
            -> const ref_ptr<R> &;

        friend inline void swap(aliasing_ref_ptr &lhs, aliasing_ref_ptr &rhs) noexcept
        {
            using std::swap;
            swap(lhs.mHandle, rhs.mHandle);
            swap(lhs.mPtr, rhs.mPtr);
        }

    private:
        T *mPtr;
        ref_ptr<R> mHandle;
    };

    template <typename T, typename R>
    constexpr aliasing_ref_ptr<T, R>::aliasing_ref_ptr() noexcept
        : mPtr{ nullptr }
        , mHandle{ nullptr }
    {
    }
    template <typename T, typename R>
    constexpr aliasing_ref_ptr<T, R>::aliasing_ref_ptr(std::nullptr_t) noexcept
        : mPtr{ nullptr }
        , mHandle{ nullptr }
    {
    }

    template <typename T, typename R>
    constexpr aliasing_ref_ptr<T, R>::aliasing_ref_ptr(T *ptr, ref_ptr<R> ctr) noexcept
        : mPtr{ ptr }
        , mHandle{ std::move(ctr) }
    {
    }

    template <typename T, typename R>
    constexpr aliasing_ref_ptr<T, R>::aliasing_ref_ptr(const aliasing_ref_ptr &other) noexcept
        : mPtr{ other.mPtr }
        , mHandle{ other.mHandle }
    {
    }
    template <typename T, typename R>
    constexpr aliasing_ref_ptr<T, R>::aliasing_ref_ptr(aliasing_ref_ptr &&other) noexcept
        : mPtr{ other.mPtr }
        , mHandle{ std::move(other.mHandle) }
    {
        other.mPtr = nullptr;
    }

    template<typename T, typename R>
    constexpr auto aliasing_ref_ptr<T, R>::operator=(const aliasing_ref_ptr &other) noexcept
        -> aliasing_ref_ptr &
    {
        mPtr = other.mPtr;
        mHandle = other.mHandle;
        return *this;
    }
    template<typename T, typename R>
    constexpr auto aliasing_ref_ptr<T, R>::operator=(aliasing_ref_ptr &&other) noexcept
        -> aliasing_ref_ptr &
    {
        mPtr = other.mPtr;
        other.mPtr = nullptr;
        mHandle = std::move(other.mHandle);
        return *this;
    }

    template<typename T, typename R>
    constexpr aliasing_ref_ptr<T, R>::operator bool() const noexcept
    {
        return mPtr != nullptr;
    }

    template<typename T, typename R>
    constexpr auto aliasing_ref_ptr<T, R>::operator*() const noexcept
        -> T &
    {
        assert(mPtr);
        return *mPtr;
    }

    template<typename T, typename R>
    constexpr auto aliasing_ref_ptr<T, R>::operator->() const noexcept
        -> T *
    {
        assert(mPtr);
        return mPtr;
    }

    template<typename T, typename R>
    constexpr auto aliasing_ref_ptr<T, R>::get() const noexcept
        -> T *
    {
        return mPtr;
    }

    template<typename T, typename R>
    constexpr auto aliasing_ref_ptr<T, R>::get_handle() const noexcept
        -> const ref_ptr<R>&
    {
        return mHandle;
    }
}

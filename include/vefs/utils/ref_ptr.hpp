#pragma once

#include <cassert>

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
        // acquires ownership of ptr (-> calls add_reference)
        inline ref_ptr(T *ptr, ref_ptr_acquire_tag);
        // imports ownership of an already acquired reference
        inline ref_ptr(T *ptr, ref_ptr_import_tag);
        inline ref_ptr(const ref_ptr &other);
        inline ref_ptr(ref_ptr &&other) noexcept;
        inline ~ref_ptr();

        inline ref_ptr & operator=(const ref_ptr &other);
        inline ref_ptr & operator=(ref_ptr &&other);

        inline explicit operator bool() const;

        inline T & operator*() const;
        inline T * operator->() const;

        inline T * get() const;

    private:
        void add_reference();
        void release();

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
    inline void ref_ptr<T>::add_reference()
    {
        mPtr->add_reference();
    }

    template<class T>
    inline void ref_ptr<T>::release()
    {
        mPtr->release();
    }

    template<class T>
    inline ref_ptr<T>::ref_ptr(T * ptr, ref_ptr_acquire_tag)
        : mPtr{ ptr }
    {
        if (mPtr)
        {
            add_reference();
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
    inline ref_ptr<T>::~ref_ptr()
    {
        if (mPtr)
        {
            release();
        }
    }

    template<class T>
    inline ref_ptr<T> & ref_ptr<T>::operator=(const ref_ptr &other)
    {
        if (mPtr)
        {
            release();
        }
        mPtr = other.mPtr;
        if (mPtr)
        {
            add_reference();
        }

        return *this;
    }

    template<class T>
    inline ref_ptr<T> & ref_ptr<T>::operator=(ref_ptr &&other)
    {
        if (mPtr)
        {
            release();
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
}

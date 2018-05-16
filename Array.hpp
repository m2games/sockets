#pragma once

#include <stdlib.h>
#include <assert.h>

// does not respect constructors & destructors
// @TODO(matiTechno): copy constructor, swap, etc.
template<typename T>
class Array
{
public:
    Array() = default;
    ~Array() {free(data_);}
    Array(const Array<T>&) = delete;
    Array<T>& operator=(const Array<T>&) = delete;

    void swap(Array<T>& other)
    {
        int size = other.size_;
        int capacity = other.capacity_;
        T* data = other.data_;

        other.size_ = size_;
        other.capacity_ = capacity_;
        other.data_ = data_;

        size_ = size;
        capacity_ = capacity;
        data_ = data;
    }

    void pushBack(const T& val)
    {
        ++size_;
        if(size_ > capacity_)
        {
            capacity_ = size_ * 2;
            grow();
        }
        data_[size_ - 1] = val;
    }


    void reserve(int size)
    {
        if(size > capacity_)
        {
            capacity_ = size;
            grow();
        }
    }

    // @ not tested
    T& insert(int i, const T& val)
    {
        ++size_;
        if(size_ > capacity_)
        {
            capacity_ = size_ * 2;
            grow();
        }
        memmove(data_ + i + 1, data_ + i, size_ - i - 1);
        data_[i] = val;
        return data_[i];
    }
    
    // @TODO(matiTechno): return iterator not reference
    T& erase(int i)
    {
        return erase(i, 1);
    }

    T& erase(int i, int count)
    {
        memmove(data_ + i, data_ + i + count, size_ - i - count);
        size_ -= count;
        return data_[i];
    }

    void resize(int size)
    {
        size_ = size;
        if(size_ > capacity_)
        {
            capacity_ = size_;
            grow();
        }
    }

    void     clear()                 {size_ = 0;}
    void     popBack()               {--size_;}
    T&       operator[](int i)       {return data_[i];}
    const T& operator[](int i) const {return data_[i];}
    T*       begin()                 {return data_;}
    const T* begin()           const {return data_;}
    T*       end()                   {return data_ + size_;}
    const T* end()             const {return data_ + size_;}
    T&       front()                 {return *data_;}
    const T& front()           const {return *data_;}
    T&       back()                  {return data_[size_ - 1];}
    const T& back()            const {return data_[size_ - 1];}
    T*       data()                  {return data_;}
    const T* data()            const {return data_;}
    bool     empty()           const {return size_ == 0;}
    int      size()            const {return size_;}

private:
    int size_ = 0;
    int capacity_ = 0;
    T* data_ = nullptr;

    void grow()
    {
        data_ = (T*)realloc(data_, capacity_ * sizeof(T));
        assert(data_);
    }
};

// does not respect constructors & destructors
template<typename T, int N>
class FixedArray
{
public:
    void pushBack(const T& t)
    {
        assert(size_ < N);
        data_[size_] = t;
        ++size_;
    }

    int      maxSize()         const {return N;}
    void     clear()                 {size_ = 0;}
    void     popBack()               {--size_;}
    T&       operator[](int i)       {return data_[i];}
    const T& operator[](int i) const {return data_[i];}
    T*       begin()                 {return data_;}
    const T* begin()           const {return data_;}
    T*       end()                   {return data_ + size_;}
    const T* end()             const {return data_ + size_;}
    T&       front()                 {return *data_;}
    const T& front()           const {return *data_;}
    T&       back()                  {return data_[size_ - 1];}
    const T& back()            const {return data_[size_ - 1];}
    T*       data()                  {return data_;}
    const T* data()            const {return data_;}
    bool     empty()           const {return size_ == 0;}
    int      size()            const {return size_;}

private:
    int size_ = 0;
    T data_[N];
};

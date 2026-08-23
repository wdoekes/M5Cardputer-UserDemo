#ifndef CBUFFER_H
#define CBUFFER_H

#include <array>
#include <cstddef>

template <typename T, std::size_t N>
class CircularBuffer {
    static_assert(N > 0, "CircularBuffer capacity must be > 0");

public:
    using value_type = T;
    using size_type  = std::size_t;

    void push(const T& value) {
        buf_[head_] = value;
        head_ = (head_ + 1) % N;
        if (size_ < N) {
            ++size_;
        } else {
            tail_ = (tail_ + 1) % N;  // overwrite oldest
        }
    }

    // Index 0 = oldest, size()-1 = newest.
    const T& operator[](size_type i) const {
        return buf_[(tail_ + i) % N];
    }
    T& operator[](size_type i) {
        return buf_[(tail_ + i) % N];
    }

    // Newest element (undefined if empty).
    const T& latest() const { return buf_[(head_ + N - 1) % N]; }
    const T& oldest() const { return buf_[tail_]; }

    size_type size()     const { return size_; }
    bool      empty()    const { return size_ == 0; }
    bool      full()     const { return size_ == N; }
    static constexpr size_type capacity() { return N; }

    // Clear all elements.
    void clear() {
        head_ = tail_ = size_ = 0;
    }

    // Clear/prune N oldest elements.
    void prune(std::size_t n = N) {
        if (n >= size_) {
            head_ = tail_ = size_ = 0;
        } else {
            tail_ = (tail_ + n) % N;
            size_ -= n;
        }
    }

    // Keep only N newest elements.
    void keep(std::size_t n = N) {
        if (n < size_) {
            std::size_t skip = size_ - n;
            tail_ = (tail_ + skip) % N;
            size_ = n;
        }
    }

    // View of the CircularBuffer for a partial iterator.
    class View {
    public:
        View(const CircularBuffer* b, size_type start, size_type count)
            : b_(b), start_(start), count_(count) {}

        class iterator {
        public:
            iterator(const CircularBuffer* b, size_type i) : b_(b), i_(i) {}
            const T& operator*() const { return (*b_)[i_]; }
            iterator& operator++() { ++i_; return *this; }
            bool operator!=(const iterator& o) const { return i_ != o.i_; }
        private:
            const CircularBuffer* b_;
            size_type i_;
        };

        iterator begin() const { return {b_, start_}; }
        iterator end()   const { return {b_, start_ + count_}; }
        size_type size() const { return count_; }

    private:
        const CircularBuffer* b_;
        size_type start_, count_;
    };

    // Range-for support: iterates oldest -> newest.
    using const_iterator = typename View::iterator;
    const_iterator begin() const { return {this, 0}; }
    const_iterator end()   const { return {this, size_}; }

    // Range-for subset from head.
    View head(size_type n) const {
        size_type count = (n < size_) ? n : size_;
        return View{this, 0, count};
    }
    // Range-for subset from tail.
    View tail(size_type n) const {
        size_type count = (n < size_) ? n : size_;
        return View{this, size_ - count, count};
    }

private:
    std::array<T, N> buf_{};
    size_type head_ = 0;   // next write position
    size_type tail_ = 0;   // oldest element
    size_type size_ = 0;
};

#endif
